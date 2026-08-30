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
  TestReaderOpenFailureKeepsDiagnosticError();
  std::cout << "CamBridge frame IPC tests passed\n";
  return 0;
}
