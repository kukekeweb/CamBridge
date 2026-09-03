#include <Windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>

#include "cambridge_media_source.h"

#include <iostream>
#include <string>

using Microsoft::WRL::ComPtr;
using GetClassObjectFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, LPVOID*);

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) {
    std::wcerr << L"Usage: cambridge_media_source_tests <media-source-dll>\n";
    return 2;
  }
  const bool useProvidedAllocator =
      argc >= 3 && std::wstring(argv[2]) == L"--provided-allocator";
  const bool usePortrait =
      (argc >= 3 && std::wstring(argv[2]) == L"--portrait") ||
      (argc >= 4 && std::wstring(argv[3]) == L"--portrait");
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool comInitialized = SUCCEEDED(hr);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return 1;
  hr = MFStartup(MF_VERSION);
  if (FAILED(hr)) {
    if (comInitialized) CoUninitialize();
    return 1;
  }
  cambridge::native::SharedFrameProducer producer;
  if (!producer.Create()) {
    std::wcerr << L"Synthetic producer setup failed\n";
    MFShutdown();
    if (comInitialized) CoUninitialize();
    return 1;
  }
  cambridge::native::Nv12Frame syntheticFrame;
  syntheticFrame.width = 1920;
  syntheticFrame.height = 1080;
  syntheticFrame.stride = 1920;
  syntheticFrame.timestamp100ns = 0;
  syntheticFrame.bytes.assign(static_cast<std::size_t>(syntheticFrame.stride) *
                                  syntheticFrame.height * 3 / 2,
                              42);
  if (!producer.Publish(syntheticFrame)) {
    std::wcerr << L"Synthetic producer initial publish failed\n";
    producer.Close();
    MFShutdown();
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
  bool hasPortrait60 = false;
  bool has720p60 = false;
  for (DWORD index = 0; index < typeCount; ++index) {
    ComPtr<IMFMediaType> candidate;
    if (FAILED(typeHandler->GetMediaTypeByIndex(index, &candidate))) continue;
    UINT32 width = 0;
    UINT32 height = 0;
    UINT32 fps = 0;
    UINT32 denominator = 0;
    if (SUCCEEDED(MFGetAttributeSize(candidate.Get(), MF_MT_FRAME_SIZE, &width, &height)) &&
        SUCCEEDED(MFGetAttributeRatio(candidate.Get(), MF_MT_FRAME_RATE, &fps, &denominator)) &&
        width == 1080 && height == 1920 && fps == 60 && denominator == 1) {
      hasPortrait60 = true;
    }
    if (width == 1280 && height == 720 && fps == 60 && denominator == 1) {
      has720p60 = true;
    }
  }
  if (has720p60) {
    std::wcerr << L"Media Source must not advertise unsupported 1280x720@60\n";
    return 1;
  }
  if (!hasPortrait60) {
    std::wcerr << L"Media Source does not expose portrait 1080x1920@60\n";
    return 1;
  }
  if (usePortrait) {
    for (DWORD index = 0; index < typeCount; ++index) {
      ComPtr<IMFMediaType> candidate;
      if (FAILED(typeHandler->GetMediaTypeByIndex(index, &candidate))) continue;
      UINT32 width = 0;
      UINT32 height = 0;
      UINT32 fps = 0;
      UINT32 denominator = 0;
      if (SUCCEEDED(MFGetAttributeSize(candidate.Get(), MF_MT_FRAME_SIZE, &width, &height)) &&
          SUCCEEDED(MFGetAttributeRatio(candidate.Get(), MF_MT_FRAME_RATE, &fps, &denominator)) &&
          width == 1080 && height == 1920 && fps == 60 && denominator == 1) {
        if (FAILED(hr = typeHandler->SetCurrentMediaType(candidate.Get()))) {
          std::wcerr << L"Portrait media type selection failed: 0x" << std::hex
                     << static_cast<unsigned long>(hr) << L"\n";
          return 1;
        }
        break;
      }
    }
  }

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
  if (useProvidedAllocator) {
    ComPtr<IMFVideoSampleAllocator> providedAllocator;
    if (FAILED(hr = MFCreateVideoSampleAllocatorEx(IID_PPV_ARGS(&providedAllocator)))) {
      std::wcerr << L"Provided sample allocator creation failed: 0x" << std::hex
                 << static_cast<unsigned long>(hr) << L"\n";
      return 1;
    }
    if (FAILED(hr = allocatorControl->SetDefaultAllocator(0, providedAllocator.Get()))) {
      std::wcerr << L"Provided sample allocator setup failed: 0x" << std::hex
                 << static_cast<unsigned long>(hr) << L"\n";
      return 1;
    }
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
  if (FAILED(hr = mediaStream->RequestSample(nullptr))) {
    std::wcerr << L"Media Stream sample request failed: 0x" << std::hex
               << static_cast<unsigned long>(hr) << L"\n";
    return 1;
  }
  for (int attempt = 0; attempt < 2000; ++attempt) {
    hr = mediaStream->GetEvent(MF_EVENT_FLAG_NO_WAIT, &streamEvent);
    if (SUCCEEDED(hr)) break;
    if (hr != MF_E_NO_EVENTS_AVAILABLE) break;
    Sleep(1);
  }
  if (FAILED(hr)) {
    std::wcerr << L"Media Stream sample event timeout/failure: 0x" << std::hex
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
  const LONGLONG now = MFGetSystemTime();
  const LONGLONG timestampDelta = sampleTime >= now ? sampleTime - now : now - sampleTime;
  if (timestampDelta > 5 * 10000000LL) {
    std::wcerr << L"Media Stream sample timestamp is not in MF system-time domain: time="
               << sampleTime << L" now=" << now << L" delta=" << timestampDelta << L"\n";
    return 1;
  }
  std::wcout << L"Media Stream sample test: time=" << sampleTime
             << L" duration=" << sampleDuration << L" bytes=" << sampleBytes << L"\n";
  const DWORD expectedBytes = usePortrait ? 1080u * 1920u * 3u / 2u : 1920u * 1080u * 3u / 2u;
  if (sampleBytes != expectedBytes) {
    std::wcerr << L"Media Stream sample size does not match selected layout: expected="
               << expectedBytes << L" actual=" << sampleBytes << L"\n";
    return 1;
  }

  // A second request must wait for a new producer sequence rather than
  // immediately creating a duplicate sample from the latest frame.
  streamEvent.Reset();
  if (FAILED(hr = mediaStream->RequestSample(nullptr))) {
    std::wcerr << L"Second media stream sample request failed: 0x" << std::hex
               << static_cast<unsigned long>(hr) << L"\n";
    return 1;
  }
  Sleep(50);
  hr = mediaStream->GetEvent(MF_EVENT_FLAG_NO_WAIT, &streamEvent);
  if (hr != MF_E_NO_EVENTS_AVAILABLE) {
    std::wcerr << L"Media Stream delivered a duplicate without a new IPC frame: 0x"
               << std::hex << static_cast<unsigned long>(hr) << L"\n";
    return 1;
  }
  if (!producer.Publish(syntheticFrame)) {
    std::wcerr << L"Synthetic producer second publish failed\n";
    return 1;
  }
  streamEvent.Reset();
  for (int attempt = 0; attempt < 2000; ++attempt) {
    hr = mediaStream->GetEvent(MF_EVENT_FLAG_NO_WAIT, &streamEvent);
    if (SUCCEEDED(hr)) break;
    if (hr != MF_E_NO_EVENTS_AVAILABLE) break;
    Sleep(1);
  }
  if (FAILED(hr)) {
    std::wcerr << L"Second media stream sample event timeout/failure: 0x" << std::hex
               << static_cast<unsigned long>(hr) << L"\n";
    return 1;
  }
  MediaEventType secondStreamEventType = MEUnknown;
  if (FAILED(hr = streamEvent->GetType(&secondStreamEventType)) ||
      secondStreamEventType != MEMediaSample) {
    std::wcerr << L"Second media stream event is not a sample: 0x" << std::hex
               << static_cast<unsigned long>(hr) << L"\n";
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
