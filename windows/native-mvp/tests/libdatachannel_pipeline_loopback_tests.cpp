#include "libdatachannel_receiver.h"
#include "receiver_media_pipeline.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <rtc/rtc.hpp>

#include <Windows.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using cambridge::native::Nv12Frame;
using cambridge::native::ReceiverMediaPipeline;
using cambridge::native::SharedFrameReader;
using cambridge::native::receiver::LibDataChannelReceiver;
using cambridge::native::receiver::ReceiverState;

constexpr std::uint8_t kPayloadType = 96;
constexpr std::uint32_t kSsrc = 0x22334455;
constexpr std::size_t kMaxRtpPayload = 1200;
constexpr std::uint32_t kTimestampStep = 1500;

struct Candidate {
  std::string value;
  std::string mid;
};

struct AccessUnit {
  std::vector<std::vector<std::uint8_t>> nals;
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
                    sizeof(text)) != nullptr) {
        return text;
      }
    }
  }
  throw std::runtime_error("no private IPv4 adapter is available");
}

std::size_t StartCodeLength(const std::vector<std::uint8_t>& bytes,
                            std::size_t offset) {
  if (offset + 3 <= bytes.size() && bytes[offset] == 0 && bytes[offset + 1] == 0 &&
      bytes[offset + 2] == 1) {
    return 3;
  }
  if (offset + 4 <= bytes.size() && bytes[offset] == 0 && bytes[offset + 1] == 0 &&
      bytes[offset + 2] == 0 && bytes[offset + 3] == 1) {
    return 4;
  }
  return 0;
}

std::vector<AccessUnit> SplitAnnexBAccessUnits(const std::vector<std::uint8_t>& bytes) {
  std::vector<AccessUnit> units;
  AccessUnit current;
  std::size_t nalStart = 0;
  while (nalStart < bytes.size()) {
    const std::size_t prefix = StartCodeLength(bytes, nalStart);
    if (prefix == 0) {
      ++nalStart;
      continue;
    }
    const std::size_t payloadStart = nalStart + prefix;
    if (payloadStart >= bytes.size()) break;
    std::size_t nalEnd = payloadStart;
    while (nalEnd < bytes.size() && StartCodeLength(bytes, nalEnd) == 0) ++nalEnd;
    if (nalEnd > payloadStart) {
      std::vector<std::uint8_t> nal(bytes.begin() + static_cast<std::ptrdiff_t>(payloadStart),
                                    bytes.begin() + static_cast<std::ptrdiff_t>(nalEnd));
      if (!current.nals.empty() && (nal.front() & 0x1f) == 9) {
        units.push_back(std::move(current));
        current = {};
      }
      current.nals.push_back(std::move(nal));
    }
    nalStart = nalEnd;
  }
  if (!current.nals.empty()) units.push_back(std::move(current));
  return units;
}

std::vector<std::byte> MakeRtpPacket(std::uint16_t sequence, std::uint32_t timestamp,
                                     const std::uint8_t* payload, std::size_t payloadSize,
                                     bool marker) {
  std::vector<std::byte> packet(sizeof(rtc::RtpHeader) + payloadSize);
  auto* header = reinterpret_cast<rtc::RtpHeader*>(packet.data());
  header->preparePacket();
  header->setPayloadType(kPayloadType);
  header->setSeqNumber(sequence);
  header->setTimestamp(timestamp);
  header->setSsrc(kSsrc);
  header->setMarker(marker);
  std::memcpy(packet.data() + header->getSize(), payload, payloadSize);
  packet.resize(header->getSize() + payloadSize);
  return packet;
}

