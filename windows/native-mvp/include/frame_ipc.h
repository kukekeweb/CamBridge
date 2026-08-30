#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cambridge::native {

inline constexpr wchar_t kFrameMappingName[] = L"Local\\CamBridge.NativeMvp.Nv12";
inline constexpr wchar_t kFrameReadyEventName[] = L"Local\\CamBridge.NativeMvp.Nv12.Ready";
inline constexpr std::uint32_t kFrameIpcVersion = 1;
inline constexpr std::uint32_t kSlotCount = 2;
inline constexpr std::uint32_t kMaxWidth = 1920;
inline constexpr std::uint32_t kMaxHeight = 1080;
inline constexpr std::uint32_t kMaxStride = 1920;
inline constexpr std::uint32_t kMaxFrameBytes = kMaxStride * kMaxHeight * 3 / 2;

struct SharedFrameHeader {
  std::uint32_t version;
  std::uint32_t headerBytes;
  std::uint32_t width;
  std::uint32_t height;
  std::uint32_t stride;
  std::uint32_t frameBytes;
  volatile LONG64 publishedSequence;
  volatile LONG producerState;
  std::uint32_t reserved;
};

struct SharedFrameSlotHeader {
  volatile LONG64 sequence;
  std::uint32_t bytes;
  std::uint32_t reserved;
  std::int64_t timestamp100ns;
};

struct Nv12Frame {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t stride = 0;
  std::int64_t timestamp100ns = 0;
  std::uint64_t sequence = 0;
  // User-mode storage type. The shared-memory wire format remains raw NV12
  // bytes; uint8_t avoids an ABI collision with libdatachannel's exported
  // std::vector<std::byte> symbols when the receiver is linked in-process.
  std::vector<std::uint8_t> bytes;
};

struct SharedFrameStatus {
  bool mappingOpen = false;
  DWORD openError = ERROR_SUCCESS;
  LONG producerState = 0;
  std::uint64_t publishedSequence = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t stride = 0;
  std::uint32_t frameBytes = 0;
  std::uint64_t lastReadSequence = 0;
};

std::size_t SharedFrameMappingBytes();

class SharedFrameProducer {
 public:
  SharedFrameProducer() = default;
  ~SharedFrameProducer();

  SharedFrameProducer(const SharedFrameProducer&) = delete;
  SharedFrameProducer& operator=(const SharedFrameProducer&) = delete;

  bool Create(const std::wstring& mappingName = kFrameMappingName,
              const std::wstring& eventName = kFrameReadyEventName);
  bool Publish(const Nv12Frame& frame);
  void Close();
  bool IsOpen() const { return view_ != nullptr; }

 private:
  HANDLE mapping_ = nullptr;
  HANDLE readyEvent_ = nullptr;
  void* view_ = nullptr;
  std::size_t mappingBytes_ = 0;
};

class SharedFrameReader {
 public:
  SharedFrameReader() = default;
  ~SharedFrameReader();

  SharedFrameReader(const SharedFrameReader&) = delete;
  SharedFrameReader& operator=(const SharedFrameReader&) = delete;

  bool Open(const std::wstring& mappingName = kFrameMappingName);
  bool ReadLatest(Nv12Frame& output);
  bool GetStatus(SharedFrameStatus* status) const;
  void Close();
  bool IsOpen() const { return view_ != nullptr; }
  DWORD lastOpenError() const { return lastOpenError_; }

 private:
  HANDLE mapping_ = nullptr;
  void* view_ = nullptr;
  std::size_t mappingBytes_ = 0;
  std::uint64_t lastSequence_ = 0;
  DWORD lastOpenError_ = ERROR_SUCCESS;
};

}  // namespace cambridge::native
