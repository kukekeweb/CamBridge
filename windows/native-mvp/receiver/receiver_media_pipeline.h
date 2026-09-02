#pragma once

#include "h264_decoder.h"
#include "latest_frame_publisher.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace cambridge::native {

double EstimateFpsFromTimestamps(std::uint64_t frameCount,
                                 std::int64_t firstTimestamp100ns,
                                 std::int64_t lastTimestamp100ns);

// Keeps the Virtual Camera IPC contract stable when a decoder or browser
// encoder changes the coded/display size. Exact target frames are returned
// without a copy; mismatched valid NV12 frames are scaled into target format.
bool NormalizeNv12Frame(const Nv12Frame& input, std::uint32_t targetWidth,
                        std::uint32_t targetHeight, Nv12Frame* output);

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
  double decodedFps = 0.0;
  double publishedFps = 0.0;
  std::uint64_t publishErrors = 0;
  std::uint64_t normalizedFrames = 0;
  std::uint64_t normalizationErrors = 0;
  std::uint32_t publishedWidth = 0;
  std::uint32_t publishedHeight = 0;
  std::uint32_t publishedStride = 0;
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
  std::int64_t firstDecodedTimestamp100ns_ = 0;
  std::int64_t firstPublishedTimestamp100ns_ = 0;
};

}  // namespace cambridge::native
