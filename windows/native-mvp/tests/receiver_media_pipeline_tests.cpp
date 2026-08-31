#include "receiver_media_pipeline.h"

#include <Windows.h>

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {

void TestEstimateFpsFromTimestamps() {
  assert(cambridge::native::EstimateFpsFromTimestamps(0, 0, 0) == 0.0);
  assert(cambridge::native::EstimateFpsFromTimestamps(1, 0, 0) == 0.0);
  assert(cambridge::native::EstimateFpsFromTimestamps(61, 0, 10000000) > 59.99);
  assert(cambridge::native::EstimateFpsFromTimestamps(61, 0, 10000000) < 60.01);
  assert(cambridge::native::EstimateFpsFromTimestamps(2, 100, 100) == 0.0);
}

std::wstring UniqueName(const wchar_t* suffix) {
  return std::wstring(L"Local\\CamBridge.ReceiverPipeline.Test.") +
         std::to_wstring(GetCurrentProcessId()) + L"." + suffix;
}

void TestRejectsInputBeforeStart() {
  cambridge::native::ReceiverMediaPipeline pipeline;
  const std::vector<std::uint8_t> input{0, 1, 2};
  assert(!pipeline.SubmitAccessUnit(input, 0));
  assert(pipeline.metrics().inputErrors == 1);
}

void TestStartStopOwnsDecoderAndPublisherLifecycle() {
  cambridge::native::ReceiverMediaPipeline pipeline;
  const auto mapping = UniqueName(L"Mapping");
  const auto event = UniqueName(L"Event");
  assert(pipeline.Start({{1920, 1080, 60, 1, true}, mapping, event}));
  assert(pipeline.IsStarted());
  assert(pipeline.metrics().publisherStarted);
  assert(pipeline.metrics().decoderStarted);
  pipeline.Stop();
  assert(!pipeline.IsStarted());
}

std::vector<std::vector<std::uint8_t>> SplitAnnexB(const std::vector<char>& input) {
  std::vector<std::vector<std::uint8_t>> units;
  auto prefixLength = [&](std::size_t offset) -> std::size_t {
    if (offset + 3 <= input.size() && input[offset] == 0 && input[offset + 1] == 0 &&
        input[offset + 2] == 1) return 3;
    if (offset + 4 <= input.size() && input[offset] == 0 && input[offset + 1] == 0 &&
        input[offset + 2] == 0 && input[offset + 3] == 1) return 4;
    return 0;
  };
  std::size_t start = 0;
  while (start < input.size()) {
    const auto prefix = prefixLength(start);
    if (prefix == 0) { ++start; continue; }
    const auto payload = start + prefix;
    if (payload >= input.size()) break;
    std::size_t end = payload;
    while (end < input.size() && prefixLength(end) == 0) ++end;
    std::vector<std::uint8_t> unit;
    unit.reserve(end - start);
    for (std::size_t index = start; index < end; ++index) {
      unit.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(input[index])));
    }
    units.push_back(std::move(unit));
    start = end;
  }
  return units;
}

void TestFixturePublishesDecodedNv12WhenProvided() {
  const char* fixturePath = std::getenv("CAMBRIDGE_H264_FIXTURE");
  if (fixturePath == nullptr || *fixturePath == '\0') return;
  std::ifstream file(fixturePath, std::ios::binary);
  assert(file.is_open());
  const std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
  const auto units = SplitAnnexB(bytes);
  assert(!units.empty());

  cambridge::native::ReceiverMediaPipeline pipeline;
  const auto mapping = UniqueName(L"FixtureMapping");
  const auto event = UniqueName(L"FixtureEvent");
  assert(pipeline.Start({{1920, 1080, 60, 1, true}, mapping, event}));
  for (std::size_t index = 0; index < units.size(); ++index) {
    assert(pipeline.SubmitAccessUnit(units[index],
                                     static_cast<std::uint32_t>(index * 1500)));
  }
  const auto metrics = pipeline.metrics();
  assert(metrics.inputAccessUnits == units.size());
  assert(metrics.decodedFrames > 0);
  assert(metrics.publishedFrames == metrics.decodedFrames);

  cambridge::native::SharedFrameReader reader;
  assert(reader.Open(mapping));
  cambridge::native::Nv12Frame frame;
  assert(reader.ReadLatest(frame));
  assert(frame.width == 1920);
  assert(frame.height == 1080);
  assert(frame.stride >= 1920);
  assert(frame.bytes.size() == static_cast<std::size_t>(frame.stride) * frame.height * 3 / 2);
  pipeline.Stop();
}

void TestFixtureAcceptsAccessUnitsFromReceiverCallbackThread() {
  const char* fixturePath = std::getenv("CAMBRIDGE_H264_FIXTURE");
  if (fixturePath == nullptr || *fixturePath == '\0') return;
  std::ifstream file(fixturePath, std::ios::binary);
  assert(file.is_open());
  const std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
  const auto units = SplitAnnexB(bytes);
  assert(!units.empty());

  cambridge::native::ReceiverMediaPipeline pipeline;
  const auto mapping = UniqueName(L"CallbackMapping");
  const auto event = UniqueName(L"CallbackEvent");
  assert(pipeline.Start({{1920, 1080, 60, 1, true}, mapping, event}));

  bool accepted = true;
  std::thread callbackThread([&] {
    for (std::size_t index = 0; index < units.size(); ++index) {
      if (!pipeline.SubmitAccessUnit(
              units[index], static_cast<std::uint32_t>(index * 1500))) {
        accepted = false;
        break;
      }
    }
  });
  callbackThread.join();

  assert(accepted);
  const auto metrics = pipeline.metrics();
  assert(metrics.inputAccessUnits > 0);
  assert(metrics.decodedFrames > 0);
  assert(metrics.publishedFrames == metrics.decodedFrames);
  pipeline.Stop();
}

}  // namespace

int main() {
  TestEstimateFpsFromTimestamps();
  TestRejectsInputBeforeStart();
  TestStartStopOwnsDecoderAndPublisherLifecycle();
  TestFixturePublishesDecodedNv12WhenProvided();
  TestFixtureAcceptsAccessUnitsFromReceiverCallbackThread();
  return 0;
}
