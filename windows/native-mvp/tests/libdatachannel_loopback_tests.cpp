#include "libdatachannel_receiver.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <rtc/rtc.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using cambridge::native::receiver::LibDataChannelReceiver;
using cambridge::native::receiver::ReceiverState;

constexpr std::uint32_t kSsrc = 0x12345678;
constexpr std::uint8_t kPayloadType = 96;

struct Candidate {
  std::string value;
  std::string mid;
};

void Require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

bool IsPrivateIpv4(const IN_ADDR& address) {
  const std::uint32_t value = ntohl(address.S_un.S_addr);
  const std::uint32_t first = (value >> 24) & 0xff;
  const std::uint32_t second = (value >> 16) & 0xff;
  return first == 10 || (first == 172 && second >= 16 && second <= 31) ||
         (first == 192 && second == 168);
}

std::string SelectPrivateIpv4() {
  ULONG bufferSize = 0;
  const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                      GAA_FLAG_SKIP_DNS_SERVER;
  if (GetAdaptersAddresses(AF_INET, flags, nullptr, nullptr, &bufferSize) !=
      ERROR_BUFFER_OVERFLOW) {
    throw std::runtime_error("could not query IPv4 adapter buffer size");
  }
  std::vector<std::byte> buffer(bufferSize);
  auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
  if (GetAdaptersAddresses(AF_INET, flags, nullptr, adapters, &bufferSize) !=
      NO_ERROR) {
    throw std::runtime_error("could not enumerate IPv4 adapters");
  }
  for (auto* adapter = adapters; adapter != nullptr; adapter = adapter->Next) {
    for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr;
         unicast = unicast->Next) {
      if (unicast->Address.lpSockaddr == nullptr ||
          unicast->Address.lpSockaddr->sa_family != AF_INET) {
        continue;
      }
      const auto* sockaddr =
          reinterpret_cast<const SOCKADDR_IN*>(unicast->Address.lpSockaddr);
      if (!IsPrivateIpv4(sockaddr->sin_addr)) continue;
      char text[INET_ADDRSTRLEN]{};
      if (InetNtopA(AF_INET, const_cast<IN_ADDR*>(&sockaddr->sin_addr), text,
                    sizeof(text)) == nullptr) {
        continue;
      }
      return text;
    }
  }
  throw std::runtime_error("no private IPv4 adapter is available");
}

std::vector<std::byte> MakeSingleNalPacket(std::uint16_t sequence,
                                           std::uint32_t timestamp) {
  constexpr std::uint8_t kNal[] = {0x65, 0xaa, 0xbb, 0xcc};
  std::vector<std::byte> packet(sizeof(rtc::RtpHeader) + sizeof(kNal));
  auto* header = reinterpret_cast<rtc::RtpHeader*>(packet.data());
  header->preparePacket();
  header->setPayloadType(kPayloadType);
  header->setSeqNumber(sequence);
  header->setTimestamp(timestamp);
  header->setSsrc(kSsrc);
  header->setMarker(true);
  std::memcpy(packet.data() + header->getSize(), kNal, sizeof(kNal));
  packet.resize(header->getSize() + sizeof(kNal));
  return packet;
}

