#include "diagnostic_log.h"

#include <ShlObj.h>
#include <Shlwapi.h>
#include <mfidl.h>

#include <cwchar>
#include <mutex>
#include <string>

#pragma comment(lib, "shlwapi.lib")

namespace cambridge::native {
namespace {

std::mutex g_logMutex;

std::wstring GetLogDirectory() {
  wchar_t configured[MAX_PATH]{};
  const DWORD configuredLength = GetEnvironmentVariableW(
      L"CAMBRIDGE_NATIVE_MVP_LOG_DIR", configured, ARRAYSIZE(configured));
  if (configuredLength > 0 && configuredLength < ARRAYSIZE(configured)) {
    return configured;
  }

  const std::wstring programData = L"C:\\ProgramData\\CamBridge\\logs";
  if (SUCCEEDED(SHCreateDirectoryExW(nullptr, programData.c_str(), nullptr)) ||
      GetLastError() == ERROR_ALREADY_EXISTS) {
    return programData;
  }

  wchar_t tempPath[MAX_PATH]{};
  const DWORD tempLength = GetTempPathW(ARRAYSIZE(tempPath), tempPath);
  if (tempLength > 0 && tempLength < ARRAYSIZE(tempPath)) {
    const std::wstring fallback = std::wstring(tempPath) + L"CamBridge";
    (void)SHCreateDirectoryExW(nullptr, fallback.c_str(), nullptr);
    return fallback;
  }
  return L".";
}

std::wstring GuidText(REFGUID guid) {
  wchar_t text[64]{};
  return StringFromGUID2(guid, text, ARRAYSIZE(text)) > 0 ? text : L"<invalid-guid>";
}

std::wstring HResultText(HRESULT hr) {
  wchar_t text[16]{};
  swprintf_s(text, L"0x%08lX", static_cast<unsigned long>(hr));
  return text;
}

std::wstring PointerText(const void* pointer) {
  wchar_t text[32]{};
  swprintf_s(text, L"0x%p", pointer);
  return text;
}

void AppendLine(const std::wstring& line) {
  const std::wstring path = GetLogDirectory() + L"\\media-source-" +
                            std::to_wstring(GetCurrentProcessId()) + L".log";
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    OutputDebugStringW((line + L"\r\n").c_str());
    return;
  }
  const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, line.data(),
                                             static_cast<int>(line.size()), nullptr, 0,
                                             nullptr, nullptr);
  std::string utf8;
  if (utf8Length > 0) {
    utf8.resize(static_cast<std::size_t>(utf8Length));
    (void)WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
                              utf8.data(), utf8Length, nullptr, nullptr);
  }
  const DWORD bytes = static_cast<DWORD>(utf8.size());
  DWORD written = 0;
  (void)WriteFile(file, utf8.data(), bytes, &written, nullptr);
  const char newline[] = "\r\n";
  (void)WriteFile(file, newline, sizeof(newline) - 1, &written, nullptr);
  CloseHandle(file);
}

void LogPrefix(const wchar_t* component, const wchar_t* eventName,
               const std::wstring& detail) {
  SYSTEMTIME now{};
  GetSystemTime(&now);
  wchar_t timestamp[64]{};
  swprintf_s(timestamp, L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
             now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
             now.wSecond, now.wMilliseconds);
  const std::wstring line = std::wstring(timestamp) + L" pid=" +
                            std::to_wstring(GetCurrentProcessId()) + L" tid=" +
                            std::to_wstring(GetCurrentThreadId()) + L" component=" +
                            (component == nullptr ? L"<null>" : component) +
                            L" event=" + (eventName == nullptr ? L"<null>" : eventName) +
                            L" " + detail;
  std::lock_guard lock(g_logMutex);
  AppendLine(line);
}

}  // namespace

