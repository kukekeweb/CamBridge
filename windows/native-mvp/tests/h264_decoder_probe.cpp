#include "h264_decoder.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <cstdint>
#include <vector>

namespace {

std::vector<std::vector<std::uint8_t>> SplitAnnexBAccessUnits(
    const std::vector<char>& input) {
  std::vector<std::vector<std::uint8_t>> units;
  std::vector<std::uint8_t> current;
  std::size_t nalStart = 0;
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
      current.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(input[index])));
    }
    nalStart = nalEnd;
  }
  if (!current.empty()) units.push_back(std::move(current));
  return units;
}

}  // namespace

int main(int argc, char** argv) {
  cambridge::native::MediaFoundationH264Decoder decoder;
  if (!decoder.Start({1920, 1080, 60, 1, true})) {
    std::cerr << "H264 decoder probe: Start failed: " << decoder.lastError() << "\n";
    return 1;
  }
  const auto metrics = decoder.metrics();
  std::cout << "H264 decoder probe: Start PASS\n"
            << "Transform: " << metrics.selectedTransform << "\n"
            << "Hardware: " << (metrics.hardware ? "yes" : "no") << "\n"
            << "Output: " << metrics.outputWidth << "x" << metrics.outputHeight
            << " coded=" << metrics.codedWidth << "x" << metrics.codedHeight
            << " stride=" << metrics.outputStride << " NV12\n"
            << "Output stream flags: 0x" << std::hex << metrics.outputStreamFlags << std::dec
            << " cbSize=" << metrics.outputBufferSize
            << " alignment=" << metrics.outputBufferAlignment << "\n";
  if (argc > 1) {
    std::ifstream file(argv[1], std::ios::binary);
    if (!file.is_open()) {
      std::cerr << "H264 decoder probe: input open failed: " << argv[1] << "\n";
      decoder.Stop();
      return 2;
    }
    const std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
    const auto accessUnits = SplitAnnexBAccessUnits(bytes);
    std::cout << "Input access units: " << accessUnits.size() << "\n";
    for (std::size_t index = 0; index < accessUnits.size(); ++index) {
      if (!decoder.SubmitAccessUnit(accessUnits[index],
                                     static_cast<std::int64_t>(index * 166666), 166666)) {
        const auto failed = decoder.metrics();
        std::cerr << "H264 decoder probe: SubmitAccessUnit failed at index " << index << ": "
                  << decoder.lastError() << "\n"
                  << "  input access units accepted: " << failed.inputAccessUnits << "\n"
                  << "  output frames: " << failed.outputFrames << "\n"
                  << "  ProcessOutput calls: " << failed.processOutputCalls << "\n"
                  << "  Need more input: " << failed.processOutputNeedMoreInput << "\n"
                  << "  Stream changes: " << failed.processOutputStreamChanges << "\n"
                  << "  Last ProcessOutput HRESULT: 0x" << std::hex
                  << static_cast<std::uint32_t>(failed.lastProcessOutputHr) << std::dec
                  << " status=0x" << std::hex << failed.lastProcessOutputStatus << std::dec << "\n"
                  << "  Stream change: " << failed.lastStreamChangeDescription << "\n";
        decoder.Stop();
        return 3;
      }
    }
    const auto decoded = decoder.metrics();
    std::cout << "Decoded frames: " << decoded.outputFrames << "\n"
              << "Final output: " << decoded.outputWidth << "x" << decoded.outputHeight
              << " coded=" << decoded.codedWidth << "x" << decoded.codedHeight
              << " stride=" << decoded.outputStride << "\n"
              << "ProcessOutput calls: " << decoded.processOutputCalls << "\n"
              << "Need more input: " << decoded.processOutputNeedMoreInput << "\n"
              << "Stream changes: " << decoded.processOutputStreamChanges << "\n"
              << "Last ProcessOutput HRESULT: 0x" << std::hex
              << static_cast<std::uint32_t>(decoded.lastProcessOutputHr) << std::dec
              << " status=0x" << std::hex << decoded.lastProcessOutputStatus << std::dec << "\n";
  }
  decoder.Stop();
  return 0;
}
