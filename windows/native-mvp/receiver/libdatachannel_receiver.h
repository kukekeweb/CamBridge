#pragma once

#include "receiver_session.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cambridge::native::receiver {

struct LibDataChannelReceiverConfig {
  std::string sessionId;
  std::string bindAddress;
  unsigned int h264BitrateKbps = 5000;
};

struct LibDataChannelReceiverMetrics {
  std::uint64_t accessUnits = 0;
  std::uint32_t lastTimestamp = 0;
};

class LibDataChannelReceiver {
 public:
  using LocalDescriptionHandler =
      std::function<void(const std::string& type, const std::string& sdp)>;
  using LocalCandidateHandler =
      std::function<void(const std::string& candidate, const std::string& mid)>;
  using AccessUnitHandler =
      std::function<void(std::vector<std::uint8_t> accessUnit, std::uint32_t timestamp)>;

  explicit LibDataChannelReceiver(LibDataChannelReceiverConfig config);
  ~LibDataChannelReceiver();

  LibDataChannelReceiver(const LibDataChannelReceiver&) = delete;
  LibDataChannelReceiver& operator=(const LibDataChannelReceiver&) = delete;

  bool Start();
  bool AcceptOffer(const std::string& sessionId, const std::string& sdp);
  bool AddRemoteCandidate(const std::string& candidate, const std::string& mid);
  void Close();

  ReceiverState state() const { return session_.state(); }
  const std::string& lastError() const { return lastError_; }
  LibDataChannelReceiverMetrics metrics() const;

  void SetLocalDescriptionHandler(LocalDescriptionHandler handler);
  void SetLocalCandidateHandler(LocalCandidateHandler handler);
  void SetAccessUnitHandler(AccessUnitHandler handler);

 private:
  struct Impl;

  bool Fail(std::string message);
  LibDataChannelReceiverConfig config_;
  ReceiverSession session_;
  std::unique_ptr<Impl> impl_;
  std::string lastError_;
  LocalDescriptionHandler localDescriptionHandler_;
  LocalCandidateHandler localCandidateHandler_;
  AccessUnitHandler accessUnitHandler_;
  std::atomic<std::uint64_t> accessUnits_{0};
  std::atomic<std::uint32_t> lastTimestamp_{0};
};

}  // namespace cambridge::native::receiver
