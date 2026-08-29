#include "cambridge_media_source.h"

#include <Windows.h>
#include <devpropdef.h>
#include <mfapi.h>
#include <mfvirtualcamera.h>
#include <shlwapi.h>

#include <iostream>
#include <string>
#include <vector>

namespace {

using cambridge::native::kCamBridgeMediaSourceClsidString;
using cambridge::native::kCamBridgeVirtualCameraName;

constexpr DEVPROPKEY kVirtualCameraSourceIdProperty = {
    {0x6ac1fbf7, 0x45f7, 0x4e06, {0xbd, 0xa7, 0xf8, 0x17, 0xeb, 0xfa, 0x04, 0xd1}}, 4};
constexpr DEVPROPKEY kVirtualCameraFriendlyNameProperty = {
    {0x6ac1fbf7, 0x45f7, 0x4e06, {0xbd, 0xa7, 0xf8, 0x17, 0xeb, 0xfa, 0x04, 0xd1}}, 5};
constexpr DEVPROPKEY kVirtualCameraLifetimeProperty = {
    {0x6ac1fbf7, 0x45f7, 0x4e06, {0xbd, 0xa7, 0xf8, 0x17, 0xeb, 0xfa, 0x04, 0xd1}}, 6};
constexpr DEVPROPKEY kVirtualCameraAccessProperty = {
    {0x6ac1fbf7, 0x45f7, 0x4e06, {0xbd, 0xa7, 0xf8, 0x17, 0xeb, 0xfa, 0x04, 0xd1}}, 7};
constexpr GUID kVirtualCameraKindAttribute =
    {0xc7f7c57b, 0xdf30, 0x41d0, {0xaf, 0xfc, 0x15, 0x20, 0x1c, 0xdf, 0x92, 0x0d}};

bool IsAtLeastVirtualCameraBuild() {
  using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
  const auto ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto rtl = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
  if (rtl == nullptr) return false;
  RTL_OSVERSIONINFOW version{};
  version.dwOSVersionInfoSize = sizeof(version);
  return rtl(&version) == 0 && version.dwBuildNumber >= 22000;
}

HRESULT RegisterSourcePath(const std::wstring& path, bool registerSource, bool machine) {
  const std::wstring key = L"Software\\Classes\\CLSID\\" +
                           std::wstring(cambridge::native::kCamBridgeMediaSourceClsidString) +
                           L"\\InprocServer32";
  HKEY root = machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
  if (!registerSource) {
    const auto status = SHDeleteKeyW(root, key.c_str());
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND
               ? S_OK
               : HRESULT_FROM_WIN32(status);
  }
  std::vector<wchar_t> absoluteBuffer(32768);
  const DWORD absoluteLength = GetFullPathNameW(path.c_str(),
                                                 static_cast<DWORD>(absoluteBuffer.size()),
                                                 absoluteBuffer.data(), nullptr);
  if (absoluteLength == 0 || absoluteLength >= absoluteBuffer.size()) {
    return HRESULT_FROM_WIN32(absoluteLength == 0 ? GetLastError() : ERROR_BUFFER_OVERFLOW);
  }
  const std::wstring absolutePath(absoluteBuffer.data(), absoluteLength);
  HKEY handle = nullptr;
  auto status = RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_WRITE,
                                nullptr, &handle, nullptr);
  if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
  status = RegSetValueExW(handle, nullptr, 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(absolutePath.c_str()),
                          static_cast<DWORD>((absolutePath.size() + 1) * sizeof(wchar_t)));
  if (status == ERROR_SUCCESS) {
    constexpr wchar_t model[] = L"Both";
    status = RegSetValueExW(handle, L"ThreadingModel", 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(model), sizeof(model));
  }
  RegCloseKey(handle);
  return status == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(status);
}

HRESULT CreateVirtualCamera(IMFVirtualCamera** result) {
  return MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource,
                               MFVirtualCameraLifetime_System,
                               MFVirtualCameraAccess_CurrentUser,
                               kCamBridgeVirtualCameraName,
                               kCamBridgeMediaSourceClsidString,
                               nullptr,
                               0,
                               result);
}

HRESULT SetVirtualCameraIdentity(IMFVirtualCamera* camera) {
  if (camera == nullptr) return E_POINTER;
  const auto sourceId = std::wstring(kCamBridgeMediaSourceClsidString);
  const auto friendlyName = std::wstring(kCamBridgeVirtualCameraName);
  const auto lifetime = static_cast<std::int32_t>(MFVirtualCameraLifetime_System);
  const auto access = static_cast<std::int32_t>(MFVirtualCameraAccess_CurrentUser);
  HRESULT hr = camera->SetUINT32(kVirtualCameraKindAttribute, 0);
  if (FAILED(hr)) return hr;
  hr = camera->AddProperty(
      &kVirtualCameraSourceIdProperty, DEVPROP_TYPE_STRING,
      reinterpret_cast<const BYTE*>(sourceId.c_str()),
      static_cast<ULONG>((sourceId.size() + 1) * sizeof(wchar_t)));
  if (FAILED(hr)) return hr;
  hr = camera->AddProperty(
      &kVirtualCameraFriendlyNameProperty, DEVPROP_TYPE_STRING,
      reinterpret_cast<const BYTE*>(friendlyName.c_str()),
      static_cast<ULONG>((friendlyName.size() + 1) * sizeof(wchar_t)));
  if (FAILED(hr)) return hr;
  hr = camera->AddProperty(
      &kVirtualCameraLifetimeProperty, DEVPROP_TYPE_INT32,
      reinterpret_cast<const BYTE*>(&lifetime), sizeof(lifetime));
  if (FAILED(hr)) return hr;
  return camera->AddProperty(
      &kVirtualCameraAccessProperty, DEVPROP_TYPE_INT32,
      reinterpret_cast<const BYTE*>(&access), sizeof(access));
}

