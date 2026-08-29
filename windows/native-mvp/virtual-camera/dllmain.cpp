#include "cambridge_media_source.h"
#include "diagnostic_log.h"

#include <Windows.h>
#include <objbase.h>

using cambridge::native::CamBridgeClassFactory;
using cambridge::native::kCamBridgeMediaSourceClsid;

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    cambridge::native::g_camBridgeModule = module;
    DisableThreadLibraryCalls(module);
  }
  return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** result) {
  if (result == nullptr) {
    cambridge::native::LogControlEvent(L"DLL", L"DllGetClassObject", E_POINTER);
    return E_POINTER;
  }
  *result = nullptr;
  if (clsid != kCamBridgeMediaSourceClsid) {
    cambridge::native::LogControlEvent(L"DLL", L"DllGetClassObject", CLASS_E_CLASSNOTAVAILABLE);
    return CLASS_E_CLASSNOTAVAILABLE;
  }
  auto factory = Microsoft::WRL::Make<CamBridgeClassFactory>();
  if (!factory) {
    cambridge::native::LogControlEvent(L"DLL", L"DllGetClassObject", E_OUTOFMEMORY);
    return E_OUTOFMEMORY;
  }
  const HRESULT hr = factory->QueryInterface(iid, result);
  cambridge::native::LogControlEvent(L"DLL", L"DllGetClassObject", hr);
  return hr;
}

STDAPI DllCanUnloadNow() {
  cambridge::native::LogControlEvent(L"DLL", L"DllCanUnloadNow", S_FALSE);
  return S_FALSE;
}

STDAPI DllRegisterServer() {
  const HRESULT hr = cambridge::native::RegisterCamBridgeMediaSource(true);
  cambridge::native::LogControlEvent(L"DLL", L"DllRegisterServer", hr);
  return hr;
}

STDAPI DllUnregisterServer() {
  const HRESULT hr = cambridge::native::UnregisterCamBridgeMediaSource(true);
  cambridge::native::LogControlEvent(L"DLL", L"DllUnregisterServer", hr);
  return hr;
}
