#include "libdatachannel_receiver.h"

#include <rtc/rtc.hpp>

#include <utility>

namespace cambridge::native::receiver {

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

  try {
    rtc::Configuration configuration;
    configuration.disableAutoGathering = true;
    configuration.disableAutoNegotiation = true;
    if (!config_.bindAddress.empty()) configuration.bindAddress = config_.bindAddress;

    impl_ = std::make_unique<Impl>();
    impl_->peerConnection = std::make_shared<rtc::PeerConnection>(configuration);

    impl_->peerConnection->onLocalDescription(
        [this](rtc::Description description) {
          if (localDescriptionHandler_) {
            localDescriptionHandler_(description.typeString(),
                                     description.generateSdp());
          }
        });
    impl_->peerConnection->onLocalCandidate(
        [this](rtc::Candidate candidate) {
          if (localCandidateHandler_) {
            localCandidateHandler_(candidate.candidate(), candidate.mid());
          }
        });
    impl_->peerConnection->onStateChange([this](rtc::PeerConnection::State state) {
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

    rtc::Description::Video media("video", rtc::Description::Direction::RecvOnly);
    media.addH264Codec(96);
    media.setBitrate(static_cast<int>(config_.h264BitrateKbps));
    impl_->track = impl_->peerConnection->addTrack(media);
    impl_->track->setMediaHandler(std::make_shared<rtc::H264RtpDepacketizer>());
    impl_->track->onFrame([this](rtc::binary data, rtc::FrameInfo info) {
      accessUnits_.fetch_add(1, std::memory_order_relaxed);
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
  return {accessUnits_.load(std::memory_order_relaxed),
          lastTimestamp_.load(std::memory_order_relaxed)};
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