const wchar_t* MediaEventTypeName(DWORD eventType) {
  switch (static_cast<MediaEventType>(eventType)) {
    case MEUnknown: return L"MEUnknown";
    case MEError: return L"MEError";
    case MENewStream: return L"MENewStream";
    case MEUpdatedStream: return L"MEUpdatedStream";
    case MESourceStarted: return L"MESourceStarted";
    case MESourceStopped: return L"MESourceStopped";
    case MESourcePaused: return L"MESourcePaused";
    case MEStreamStarted: return L"MEStreamStarted";
    case MEStreamStopped: return L"MEStreamStopped";
    case MEStreamPaused: return L"MEStreamPaused";
    case MEStreamTick: return L"MEStreamTick";
    case MEEndOfStream: return L"MEEndOfStream";
    case MEMediaSample: return L"MEMediaSample";
    case MEStreamSeeked: return L"MEStreamSeeked";
    default: return L"Unknown";
  }
}

void LogControlEvent(const wchar_t* component, const wchar_t* eventName, HRESULT hr) {
  LogPrefix(component, eventName, L"hr=" + HResultText(hr));
}

void LogQueryInterface(const wchar_t* component, REFIID requestedIid, HRESULT hr) {
  const bool isSourceOrStream = component != nullptr &&
      (wcscmp(component, L"CamBridgeMediaSource") == 0 ||
       wcscmp(component, L"CamBridgeMediaStream") == 0);
  wchar_t verbose[8]{};
  const DWORD length = GetEnvironmentVariableW(
      L"CAMBRIDGE_NATIVE_MVP_VERBOSE_QI", verbose, ARRAYSIZE(verbose));
  if (isSourceOrStream && SUCCEEDED(hr) &&
      !(length > 0 && verbose[0] == L'1')) {
    return;
  }
  LogPrefix(component, L"QueryInterface",
            L"iid=" + GuidText(requestedIid) + L" hr=" + HResultText(hr));
}

void LogMediaEvent(const wchar_t* component, const wchar_t* operation,
                   DWORD eventType, HRESULT callHr, HRESULT status,
                   REFGUID extendedType, bool associatedObject,
                   const void* associatedPointer, DWORD streamId,
                   std::uint64_t sequence, HRESULT valueHr) {
  LogPrefix(component, operation,
            L"eventType=" + std::wstring(MediaEventTypeName(eventType)) +
                L" eventTypeValue=" + std::to_wstring(eventType) +
                L" callHr=" + HResultText(callHr) +
                L" status=" + HResultText(status) +
                L" extendedType=" + GuidText(extendedType) +
                L" associatedObject=" + std::to_wstring(associatedObject ? 1 : 0) +
                L" associatedPointer=" + PointerText(associatedPointer) +
                L" streamId=" + std::to_wstring(streamId) +
                L" sequence=" + std::to_wstring(sequence) +
                L" valueHr=" + HResultText(valueHr));
}

void LogDescriptorEvent(const wchar_t* component, const wchar_t* eventName, HRESULT hr,
                        DWORD descriptorCount, DWORD streamId, bool selected,
                        REFGUID majorType, REFGUID subtype,
                        std::uint32_t width, std::uint32_t height,
                        std::uint32_t fps, std::uint32_t denominator) {
  LogPrefix(component, eventName,
            L"hr=" + HResultText(hr) + L" descriptorCount=" +
                std::to_wstring(descriptorCount) + L" streamId=" +
                std::to_wstring(streamId) + L" selected=" +
                std::to_wstring(selected ? 1 : 0) + L" majorType=" +
                GuidText(majorType) + L" subtype=" + GuidText(subtype) +
                L" width=" + std::to_wstring(width) + L" height=" +
                std::to_wstring(height) + L" fps=" + std::to_wstring(fps) +
                L" denominator=" + std::to_wstring(denominator));
}

void LogIpcStatus(const wchar_t* component, const wchar_t* eventName, HRESULT hr,
                  bool mappingOpen, DWORD openError, LONG producerState,
                  std::uint64_t publishedSequence, std::uint64_t lastReadSequence) {
  LogPrefix(component, eventName,
            L"hr=" + HResultText(hr) + L" mappingOpen=" +
                std::to_wstring(mappingOpen ? 1 : 0) + L" openError=" +
                HResultText(HRESULT_FROM_WIN32(openError)) + L" producerState=" +
                std::to_wstring(producerState) + L" publishedSequence=" +
                std::to_wstring(publishedSequence) + L" lastReadSequence=" +
                std::to_wstring(lastReadSequence));
}

