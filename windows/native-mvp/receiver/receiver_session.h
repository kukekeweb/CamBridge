#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace cambridge::native::receiver {

enum class ReceiverState {
  Idle,
  WaitingForOffer,
  OfferReceived,
  Connected,
  Disconnected,
};

enum class ReceiverError {
  None,
  InvalidState,
  SessionMismatch,
  VideoMLineMissing,
  MultipleVideoMLines,
  H264Missing,
  VideoDirectionInvalid,
  InvalidCandidate,
  RelayRejected,
  NonPrivateHostRejected,
};

enum class CandidateKind {
  Invalid,
  PrivateIpv4Host,
  NonPrivateHost,
  Relay,
};

struct OfferDescription {
  std::uint32_t videoMLineCount = 0;
  bool hasH264 = false;
  bool sendsVideo = false;
};

struct CandidateResult {
  ReceiverError error = ReceiverError::InvalidCandidate;
  CandidateKind kind = CandidateKind::Invalid;
  std::string address;
};

class ReceiverSession {
 public:
  explicit ReceiverSession(std::string sessionId);

  ReceiverError Start();
  ReceiverError AcceptOffer(std::string_view sessionId, std::string_view sdp);
  CandidateResult AcceptIce(std::string_view candidate);
  ReceiverError MarkConnected();
  ReceiverError MarkDisconnected();
  ReceiverError Restart(std::string sessionId);

  ReceiverState state() const { return state_; }
  const OfferDescription& offer() const { return offer_; }
  const std::string& sessionId() const { return sessionId_; }

 private:
  std::string sessionId_;
  ReceiverState state_ = ReceiverState::Idle;
  OfferDescription offer_;
};

const char* ReceiverErrorName(ReceiverError error);
const char* ReceiverStateName(ReceiverState state);

}  // namespace cambridge::native::receiver
