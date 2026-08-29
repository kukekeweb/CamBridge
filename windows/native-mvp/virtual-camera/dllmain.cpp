#include "cambridge_media_source.h"

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
  if (result == nullptr) return E_POINTER;
  *result = nullptr;
  if (clsid != kCamBridgeMediaSourceClsid) return CLASS_E_CLASSNOTAVAILABLE;
  auto factory = Microsoft::WRL::Make<CamBridgeClassFactory>();
  if (!factory) return E_OUTOFMEMORY;
  return factory->QueryInterface(iid, result);
}

STDAPI DllCanUnloadNow() {
  return S_FALSE;
}

STDAPI DllRegisterServer() {
  return cambridge::native::RegisterCamBridgeMediaSource(true);
}

STDAPI DllUnregisterServer() {
  return cambridge::native::UnregisterCamBridgeMediaSource(true);
}
