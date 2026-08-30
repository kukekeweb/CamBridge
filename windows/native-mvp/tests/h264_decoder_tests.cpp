#include "h264_decoder.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

void TestRejectsInvalidConfiguration() {
  cambridge::native::MediaFoundationH264Decoder decoder;
  assert(!decoder.Start({0, 1080, 60, 1, true}));
  assert(!decoder.IsStarted());
  assert(decoder.lastError() == "invalid decoder dimensions");
}

void TestRejectsInputBeforeStart() {
  cambridge::native::MediaFoundationH264Decoder decoder;
  const std::vector<std::byte> accessUnit{std::byte{0x00}, std::byte{0x01}};
  assert(!decoder.SubmitAccessUnit(accessUnit, 0, 166666));
  assert(decoder.metrics().decodeErrors == 1);
  assert(decoder.lastError() == "decoder is not started");
}

void TestStopIsIdempotent() {
  cambridge::native::MediaFoundationH264Decoder decoder;
  decoder.Stop();
  decoder.Stop();
  assert(!decoder.IsStarted());
}

void TestAvailableDecoderCanStartAndStop() {
  cambridge::native::MediaFoundationH264Decoder decoder;
  if (!decoder.Start({1920, 1080, 60, 1, true})) {
    // Decoder availability is a host capability. The contract test above
    // still verifies deterministic validation when no MFT is installed.
    return;
  }
  assert(decoder.IsStarted());
  assert(decoder.metrics().outputWidth == 1920);
  assert(decoder.metrics().outputHeight == 1080);
  const auto metrics = decoder.metrics();
  assert(metrics.outputStreamFlags != 0 || metrics.outputBufferSize != 0);
  decoder.Stop();
  assert(!decoder.IsStarted());
}

std::vector<std::vector<std::byte>> SplitAnnexBAccessUnits(
    const std::vector<char>& input) {
  std::vector<std::vector<std::byte>> units;
  std::vector<std::byte> current;
  auto startCodeLength = [&](std::size_t offset) -> std::size_t {
    if (offset + 3 <= input.size() && input[offset] == 0 && input[offset + 1] == 0 &&
        input[offset + 2] == 1) {
      return 3;
    }
    if (offset + 4 <= input.size() && input[offset] == 0 && input[offset + 1] == 0 &&
        input[offset + 2] == 0 && input[offset + 3] == 1) {
      return 4;
    }
    return 0;
  };
  std::size_t nalStart = 0;
  while (nalStart < input.size()) {
    const std::size_t prefix = startCodeLength(nalStart);
    if (prefix == 0) {
      ++nalStart;
      continue;
    }
    const std::size_t payloadStart = nalStart + prefix;
    if (payloadStart >= input.size()) break;
    std::size_t nalEnd = payloadStart;
    while (nalEnd < input.size() && startCodeLength(nalEnd) == 0) ++nalEnd;
    const auto nalType = static_cast<std::uint8_t>(input[payloadStart]) & 0x1f;
    if (nalType == 9 && !current.empty()) {
      units.push_back(std::move(current));
      current = {};
    }
    current.reserve(current.size() + (nalEnd - nalStart));
    for (std::size_t index = nalStart; index < nalEnd; ++index) {
      current.push_back(static_cast<std::byte>(static_cast<unsigned char>(input[index])));
    }
    nalStart = nalEnd;
  }
  if (!current.empty()) units.push_back(std::move(current));
  return units;
}

void TestDecodesProvidedYuv420Fixture() {
  const char* fixturePath = std::getenv("CAMBRIDGE_H264_FIXTURE");
  if (fixturePath == nullptr || *fixturePath == '\0') return;
  std::ifstream file(fixturePath, std::ios::binary);
  assert(file.is_open());
  const std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
  const auto accessUnits = SplitAnnexBAccessUnits(bytes);
  assert(accessUnits.size() >= 30);

  cambridge::native::MediaFoundationH264Decoder decoder;
  std::uint64_t receivedFrames = 0;
  std::size_t receivedBytes = 0;
  decoder.SetFrameHandler([&](cambridge::native::Nv12Frame frame) {
    ++receivedFrames;
    receivedBytes = frame.bytes.size();
    assert(frame.width == 1920);
    assert(frame.height == 1080);
    assert(frame.stride == 1920);
  });
  assert(decoder.Start({1920, 1080, 60, 1, true}));
  for (std::size_t index = 0; index < accessUnits.size(); ++index) {
    assert(decoder.SubmitAccessUnit(accessUnits[index],
                                    static_cast<std::int64_t>(index * 166666), 166666));
  }
  const auto metrics = decoder.metrics();
  assert(metrics.processOutputStreamChanges > 0);
  assert(metrics.outputFrames > 0);
  assert(receivedFrames == metrics.outputFrames);
  assert(receivedBytes == static_cast<std::size_t>(1920) * 1080 * 3 / 2);
  decoder.Stop();
}

}  // namespace

int main() {
  TestRejectsInvalidConfiguration();
  TestRejectsInputBeforeStart();
  TestStopIsIdempotent();
  TestAvailableDecoderCanStartAndStop();
  TestDecodesProvidedYuv420Fixture();
  std::cout << "CamBridge H264 decoder contract tests passed\n";
  return 0;
}
