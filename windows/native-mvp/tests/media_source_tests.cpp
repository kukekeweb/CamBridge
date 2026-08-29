#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>

#include "cambridge_media_source.h"

#include <iostream>

using Microsoft::WRL::ComPtr;
using GetClassObjectFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, LPVOID*);

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) {
    std::wcerr << L"Usage: cambridge_media_source_tests <media-source-dll>\n";
    return 2;
  }
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool comInitialized = SUCCEEDED(hr);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return 1;
  hr = MFStartup(MF_VERSION);
  if (FAILED(hr)) {
    if (comInitialized) CoUninitialize();
    return 1;
  }
  HMODULE dll = LoadLibraryW(argv[1]);
  const auto getClassObject = dll == nullptr
                                  ? nullptr
                                  : reinterpret_cast<GetClassObjectFn>(GetProcAddress(dll, "DllGetClassObject"));
  ComPtr<IClassFactory> factory;
  ComPtr<IMFActivate> activate;
  ComPtr<IMFMediaSource> source;
  ComPtr<IMFPresentationDescriptor> presentation;
  ComPtr<IMFStreamDescriptor> streamDescriptor;
  ComPtr<IMFMediaTypeHandler> typeHandler;
  if (getClassObject == nullptr ||
      FAILED(hr = getClassObject(cambridge::native::kCamBridgeMediaSourceClsid,
                                 IID_PPV_ARGS(&factory))) ||
      FAILED(hr = factory->CreateInstance(nullptr, IID_PPV_ARGS(&activate))) ||
      FAILED(hr = activate->ActivateObject(IID_PPV_ARGS(&source))) ||
      FAILED(hr = source->CreatePresentationDescriptor(&presentation))) {
    std::wcerr << L"Media Source activation failed: 0x" << std::hex
               << static_cast<unsigned long>(hr) << L"\n";
    source.Reset();
    presentation.Reset();
    streamDescriptor.Reset();
    typeHandler.Reset();
    activate.Reset();
    factory.Reset();
    if (dll) FreeLibrary(dll);
    MFShutdown();
    if (comInitialized) CoUninitialize();
    return 1;
  }
  BOOL selected = FALSE;
  if (FAILED(hr = presentation->GetStreamDescriptorByIndex(0, &selected, &streamDescriptor)) ||
      !selected || FAILED(hr = streamDescriptor->GetMediaTypeHandler(&typeHandler))) {
    std::wcerr << L"Media Source descriptor failed: 0x" << std::hex
               << static_cast<unsigned long>(hr) << L"\n";
    source->Shutdown();
    source.Reset();
    presentation.Reset();
    streamDescriptor.Reset();
    typeHandler.Reset();
    activate.Reset();
    factory.Reset();
    FreeLibrary(dll);
    MFShutdown();
    if (comInitialized) CoUninitialize();
    return 1;
  }
  DWORD typeCount = 0;
  typeHandler->GetMediaTypeCount(&typeCount);

  struct RequiredInterface {
    REFIID iid;
    const wchar_t* name;
  };
  const RequiredInterface requiredSourceInterfaces[] = {
      {__uuidof(IMFMediaSourceEx), L"IMFMediaSourceEx"},
      {__uuidof(IMFMediaSource), L"IMFMediaSource"},
      {__uuidof(IMFMediaEventGenerator), L"IMFMediaEventGenerator"},
      {__uuidof(IMFGetService), L"IMFGetService"},
      {__uuidof(IKsControl), L"IKsControl"},
      {__uuidof(IMFSampleAllocatorControl), L"IMFSampleAllocatorControl"},
  };
  for (const auto& required : requiredSourceInterfaces) {
    ComPtr<IUnknown> queried;
    const HRESULT queryHr = source->QueryInterface(required.iid,
                                                    reinterpret_cast<void**>(queried.GetAddressOf()));
    if (FAILED(queryHr)) {
      std::wcerr << L"Required source interface missing: " << required.name << L" 0x"
                 << std::hex << static_cast<unsigned long>(queryHr) << L"\n";
      source->Shutdown();
      source.Reset();
      presentation.Reset();
      streamDescriptor.Reset();
      typeHandler.Reset();
      activate.Reset();
      factory.Reset();
      FreeLibrary(dll);
      MFShutdown();
      if (comInitialized) CoUninitialize();
      return 1;
    }
  }

  ComPtr<IMFAttributes> descriptorAttributes;
  if (FAILED(hr = streamDescriptor.As(&descriptorAttributes))) {
    std::wcerr << L"Stream descriptor does not expose IMFAttributes: 0x" << std::hex
               << static_cast<unsigned long>(hr) << L"\n";
    return 1;
  }
  GUID category{};
  UINT32 streamId = 0;
  UINT32 frameServerShared = 0;
  UINT32 frameSourceTypes = 0;
  if (FAILED(hr = descriptorAttributes->GetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, &category)) ||
      category != PINNAME_VIDEO_CAPTURE ||
      FAILED(hr = descriptorAttributes->GetUINT32(MF_DEVICESTREAM_STREAM_ID, &streamId)) ||
      streamId != 0 ||
      FAILED(hr = descriptorAttributes->GetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED,
                                                   &frameServerShared)) ||
      frameServerShared != 1 ||
      FAILED(hr = descriptorAttributes->GetUINT32(
          MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, &frameSourceTypes)) ||
      frameSourceTypes != MFFrameSourceTypes::MFFrameSourceTypes_Color) {
    std::wcerr << L"Stream descriptor attributes contract mismatch: 0x" << std::hex
               << static_cast<unsigned long>(hr) << L"\n";
    return 1;
  }

  ComPtr<IMFSampleAllocatorControl> allocatorControl;
  if (FAILED(hr = source.As(&allocatorControl))) {
    std::wcerr << L"Media Source allocator control missing: 0x" << std::hex
               << static_cast<unsigned long>(hr) << L"\n";
    source->Shutdown();
    source.Reset();
    presentation.Reset();
    streamDescriptor.Reset();
    typeHandler.Reset();
    activate.Reset();
    factory.Reset();
    FreeLibrary(dll);
    MFShutdown();
    if (comInitialized) CoUninitialize();
    return 1;
  }
  DWORD inputStreamId = 0;
  MFSampleAllocatorUsage allocatorUsage = MFSampleAllocatorUsage_UsesCustomAllocator;
  if (FAILED(hr = allocatorControl->GetAllocatorUsage(0, &inputStreamId, &allocatorUsage)) ||
      inputStreamId != 0 || allocatorUsage != MFSampleAllocatorUsage_UsesProvidedAllocator) {
    std::wcerr << L"Media Source allocator contract mismatch: 0x" << std::hex
               << static_cast<unsigned long>(hr) << std::dec << L"\n";
    source->Shutdown();
    source.Reset();
    allocatorControl.Reset();
    presentation.Reset();
    streamDescriptor.Reset();
    typeHandler.Reset();
    activate.Reset();
    factory.Reset();
    FreeLibrary(dll);
    MFShutdown();
    if (comInitialized) CoUninitialize();
    return 1;
  }
  if (FAILED(hr = source->Start(presentation.Get(), nullptr, nullptr))) {
    std::wcerr << L"Media Source start failed: 0x" << std::hex
               << static_cast<unsigned long>(hr) << L"\n";
    source->Shutdown();
    source.Reset();
    presentation.Reset();
    streamDescriptor.Reset();
    typeHandler.Reset();
    activate.Reset();
    factory.Reset();
    FreeLibrary(dll);
    MFShutdown();
    if (comInitialized) CoUninitialize();
    return 1;
  }
  std::wcout << L"Media Source test: types=" << typeCount
             << L" start=0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << L"\n";
  source->Stop();
  source->Shutdown();
  source.Reset();
  allocatorControl.Reset();
  presentation.Reset();
  streamDescriptor.Reset();
  typeHandler.Reset();
  activate.Reset();
  factory.Reset();
  FreeLibrary(dll);
  MFShutdown();
  if (comInitialized) CoUninitialize();
  return SUCCEEDED(hr) ? 0 : 1;
}