void LogSampleEvent(const wchar_t* component, const wchar_t* eventName, HRESULT hr,
                    std::uint64_t sampleIndex, std::uint64_t sequence,
                    LONGLONG timestamp100ns, DWORD bufferBytes) {
  LogPrefix(component, eventName,
            L"hr=" + HResultText(hr) + L" sampleIndex=" +
                std::to_wstring(sampleIndex) + L" sequence=" +
                std::to_wstring(sequence) + L" timestamp100ns=" +
                std::to_wstring(timestamp100ns) + L" bufferBytes=" +
                std::to_wstring(bufferBytes));
}

void LogFormatEvent(const wchar_t* component, const wchar_t* eventName, HRESULT hr,
                    std::uint32_t width, std::uint32_t height,
                    std::uint32_t fps, std::uint32_t denominator) {
  LogPrefix(component, eventName,
            L"hr=" + HResultText(hr) + L" width=" + std::to_wstring(width) +
                L" height=" + std::to_wstring(height) + L" fps=" +
                std::to_wstring(fps) + L" denominator=" +
                std::to_wstring(denominator));
}

void LogAllocatorEvent(const wchar_t* component, const wchar_t* eventName, HRESULT hr,
                       const wchar_t* allocatorSource, const void* stream,
                       const void* allocator, const void* mediaType, REFGUID subtype,
                       std::uint32_t width, std::uint32_t height,
                       std::uint32_t fps, std::uint32_t denominator) {
  LogPrefix(component, eventName,
            L"hr=" + HResultText(hr) + L" allocatorSource=" +
                (allocatorSource == nullptr ? L"<null>" : allocatorSource) +
                L" stream=" + PointerText(stream) + L" allocator=" +
                PointerText(allocator) + L" mediaType=" + PointerText(mediaType) +
                L" subtype=" + GuidText(subtype) + L" width=" +
                std::to_wstring(width) + L" height=" + std::to_wstring(height) +
                L" fps=" + std::to_wstring(fps) + L" denominator=" +
                std::to_wstring(denominator));
}

void LogStreamSummary(const wchar_t* component, const wchar_t* eventName, HRESULT hr,
                      std::uint64_t requestSamples, std::uint64_t samplesProduced,
                      std::uint64_t samplesDelivered, std::uint64_t lastSequence) {
  LogPrefix(component, eventName,
            L"hr=" + HResultText(hr) + L" requestSamples=" +
                std::to_wstring(requestSamples) + L" samplesProduced=" +
                std::to_wstring(samplesProduced) + L" samplesDelivered=" +
                std::to_wstring(samplesDelivered) + L" lastSequence=" +
                std::to_wstring(lastSequence));
}

void LogRequestSampleSummary(const wchar_t* component, const wchar_t* eventName,
                             HRESULT hr, std::uint64_t requestSamples,
                             std::uint64_t requestSuccesses,
                             std::uint64_t requestFailures,
                             std::uint64_t samplesProduced,
                             std::uint64_t samplesDelivered,
                             std::uint64_t firstRequestUtc100ns,
                             std::uint64_t lastSequence) {
  LogPrefix(component, eventName,
            L"hr=" + HResultText(hr) + L" requestSamples=" +
                std::to_wstring(requestSamples) + L" requestSuccesses=" +
                std::to_wstring(requestSuccesses) + L" requestFailures=" +
                std::to_wstring(requestFailures) + L" samplesProduced=" +
                std::to_wstring(samplesProduced) + L" samplesDelivered=" +
                std::to_wstring(samplesDelivered) + L" firstRequestUtc100ns=" +
                std::to_wstring(firstRequestUtc100ns) + L" lastSequence=" +
                std::to_wstring(lastSequence));
}

}  // namespace cambridge::native
