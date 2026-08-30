#include "signaling_protocol.h"

#include <cassert>
#include <iostream>

using cambridge::native::receiver::ParseSignalingMessage;
using cambridge::native::receiver::SerializeAnswer;
using cambridge::native::receiver::SerializeHello;
using cambridge::native::receiver::SerializeIce;
using cambridge::native::receiver::SignalingMessage;
using cambridge::native::receiver::SignalingMessageType;

namespace {
void TestHelloRoundTrip() {
  SignalingMessage parsed;
  std::string error;
  assert(ParseSignalingMessage(SerializeHello("s1"), &parsed, &error));
  assert(parsed.type == SignalingMessageType::Hello);
  assert(parsed.role == "native");
  assert(parsed.sessionId == "s1");
  assert(error.empty());
}

void TestOfferAndAnswer() {
  SignalingMessage parsed;
  std::string error;
  assert(ParseSignalingMessage(
      R"({"version":1,"type":"offer","sessionId":"s1","sdp":"v=0\r\nm=video"})",
      &parsed, &error));
  assert(parsed.type == SignalingMessageType::Offer);
  assert(parsed.sdp == "v=0\r\nm=video");
  assert(ParseSignalingMessage(SerializeAnswer("s1", "v=0\r\na=answer"), &parsed, &error));
  assert(parsed.type == SignalingMessageType::Answer);
  assert(parsed.sdp == "v=0\r\na=answer");
}

void TestIceAndValidation() {
  SignalingMessage parsed;
  std::string error;
  assert(ParseSignalingMessage(
      SerializeIce("s1", "candidate:1 1 UDP 1 192.168.11.2 50000 typ host", "0"),
      &parsed, &error));
  assert(parsed.type == SignalingMessageType::Ice);
  assert(parsed.candidate.find("192.168.11.2") != std::string::npos);
  assert(parsed.mid == "0");
  assert(!ParseSignalingMessage(R"({"type":"offer","sessionId":"s1"})", &parsed, &error));
  assert(!error.empty());
  assert(!ParseSignalingMessage("not-json", &parsed, &error));
}
}  // namespace

int main() {
  TestHelloRoundTrip();
  TestOfferAndAnswer();
  TestIceAndValidation();
  std::cout << "CamBridge signaling protocol tests passed\n";
  return 0;
}
