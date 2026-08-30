#include "libdatachannel_receiver.h"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>

using cambridge::native::receiver::LibDataChannelReceiver;
using cambridge::native::receiver::LibDataChannelReceiverConfig;
using cambridge::native::receiver::ReceiverState;

namespace {
constexpr char kOffer[] =
    "v=0\r\n"
    "o=- 1 1 IN IP4 192.168.11.2\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=group:BUNDLE 0\r\n"
    "m=video 50000 UDP/TLS/RTP/SAVPF 96\r\n"
    "a=mid:0\r\n"
    "a=sendonly\r\n"
    "a=rtpmap:96 H264/90000\r\n"
    "a=ice-ufrag:test\r\n"
    "a=ice-pwd:test-password\r\n"
    "a=fingerprint:sha-256 00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF\r\n"
    "a=setup:actpass\r\n";

void TestStartAndValidation() {
  LibDataChannelReceiver receiver({"session-1", "192.168.11.2", 5000});
  assert(receiver.Start());
  assert(receiver.state() == ReceiverState::WaitingForOffer);
  assert(!receiver.AcceptOffer("session-1", "v=0\r\nm=audio 9 RTP/AVP 0\r\n"));
  assert(receiver.state() == ReceiverState::WaitingForOffer);
}

void TestOfferReachesPeerConnection() {
  LibDataChannelReceiver receiver({"session-2", "192.168.11.2", 5000});
  std::mutex mutex;
  std::condition_variable condition;
  bool answerObserved = false;
  receiver.SetLocalDescriptionHandler([&](const std::string& type, const std::string& sdp) {
    {
      std::lock_guard lock(mutex);
      answerObserved = type == "answer" && sdp.find("m=video") != std::string::npos;
    }
    condition.notify_one();
  });
  assert(receiver.Start());
  const bool accepted = receiver.AcceptOffer("session-2", kOffer);
  if (!accepted) std::cerr << "AcceptOffer failed: " << receiver.lastError() << "\n";
  assert(accepted);
  assert(receiver.lastError().empty());
  {
    std::unique_lock lock(mutex);
    assert(condition.wait_for(lock, std::chrono::seconds(1), [&] { return answerObserved; }));
  }
  const auto metrics = receiver.metrics();
  assert(metrics.remoteOfferHasH264);
  assert(metrics.localAnswerHasH264);
  receiver.Close();
  assert(receiver.state() != ReceiverState::Connected);
}

void TestOfferProducesLocalHostCandidate() {
  LibDataChannelReceiver receiver({"session-candidates", "127.0.0.1", 5000});
  std::mutex mutex;
  std::condition_variable condition;
  std::size_t candidateCount = 0;
  receiver.SetLocalCandidateHandler([&](const std::string& candidate,
                                        const std::string& mid) {
    assert(!candidate.empty());
    assert(!mid.empty());
    {
      std::lock_guard lock(mutex);
      ++candidateCount;
    }
    condition.notify_one();
  });

  assert(receiver.Start());
  assert(receiver.AcceptOffer("session-candidates", kOffer));
  {
    std::unique_lock lock(mutex);
    assert(condition.wait_for(lock, std::chrono::seconds(1),
                              [&] { return candidateCount > 0; }));
  }
}
}  // namespace

int main() {
  TestStartAndValidation();
  TestOfferReachesPeerConnection();
  TestOfferProducesLocalHostCandidate();
  std::cout << "CamBridge libdatachannel receiver tests passed\n";
  return 0;
}
