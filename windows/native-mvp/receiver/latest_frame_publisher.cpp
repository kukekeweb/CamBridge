#include "latest_frame_publisher.h"

namespace cambridge::native {

LatestFramePublisher::~LatestFramePublisher() { Stop(); }

bool LatestFramePublisher::Start(const std::wstring& mappingName,
                                 const std::wstring& eventName) {
  Stop();
  metrics_ = {};
  started_ = producer_.Create(mappingName, eventName);
  return started_;
}

bool LatestFramePublisher::Publish(const Nv12Frame& frame) {
  ++metrics_.publishCalls;
  if (!started_ || !producer_.Publish(frame)) {
    ++metrics_.rejectedFrames;
    return false;
  }
  ++metrics_.publishedFrames;
  metrics_.lastTimestamp100ns = frame.timestamp100ns;
  return true;
}

void LatestFramePublisher::Stop() {
  producer_.Close();
  started_ = false;
}

}  // namespace cambridge::native
