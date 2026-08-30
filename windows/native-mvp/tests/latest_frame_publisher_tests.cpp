#include "latest_frame_publisher.h"

#include <Windows.h>

#include <cassert>
#include <cstddef>
#include <iostream>

namespace {

std::wstring UniqueName(const wchar_t* suffix) {
  return std::wstring(L"Local\\CamBridge.Test.LatestPublisher.") +
         std::to_wstring(GetCurrentProcessId()) + L"." + suffix;
}

cambridge::native::Nv12Frame MakeFrame(std::uint64_t sequence,
                                       std::byte value = std::byte{0x2a}) {
  cambridge::native::Nv12Frame frame;
  frame.width = 4;
  frame.height = 2;
  frame.stride = 4;
  frame.timestamp100ns = static_cast<std::int64_t>(sequence * 166666);
  frame.sequence = sequence;
  frame.bytes.assign(12, value);
  return frame;
}

void TestPublishesDecodedNv12Frame() {
  cambridge::native::LatestFramePublisher publisher;
  assert(publisher.Start(UniqueName(L"mapping"), UniqueName(L"event")));

  cambridge::native::SharedFrameReader reader;
  assert(reader.Open(UniqueName(L"mapping")));
  assert(publisher.Publish(MakeFrame(7)));

  cambridge::native::Nv12Frame output;
  assert(reader.ReadLatest(output));
  assert(output.width == 4);
  assert(output.height == 2);
  assert(output.stride == 4);
  assert(output.timestamp100ns == 7 * 166666);
  assert(output.bytes.size() == 12);
  assert(output.bytes.front() == std::byte{0x2a});

  const auto metrics = publisher.metrics();
  assert(metrics.publishCalls == 1);
  assert(metrics.publishedFrames == 1);
  assert(metrics.rejectedFrames == 0);
}

void TestRejectsInvalidFrameWithoutPublishing() {
  cambridge::native::LatestFramePublisher publisher;
  assert(publisher.Start(UniqueName(L"invalid-mapping"), UniqueName(L"invalid-event")));

  cambridge::native::SharedFrameReader reader;
  assert(reader.Open(UniqueName(L"invalid-mapping")));
  auto invalid = MakeFrame(1);
  invalid.bytes.clear();
  assert(!publisher.Publish(invalid));

  cambridge::native::Nv12Frame output;
  assert(!reader.ReadLatest(output));
  const auto metrics = publisher.metrics();
  assert(metrics.publishCalls == 1);
  assert(metrics.publishedFrames == 0);
  assert(metrics.rejectedFrames == 1);
}

}  // namespace

int wmain() {
  TestPublishesDecodedNv12Frame();
  TestRejectsInvalidFrameWithoutPublishing();
  std::cout << "CamBridge latest frame publisher tests passed\n";
  return 0;
}
