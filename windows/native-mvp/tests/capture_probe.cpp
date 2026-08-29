#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include "frame_ipc.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr DWORD kDefaultTimeoutMs = 10000;
constexpr int kTargetSamples = 120;

struct SampleObservation {
  HRESULT status = S_OK;
  DWORD flags = 0;
  LONGLONG callbackTimestamp100ns = 0;
  LONGLONG sampleTimestamp100ns = 0;
  LONGLONG sampleDuration100ns = 0;
  DWORD bufferBytes = 0;
};

std::wstring SafeDeviceName(const WCHAR* name) {
  if (name == nullptr) return L"<name unavailable>";
  constexpr std::size_t kMaxNameChars = 256;
  const auto length = wcsnlen_s(name, kMaxNameChars);
  std::wstring sanitized;
  sanitized.reserve(length);
  for (std::size_t i = 0; i < length; ++i) {
    const WCHAR character = name[i];
    sanitized.push_back(character >= 0x20 && character <= 0x7e ? character : L'?');
  }
  return sanitized;
}

void PrintHr(const wchar_t* label, HRESULT hr) {
  std::wcout << label << L": 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec
             << (SUCCEEDED(hr) ? L" (success)" : L" (failure)") << L"\n";
}

void PrintIpcStatus() {
  cambridge::native::SharedFrameReader reader;
  cambridge::native::SharedFrameStatus status;
  (void)reader.Open();
  (void)reader.GetStatus(&status);
  std::wcout << L"IPC state: mappingOpen=" << (status.mappingOpen ? L"true" : L"false")
             << L" openError=0x" << std::hex << status.openError << std::dec
             << L" producerState=" << status.producerState
             << L" latest synthetic sequence=" << status.publishedSequence
             << L" last read sequence=" << status.lastReadSequence << L"\n";
}

int RunCaptureChild() {
  std::wcout.setf(std::ios::unitbuf);
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool comInitialized = SUCCEEDED(hr);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    PrintHr(L"CoInitializeEx", hr);
    return 1;
  }
  hr = MFStartup(MF_VERSION);
  if (FAILED(hr)) {
    PrintHr(L"MFStartup", hr);
    if (comInitialized) CoUninitialize();
    return 1;
  }

  ComPtr<IMFAttributes> attributes;
  hr = MFCreateAttributes(&attributes, 2);
  if (SUCCEEDED(hr)) hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  IMFActivate** devices = nullptr;
  UINT32 count = 0;
  if (SUCCEEDED(hr)) hr = MFEnumDeviceSources(attributes.Get(), &devices, &count);
  std::wcout << L"Capture child video input count: " << count << L"\n";
  bool found = false;
  int frames = 0;
  for (UINT32 i = 0; i < count; ++i) {
    WCHAR* name = nullptr;
    WCHAR* link = nullptr;
    UINT32 nameLength = 0;
    UINT32 linkLength = 0;
    const HRESULT nameHr = devices[i]->GetAllocatedString(
        MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &nameLength);
    const std::wstring deviceName = nameHr == S_OK ? SafeDeviceName(name)
                                                   : L"<name unavailable>";
    if (nameHr != S_OK || deviceName.find(L"CamBridge") == std::wstring::npos) {
      if (name) CoTaskMemFree(name);
      devices[i]->Release();
      continue;
    }
    found = true;
    devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                                   &link, &linkLength);
    std::wcout << L"Capture child device: " << deviceName << L"\n"
               << L"    identity: " << (link ? link : L"<no symbolic link>") << L"\n";
    if (link) CoTaskMemFree(link);

    ComPtr<IMFMediaSource> source;
    hr = devices[i]->ActivateObject(IID_PPV_ARGS(&source));
    PrintHr(L"    ActivateObject", hr);
    if (SUCCEEDED(hr)) {
      ComPtr<IMFAttributes> readerAttributes;
      hr = MFCreateAttributes(&readerAttributes, 2);
      if (SUCCEEDED(hr)) hr = readerAttributes->SetUINT32(
          MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, FALSE);
      ComPtr<IMFSourceReader> reader;
      if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromMediaSource(
          source.Get(), readerAttributes.Get(), &reader);
      PrintHr(L"    SourceReader(sync)", hr);
      if (SUCCEEDED(hr)) {
        DWORD actualStream = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        for (int attempt = 0; attempt < 180 && frames < kTargetSamples; ++attempt) {
          ComPtr<IMFSample> sample;
          hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &actualStream,
                                  &flags, &timestamp, &sample);
          if (FAILED(hr)) break;
          if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) break;
          if (!sample) continue;
          ++frames;
          if (frames <= 3) {
            SampleObservation observation;
            observation.status = hr;
            observation.flags = flags;
            observation.callbackTimestamp100ns = timestamp;
            (void)sample->GetSampleTime(&observation.sampleTimestamp100ns);
            (void)sample->GetSampleDuration(&observation.sampleDuration100ns);
            ComPtr<IMFMediaBuffer> buffer;
            if (SUCCEEDED(sample->GetBufferByIndex(0, &buffer))) {
              (void)buffer->GetCurrentLength(&observation.bufferBytes);
            }
            std::wcout << L"    sample[" << frames << L"] status=0x" << std::hex
                       << static_cast<unsigned long>(observation.status) << std::dec
                       << L" timestamp=" << observation.sampleTimestamp100ns
                       << L" duration=" << observation.sampleDuration100ns
                       << L" bufferBytes=" << observation.bufferBytes << L"\n";
          }
        }
        PrintHr(L"    Last ReadSample", hr);
        std::wcout << L"    Samples received: " << frames << L"\n";
      }
      (void)source->Shutdown();
    }
    devices[i]->Release();
    for (UINT32 remaining = i + 1; remaining < count; ++remaining) {
      devices[remaining]->Release();
    }
    break;
  }
  if (devices) {
    CoTaskMemFree(devices);
  }
  std::wcout << L"Capture child result: device=" << (found ? L"found" : L"not-found")
             << L" samples=" << frames << L"\n";
  MFShutdown();
  if (comInitialized) CoUninitialize();
  return found && frames >= kTargetSamples ? 0 : 1;
}

