#pragma once

#include "frame_ipc.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cambridge::native {

struct H264DecoderConfig {
  std::uint32_t width = 1920;
  std::uint32_t height = 1080;
  std::uint32_t fpsNumerator = 60;
  std::uint32_t fpsDenominator = 1;
  bool preferHardware = true;
};

struct H264DecoderMetrics {
  std::uint64_t inputAccessUnits = 0;
  std::uint64_t outputFrames = 0;
  std::uint64_t decodeErrors = 0;
  std::uint32_t outputWidth = 0;
  std::uint32_t outputHeight = 0;
  std::uint32_t codedWidth = 0;
  std::uint32_t codedHeight = 0;
  std::uint32_t outputStride = 0;
  std::uint32_t outputStreamFlags = 0;
  std::uint32_t outputBufferSize = 0;
  std::uint32_t outputBufferAlignment = 0;
  std::uint64_t processOutputCalls = 0;
  std::uint64_t processOutputNeedMoreInput = 0;
  std::uint64_t processOutputStreamChanges = 0;
  std::int32_t lastProcessOutputHr = 0;
  std::uint32_t lastProcessOutputStatus = 0;
  std::string lastStreamChangeDescription;
  bool hardware = false;
  std::string selectedTransform;
};

class MediaFoundationH264Decoder {
 public:
  using FrameHandler = std::function<void(Nv12Frame)>;

  MediaFoundationH264Decoder();
  ~MediaFoundationH264Decoder();

  MediaFoundationH264Decoder(const MediaFoundationH264Decoder&) = delete;
  MediaFoundationH264Decoder& operator=(const MediaFoundationH264Decoder&) = delete;

  bool Start(const H264DecoderConfig& config = {});
  bool SubmitAccessUnit(const std::vector<std::uint8_t>& accessUnit,
                        std::int64_t timestamp100ns,
                        std::int64_t duration100ns);
  void Stop();

  void SetFrameHandler(FrameHandler handler) { frameHandler_ = std::move(handler); }
  bool IsStarted() const { return started_; }
  const std::string& lastError() const { return lastError_; }
  H264DecoderMetrics metrics() const { return metrics_; }

 private:
  bool Fail(std::string message);
  bool DrainOutput();
  H264DecoderConfig config_;
  H264DecoderMetrics metrics_;
  FrameHandler frameHandler_;
  std::string lastError_;
  struct Impl;
  std::unique_ptr<Impl> impl_;
  bool started_ = false;
};

}  // namespace cambridge::native
