#include "frame_ipc.h"

#include <chrono>
#include <csignal>
#include <algorithm>
#include <iostream>
#include <string>
#include <thread>

namespace {
volatile std::sig_atomic_t g_running = 1;

void Stop(int) { g_running = 0; }

cambridge::native::Nv12Frame MakeFrame(std::uint64_t sequence) {
  cambridge::native::Nv12Frame frame;
  frame.width = 1920;
  frame.height = 1080;
  frame.stride = 1920;
  frame.timestamp100ns = static_cast<std::int64_t>(sequence * 166667);
  frame.sequence = sequence;
  frame.bytes.resize(static_cast<std::size_t>(frame.stride) * frame.height * 3 / 2);
  auto* y = reinterpret_cast<std::uint8_t*>(frame.bytes.data());
  auto* uv = y + static_cast<std::size_t>(frame.stride) * frame.height;
  for (std::uint32_t row = 0; row < frame.height; ++row) {
    for (std::uint32_t col = 0; col < frame.width; ++col) {
      y[static_cast<std::size_t>(row) * frame.stride + col] =
          static_cast<std::uint8_t>((col / 8 + row / 8 + sequence) & 0xffu);
    }
  }
  for (std::uint32_t row = 0; row < frame.height / 2; ++row) {
    for (std::uint32_t col = 0; col < frame.width; col += 2) {
      uv[static_cast<std::size_t>(row) * frame.stride + col] = 128;
      uv[static_cast<std::size_t>(row) * frame.stride + col + 1] = 128;
    }
  }
  return frame;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::signal(SIGINT, Stop);
  std::signal(SIGTERM, Stop);
  std::uint32_t durationMs = 0;
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--duration-ms" && i + 1 < argc) {
      durationMs = static_cast<std::uint32_t>(std::max(1, _wtoi(argv[++i])));
    }
  }
  cambridge::native::SharedFrameProducer producer;
  if (!producer.Create()) {
    std::wcerr << L"CamBridge synthetic producer: CreateFileMapping/CreateEvent failed: "
               << GetLastError() << L"\n";
    return 1;
  }
  std::wcout << L"CamBridge synthetic producer: 1920x1080 NV12 @ 60fps\n"
             << L"IPC ready: mapping=" << cambridge::native::kFrameMappingName
             << L" event=" << cambridge::native::kFrameReadyEventName << L"\n";
  std::uint64_t sequence = 0;
  auto next = std::chrono::steady_clock::now();
  const auto deadline = durationMs == 0
                            ? std::chrono::steady_clock::time_point::max()
                            : next + std::chrono::milliseconds(durationMs);
  while (g_running != 0 && std::chrono::steady_clock::now() < deadline) {
    ++sequence;
    auto frame = MakeFrame(sequence);
    if (!producer.Publish(frame)) {
      std::wcerr << L"CamBridge synthetic producer: publish failed\n";
      return 1;
    }
    next += std::chrono::microseconds(16667);
    std::this_thread::sleep_until(next);
  }
  std::wcout << L"Synthetic publisher stopped: publishedSequence=" << sequence << L"\n";
  producer.Close();
  return 0;
}
