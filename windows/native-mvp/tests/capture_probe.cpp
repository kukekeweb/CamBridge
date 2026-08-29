#include <Windows.h>
#include <mfapi.h>
#include <mferror.h>
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

const wchar_t* HrName(HRESULT hr) {
  switch (hr) {
    case S_OK: return L"S_OK";
    case E_ACCESSDENIED: return L"E_ACCESSDENIED";
    case E_INVALIDARG: return L"E_INVALIDARG";
    case E_NOINTERFACE: return L"E_NOINTERFACE";
    case E_POINTER: return L"E_POINTER";
    case MF_E_INVALIDMEDIATYPE: return L"MF_E_INVALIDMEDIATYPE";
    case MF_E_INVALIDREQUEST: return L"MF_E_INVALIDREQUEST";
    case MF_E_NOTACCEPTING: return L"MF_E_NOTACCEPTING";
    case MF_E_NOT_INITIALIZED: return L"MF_E_NOT_INITIALIZED";
    case MF_E_SHUTDOWN: return L"MF_E_SHUTDOWN";
    default: return L"unknown";
  }
}

std::wstring HrText(HRESULT hr) {
  if (hr == MF_E_NOT_INITIALIZED) {
    return L"Media Foundation platform is not initialized (MFStartup is required)";
  }
  WCHAR buffer[512]{};
  const DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
  const DWORD length = FormatMessageW(flags, nullptr, static_cast<DWORD>(hr), 0,
                                      buffer, ARRAYSIZE(buffer), nullptr);
  return length == 0 ? L"text unavailable" : std::wstring(buffer, length);
}

void PrintHr(const wchar_t* label, HRESULT hr) {
  std::wcout << label << L": 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec
             << (SUCCEEDED(hr) ? L" (success; " : L" (failure; ") << HrName(hr);
  if (FAILED(hr)) std::wcout << L"; " << HrText(hr);
  std::wcout << L")\n";
}

void PrintMediaType(const wchar_t* label, IMFMediaType* type) {
  if (type == nullptr) {
    std::wcout << label << L": <null>\n";
    return;
  }
  GUID subtype{};
  UINT32 width = 0, height = 0, fps = 0, denominator = 1;
  HRESULT hr = type->GetGUID(MF_MT_SUBTYPE, &subtype);
  if (FAILED(hr)) {
    PrintHr(label, hr);
    return;
  }
  (void)MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height);
  (void)MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &fps, &denominator);
  std::wcout << label << L": subtype=" << (subtype == MFVideoFormat_NV12 ? L"NV12" : L"other")
             << L" width=" << width << L" height=" << height << L" fps=" << fps
             << L"/" << denominator << L"\n";
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

class ChildRuntime final {
 public:
  ChildRuntime() = default;

  HRESULT Initialize() {
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    comInitialized_ = SUCCEEDED(comHr);
    PrintHr(L"CoInitializeEx", comHr);
    if (FAILED(comHr) && comHr != RPC_E_CHANGED_MODE) return comHr;

    const HRESULT mfHr = MFStartup(MF_VERSION);
    std::wcout << L"MFStartup version: 0x" << std::hex << MF_VERSION << std::dec << L"\n";
    PrintHr(L"MFStartup(MF_VERSION)", mfHr);
    if (FAILED(mfHr)) return mfHr;
    mfStarted_ = true;
    return S_OK;
  }

  ~ChildRuntime() {
    if (mfStarted_) {
      MFShutdown();
      std::wcout << L"MFShutdown: executed\n";
    }
    if (comInitialized_) {
      CoUninitialize();
      std::wcout << L"CoUninitialize: executed\n";
    }
  }

  ChildRuntime(const ChildRuntime&) = delete;
  ChildRuntime& operator=(const ChildRuntime&) = delete;

 private:
  bool comInitialized_ = false;
  bool mfStarted_ = false;
};

