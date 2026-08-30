#include "receiver_media_pipeline.h"

#include <limits>
#include <utility>

namespace cambridge::native {
namespace {

constexpr std::int64_t k100nsPerSecond = 10000000;

bool ValidClockRate(std::uint32_t value) { return value > 0; }

std::int64_t RtpTo100ns(std::uint32_t timestamp, std::uint32_t firstTimestamp,
                        std::uint32_t clockRate) {
  const std::uint32_t delta = timestamp - firstTimestamp;
  const auto scaled = static_cast<std::uint64_t>(delta) * k100nsPerSecond;
  return static_cast<std::int64_t>(scaled / clockRate);
}

}  // namespace

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
  ++metrics_.decodedFrames;
  metrics_.lastTimestamp100ns = frame.timestamp100ns;
  if (publisher_.Publish(frame)) {
    ++metrics_.publishedFrames;
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