void SendNal(const std::shared_ptr<rtc::Track>& track, std::uint16_t* sequence,
             std::uint32_t timestamp, const std::vector<std::uint8_t>& nal,
             bool marker) {
  Require(track != nullptr && sequence != nullptr && !nal.empty(), "invalid NAL input");
  if (nal.size() <= kMaxRtpPayload) {
    const auto packet = MakeRtpPacket((*sequence)++, timestamp, nal.data(), nal.size(), marker);
    Require(track->send(reinterpret_cast<const std::byte*>(packet.data()), packet.size()),
            "single-NAL RTP send failed");
    return;
  }

  Require(nal.size() > 1, "oversized NAL has no header");
  const std::uint8_t nalHeader = nal.front();
  const std::uint8_t fuIndicator = static_cast<std::uint8_t>((nalHeader & 0xe0) | 28);
  const std::uint8_t nalType = static_cast<std::uint8_t>(nalHeader & 0x1f);
  std::size_t offset = 1;
  bool first = true;
  while (offset < nal.size()) {
    const std::size_t chunk = std::min(kMaxRtpPayload - 2, nal.size() - offset);
    std::vector<std::uint8_t> payload(2 + chunk);
    payload[0] = fuIndicator;
    payload[1] = static_cast<std::uint8_t>(nalType | (first ? 0x80 : 0) |
                                           ((offset + chunk == nal.size()) ? 0x40 : 0));
    std::memcpy(payload.data() + 2, nal.data() + offset, chunk);
    const bool last = offset + chunk == nal.size();
    const auto packet = MakeRtpPacket((*sequence)++, timestamp, payload.data(), payload.size(),
                                      last && marker);
    Require(track->send(reinterpret_cast<const std::byte*>(packet.data()), packet.size()),
            "FU-A RTP send failed");
    offset += chunk;
    first = false;
  }
}

void SendAccessUnit(const std::shared_ptr<rtc::Track>& track, std::uint16_t* sequence,
                    std::uint32_t timestamp, const AccessUnit& unit) {
  Require(!unit.nals.empty(), "empty access unit");
  for (std::size_t index = 0; index < unit.nals.size(); ++index) {
    SendNal(track, sequence, timestamp, unit.nals[index], index + 1 == unit.nals.size());
  }
}

std::vector<Candidate> WaitCandidates(std::mutex& mutex, std::condition_variable& condition,
                                      std::vector<Candidate>* candidates,
                                      const char* label) {
  std::unique_lock lock(mutex);
  Require(condition.wait_for(lock, std::chrono::seconds(3),
                             [&] { return !candidates->empty(); }),
          std::string(label) + " ICE candidate timeout");
  std::vector<Candidate> result;
  result.swap(*candidates);
  return result;
}