void RunNativeRtpLoopback() {
  const std::string bindAddress = SelectPrivateIpv4();
  std::mutex mutex;
  std::condition_variable condition;
  bool offerReady = false;
  bool answerReady = false;
  std::string offer;
  std::string answer;
  std::vector<Candidate> senderCandidates;
  std::vector<Candidate> receiverCandidates;
  std::vector<std::uint8_t> receivedAccessUnit;
  std::uint32_t receivedTimestamp = 0;

  LibDataChannelReceiver receiver({"loopback", bindAddress, 5000});
  auto sender = std::make_shared<rtc::PeerConnection>([&bindAddress] {
    rtc::Configuration configuration;
    configuration.iceServers.clear();
    configuration.bindAddress = bindAddress;
    configuration.disableAutoGathering = true;
    configuration.disableAutoNegotiation = true;
    return configuration;
  }());

  receiver.SetLocalDescriptionHandler([&](const std::string& type,
                                           const std::string& sdp) {
    if (type != "answer") return;
    std::lock_guard lock(mutex);
    answer = sdp;
    answerReady = true;
    condition.notify_all();
  });
  receiver.SetLocalCandidateHandler([&](const std::string& candidate,
                                         const std::string& mid) {
    std::lock_guard lock(mutex);
    receiverCandidates.push_back({candidate, mid});
    condition.notify_all();
  });
  receiver.SetAccessUnitHandler(
      [&](std::vector<std::uint8_t> accessUnit, std::uint32_t timestamp) {
        std::lock_guard lock(mutex);
        receivedAccessUnit = std::move(accessUnit);
        receivedTimestamp = timestamp;
        condition.notify_all();
      });

  sender->onLocalDescription([&](rtc::Description description) {
    if (description.typeString() != "offer") return;
    std::lock_guard lock(mutex);
    offer = description.generateSdp();
    offerReady = true;
    condition.notify_all();
  });
  sender->onLocalCandidate([&](rtc::Candidate candidate) {
    std::lock_guard lock(mutex);
    senderCandidates.push_back({candidate.candidate(), candidate.mid()});
    condition.notify_all();
  });

  rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
  media.addH264Codec(kPayloadType);
  media.addSSRC(kSsrc, "cambridge-loopback");
  auto senderTrack = sender->addTrack(media);

  sender->setLocalDescription();
  sender->gatherLocalCandidates();
  {
    std::unique_lock lock(mutex);
    Require(condition.wait_for(lock, std::chrono::seconds(2),
                               [&] { return offerReady; }),
            "native loopback offer timeout");
  }

  Require(receiver.Start(), "native loopback receiver start failed: " +
                                receiver.lastError());
  Require(receiver.AcceptOffer("loopback", offer),
          "native loopback offer rejected: " + receiver.lastError());
  {
    std::unique_lock lock(mutex);
    Require(condition.wait_for(lock, std::chrono::seconds(2),
                               [&] { return answerReady; }),
            "native loopback answer timeout");
  }

  sender->setRemoteDescription(
      rtc::Description(answer, rtc::Description::Type::Answer));
  std::vector<Candidate> pendingSender;
  std::vector<Candidate> pendingReceiver;
  {
    std::unique_lock lock(mutex);
    Require(condition.wait_for(lock, std::chrono::seconds(2), [&] {
              return !senderCandidates.empty() && !receiverCandidates.empty();
            }),
            "native loopback ICE candidate timeout");
    pendingSender.swap(senderCandidates);
    pendingReceiver.swap(receiverCandidates);
  }
  for (const Candidate& candidate : pendingSender) {
    Require(receiver.AddRemoteCandidate(candidate.value, candidate.mid),
            "native loopback sender candidate rejected: " + receiver.lastError());
  }
  for (const Candidate& candidate : pendingReceiver) {
    try {
      sender->addRemoteCandidate(rtc::Candidate(candidate.value, candidate.mid));
    } catch (const std::exception& error) {
      throw std::runtime_error(std::string("native loopback receiver candidate rejected: ") +
                               error.what());
    }
  }

  {
    std::unique_lock lock(mutex);
    Require(condition.wait_for(lock, std::chrono::seconds(3), [&] {
              return sender->state() == rtc::PeerConnection::State::Connected &&
                     receiver.state() == ReceiverState::Connected && senderTrack->isOpen();
            }),
            "native loopback connection timeout");
  }

  const auto packet = MakeSingleNalPacket(1, 9000);
  Require(senderTrack->send(reinterpret_cast<const std::byte*>(packet.data()), packet.size()),
          "native loopback RTP send failed");
  {
    std::unique_lock lock(mutex);
    Require(condition.wait_for(lock, std::chrono::seconds(2),
                               [&] { return !receivedAccessUnit.empty(); }),
            "native loopback access-unit timeout");
  }
  Require(receivedTimestamp == 9000, "native loopback RTP timestamp mismatch");
  Require(receiver.metrics().accessUnits >= 1,
          "native loopback receiver metric did not increment");

  receiver.Close();
  sender->close();
}

}  // namespace

int main() {
  try {
    RunNativeRtpLoopback();
    std::cout << "CamBridge libdatachannel loopback test passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "CamBridge libdatachannel loopback test failed: " << error.what()
              << "\n";
    return 1;
  }
}
