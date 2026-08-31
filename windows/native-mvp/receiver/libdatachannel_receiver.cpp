#include "libdatachannel_receiver.h"

#include <rtc/rtc.hpp>

#include <atomic>
#include <cstddef>
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

std::string FirstH264Codec(const std::string& sdp) {
  std::size_t lineStart = 0;
  while (lineStart <= sdp.size()) {
    const std::size_t lineEnd = sdp.find_first_of("\r\n", lineStart);
    const std::string line = sdp.substr(
        lineStart, lineEnd == std::string::npos ? sdp.size() - lineStart : lineEnd - lineStart);
    std::string lower = line;
    for (char& value : lower) {
      if (value >= 'A' && value <= 'Z') value = static_cast<char>(value - 'A' + 'a');
    }
    if (lower.rfind("a=rtpmap:", 0) == 0) {
      const std::size_t codecStart = lower.find("h264/");
      if (codecStart != std::string::npos) {
        const std::size_t codecEnd = lower.find_first_of(" \t;", codecStart);
        std::string codec = lower.substr(
            codecStart, codecEnd == std::string::npos ? lower.size() - codecStart
                                                       : codecEnd - codecStart);
        if (codec.size() >= 4) {
          codec[0] = 'H';
          codec[1] = '2';
          codec[2] = '6';
          codec[3] = '4';
        }
        return codec;
      }
    }
    if (lineEnd == std::string::npos) break;
    lineStart = lineEnd + 1;
    if (lineStart < sdp.size() && sdp[lineStart - 1] == '\r' && sdp[lineStart] == '\n') {
      ++lineStart;
    }
  }
  return {};
}

class IncomingMediaAudit final : public rtc::MediaHandler {
 public:
  void incoming(rtc::message_vector& messages,
                const rtc::message_callback& /*send*/) override {
    for (const auto& message : messages) {
      if (!message) continue;
      rawMediaBytes.fetch_add(message->size(), std::memory_order_relaxed);
      if (message->type == rtc::Message::Control || rtc::IsRtcp(*message)) {
        rawRtcpPackets.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
      rawRtpPackets.fetch_add(1, std::memory_order_relaxed);
      if (!firstRtpObserved.load(std::memory_order_acquire) &&
          message->size() >= sizeof(rtc::RtpHeader)) {
        const auto* header = reinterpret_cast<const rtc::RtpHeader*>(message->data());
        bool expected = false;
        if (firstRtpObserved.compare_exchange_strong(expected, true,
                                                       std::memory_order_acq_rel)) {
          firstRtpPayloadType.store(header->payloadType(), std::memory_order_relaxed);
          firstRtpTimestamp.store(header->timestamp(), std::memory_order_relaxed);
          firstRtpSsrc.store(header->ssrc(), std::memory_order_relaxed);
        }
      }
    }
  }

  std::atomic<std::uint64_t> rawRtpPackets = 0;
  std::atomic<std::uint64_t> rawRtcpPackets = 0;
  std::atomic<std::uint64_t> rawMediaBytes = 0;
  std::atomic<std::uint8_t> firstRtpPayloadType = 0;
  std::atomic<std::uint32_t> firstRtpTimestamp = 0;
  std::atomic<std::uint32_t> firstRtpSsrc = 0;
  std::atomic_bool firstRtpObserved = false;
};

class TrackInputAudit final : public rtc::MediaHandler {
 public:
  void incoming(rtc::message_vector& messages,
                const rtc::message_callback& /*send*/) override {
    for (const auto& message : messages) {
      if (!message) continue;
      if (message->type == rtc::Message::Control || rtc::IsRtcp(*message)) {
        trackRtcpPackets.fetch_add(1, std::memory_order_relaxed);
      } else {
        trackRtpPackets.fetch_add(1, std::memory_order_relaxed);
      }
    }
  }

  std::atomic<std::uint64_t> trackRtpPackets = 0;
  std::atomic<std::uint64_t> trackRtcpPackets = 0;
};

class DepacketizerOutputAudit final : public rtc::MediaHandler {
 public:
  void incoming(rtc::message_vector& messages,
                const rtc::message_callback& /*send*/) override {
    for (const auto& message : messages) {
      if (message && message->frameInfo) {
        depacketizerFrames.fetch_add(1, std::memory_order_relaxed);
      }
    }
  }

  std::atomic<std::uint64_t> depacketizerFrames = 0;
};

}  // namespace

struct LibDataChannelReceiver::Impl {
  std::shared_ptr<rtc::PeerConnection> peerConnection;
  std::shared_ptr<rtc::Track> track;
  std::shared_ptr<IncomingMediaAudit> incomingMediaAudit;
  std::shared_ptr<TrackInputAudit> trackInputAudit;
  std::shared_ptr<DepacketizerOutputAudit> depacketizerOutputAudit;
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
  {
    std::lock_guard lock(codecMutex_);
    remoteOfferCodec_.clear();
    localAnswerCodec_.clear();
  }