void RunPipelineLoopback() {
  const char* fixturePath = std::getenv("CAMBRIDGE_H264_FIXTURE");
  if (fixturePath == nullptr || *fixturePath == '\0') {
    std::cout << "CamBridge libdatachannel pipeline loopback skipped: "
                 "CAMBRIDGE_H264_FIXTURE is not set\n";
    return;
  }
  std::ifstream file(fixturePath, std::ios::binary);
  Require(file.is_open(), "could not open CAMBRIDGE_H264_FIXTURE");
  const std::vector<char> input((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
  const std::vector<std::uint8_t> bytes(
      reinterpret_cast<const std::uint8_t*>(input.data()),
      reinterpret_cast<const std::uint8_t*>(input.data()) + input.size());
  const auto accessUnits = SplitAnnexBAccessUnits(bytes);
  Require(accessUnits.size() >= 2, "fixture has too few H264 access units");

  const std::string bindAddress = SelectPrivateIpv4();
  std::mutex mutex;
  std::condition_variable condition;
  std::string offer;
  std::string answer;
  bool offerReady = false;
  bool answerReady = false;
  std::vector<Candidate> senderCandidates;
  std::vector<Candidate> receiverCandidates;

  const std::wstring mapping = L"Local\\CamBridge.WebRtcPipelineLoopback.Mapping." +
                               std::to_wstring(GetCurrentProcessId());
  const std::wstring event = L"Local\\CamBridge.WebRtcPipelineLoopback.Event." +
                             std::to_wstring(GetCurrentProcessId());
  ReceiverMediaPipeline pipeline;
  Require(pipeline.Start({{1920, 1080, 60, 1, true}, mapping, event}),
          "receiver media pipeline start failed: " + pipeline.lastError());
  SharedFrameReader frameReader;
  Require(frameReader.Open(mapping), "pipeline IPC reader could not open");

  LibDataChannelReceiver receiver({"pipeline-loopback", bindAddress, 5000});
  receiver.SetAccessUnitHandler([&](std::vector<std::uint8_t> accessUnit,
                                    std::uint32_t timestamp) {
    if (!pipeline.SubmitAccessUnit(accessUnit, timestamp)) {
      std::lock_guard lock(mutex);
      condition.notify_all();
    }
  });
  receiver.SetLocalDescriptionHandler([&](const std::string& type, const std::string& sdp) {
    if (type != "answer") return;
    std::lock_guard lock(mutex);
    answer = sdp;
    answerReady = true;
    condition.notify_all();
  });
  receiver.SetLocalCandidateHandler([&](const std::string& candidate, const std::string& mid) {
    std::lock_guard lock(mutex);
    receiverCandidates.push_back({candidate, mid});
    condition.notify_all();
  });

  rtc::Configuration senderConfiguration;
  senderConfiguration.iceServers.clear();
  senderConfiguration.bindAddress = bindAddress;
  senderConfiguration.disableAutoGathering = true;
  senderConfiguration.disableAutoNegotiation = true;
  auto sender = std::make_shared<rtc::PeerConnection>(senderConfiguration);
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
  media.addSSRC(kSsrc, "cambridge-pipeline-loopback");
  auto senderTrack = sender->addTrack(media);
  sender->setLocalDescription();
  sender->gatherLocalCandidates();
  {
    std::unique_lock lock(mutex);
    Require(condition.wait_for(lock, std::chrono::seconds(3), [&] { return offerReady; }),
            "pipeline loopback offer timeout");
  }

  Require(receiver.Start(), "pipeline loopback receiver start failed: " + receiver.lastError());
  Require(receiver.AcceptOffer("pipeline-loopback", offer),
          "pipeline loopback offer rejected: " + receiver.lastError());
  {
    std::unique_lock lock(mutex);
    Require(condition.wait_for(lock, std::chrono::seconds(3), [&] { return answerReady; }),
            "pipeline loopback answer timeout");
  }
  sender->setRemoteDescription(rtc::Description(answer, rtc::Description::Type::Answer));

  const auto pendingSender = WaitCandidates(mutex, condition, &senderCandidates, "sender");
  const auto pendingReceiver = WaitCandidates(mutex, condition, &receiverCandidates, "receiver");
  for (const Candidate& candidate : pendingSender) {
    Require(receiver.AddRemoteCandidate(candidate.value, candidate.mid),
            "pipeline loopback sender candidate rejected: " + receiver.lastError());
  }
  for (const Candidate& candidate : pendingReceiver) {
    sender->addRemoteCandidate(rtc::Candidate(candidate.value, candidate.mid));
  }
  {
    std::unique_lock lock(mutex);
    Require(condition.wait_for(lock, std::chrono::seconds(5), [&] {
              return sender->state() == rtc::PeerConnection::State::Connected &&
                     receiver.state() == ReceiverState::Connected && senderTrack->isOpen();
            }),
            "pipeline loopback connection timeout");
  }

  std::uint16_t sequence = 1;
  std::uint32_t timestamp = 0;
  for (const auto& unit : accessUnits) {
    SendAccessUnit(senderTrack, &sequence, timestamp, unit);
    timestamp += kTimestampStep;
  }

  Require([&] {
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
      const auto metrics = pipeline.metrics();
      Nv12Frame frame;
      if (metrics.publishedFrames > 0 && frameReader.ReadLatest(frame)) {
        Require(frame.width == 1920 && frame.height == 1080 && frame.stride >= 1920,
                "pipeline loopback published invalid NV12 dimensions");
        Require(frame.bytes.size() == static_cast<std::size_t>(frame.stride) * frame.height * 3 / 2,
                "pipeline loopback published invalid NV12 byte count");
        return true;
      }
      if (std::chrono::steady_clock::now() - start > std::chrono::seconds(10)) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }(), "pipeline loopback did not publish decoded NV12 to IPC");

  const auto pipelineMetrics = pipeline.metrics();
  Require(pipelineMetrics.inputAccessUnits > 0, "pipeline loopback input counter is zero");
  Require(pipelineMetrics.decodedFrames > 0, "pipeline loopback decoded frame counter is zero");
  Require(pipelineMetrics.publishedFrames > 0,
          "pipeline loopback published frame counter is zero");
  Require(receiver.metrics().accessUnits > 0, "pipeline loopback RTP access-unit counter is zero");

  receiver.Close();
  sender->close();
  pipeline.Stop();
}

}  // namespace

int main() {
  try {
    RunPipelineLoopback();
    std::cout << "CamBridge libdatachannel pipeline loopback test passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "CamBridge libdatachannel pipeline loopback test failed: "
              << error.what() << "\n";
    return 1;
  }
}