bool IsCamBridgePresent() {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool comInitialized = SUCCEEDED(hr);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;
  hr = MFStartup(MF_VERSION);
  if (FAILED(hr)) {
    if (comInitialized) CoUninitialize();
    return false;
  }
  ComPtr<IMFAttributes> attributes;
  bool found = false;
  if (SUCCEEDED(MFCreateAttributes(&attributes, 2)) &&
      SUCCEEDED(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                    MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))) {
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    if (SUCCEEDED(MFEnumDeviceSources(attributes.Get(), &devices, &count))) {
      std::wcout << L"Video input count: " << count << L"\n";
      for (UINT32 i = 0; i < count; ++i) {
        WCHAR* name = nullptr;
        UINT32 nameLength = 0;
        const HRESULT nameHr = devices[i]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &nameLength);
        const std::wstring deviceName = nameHr == S_OK ? SafeDeviceName(name)
                                                       : L"<name unavailable>";
        std::wcout << L"[" << i << L"] " << deviceName << L"\n";
        if (nameHr == S_OK && deviceName.find(L"CamBridge") != std::wstring::npos) {
          found = true;
        }
        if (name) CoTaskMemFree(name);
        devices[i]->Release();
      }
      CoTaskMemFree(devices);
    }
  }
  MFShutdown();
  if (comInitialized) CoUninitialize();
  return found;
}

bool RunBoundedChild(DWORD timeoutMs, DWORD* childExitCode) {
  if (childExitCode == nullptr) return false;
  *childExitCode = 1;
  wchar_t modulePath[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
  if (length == 0 || length >= ARRAYSIZE(modulePath)) return false;
  std::wstring commandLine = L"\"" + std::wstring(modulePath, length) +
                             L"\" --capture-child";
  std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
  mutableCommand.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;
  HANDLE nullOutput = CreateFileW(L"NUL", GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (nullOutput == INVALID_HANDLE_VALUE) return false;
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = nullOutput;
  startup.hStdOutput = nullOutput;
  startup.hStdError = nullOutput;
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE, 0, nullptr,
                      nullptr, &startup, &process)) {
    CloseHandle(nullOutput);
    return false;
  }
  CloseHandle(nullOutput);
  const DWORD waitResult = WaitForSingleObject(process.hProcess, timeoutMs);
  if (waitResult == WAIT_TIMEOUT) {
    (void)TerminateProcess(process.hProcess, WAIT_TIMEOUT);
    (void)WaitForSingleObject(process.hProcess, 2000);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return false;
  }
  if (waitResult != WAIT_OBJECT_0 || !GetExitCodeProcess(process.hProcess, childExitCode)) {
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return false;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::wcout.setf(std::ios::unitbuf);
  bool child = false;
  DWORD timeoutMs = kDefaultTimeoutMs;
  for (int i = 1; i < argc; ++i) {
    const std::wstring argument = argv[i];
    if (argument == L"--capture-child") child = true;
    else if (argument == L"--timeout-ms" && i + 1 < argc) {
      timeoutMs = std::max<DWORD>(1, _wtoi(argv[++i]));
    }
  }
  if (child) return RunCaptureChild();

  const bool cambridgeFound = IsCamBridgePresent();
  std::wcout << L"CamBridge camera found: " << (cambridgeFound ? L"YES" : L"NO") << L"\n";
  int maximumSamplesReceived = 0;
  if (cambridgeFound) {
    DWORD childExitCode = 1;
    const bool childCompleted = RunBoundedChild(timeoutMs, &childExitCode);
    if (!childCompleted) {
      std::wcout << L"Sample delivery timeout: " << timeoutMs << L" ms\n"
                 << L"Media Source Start / Stream Start / RequestSample / sample creation / "
                    L"sample delivery: inspect media-source-<pid>.log\n"
                 << L"Samples produced/delivered: inspect media-source-<pid>.log\n";
      PrintIpcStatus();
    } else {
      maximumSamplesReceived = childExitCode == 0 ? kTargetSamples : 0;
      std::wcout << L"Capture child exit: " << childExitCode << L"\n";
      if (childExitCode == 0) {
        std::wcout << L"Samples received: " << kTargetSamples << L"+\n";
      }
    }
  }
  std::wcout << L"Synthetic/sample probe: " << maximumSamplesReceived << L" samples\n";
  return cambridgeFound && maximumSamplesReceived >= kTargetSamples ? 0 : 1;
}
