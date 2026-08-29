#include "frame_ipc.h"

#include <algorithm>
#include <cstring>

namespace cambridge::native {
namespace {

constexpr std::size_t SlotBytes() {
  return sizeof(SharedFrameSlotHeader) + kMaxFrameBytes;
}

SharedFrameSlotHeader* SlotHeader(void* base, std::uint32_t index) {
  auto* bytes = static_cast<std::byte*>(base);
  return reinterpret_cast<SharedFrameSlotHeader*>(
      bytes + sizeof(SharedFrameHeader) + SlotBytes() * index);
}

std::byte* SlotPayload(SharedFrameSlotHeader* slot) {
  return reinterpret_cast<std::byte*>(slot) + sizeof(SharedFrameSlotHeader);
}

bool ValidFrame(const Nv12Frame& frame) {
  if (frame.width == 0 || frame.height == 0 || frame.width > kMaxWidth ||
      frame.height > kMaxHeight || frame.stride < frame.width ||
      frame.stride > kMaxStride || (frame.height & 1u) != 0 ||
      frame.bytes.size() != static_cast<std::size_t>(frame.stride) * frame.height * 3 / 2) {
    return false;
  }
  return frame.bytes.size() <= kMaxFrameBytes;
}

}  // namespace

std::size_t SharedFrameMappingBytes() {
  return sizeof(SharedFrameHeader) + SlotBytes() * kSlotCount;
}

SharedFrameProducer::~SharedFrameProducer() { Close(); }

bool SharedFrameProducer::Create(const std::wstring& mappingName,
                                 const std::wstring& eventName) {
  Close();
  mappingBytes_ = SharedFrameMappingBytes();
  mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                static_cast<DWORD>(mappingBytes_), mappingName.c_str());
  if (mapping_ == nullptr) {
    return false;
  }
  view_ = MapViewOfFile(mapping_, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, mappingBytes_);
  if (view_ == nullptr) {
    Close();
    return false;
  }
  std::memset(view_, 0, mappingBytes_);
  auto* header = static_cast<SharedFrameHeader*>(view_);
  header->version = kFrameIpcVersion;
  header->headerBytes = sizeof(SharedFrameHeader);
  InterlockedExchange(&header->producerState, 1);
  readyEvent_ = CreateEventW(nullptr, FALSE, FALSE, eventName.c_str());
  if (readyEvent_ == nullptr) {
    Close();
    return false;
  }
  return true;
}

bool SharedFrameProducer::Publish(const Nv12Frame& frame) {
  if (!IsOpen() || readyEvent_ == nullptr || !ValidFrame(frame)) {
    return false;
  }
  auto* header = static_cast<SharedFrameHeader*>(view_);
  const auto current = static_cast<std::uint64_t>(
      InterlockedCompareExchange64(&header->publishedSequence, 0, 0));
  const auto next = current + 1;
  header->width = frame.width;
  header->height = frame.height;
  header->stride = frame.stride;
  header->frameBytes = static_cast<std::uint32_t>(frame.bytes.size());
  const auto slotIndex = static_cast<std::uint32_t>(next % kSlotCount);
  auto* slot = SlotHeader(view_, slotIndex);
  slot->bytes = static_cast<std::uint32_t>(frame.bytes.size());
  slot->timestamp100ns = frame.timestamp100ns;
  std::memcpy(SlotPayload(slot), frame.bytes.data(), frame.bytes.size());
  MemoryBarrier();
  InterlockedExchange64(&slot->sequence, static_cast<LONG64>(next));
  MemoryBarrier();
  InterlockedExchange64(&header->publishedSequence, static_cast<LONG64>(next));
  SetEvent(readyEvent_);
  return true;
}

