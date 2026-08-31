#include "frame_ipc.h"

#include <cassert>
#include <iostream>

namespace {
cambridge::native::Nv12Frame MakeFrame(std::uint64_t sequence, std::uint8_t value) {
  cambridge::native::Nv12Frame frame;
  frame.width = 4;
  frame.height = 2;
  frame.stride = 4;
  frame.timestamp100ns = static_cast<std::int64_t>(sequence * 100);
  frame.sequence = sequence;
  frame.bytes.assign(12, value);
  return frame;
}

void TestMappingSize() {
  assert(cambridge::native::SharedFrameMappingBytes() > cambridge::native::kMaxFrameBytes);
}

void TestLatestFrameAndMissingInitialFrame() {
  const std::wstring mapping = L"Local\\CamBridge.Test.Mapping.Latest";
  const std::wstring event = L"Local\\CamBridge.Test.Event.Latest";
  cambridge::native::SharedFrameProducer producer;
  cambridge::native::SharedFrameReader reader;
  assert(producer.Create(mapping, event));
  assert(reader.Open(mapping));
  cambridge::native::Nv12Frame output;
  assert(!reader.ReadLatest(output));
  assert(producer.Publish(MakeFrame(1, 10)));
  cambridge::native::SharedFrameStatus status;
  assert(reader.GetStatus(&status));
  assert(status.mappingOpen);
  assert(status.producerState == 1);
  assert(status.publishedSequence == 1);
  assert(status.width == 4);
  assert(status.height == 2);
  assert(status.frameBytes == 12);
  assert(reader.ReadLatest(output));
  assert(output.sequence == 1);
  assert(output.bytes.front() == 10);
  assert(!reader.ReadLatest(output));
  assert(producer.Publish(MakeFrame(2, 20)));
  assert(producer.Publish(MakeFrame(3, 30)));
  assert(reader.GetStatus(&status));
  assert(status.publishedSequence == 3);
  assert(reader.ReadLatest(output));
  assert(output.sequence == 3);
  assert(output.bytes.front() == 30);
}

void TestInvalidFrameIsRejected() {
  const std::wstring mapping = L"Local\\CamBridge.Test.Mapping.Invalid";
  const std::wstring event = L"Local\\CamBridge.Test.Event.Invalid";
  cambridge::native::SharedFrameProducer producer;
  assert(producer.Create(mapping, event));
  auto invalid = MakeFrame(1, 1);
  invalid.width = 5;
  invalid.bytes.clear();
  assert(!producer.Publish(invalid));
}

void TestPortraitFrameIsAccepted() {
  const std::wstring mapping = L"Local\\CamBridge.Test.Mapping.Portrait";
  const std::wstring event = L"Local\\CamBridge.Test.Event.Portrait";
  cambridge::native::SharedFrameProducer producer;
  cambridge::native::SharedFrameReader reader;
  assert(producer.Create(mapping, event));
  assert(reader.Open(mapping));

  cambridge::native::Nv12Frame frame;
  frame.width = 1080;
  frame.height = 1920;
  frame.stride = 1080;
  frame.timestamp100ns = 1234;
  frame.sequence = 1;
  frame.bytes.assign(static_cast<std::size_t>(frame.stride) * frame.height * 3 / 2, 42);
  assert(producer.Publish(frame));

  cambridge::native::Nv12Frame output;
  assert(reader.ReadLatest(output));
  assert(output.width == 1080);
  assert(output.height == 1920);
  assert(output.stride == 1080);
  assert(output.bytes.size() == frame.bytes.size());
}

void TestReaderOpenFailureKeepsDiagnosticError() {
  cambridge::native::SharedFrameReader reader;
  assert(!reader.Open(L"Local\\CamBridge.Test.Mapping.DoesNotExist"));
  cambridge::native::SharedFrameStatus status;
  assert(!reader.GetStatus(&status));
  assert(!status.mappingOpen);
  assert(status.openError != ERROR_SUCCESS);
}
}  // namespace

int wmain() {
  TestMappingSize();
  TestLatestFrameAndMissingInitialFrame();
  TestInvalidFrameIsRejected();
  TestPortraitFrameIsAccepted();
  TestReaderOpenFailureKeepsDiagnosticError();
  std::cout << "CamBridge frame IPC tests passed\n";
  return 0;
}