int RunCaptureChild() {
  std::wcout.setf(std::ios::unitbuf);
  ChildRuntime runtime;
  HRESULT hr = runtime.Initialize();
  if (FAILED(hr)) return 1;

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
      PrintHr(L"    MFCreateSourceReaderFromMediaSource", hr);
      if (SUCCEEDED(hr)) {
        ComPtr<IMFMediaType> requestedType;
        HRESULT typeHr = MFCreateMediaType(&requestedType);
        if (SUCCEEDED(typeHr)) typeHr = requestedType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(typeHr)) typeHr = requestedType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        if (SUCCEEDED(typeHr)) typeHr = MFSetAttributeSize(requestedType.Get(), MF_MT_FRAME_SIZE,
                                                            1920, 1080);
        if (SUCCEEDED(typeHr)) typeHr = MFSetAttributeRatio(requestedType.Get(), MF_MT_FRAME_RATE,
                                                             60, 1);
        if (SUCCEEDED(typeHr)) {
          typeHr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                               nullptr, requestedType.Get());
        }
        PrintHr(L"    IMFSourceReader::SetCurrentMediaType(NV12 1920x1080@60)", typeHr);
        ComPtr<IMFMediaType> currentType;
        HRESULT currentHr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                         &currentType);
        PrintHr(L"    IMFSourceReader::GetCurrentMediaType", currentHr);
        if (SUCCEEDED(currentHr)) PrintMediaType(L"    SourceReader selected media type", currentType.Get());
        DWORD actualStream = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        for (int attempt = 0; attempt < 180 && frames < kTargetSamples; ++attempt) {
          ComPtr<IMFSample> sample;
          hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &actualStream,
                                  &flags, &timestamp, &sample);
          if (attempt < 3 || FAILED(hr)) {
            PrintHr(L"    IMFSourceReader::ReadSample", hr);
            std::wcout << L"        stream=" << actualStream << L" flags=0x" << std::hex << flags
                       << std::dec << L" timestamp=" << timestamp
                       << L" sample=" << (sample ? L"yes" : L"no") << L"\n";
          }
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

bool RunBoundedChild(DWORD timeoutMs, DWORD* childExitCode, std::string* childOutput) {
  if (childExitCode == nullptr || childOutput == nullptr) return false;
  *childExitCode = 1;
  childOutput->clear();
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
  HANDLE outputRead = nullptr;
  HANDLE outputWrite = nullptr;
  if (!CreatePipe(&outputRead, &outputWrite, &security, 0)) return false;
  if (!SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0)) {
    CloseHandle(outputRead);
    CloseHandle(outputWrite);
    return false;
  }
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup.hStdOutput = outputWrite;
  startup.hStdError = outputWrite;
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE, 0, nullptr,
                      nullptr, &startup, &process)) {
    CloseHandle(outputRead);
    CloseHandle(outputWrite);
    return false;
  }
  CloseHandle(outputWrite);
  const DWORD waitResult = WaitForSingleObject(process.hProcess, timeoutMs);
  if (waitResult == WAIT_TIMEOUT) {
    (void)TerminateProcess(process.hProcess, WAIT_TIMEOUT);
    (void)WaitForSingleObject(process.hProcess, 2000);
  } else if (waitResult != WAIT_OBJECT_0 || !GetExitCodeProcess(process.hProcess, childExitCode)) {
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(outputRead);
    return false;
  }
  char buffer[4096];
  DWORD bytesRead = 0;
  while (ReadFile(outputRead, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead != 0) {
    childOutput->append(buffer, bytesRead);
  }
  CloseHandle(outputRead);
  if (waitResult == WAIT_TIMEOUT) {
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
    std::string childOutput;
    const bool childCompleted = RunBoundedChild(timeoutMs, &childExitCode, &childOutput);
    if (!childOutput.empty()) {
      std::cout << "Capture child diagnostics:\n" << childOutput;
      if (childOutput.back() != '\n') std::cout << '\n';
    }
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
