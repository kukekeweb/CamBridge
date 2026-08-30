#pragma once

#include <string>

namespace cambridge::native::receiver {

enum class SignalingMessageType {
  Unknown,
  Hello,
  Offer,
  Answer,
  Ice,
  Close,
  State,
  Stats,
};

struct SignalingMessage {
  SignalingMessageType type = SignalingMessageType::Unknown;
  std::string role;
  std::string sessionId;
  std::string sdp;
  std::string candidate;
  std::string mid;
  bool candidateIsNull = false;
};

const char* SignalingMessageTypeName(SignalingMessageType type);

bool ParseSignalingMessage(const std::string& jsonText,
                           SignalingMessage* message,
                           std::string* error);
std::string SerializeHello(const std::string& sessionId);
std::string SerializeAnswer(const std::string& sessionId, const std::string& sdp);
std::string SerializeIce(const std::string& sessionId,
                         const std::string& candidate,
                         const std::string& mid);

}  // namespace cambridge::native::receiver
