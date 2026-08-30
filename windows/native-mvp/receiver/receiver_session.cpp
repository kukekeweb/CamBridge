#include "receiver_session.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace cambridge::native::receiver {
namespace {

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return value;
}

std::vector<std::string> Lines(std::string_view text) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t end = text.find('\n', start);
    std::string line(text.substr(start, end == std::string_view::npos ? text.size() - start : end - start));
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(std::move(line));
    if (end == std::string_view::npos) break;
    start = end + 1;
  }
  return lines;
}

bool IsPrivateIpv4(std::string_view address) {
  std::istringstream input{std::string(address)};
  std::string part;
  int octets[4]{};
  for (int index = 0; index < 4; ++index) {
    if (!std::getline(input, part, '.') || part.empty() || part.size() > 3 ||
        !std::all_of(part.begin(), part.end(), [](unsigned char character) {
          return std::isdigit(character) != 0;
        })) {
      return false;
    }
    try {
      octets[index] = std::stoi(part);
    } catch (...) {
      return false;
    }
    if (octets[index] < 0 || octets[index] > 255) return false;
  }
  if (input.rdbuf()->in_avail() != 0) return false;
  return octets[0] == 10 ||
         (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31) ||
         (octets[0] == 192 && octets[1] == 168);
}

ReceiverError ParseOffer(std::string_view sdp, OfferDescription* description) {
  if (description == nullptr) return ReceiverError::InvalidState;
  OfferDescription parsed;
  bool inVideo = false;
  bool videoDirectionSpecified = false;
  for (const std::string& line : Lines(sdp)) {
    if (line.rfind("m=", 0) == 0) {
      inVideo = line.rfind("m=video ", 0) == 0;
      if (inVideo) ++parsed.videoMLineCount;
      continue;
    }
    if (!inVideo) continue;
    const std::string lower = Lower(line);
    if (lower.rfind("a=rtpmap:", 0) == 0 && lower.find(" h264/") != std::string::npos) {
      parsed.hasH264 = true;
    } else if (lower == "a=sendonly" || lower == "a=sendrecv") {
      parsed.sendsVideo = true;
      videoDirectionSpecified = true;
    } else if (lower == "a=recvonly" || lower == "a=inactive") {
      videoDirectionSpecified = true;
    }
  }
  if (parsed.videoMLineCount == 0) return ReceiverError::VideoMLineMissing;
  if (parsed.videoMLineCount > 1) return ReceiverError::MultipleVideoMLines;
  if (!parsed.hasH264) return ReceiverError::H264Missing;
  if (videoDirectionSpecified && !parsed.sendsVideo) return ReceiverError::VideoDirectionInvalid;
  *description = parsed;
  return ReceiverError::None;
}

}  // namespace

ReceiverSession::ReceiverSession(std::string sessionId) : sessionId_(std::move(sessionId)) {}

ReceiverError ReceiverSession::Start() {
  if (state_ != ReceiverState::Idle) return ReceiverError::InvalidState;
  state_ = ReceiverState::WaitingForOffer;
  return ReceiverError::None;
}

ReceiverError ReceiverSession::AdoptSessionId(std::string_view sessionId) {
  if (state_ != ReceiverState::WaitingForOffer || sessionId.empty()) {
    return ReceiverError::InvalidState;
  }
  sessionId_ = std::string(sessionId);
  return ReceiverError::None;
}

ReceiverError ReceiverSession::AcceptOffer(std::string_view sessionId, std::string_view sdp) {
  if (state_ != ReceiverState::WaitingForOffer) return ReceiverError::InvalidState;
  if (sessionId != sessionId_) return ReceiverError::SessionMismatch;
  OfferDescription parsed;
  const ReceiverError error = ParseOffer(sdp, &parsed);
  if (error != ReceiverError::None) return error;
  offer_ = parsed;
  state_ = ReceiverState::OfferReceived;
  return ReceiverError::None;
}

CandidateResult ReceiverSession::AcceptIce(std::string_view candidate) {
  CandidateResult result;
  if (state_ != ReceiverState::OfferReceived && state_ != ReceiverState::Connected) {
    result.error = ReceiverError::InvalidState;
    return result;
  }
  std::istringstream input{std::string(candidate)};
  std::string foundation;
  std::string component;
  std::string transport;
  std::string priority;
  std::string address;
  std::string port;
  std::string typLabel;
  std::string type;
  if (!(input >> foundation >> component >> transport >> priority >> address >> port >> typLabel >> type) ||
      typLabel != "typ") {
    return result;
  }
  result.address = address;
  if (type == "relay") {
    result.kind = CandidateKind::Relay;
    result.error = ReceiverError::RelayRejected;
    return result;
  }
  if (type != "host" || !IsPrivateIpv4(address)) {
    result.kind = CandidateKind::NonPrivateHost;
    result.error = ReceiverError::NonPrivateHostRejected;
    return result;
  }
  result.kind = CandidateKind::PrivateIpv4Host;
  result.error = ReceiverError::None;
  return result;
}

ReceiverError ReceiverSession::MarkConnected() {
  if (state_ != ReceiverState::OfferReceived) return ReceiverError::InvalidState;
  state_ = ReceiverState::Connected;
  return ReceiverError::None;
}

ReceiverError ReceiverSession::MarkDisconnected() {
  if (state_ != ReceiverState::Connected) return ReceiverError::InvalidState;
  state_ = ReceiverState::Disconnected;
  return ReceiverError::None;
}

ReceiverError ReceiverSession::Restart(std::string sessionId) {
  if (state_ != ReceiverState::Disconnected && state_ != ReceiverState::Idle) {
    return ReceiverError::InvalidState;
  }
  sessionId_ = std::move(sessionId);
  offer_ = {};
  state_ = ReceiverState::WaitingForOffer;
  return ReceiverError::None;
}

const char* ReceiverErrorName(ReceiverError error) {
  switch (error) {
    case ReceiverError::None: return "none";
    case ReceiverError::InvalidState: return "invalid-state";
    case ReceiverError::SessionMismatch: return "session-mismatch";
    case ReceiverError::VideoMLineMissing: return "video-m-line-missing";
    case ReceiverError::MultipleVideoMLines: return "multiple-video-m-lines";
    case ReceiverError::H264Missing: return "h264-missing";
    case ReceiverError::VideoDirectionInvalid: return "video-direction-invalid";
    case ReceiverError::InvalidCandidate: return "invalid-candidate";
    case ReceiverError::RelayRejected: return "relay-rejected";
    case ReceiverError::NonPrivateHostRejected: return "non-private-host-rejected";
  }
  return "unknown";
}

const char* ReceiverStateName(ReceiverState state) {
  switch (state) {
    case ReceiverState::Idle: return "idle";
    case ReceiverState::WaitingForOffer: return "waiting-for-offer";
    case ReceiverState::OfferReceived: return "offer-received";
    case ReceiverState::Connected: return "connected";
    case ReceiverState::Disconnected: return "disconnected";
  }
  return "unknown";
}

}  // namespace cambridge::native::receiver
