#include "receiver_media_pipeline.h"

#include <limits>
#include <cstring>
#include <utility>

namespace cambridge::native {
namespace {

constexpr std::int64_t k100nsPerSecond = 10000000;

bool ValidClockRate(std::uint32_t value) { return value > 0; }

bool ValidNv12Frame(const Nv12Frame& frame) {
  return frame.width > 0 && frame.height > 0 && (frame.width % 2) == 0 &&
         (frame.height % 2) == 0 && frame.stride >= frame.width &&
         frame.bytes.size() >= static_cast<std::size_t>(frame.stride) * frame.height * 3 / 2;
}

std::int64_t RtpTo100ns(std::uint32_t timestamp, std::uint32_t firstTimestamp,
                        std::uint32_t clockRate) {
  const std::uint32_t delta = timestamp - firstTimestamp;
  const auto scaled = static_cast<std::uint64_t>(delta) * k100nsPerSecond;
  return static_cast<std::int64_t>(scaled / clockRate);
}

}  // namespace

bool NormalizeNv12Frame(const Nv12Frame& input, std::uint32_t targetWidth,
                        std::uint32_t targetHeight, Nv12Frame* output) {
  if (output == nullptr || targetWidth == 0 || targetHeight == 0 ||
      (targetWidth % 2) != 0 || (targetHeight % 2) != 0 || !ValidNv12Frame(input)) {
    return false;
  }

  if (input.width == targetWidth && input.height == targetHeight &&
      input.stride == targetWidth &&
      input.bytes.size() >= static_cast<std::size_t>(targetWidth) * targetHeight * 3 / 2) {
    *output = input;
    return true;
  }

  Nv12Frame result;
  result.width = targetWidth;
  result.height = targetHeight;
  result.stride = targetWidth;
  result.timestamp100ns = input.timestamp100ns;
  result.sequence = input.sequence;
  const std::size_t targetLumaBytes = static_cast<std::size_t>(targetWidth) * targetHeight;
  result.bytes.resize(targetLumaBytes + targetLumaBytes / 2);

  const auto* sourceY = input.bytes.data();
  auto* destinationY = result.bytes.data();
  for (std::uint32_t y = 0; y < targetHeight; ++y) {
    const std::uint32_t sourceYRow =
        static_cast<std::uint64_t>(y) * input.height / targetHeight;
    for (std::uint32_t x = 0; x < targetWidth; ++x) {
      const std::uint32_t sourceX =
          static_cast<std::uint64_t>(x) * input.width / targetWidth;
      destinationY[static_cast<std::size_t>(y) * targetWidth + x] =
          sourceY[static_cast<std::size_t>(sourceYRow) * input.stride + sourceX];
    }
  }

  const auto* sourceUv = input.bytes.data() + static_cast<std::size_t>(input.stride) * input.height;
  auto* destinationUv = result.bytes.data() + targetLumaBytes;
  const std::uint32_t targetChromaHeight = targetHeight / 2;
  const std::uint32_t targetChromaWidth = targetWidth / 2;
  const std::uint32_t sourceChromaHeight = input.height / 2;
  const std::uint32_t sourceChromaWidth = input.width / 2;
  for (std::uint32_t y = 0; y < targetChromaHeight; ++y) {
    const std::uint32_t sourceYRow =
        static_cast<std::uint64_t>(y) * sourceChromaHeight / targetChromaHeight;
    for (std::uint32_t x = 0; x < targetChromaWidth; ++x) {
      const std::uint32_t sourceX =
          static_cast<std::uint64_t>(x) * sourceChromaWidth / targetChromaWidth;
      const std::size_t sourceOffset = static_cast<std::size_t>(sourceYRow) * input.stride +
                                       static_cast<std::size_t>(sourceX) * 2;
      const std::size_t destinationOffset = static_cast<std::size_t>(y) * targetWidth +
                                            static_cast<std::size_t>(x) * 2;
      destinationUv[destinationOffset] = sourceUv[sourceOffset];
      destinationUv[destinationOffset + 1] = sourceUv[sourceOffset + 1];
    }
  }
  *output = std::move(result);
  return true;
}

double EstimateFpsFromTimestamps(std::uint64_t frameCount,
                                 std::int64_t firstTimestamp100ns,
                                 std::int64_t lastTimestamp100ns) {
  if (frameCount < 2 || lastTimestamp100ns <= firstTimestamp100ns) return 0.0;
  const auto elapsed100ns = static_cast<long double>(lastTimestamp100ns) -
                            static_cast<long double>(firstTimestamp100ns);
  const auto elapsedSeconds = elapsed100ns / static_cast<long double>(k100nsPerSecond);
  if (elapsedSeconds <= 0.0L) return 0.0;
  return static_cast<double>(static_cast<long double>(frameCount - 1) / elapsedSeconds);
}

ReceiverMediaPipeline::~ReceiverMediaPipeline() { Stop(); }

bool ReceiverMediaPipeline::Fail(std::string message) {
  lastError_ = std::move(message);
  return false;
}

bool ReceiverMediaPipeline::Start(const ReceiverMediaPipelineConfig& config) {
  Stop();
  std::lock_guard lock(mutex_);
  metrics_ = {};
  lastError_.clear();
  config_ = config;
  hasFirstRtpTimestamp_ = false;
  firstRtpTimestamp_ = 0;
  lastRtpTimestamp_ = 0;
  firstDecodedTimestamp100ns_ = 0;
  firstPublishedTimestamp100ns_ = 0;

  if (!ValidClockRate(config_.rtpClockRate)) return Fail("invalid RTP clock rate");
  if (!publisher_.Start(config_.mappingName, config_.eventName)) {
    return Fail("latest-frame publisher could not start");
  }
  metrics_.publisherStarted = true;

  decoder_.SetFrameHandler([this](Nv12Frame frame) {
    OnDecodedFrame(std::move(frame));
  });
  if (!decoder_.Start(config_.decoder)) {
    publisher_.Stop();
    metrics_.publisherStarted = false;
    return Fail(decoder_.lastError().empty() ? "H264 decoder could not start"
                                             : decoder_.lastError());
  }
  metrics_.decoderStarted = true;
  started_ = true;
  return true;
}

bool ReceiverMediaPipeline::SubmitAccessUnit(
    const std::vector<std::uint8_t>& accessUnit, std::uint32_t rtpTimestamp) {
  std::lock_guard lock(mutex_);
  if (!started_) {
    ++metrics_.inputErrors;
    return Fail("receiver media pipeline is not started");
  }
  if (accessUnit.empty()) {
    ++metrics_.inputErrors;
    return Fail("empty H264 access unit");
  }

  if (!hasFirstRtpTimestamp_) {
    hasFirstRtpTimestamp_ = true;
    firstRtpTimestamp_ = rtpTimestamp;
  }
  const std::int64_t timestamp100ns =
      RtpTo100ns(rtpTimestamp, firstRtpTimestamp_, config_.rtpClockRate);
  const std::int64_t duration100ns =
      static_cast<std::int64_t>(k100nsPerSecond) * config_.decoder.fpsDenominator /
      config_.decoder.fpsNumerator;
  if (duration100ns <= 0) {
    ++metrics_.inputErrors;
    return Fail("invalid decoder frame duration");
  }

  if (!decoder_.SubmitAccessUnit(accessUnit, timestamp100ns, duration100ns)) {
    ++metrics_.inputErrors;
    return Fail(decoder_.lastError().empty() ? "H264 access unit decode failed"
                                             : decoder_.lastError());
  }
  ++metrics_.inputAccessUnits;
  lastRtpTimestamp_ = rtpTimestamp;
  metrics_.lastTimestamp100ns = timestamp100ns;
  metrics_.decoder = decoder_.metrics();
  metrics_.publisher = publisher_.metrics();
  lastError_.clear();
  return true;
}

void ReceiverMediaPipeline::OnDecodedFrame(Nv12Frame frame) {
  if (metrics_.decodedFrames == 0) {
    firstDecodedTimestamp100ns_ = frame.timestamp100ns;
  }
  ++metrics_.decodedFrames;
  metrics_.lastTimestamp100ns = frame.timestamp100ns;
  metrics_.decodedFps = EstimateFpsFromTimestamps(
      metrics_.decodedFrames, firstDecodedTimestamp100ns_, frame.timestamp100ns);
  Nv12Frame publishFrame;
  if (!NormalizeNv12Frame(frame, config_.decoder.width, config_.decoder.height,
                          &publishFrame)) {
    ++metrics_.normalizationErrors;
    lastError_ = "decoded NV12 frame could not be normalized to Virtual Camera format";
    return;
  }
  if (publishFrame.width != frame.width || publishFrame.height != frame.height ||
      publishFrame.stride != frame.stride) {
    ++metrics_.normalizedFrames;
  }
  metrics_.publishedWidth = publishFrame.width;
  metrics_.publishedHeight = publishFrame.height;
  metrics_.publishedStride = publishFrame.stride;
  if (publisher_.Publish(publishFrame)) {
    if (metrics_.publishedFrames == 0) {
      firstPublishedTimestamp100ns_ = frame.timestamp100ns;
    }
    ++metrics_.publishedFrames;
    metrics_.publishedFps = EstimateFpsFromTimestamps(
        metrics_.publishedFrames, firstPublishedTimestamp100ns_, frame.timestamp100ns);
  } else {
    ++metrics_.publishErrors;
    lastError_ = "latest-frame publisher rejected decoded NV12 frame";
  }
  metrics_.publisher = publisher_.metrics();
}

void ReceiverMediaPipeline::Stop() {
  std::lock_guard lock(mutex_);
  decoder_.Stop();
  publisher_.Stop();
  started_ = false;
  metrics_.decoderStarted = false;
  metrics_.publisherStarted = false;
  metrics_.decoder = decoder_.metrics();
  metrics_.publisher = publisher_.metrics();
}

bool ReceiverMediaPipeline::IsStarted() const {
  std::lock_guard lock(mutex_);
  return started_;
}

ReceiverMediaPipelineMetrics ReceiverMediaPipeline::metrics() const {
  std::lock_guard lock(mutex_);
  ReceiverMediaPipelineMetrics result = metrics_;
  result.decoder = decoder_.metrics();
  result.publisher = publisher_.metrics();
  return result;
}

}  // namespace cambridge::native
