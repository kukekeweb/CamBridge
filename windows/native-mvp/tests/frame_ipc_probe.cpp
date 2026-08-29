#include "frame_ipc.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

int wmain(int argc, wchar_t** argv) {
  const int seconds = argc > 1 ? std::max(1, _wtoi(argv[1])) : 10;
  cambridge::native::SharedFrameReader reader;
  for (int attempt = 0; attempt < 100 && !reader.Open(); ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::wcerr << L"IPC probe: open=" << (reader.IsOpen() ? L"true" : L"false") << L"\n";
  if (!reader.IsOpen()) {
    std::wcerr << L"IPC probe: mapping not available\n";
    return 1;
  }
  std::uint64_t count = 0;
  std::uint64_t last = 0;
  auto start = std::chrono::steady_clock::now();
  cambridge::native::Nv12Frame frame;
  while (std::chrono::steady_clock::now() - start < std::chrono::seconds(seconds)) {
    if (reader.ReadLatest(frame)) {
      ++count;
      if (frame.sequence <= last) {
        std::wcerr << L"IPC probe: non-monotonic sequence\n";
        return 1;
      }
      last = frame.sequence;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::wcout << L"IPC probe: frames=" << count << L" lastSequence=" << last
             << L" observedFps=" << (elapsed > 0 ? count / elapsed : 0.0) << L"\n";
  return count > 0 ? 0 : 2;
}
