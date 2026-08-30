#pragma once

#include "h264_decoder.h"
#include "latest_frame_publisher.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace cambridge::native {

struct ReceiverMediaPipelineConfig {
  H264DecoderConfig decoder;
  std::wstring mappingName = kFrameMappingName;
  std::wstring eventName = kFrameReadyEventName;
  std::uint32_t rtpClockRate = 90000;
};

struct ReceiverMediaPipelineMetrics {
  std::uint64_t inputAccessUnits = 0;
  std::uint64_t inputErrors = 0;
  std::uint64_t decodedFrames = 0;
  std::uint64_t publishedFrames = 0;
  std::uint64_t publishErrors = 0;
  std::int64_t lastTimestamp100ns = 0;
  bool decoderStarted = false;
  bool publisherStarted = false;
  H264DecoderMetrics decoder;
  LatestFramePublisherMetrics publisher;
};

// User-mode boundary between a WebRTC receiver and the existing latest-frame
// IPC. It owns neither PeerConnection nor Frame Server state.
class ReceiverMediaPipeline {
 public:
  ReceiverMediaPipeline() = default;
  ~ReceiverMediaPipeline();

  ReceiverMediaPipeline(const ReceiverMediaPipeline&) = delete;
  ReceiverMediaPipeline& operator=(const ReceiverMediaPipeline&) = delete;

  bool Start(const ReceiverMediaPipelineConfig& config = {});
  bool SubmitAccessUnit(const std::vector<std::uint8_t>& accessUnit,
                        std::uint32_t rtpTimestamp);
  void Stop();

  bool IsStarted() const;
  const std::string& lastError() const { return lastError_; }
  ReceiverMediaPipelineMetrics metrics() const;

 private:
  void OnDecodedFrame(Nv12Frame frame);
  bool Fail(std::string message);

  mutable std::mutex mutex_;
  ReceiverMediaPipelineConfig config_;
  MediaFoundationH264Decoder decoder_;
  LatestFramePublisher publisher_;
  ReceiverMediaPipelineMetrics metrics_;
  std::string lastError_;
  bool started_ = false;
  bool hasFirstRtpTimestamp_ = false;
  std::uint32_t firstRtpTimestamp_ = 0;
  std::uint32_t lastRtpTimestamp_ = 0;
};

}  // namespace cambridge::native