HRESULT ProbeMediaSourceActivation() {
  Microsoft::WRL::ComPtr<IMFActivate> activate;
  auto hr = CoCreateInstance(cambridge::native::kCamBridgeMediaSourceClsid, nullptr,
                             CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&activate));
  if (FAILED(hr)) return hr;
  Microsoft::WRL::ComPtr<IMFMediaSource> source;
  return activate->ActivateObject(IID_PPV_ARGS(&source));
}

void PrintHr(const wchar_t* label, HRESULT hr) {
  std::wcout << label << L": 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec
             << (SUCCEEDED(hr) ? L" (success)" : L" (failure)") << L"\n";
}
}

int wmain(int argc, wchar_t** argv) {
  bool install = false;
  bool uninstall = false;
  bool machine = false;
  std::wstring sourcePath;
  for (int i = 1; i < argc; ++i) {
    const std::wstring arg = argv[i];
    if (arg == L"--install") install = true;
    else if (arg == L"--uninstall") uninstall = true;
    else if (arg == L"--source" && i + 1 < argc) sourcePath = argv[++i];
    else if (arg == L"--machine") machine = true;
  }
  if (install == uninstall || (!install && !uninstall)) {
    std::wcerr << L"Usage: cambridge_virtual_camera_manager --install --source <dll>\n"
               << L"       cambridge_virtual_camera_manager --uninstall --source <dll>\n"
               << L"       add --machine for one-time elevated machine registration\n";
    return 2;
  }
  if (!IsAtLeastVirtualCameraBuild()) {
    std::wcerr << L"Windows build is below 22000; MFCreateVirtualCamera is unavailable.\n";
    return 3;
  }
  std::wcout << L"Control-path diagnostics: C:\\ProgramData\\CamBridge\\logs\\media-source-<pid>.log\n";
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool comInitialized = SUCCEEDED(hr);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    PrintHr(L"CoInitializeEx", hr);
    return 1;
  }
  hr = MFStartup(MF_VERSION);
  if (FAILED(hr)) {
    PrintHr(L"MFStartup", hr);
    if (hr != RPC_E_CHANGED_MODE) CoUninitialize();
    return 1;
  }
  BOOL supported = FALSE;
  const auto supportHr = MFIsVirtualCameraTypeSupported(MFVirtualCameraType_SoftwareCameraSource, &supported);
  PrintHr(L"MFIsVirtualCameraTypeSupported", supportHr);

  int exitCode = 0;
  if (!sourcePath.empty()) {
    hr = RegisterSourcePath(sourcePath, install, machine);
    PrintHr(install ? (machine ? L"Machine Custom Media Source registration" : L"Per-user Custom Media Source registration")
                    : (machine ? L"Machine Custom Media Source removal" : L"Per-user Custom Media Source removal"), hr);
    if (FAILED(hr)) exitCode = 1;
  }

  Microsoft::WRL::ComPtr<IMFVirtualCamera> camera;
  const auto activateHr = ProbeMediaSourceActivation();
  PrintHr(L"CoCreateInstance(IMFActivate)", activateHr);
  if (FAILED(activateHr)) exitCode = 1;
  hr = CreateVirtualCamera(camera.GetAddressOf());
  PrintHr(install ? L"MFCreateVirtualCamera(CurrentUser)" : L"MFCreateVirtualCamera(open existing)", hr);
  if (SUCCEEDED(hr)) {
    if (install) {
      const HRESULT identityHr = SetVirtualCameraIdentity(camera.Get());
      PrintHr(L"Virtual Camera identity properties", identityHr);
      if (SUCCEEDED(identityHr) || identityHr == E_ACCESSDENIED) {
        if (identityHr == E_ACCESSDENIED) {
          std::wcout << L"Virtual Camera identity properties require elevation; continuing to Start for CurrentUser diagnostics.\n";
        }
        hr = camera->Start(nullptr);
        PrintHr(L"IMFVirtualCamera::Start", hr);
      } else {
        hr = identityHr;
      }
      if (FAILED(hr)) {
        (void)camera->Stop();
        exitCode = 1;
      }
    } else {
      (void)camera->Stop();
      hr = camera->Remove();
      PrintHr(L"IMFVirtualCamera::Remove", hr);
    }
    if (FAILED(hr)) exitCode = 1;
  } else {
    exitCode = 1;
  }

  camera.Reset();
  MFShutdown();
  if (comInitialized) CoUninitialize();
  return exitCode;
}
