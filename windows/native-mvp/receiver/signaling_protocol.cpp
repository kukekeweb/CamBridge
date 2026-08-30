#include "signaling_protocol.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace cambridge::native::receiver {
namespace {

using json = nlohmann::json;

SignalingMessageType ParseType(const std::string& type) {
  if (type == "hello") return SignalingMessageType::Hello;
  if (type == "offer") return SignalingMessageType::Offer;
  if (type == "answer") return SignalingMessageType::Answer;
  if (type == "ice") return SignalingMessageType::Ice;
  if (type == "close") return SignalingMessageType::Close;
  if (type == "state") return SignalingMessageType::State;
  if (type == "stats") return SignalingMessageType::Stats;
  return SignalingMessageType::Unknown;
}

bool Fail(std::string* error, const char* message) {
  if (error != nullptr) *error = message;
  return false;
}

bool RequireString(const json& object, const char* key, std::string* value,
                   std::string* error) {
  const auto iterator = object.find(key);
  if (iterator == object.end() || !iterator->is_string()) {
    return Fail(error, key);
  }
  *value = iterator->get<std::string>();
  if (value->empty()) return Fail(error, key);
  return true;
}

}  // namespace

const char* SignalingMessageTypeName(SignalingMessageType type) {
  switch (type) {
    case SignalingMessageType::Unknown: return "unknown";
    case SignalingMessageType::Hello: return "hello";
    case SignalingMessageType::Offer: return "offer";
    case SignalingMessageType::Answer: return "answer";
    case SignalingMessageType::Ice: return "ice";
    case SignalingMessageType::Close: return "close";
    case SignalingMessageType::State: return "state";
    case SignalingMessageType::Stats: return "stats";
  }
  return "unknown";
}

bool ParseSignalingMessage(const std::string& jsonText,
                           SignalingMessage* message,
                           std::string* error) {
  if (message == nullptr) return Fail(error, "message output is null");
  *message = {};
  if (error != nullptr) error->clear();

  json object;
  try {
    object = json::parse(jsonText);
  } catch (const json::exception&) {
    return Fail(error, "invalid json");
  }
  if (!object.is_object()) return Fail(error, "message is not an object");

  const auto version = object.find("version");
  if (version != object.end() && (!version->is_number_integer() || *version != 1)) {
    return Fail(error, "unsupported version");
  }

  std::string type;
  if (!RequireString(object, "type", &type, error)) return false;
  message->type = ParseType(type);
  if (message->type == SignalingMessageType::Unknown) {
    return Fail(error, "unsupported type");
  }
  if (!RequireString(object, "sessionId", &message->sessionId, error)) return false;

  if (message->type == SignalingMessageType::Hello) {
    if (!RequireString(object, "role", &message->role, error)) return false;
    if (message->role != "browser" && message->role != "native") {
      return Fail(error, "invalid role");
    }
  } else if (message->type == SignalingMessageType::Offer ||
             message->type == SignalingMessageType::Answer) {
    if (!RequireString(object, "sdp", &message->sdp, error)) return false;
  } else if (message->type == SignalingMessageType::Ice) {
    const auto candidate = object.find("candidate");
    if (candidate == object.end() || candidate->is_null()) {
      message->candidateIsNull = true;
    } else if (candidate->is_object()) {
      if (!RequireString(*candidate, "candidate", &message->candidate, error)) return false;
      const auto mid = candidate->find("sdpMid");
      if (mid != candidate->end() && mid->is_string()) message->mid = mid->get<std::string>();
      if (message->mid.empty()) {
        const auto alternateMid = candidate->find("mid");
        if (alternateMid != candidate->end() && alternateMid->is_string()) {
          message->mid = alternateMid->get<std::string>();
        }
      }
    } else {
      return Fail(error, "invalid candidate");
    }
  }
  return true;
}

std::string SerializeHello(const std::string& sessionId) {
  return json{{"version", 1}, {"type", "hello"}, {"role", "native"},
              {"sessionId", sessionId}}
      .dump();
}

std::string SerializeAnswer(const std::string& sessionId, const std::string& sdp) {
  return json{{"version", 1}, {"type", "answer"}, {"sessionId", sessionId}, {"sdp", sdp}}
      .dump();
}

std::string SerializeIce(const std::string& sessionId,
                         const std::string& candidate,
                         const std::string& mid) {
  return json{{"version", 1},
              {"type", "ice"},
              {"sessionId", sessionId},
              {"candidate", {{"candidate", candidate}, {"sdpMid", mid}}}}
      .dump();
}

}  // namespace cambridge::native::receiver
