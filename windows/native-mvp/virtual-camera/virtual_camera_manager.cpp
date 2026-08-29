#include "cambridge_media_source.h"

#include <Windows.h>
#include <mfapi.h>
#include <mfvirtualcamera.h>
#include <shlwapi.h>

#include <iostream>
#include <string>

namespace {

using cambridge::native::kCamBridgeMediaSourceClsidString;
using cambridge::native::kCamBridgeVirtualCameraName;

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
  HKEY handle = nullptr;
  auto status = RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_WRITE,
                                nullptr, &handle, nullptr);
  if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
  status = RegSetValueExW(handle, nullptr, 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(path.c_str()),
                          static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t)));
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
      hr = camera->Start(nullptr);
      PrintHr(L"IMFVirtualCamera::Start", hr);
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
