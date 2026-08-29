#include "diagnostic_log.h"

#include <ShlObj.h>
#include <Shlwapi.h>

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

void LogControlEvent(const wchar_t* component, const wchar_t* eventName, HRESULT hr) {
  LogPrefix(component, eventName, L"hr=" + HResultText(hr));
}

void LogQueryInterface(const wchar_t* component, REFIID requestedIid, HRESULT hr) {
  LogPrefix(component, L"QueryInterface",
            L"iid=" + GuidText(requestedIid) + L" hr=" + HResultText(hr));
}

}  // namespace cambridge::native
