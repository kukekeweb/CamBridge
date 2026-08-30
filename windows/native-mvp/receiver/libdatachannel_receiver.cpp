#include "libdatachannel_receiver.h"

#include <rtc/rtc.hpp>

#include <utility>

namespace cambridge::native::receiver {

const char* ReceiverPeerStateName(ReceiverPeerState state) {
  switch (state) {
    case ReceiverPeerState::New: return "new";
    case ReceiverPeerState::Connecting: return "connecting";
    case ReceiverPeerState::Connected: return "connected";
    case ReceiverPeerState::Disconnected: return "disconnected";
    case ReceiverPeerState::Failed: return "failed";
    case ReceiverPeerState::Closed: return "closed";
  }
  return "unknown";
}

const char* ReceiverIceStateName(ReceiverIceState state) {
  switch (state) {
    case ReceiverIceState::New: return "new";
    case ReceiverIceState::Checking: return "checking";
    case ReceiverIceState::Connected: return "connected";
    case ReceiverIceState::Completed: return "completed";
    case ReceiverIceState::Failed: return "failed";
    case ReceiverIceState::Disconnected: return "disconnected";
    case ReceiverIceState::Closed: return "closed";
  }
  return "unknown";
}

namespace {

ReceiverPeerState PeerStateFromRtc(rtc::PeerConnection::State state) {
  switch (state) {
    case rtc::PeerConnection::State::New: return ReceiverPeerState::New;
    case rtc::PeerConnection::State::Connecting: return ReceiverPeerState::Connecting;
    case rtc::PeerConnection::State::Connected: return ReceiverPeerState::Connected;
    case rtc::PeerConnection::State::Disconnected: return ReceiverPeerState::Disconnected;
    case rtc::PeerConnection::State::Failed: return ReceiverPeerState::Failed;
    case rtc::PeerConnection::State::Closed: return ReceiverPeerState::Closed;
  }
  return ReceiverPeerState::New;
}

ReceiverIceState IceStateFromRtc(rtc::PeerConnection::IceState state) {
  switch (state) {
    case rtc::PeerConnection::IceState::New: return ReceiverIceState::New;
    case rtc::PeerConnection::IceState::Checking: return ReceiverIceState::Checking;
    case rtc::PeerConnection::IceState::Connected: return ReceiverIceState::Connected;
    case rtc::PeerConnection::IceState::Completed: return ReceiverIceState::Completed;
    case rtc::PeerConnection::IceState::Failed: return ReceiverIceState::Failed;
    case rtc::PeerConnection::IceState::Disconnected: return ReceiverIceState::Disconnected;
    case rtc::PeerConnection::IceState::Closed: return ReceiverIceState::Closed;
  }
  return ReceiverIceState::New;
}

bool SdpContainsH264(const std::string& sdp) {
  std::string lower = sdp;
  for (char& value : lower) {
    if (value >= 'A' && value <= 'Z') value = static_cast<char>(value - 'A' + 'a');
  }
  return lower.find("h264/") != std::string::npos;
}

}  // namespace

struct LibDataChannelReceiver::Impl {
  std::shared_ptr<rtc::PeerConnection> peerConnection;
  std::shared_ptr<rtc::Track> track;
};

LibDataChannelReceiver::LibDataChannelReceiver(LibDataChannelReceiverConfig config)
    : config_(std::move(config)), session_(config_.sessionId) {}

LibDataChannelReceiver::~LibDataChannelReceiver() { Close(); }

bool LibDataChannelReceiver::Fail(std::string message) {
  lastError_ = std::move(message);
  return false;
}

bool LibDataChannelReceiver::Start() {
  if (session_.Start() != ReceiverError::None) {
    return Fail("receiver session could not start");
  }
  peerState_.store(ReceiverPeerState::New, std::memory_order_relaxed);
  iceState_.store(ReceiverIceState::New, std::memory_order_relaxed);
  peerStateChanges_.store(0, std::memory_order_relaxed);
  iceStateChanges_.store(0, std::memory_order_relaxed);
  remoteOfferHasH264_.store(false, std::memory_order_relaxed);
  localAnswerHasH264_.store(false, std::memory_order_relaxed);

  try {
    rtc::Configuration configuration;
    configuration.disableAutoGathering = true;
    configuration.disableAutoNegotiation = true;
    if (!config_.bindAddress.empty()) configuration.bindAddress = config_.bindAddress;

    impl_ = std::make_unique<Impl>();
    impl_->peerConnection = std::make_shared<rtc::PeerConnection>(configuration);

    impl_->peerConnection->onLocalDescription(
        [this](rtc::Description description) {
          const std::string type = description.typeString();
          const std::string sdp = description.generateSdp();
          if (type == "answer") {
            localAnswerHasH264_.store(SdpContainsH264(sdp), std::memory_order_relaxed);
          }
          if (localDescriptionHandler_) {
            localDescriptionHandler_(type, sdp);
          }
        });
    impl_->peerConnection->onLocalCandidate(
        [this](rtc::Candidate candidate) {
          if (localCandidateHandler_) {
            localCandidateHandler_(candidate.candidate(), candidate.mid());
          }
        });
    impl_->peerConnection->onStateChange([this](rtc::PeerConnection::State state) {
      peerState_.store(PeerStateFromRtc(state), std::memory_order_relaxed);
      peerStateChanges_.fetch_add(1, std::memory_order_relaxed);
      if (state == rtc::PeerConnection::State::Connected &&
          session_.state() == ReceiverState::OfferReceived) {
        session_.MarkConnected();
      } else if ((state == rtc::PeerConnection::State::Disconnected ||
                  state == rtc::PeerConnection::State::Failed ||
                  state == rtc::PeerConnection::State::Closed) &&
                 session_.state() == ReceiverState::Connected) {
        session_.MarkDisconnected();
      }
    });
    impl_->peerConnection->onIceStateChange([this](rtc::PeerConnection::IceState state) {
      iceState_.store(IceStateFromRtc(state), std::memory_order_relaxed);
      iceStateChanges_.fetch_add(1, std::memory_order_relaxed);
    });

    rtc::Description::Video media("video", rtc::Description::Direction::RecvOnly);
    media.addH264Codec(96);
    media.setBitrate(static_cast<int>(config_.h264BitrateKbps));
    impl_->track = impl_->peerConnection->addTrack(media);
    impl_->track->setMediaHandler(std::make_shared<rtc::H264RtpDepacketizer>());
    impl_->track->onFrame([this](rtc::binary data, rtc::FrameInfo info) {
      accessUnits_.fetch_add(1, std::memory_order_relaxed);
      accessUnitBytes_.fetch_add(data.size(), std::memory_order_relaxed);
      lastTimestamp_.store(info.timestamp, std::memory_order_relaxed);
      if (accessUnitHandler_) {
        std::vector<std::uint8_t> accessUnit;
        accessUnit.reserve(data.size());
        for (const auto value : data) {
          accessUnit.push_back(std::to_integer<std::uint8_t>(value));
        }
        accessUnitHandler_(std::move(accessUnit), info.timestamp);
      }
    });
  } catch (const std::exception& error) {
    impl_.reset();
    session_.Restart(config_.sessionId);
    return Fail(error.what());
  }

  lastError_.clear();
  return true;
}

bool LibDataChannelReceiver::AcceptOffer(const std::string& sessionId,
                                         const std::string& sdp) {
  const ReceiverError validation = session_.AcceptOffer(sessionId, sdp);
  if (validation != ReceiverError::None) {
    return Fail(ReceiverErrorName(validation));
  }
  if (!impl_ || !impl_->peerConnection) return Fail("receiver is not started");

  try {
    impl_->peerConnection->setRemoteDescription(
        rtc::Description(sdp, rtc::Description::Type::Offer));
    impl_->peerConnection->setLocalDescription();
    impl_->peerConnection->gatherLocalCandidates();
    remoteOfferHasH264_.store(session_.offer().hasH264, std::memory_order_relaxed);
  } catch (const std::exception& error) {
    return Fail(error.what());
  }

  lastError_.clear();
  return true;
}

bool LibDataChannelReceiver::AddRemoteCandidate(const std::string& candidate,
                                                const std::string& mid) {
  const CandidateResult validation = session_.AcceptIce(candidate);
  if (validation.error != ReceiverError::None) {
    return Fail(ReceiverErrorName(validation.error));
  }
  if (!impl_ || !impl_->peerConnection) return Fail("receiver is not started");

  try {
    impl_->peerConnection->addRemoteCandidate(rtc::Candidate(candidate, mid));
  } catch (const std::exception& error) {
    return Fail(error.what());
  }

  lastError_.clear();
  return true;
}

void LibDataChannelReceiver::Close() {
  if (impl_ && impl_->peerConnection) {
    impl_->peerConnection->resetCallbacks();
    impl_->peerConnection->close();
  }
  impl_.reset();
  if (session_.state() == ReceiverState::Connected) session_.MarkDisconnected();
}

LibDataChannelReceiverMetrics LibDataChannelReceiver::metrics() const {
  LibDataChannelReceiverMetrics result;
  result.accessUnits = accessUnits_.load(std::memory_order_relaxed);
  result.accessUnitBytes = accessUnitBytes_.load(std::memory_order_relaxed);
  result.lastTimestamp = lastTimestamp_.load(std::memory_order_relaxed);
  if (impl_ && impl_->peerConnection) {
    result.bytesReceived = impl_->peerConnection->bytesReceived();
    if (const auto rtt = impl_->peerConnection->rtt()) {
      result.rttMilliseconds = rtt->count();
    }
    rtc::Candidate local;
    rtc::Candidate remote;
    if (impl_->peerConnection->getSelectedCandidatePair(&local, &remote)) {
      result.selectedLocalCandidate = local.candidate();
      result.selectedRemoteCandidate = remote.candidate();
    }
  }
  result.peerState = peerState_.load(std::memory_order_relaxed);
  result.iceState = iceState_.load(std::memory_order_relaxed);
  result.peerStateChanges = peerStateChanges_.load(std::memory_order_relaxed);
  result.iceStateChanges = iceStateChanges_.load(std::memory_order_relaxed);
  result.remoteOfferHasH264 = remoteOfferHasH264_.load(std::memory_order_relaxed);
  result.localAnswerHasH264 = localAnswerHasH264_.load(std::memory_order_relaxed);
  return result;
}

void LibDataChannelReceiver::SetLocalDescriptionHandler(LocalDescriptionHandler handler) {
  localDescriptionHandler_ = std::move(handler);
}

void LibDataChannelReceiver::SetLocalCandidateHandler(LocalCandidateHandler handler) {
  localCandidateHandler_ = std::move(handler);
}

void LibDataChannelReceiver::SetAccessUnitHandler(AccessUnitHandler handler) {
  accessUnitHandler_ = std::move(handler);
}

}  // namespace cambridge::native::receiver
