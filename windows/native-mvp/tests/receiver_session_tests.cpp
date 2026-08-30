#include "receiver_session.h"

#include <cassert>
#include <iostream>

using cambridge::native::receiver::CandidateKind;
using cambridge::native::receiver::ReceiverError;
using cambridge::native::receiver::ReceiverSession;
using cambridge::native::receiver::ReceiverState;

namespace {
constexpr char kOffer[] =
    "v=0\r\n"
    "o=- 1 1 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=group:BUNDLE 0\r\n"
    "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
    "a=sendonly\r\n"
    "a=rtpmap:96 H264/90000\r\n";

void TestOfferLifecycle() {
  ReceiverSession session("session-1");
  assert(session.Start() == ReceiverError::None);
  assert(session.state() == ReceiverState::WaitingForOffer);
  assert(session.AcceptOffer("session-1", kOffer) == ReceiverError::None);
  assert(session.state() == ReceiverState::OfferReceived);
  assert(session.offer().videoMLineCount == 1);
  assert(session.offer().hasH264);
  assert(session.MarkConnected() == ReceiverError::None);
  assert(session.state() == ReceiverState::Connected);
  assert(session.MarkDisconnected() == ReceiverError::None);
  assert(session.state() == ReceiverState::Disconnected);
  assert(session.Restart("session-2") == ReceiverError::None);
  assert(session.state() == ReceiverState::WaitingForOffer);
}

void TestOfferValidation() {
  ReceiverSession session("session-1");
  assert(session.Start() == ReceiverError::None);
  assert(session.AcceptOffer("wrong", kOffer) == ReceiverError::SessionMismatch);
  assert(session.AcceptOffer("session-1", "v=0\r\nm=audio 9 RTP/AVP 0\r\n") == ReceiverError::VideoMLineMissing);
  assert(session.AcceptOffer("session-1", "v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\na=recvonly\r\na=rtpmap:96 H264/90000\r\n") == ReceiverError::VideoDirectionInvalid);
  assert(session.AcceptOffer("session-1", "v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\na=sendonly\r\na=rtpmap:96 VP8/90000\r\n") == ReceiverError::H264Missing);
}

void TestCandidatePolicy() {
  ReceiverSession session("session-1");
  assert(session.Start() == ReceiverError::None);
  assert(session.AcceptOffer("session-1", kOffer) == ReceiverError::None);
  const auto host = session.AcceptIce("candidate:1 1 UDP 2122260223 192.168.11.2 50000 typ host");
  assert(host.error == ReceiverError::None);
  assert(host.kind == CandidateKind::PrivateIpv4Host);
  const auto relay = session.AcceptIce("candidate:2 1 UDP 16777215 203.0.113.10 50001 typ relay");
  assert(relay.error == ReceiverError::RelayRejected);
  const auto publicHost = session.AcceptIce("candidate:3 1 UDP 2122260223 203.0.113.10 50002 typ host");
  assert(publicHost.error == ReceiverError::NonPrivateHostRejected);
}
}  // namespace

int main() {
  TestOfferLifecycle();
  TestOfferValidation();
  TestCandidatePolicy();
  std::cout << "CamBridge receiver session tests passed\n";
  return 0;
}
