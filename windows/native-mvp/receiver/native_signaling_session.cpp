#include "native_signaling_session.h"

#include "signaling_protocol.h"

#include <utility>

namespace cambridge::native::receiver {

NativeSignalingSession::NativeSignalingSession(NativeSignalingSessionConfig config)
    : config_(std::move(config)) {
  receiver_ = std::make_unique<LibDataChannelReceiver>(LibDataChannelReceiverConfig{
      config_.sessionId, config_.bindAddress, config_.h264BitrateKbps});
  receiver_->SetLocalDescriptionHandler([this](const std::string& type,
                                               const std::string& sdp) {
    if (type == "answer") Send(SerializeAnswer(config_.sessionId, sdp));
  });
  receiver_->SetLocalCandidateHandler([this](const std::string& candidate,
                                             const std::string& mid) {
    Send(SerializeIce(config_.sessionId, candidate, mid));
  });
}

NativeSignalingSession::~NativeSignalingSession() { Close(); }

bool NativeSignalingSession::Fail(std::string message) {
  lastError_ = std::move(message);
  return false;
}

bool NativeSignalingSession::Send(std::string message) {
  if (!sendHandler_) return Fail("signaling send handler is not configured");
  sendHandler_(message);
  return true;
}

bool NativeSignalingSession::Start() {
  if (started_) return Fail("signaling session already started");
  if (config_.sessionId.empty()) return Fail("session id is empty");
  if (!receiver_->Start()) return Fail(receiver_->lastError());
  started_ = true;
  lastError_.clear();
  return true;
}

bool NativeSignalingSession::OnSocketOpen() {
  if (!started_) return Fail("signaling session is not started");
  if (socketOpen_) return Fail("signaling socket is already open");
  socketOpen_ = true;
  return Send(SerializeHello(config_.sessionId));
}

bool NativeSignalingSession::OnSocketMessage(const std::string& message) {
  if (!socketOpen_) return Fail("signaling socket is not open");

  SignalingMessage parsed;
  std::string error;
  if (!ParseSignalingMessage(message, &parsed, &error)) return Fail(error);
  if (parsed.sessionId != config_.sessionId) return Fail("session mismatch");

  if (parsed.type == SignalingMessageType::Ice) {
    if (parsed.candidateIsNull) return true;
    if (receiver_->state() == ReceiverState::WaitingForOffer) {
      pendingIce_.push_back(std::move(parsed));
      return true;
    }
    if (!receiver_->AddRemoteCandidate(parsed.candidate, parsed.mid)) {
      return Fail(receiver_->lastError());
    }
    return true;
  }

  if (parsed.type == SignalingMessageType::Offer) {
    if (!receiver_->AcceptOffer(parsed.sessionId, parsed.sdp)) {
      return Fail(receiver_->lastError());
    }
    for (const SignalingMessage& candidate : pendingIce_) {
      if (!receiver_->AddRemoteCandidate(candidate.candidate, candidate.mid)) {
        pendingIce_.clear();
        return Fail(receiver_->lastError());
      }
    }
    pendingIce_.clear();
    lastError_.clear();
    return true;
  }

  if (parsed.type == SignalingMessageType::Close) {
    Close();
    return true;
  }
  return Fail("unexpected signaling message");
}

void NativeSignalingSession::Close() {
  socketOpen_ = false;
  if (receiver_) receiver_->Close();
}

ReceiverState NativeSignalingSession::state() const { return receiver_->state(); }

LibDataChannelReceiverMetrics NativeSignalingSession::metrics() const {
  return receiver_->metrics();
}

}  // namespace cambridge::native::receiver