void SharedFrameProducer::Close() {
  if (view_ != nullptr) {
    auto* header = static_cast<SharedFrameHeader*>(view_);
    InterlockedExchange(&header->producerState, 0);
    UnmapViewOfFile(view_);
    view_ = nullptr;
  }
  if (readyEvent_ != nullptr) {
    CloseHandle(readyEvent_);
    readyEvent_ = nullptr;
  }
  if (mapping_ != nullptr) {
    CloseHandle(mapping_);
    mapping_ = nullptr;
  }
  mappingBytes_ = 0;
}

SharedFrameReader::~SharedFrameReader() { Close(); }

bool SharedFrameReader::Open(const std::wstring& mappingName) {
  Close();
  lastOpenError_ = ERROR_SUCCESS;
  mapping_ = OpenFileMappingW(FILE_MAP_READ, FALSE, mappingName.c_str());
  if (mapping_ == nullptr) {
    lastOpenError_ = GetLastError();
    return false;
  }
  mappingBytes_ = SharedFrameMappingBytes();
  view_ = MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, mappingBytes_);
  if (view_ == nullptr) {
    const DWORD error = GetLastError();
    Close();
    lastOpenError_ = error;
    return false;
  }
  const auto* header = static_cast<const SharedFrameHeader*>(view_);
  if (header->version != kFrameIpcVersion || header->headerBytes != sizeof(SharedFrameHeader)) {
    Close();
    lastOpenError_ = ERROR_INVALID_DATA;
    return false;
  }
  lastSequence_ = 0;
  return true;
}

bool SharedFrameReader::GetStatus(SharedFrameStatus* status) const {
  if (status == nullptr) return false;
  *status = {};
  status->mappingOpen = view_ != nullptr;
  status->openError = lastOpenError_;
  status->lastReadSequence = lastSequence_;
  if (view_ == nullptr) return false;
  const auto* header = static_cast<const SharedFrameHeader*>(view_);
  // The reader maps the section with FILE_MAP_READ. Do not use an interlocked
  // read here: InterlockedCompareExchange is a write-capable operation and can
  // fault in a read-only view while the publisher is active.
  MemoryBarrier();
  status->producerState = header->producerState;
  status->publishedSequence = static_cast<std::uint64_t>(header->publishedSequence);
  MemoryBarrier();
  status->width = header->width;
  status->height = header->height;
  status->stride = header->stride;
  status->frameBytes = header->frameBytes;
  return true;
}

bool SharedFrameReader::ReadLatest(Nv12Frame& output) {
  if (!IsOpen()) {
    return false;
  }
  const auto* header = static_cast<const SharedFrameHeader*>(view_);
  MemoryBarrier();
  const auto published = static_cast<std::uint64_t>(header->publishedSequence);
  if (published == 0 || published == lastSequence_) {
    return false;
  }
  const auto slotIndex = static_cast<std::uint32_t>(published % kSlotCount);
  auto* slot = SlotHeader(const_cast<void*>(view_), slotIndex);
  const auto before = static_cast<std::uint64_t>(slot->sequence);
  if (before != published || slot->bytes > kMaxFrameBytes) {
    return false;
  }
  Nv12Frame candidate;
  candidate.width = header->width;
  candidate.height = header->height;
  candidate.stride = header->stride;
  candidate.timestamp100ns = slot->timestamp100ns;
  candidate.sequence = published;
  candidate.bytes.resize(slot->bytes);
  std::memcpy(candidate.bytes.data(), SlotPayload(slot), slot->bytes);
  MemoryBarrier();
  MemoryBarrier();
  const auto after = static_cast<std::uint64_t>(slot->sequence);
  if (after != before) {
    return false;
  }
  output = std::move(candidate);
  lastSequence_ = published;
  return true;
}

void SharedFrameReader::Close() {
  if (view_ != nullptr) {
    UnmapViewOfFile(view_);
    view_ = nullptr;
  }
  if (mapping_ != nullptr) {
    CloseHandle(mapping_);
    mapping_ = nullptr;
  }
  mappingBytes_ = 0;
  lastSequence_ = 0;
  lastOpenError_ = ERROR_SUCCESS;
}

}  // namespace cambridge::native
