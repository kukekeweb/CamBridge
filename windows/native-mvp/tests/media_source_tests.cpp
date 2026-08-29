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
  ComPtr<IMFMediaEvent> sourceEvent;
  if (FAILED(hr = source->GetEvent(MF_EVENT_FLAG_NO_WAIT, &sourceEvent))) {
    std::wcerr << L"Media Source start event missing: 0x" << std::hex
               << static_cast<unsigned long>(hr) << L"\n";
    return 1;
  }
  MediaEventType sourceEventType = MEUnknown;
  if (FAILED(hr = sourceEvent->GetType(&sourceEventType)) ||
      (sourceEventType != MENewStream && sourceEventType != MEUpdatedStream)) {
    std::wcerr << L"Media Source start event is not stream announcement: 0x" << std::hex
               << static_cast<unsigned long>(hr) << L" type=" << std::dec
               << static_cast<unsigned long>(sourceEventType) << L"\n";
    return 1;
  }
  PROPVARIANT eventValue;
  PropVariantInit(&eventValue);
  ComPtr<IMFMediaStream> mediaStream;
  if (FAILED(hr = sourceEvent->GetValue(&eventValue)) || eventValue.vt != VT_UNKNOWN ||
      eventValue.punkVal == nullptr ||
      FAILED(hr = eventValue.punkVal->QueryInterface(IID_PPV_ARGS(&mediaStream)))) {
    std::wcerr << L"Media Source stream announcement payload missing: 0x" << std::hex
               << static_cast<unsigned long>(hr) << L"\n";
    PropVariantClear(&eventValue);
    return 1;
  }
  PropVariantClear(&eventValue);
  ComPtr<IMFMediaEvent> streamStartedEvent;
  MediaEventType streamStartedType = MEUnknown;
  if (FAILED(hr = mediaStream->GetEvent(MF_EVENT_FLAG_NO_WAIT, &streamStartedEvent)) ||
      FAILED(hr = streamStartedEvent->GetType(&streamStartedType)) ||
      streamStartedType != MEStreamStarted) {
    std::wcerr << L"Media Stream start event missing: 0x" << std::hex
               << static_cast<unsigned long>(hr) << L" type=" << std::dec
               << static_cast<unsigned long>(streamStartedType) << L"\n";
    return 1;
  }
  ComPtr<IMFMediaEvent> streamEvent;
  if (FAILED(hr = mediaStream->RequestSample(nullptr)) ||
      FAILED(hr = mediaStream->GetEvent(MF_EVENT_FLAG_NO_WAIT, &streamEvent))) {
    std::wcerr << L"Media Stream sample request failed: 0x" << std::hex
               << static_cast<unsigned long>(hr) << L"\n";
    return 1;
  }
  MediaEventType streamEventType = MEUnknown;
  PROPVARIANT sampleValue;
  PropVariantInit(&sampleValue);
  ComPtr<IMFSample> sample;
  if (FAILED(hr = streamEvent->GetType(&streamEventType)) ||
      streamEventType != MEMediaSample || FAILED(hr = streamEvent->GetValue(&sampleValue)) ||
      sampleValue.vt != VT_UNKNOWN || sampleValue.punkVal == nullptr ||
      FAILED(hr = sampleValue.punkVal->QueryInterface(IID_PPV_ARGS(&sample)))) {
    std::wcerr << L"Media Stream sample event missing: 0x" << std::hex
               << static_cast<unsigned long>(hr) << L" type=" << std::dec
               << static_cast<unsigned long>(streamEventType) << L"\n";
    PropVariantClear(&sampleValue);
    return 1;
  }
  PropVariantClear(&sampleValue);
  LONGLONG sampleTime = 0;
  LONGLONG sampleDuration = 0;
  ComPtr<IMFMediaBuffer> sampleBuffer;
  DWORD sampleBytes = 0;
  if (FAILED(hr = sample->GetSampleTime(&sampleTime)) ||
      FAILED(hr = sample->GetSampleDuration(&sampleDuration)) ||
      FAILED(hr = sample->GetBufferByIndex(0, &sampleBuffer)) ||
      FAILED(hr = sampleBuffer->GetCurrentLength(&sampleBytes)) || sampleBytes == 0) {
    std::wcerr << L"Media Stream sample payload invalid: 0x" << std::hex
               << static_cast<unsigned long>(hr) << L"\n";
    return 1;
  }
  std::wcout << L"Media Stream sample test: time=" << sampleTime
             << L" duration=" << sampleDuration << L" bytes=" << sampleBytes << L"\n";
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
  sampleBuffer.Reset();
  sample.Reset();
  streamEvent.Reset();
  streamStartedEvent.Reset();
  mediaStream.Reset();
  sourceEvent.Reset();
  FreeLibrary(dll);
  MFShutdown();
  if (comInitialized) CoUninitialize();
  return SUCCEEDED(hr) ? 0 : 1;
}