  try {
    rtc::Configuration configuration;
    configuration.disableAutoGathering = true;
    configuration.disableAutoNegotiation = true;
    if (!config_.bindAddress.empty()) configuration.bindAddress = config_.bindAddress;

    impl_ = std::make_unique<Impl>();
    impl_->peerConnection = std::make_shared<rtc::PeerConnection>(configuration);
    impl_->incomingMediaAudit = std::make_shared<IncomingMediaAudit>();
    impl_->peerConnection->setMediaHandler(impl_->incomingMediaAudit);

    impl_->peerConnection->onLocalDescription(
        [this](rtc::Description description) {
          const std::string type = description.typeString();
          const std::string sdp = description.generateSdp();
          if (type == "answer") {
            localAnswerHasH264_.store(SdpContainsH264(sdp), std::memory_order_relaxed);
            std::lock_guard lock(codecMutex_);
            localAnswerCodec_ = FirstH264Codec(sdp);
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

    impl_->peerConnection->onTrack([this](std::shared_ptr<rtc::Track> track) {
      if (!track) return;
      impl_->track = std::move(track);

      // Configure the remote Track created from the browser's offer. A local
      // placeholder Track is intentionally not added here: doing so can
      // leave two track lines and make libdatachannel route RTP by an SSRC
      // map that is not populated for the browser's remote Track.
      // Keep the input audit and RTCP session in the receive chain before
      // H.264 depacketization, while preserving the depacketized frameInfo
      // messages for the output audit and onFrame callback.
      auto h264Depacketizer = std::make_shared<rtc::H264RtpDepacketizer>();
      impl_->trackInputAudit = std::make_shared<TrackInputAudit>();
      impl_->depacketizerOutputAudit = std::make_shared<DepacketizerOutputAudit>();
      h264Depacketizer->addToChain(impl_->trackInputAudit);
      impl_->trackInputAudit->addToChain(std::make_shared<rtc::RtcpReceivingSession>());
      auto outputAudit = impl_->depacketizerOutputAudit;
      outputAudit->addToChain(h264Depacketizer);
      impl_->track->setMediaHandler(outputAudit);
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
    });
  } catch (const std::exception& error) {
    impl_.reset();
    session_.Restart(config_.sessionId);
    return Fail(error.what());
  }

  lastError_.clear();
  return true;
}

bool LibDataChannelReceiver::AdoptSessionId(const std::string& sessionId) {
  const ReceiverError result = session_.AdoptSessionId(sessionId);
  if (result != ReceiverError::None) return Fail(ReceiverErrorName(result));
  config_.sessionId = sessionId;
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
    {
      std::lock_guard lock(codecMutex_);
      remoteOfferCodec_ = FirstH264Codec(sdp);
    }
  } catch (const std::exception& error) {
    return Fail(error.what());
  }

  lastError_.clear();
  return true;
}

bool LibDataChannelReceiver::AddRemoteCandidate(const std::string& candidate,
                                                 const std::string& mid) {
  const CandidateResult validation = session_.AcceptIce(candidate);
  if (validation.error == ReceiverError::RelayRejected ||
      validation.error == ReceiverError::NonPrivateHostRejected) {
    // Browser ICE gathering can expose VPN/public/IPv6 host candidates. They
    // are outside the LAN-only policy, but must not terminate the signaling
    // session before a usable private IPv4 candidate arrives.
    lastError_.clear();
    return true;
  }
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
    if (impl_->incomingMediaAudit) {
      result.rawRtpPackets = impl_->incomingMediaAudit->rawRtpPackets.load(
          std::memory_order_relaxed);
      result.rawRtcpPackets = impl_->incomingMediaAudit->rawRtcpPackets.load(
          std::memory_order_relaxed);
      result.rawMediaBytes = impl_->incomingMediaAudit->rawMediaBytes.load(
          std::memory_order_relaxed);
      if (impl_->trackInputAudit) {
        result.trackRtpPackets = impl_->trackInputAudit->trackRtpPackets.load(
            std::memory_order_relaxed);
        result.trackRtcpPackets = impl_->trackInputAudit->trackRtcpPackets.load(
            std::memory_order_relaxed);
      }
      if (impl_->depacketizerOutputAudit) {
        result.depacketizerFrames = impl_->depacketizerOutputAudit->depacketizerFrames.load(
            std::memory_order_relaxed);
      }
      result.firstRtpPayloadType = impl_->incomingMediaAudit->firstRtpPayloadType.load(
          std::memory_order_relaxed);
      result.firstRtpTimestamp = impl_->incomingMediaAudit->firstRtpTimestamp.load(
          std::memory_order_relaxed);
      result.firstRtpSsrc = impl_->incomingMediaAudit->firstRtpSsrc.load(
          std::memory_order_relaxed);
      result.firstRtpObserved = impl_->incomingMediaAudit->firstRtpObserved.load(
          std::memory_order_relaxed);
    }
    result.trackOpen = impl_->track && impl_->track->isOpen();
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
  {
    std::lock_guard lock(codecMutex_);
    result.remoteOfferCodec = remoteOfferCodec_;
    result.localAnswerCodec = localAnswerCodec_;
  }
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
