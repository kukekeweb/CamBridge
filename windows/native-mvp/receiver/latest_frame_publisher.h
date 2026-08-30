#pragma once

#include "frame_ipc.h"

#include <cstdint>
#include <string>

namespace cambridge::native {

struct LatestFramePublisherMetrics {
  std::uint64_t publishCalls = 0;
  std::uint64_t publishedFrames = 0;
  std::uint64_t rejectedFrames = 0;
  std::int64_t lastTimestamp100ns = 0;
};

// User-mode boundary between a decoder and the existing latest-frame IPC.
// The publisher owns no network, Media Foundation, or Frame Server state.
class LatestFramePublisher {
 public:
  LatestFramePublisher() = default;
  ~LatestFramePublisher();

  LatestFramePublisher(const LatestFramePublisher&) = delete;
  LatestFramePublisher& operator=(const LatestFramePublisher&) = delete;

  bool Start(const std::wstring& mappingName = kFrameMappingName,
             const std::wstring& eventName = kFrameReadyEventName);
  bool Publish(const Nv12Frame& frame);
  void Stop();

  bool IsStarted() const { return started_; }
  LatestFramePublisherMetrics metrics() const { return metrics_; }

 private:
  SharedFrameProducer producer_;
  LatestFramePublisherMetrics metrics_;
  bool started_ = false;
};

}  // namespace cambridge::native
