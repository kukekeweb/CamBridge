#include "native_signaling_session.h"

#include <cassert>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

using cambridge::native::receiver::NativeSignalingSession;
using cambridge::native::receiver::NativeSignalingSessionConfig;
using cambridge::native::receiver::ParseSignalingMessage;
using cambridge::native::receiver::ReceiverState;
using cambridge::native::receiver::SignalingMessage;
using cambridge::native::receiver::SignalingMessageType;

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

void TestHelloAndOfferAnswer() {
  NativeSignalingSession session({"s1", "192.168.11.2", 5000});
  std::mutex mutex;
  std::condition_variable condition;
  std::vector<std::string> sent;
  session.SetSendHandler([&](const std::string& message) {
    {
      std::lock_guard lock(mutex);
      sent.push_back(message);
    }
    condition.notify_all();
  });

  assert(session.Start());
  assert(session.OnSocketOpen());
  SignalingMessage hello;
  std::string error;
  assert(ParseSignalingMessage(sent.front(), &hello, &error));
  assert(hello.type == SignalingMessageType::Hello);
  assert(session.OnSocketMessage(
      R"({"version":1,"type":"offer","sessionId":"s1","sdp":"v=0\r\no=- 1 1 IN IP4 192.168.11.2\r\ns=-\r\nt=0 0\r\nm=video 50000 UDP/TLS/RTP/SAVPF 96\r\na=mid:0\r\na=sendonly\r\na=rtpmap:96 H264/90000\r\na=ice-ufrag:test\r\na=ice-pwd:test-password\r\na=fingerprint:sha-256 00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF\r\na=setup:actpass\r\n"})"));

  std::unique_lock lock(mutex);
  assert(condition.wait_for(lock, std::chrono::seconds(1), [&] { return sent.size() >= 2; }));
  auto answerIt = std::find_if(sent.begin(), sent.end(), [&](const std::string& message) {
    SignalingMessage parsed;
    return ParseSignalingMessage(message, &parsed, &error) &&
           parsed.type == SignalingMessageType::Answer;
  });
  assert(answerIt != sent.end());
  SignalingMessage answer;
  assert(ParseSignalingMessage(*answerIt, &answer, &error));
  assert(answer.sdp.find("m=video") != std::string::npos);
  assert(session.state() == ReceiverState::OfferReceived);
}

void TestIceBeforeOfferIsQueued() {
  NativeSignalingSession session({"s2", "192.168.11.2", 5000});
  session.SetSendHandler([](const std::string&) {});
  assert(session.Start());
  assert(session.OnSocketOpen());
  assert(session.OnSocketMessage(
      R"({"version":1,"type":"ice","sessionId":"s2","candidate":{"candidate":"candidate:1 1 UDP 1 192.168.11.2 50000 typ host","sdpMid":"0"}})"));
  assert(session.OnSocketMessage(R"({"version":1,"type":"close","sessionId":"s2"})"));
  assert(session.state() == ReceiverState::WaitingForOffer);
}

void TestRestartCreatesFreshSessionAfterClose() {
  NativeSignalingSession session({"s3", "192.168.11.2", 5000});
  std::vector<std::string> sent;
  session.SetSendHandler([&](const std::string& message) { sent.push_back(message); });
  assert(session.Start());
  assert(session.OnSocketOpen());
  session.Close();
  assert(session.Restart("s4"));
  assert(session.OnSocketOpen());
  SignalingMessage hello;
  std::string error;
  assert(ParseSignalingMessage(sent.back(), &hello, &error));
  assert(hello.type == SignalingMessageType::Hello);
  assert(hello.sessionId == "s4");
  assert(session.state() == ReceiverState::WaitingForOffer);
}
}  // namespace

int main() {
  TestHelloAndOfferAnswer();
  TestIceBeforeOfferIsQueued();
  TestRestartCreatesFreshSessionAfterClose();
  std::cout << "CamBridge native signaling session tests passed\n";
  return 0;
}
