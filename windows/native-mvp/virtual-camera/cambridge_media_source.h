#pragma once

#include "frame_ipc.h"

#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfvirtualcamera.h>
#include <ks.h>
#include <ksproxy.h>
#include <wrl.h>
#include <wrl/client.h>

#include <cstdint>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace cambridge::native {

extern HMODULE g_camBridgeModule;

// Converts an NV12 frame to the layout requested by the Media Foundation
// stream. Equal layouts are copied without a transform; swapped dimensions
// are rotated clockwise so portrait clients receive portrait video.
bool ConvertNv12FrameToLayout(const Nv12Frame& input, std::uint32_t targetWidth,
                              std::uint32_t targetHeight, std::uint32_t targetStride,
                              Nv12Frame* output);

// {F6DC0D8C-8D0E-4DD2-9F5C-A9B83A2A3A61}
inline constexpr GUID kCamBridgeMediaSourceClsid =
    {0xf6dc0d8c, 0x8d0e, 0x4dd2, {0x9f, 0x5c, 0xa9, 0xb8, 0x3a, 0x2a, 0x3a, 0x61}};
inline constexpr wchar_t kCamBridgeMediaSourceClsidString[] =
    L"{F6DC0D8C-8D0E-4DD2-9F5C-A9B83A2A3A61}";
inline constexpr wchar_t kCamBridgeVirtualCameraName[] = L"CamBridge";

class CamBridgeMediaSource;

class CamBridgeMediaStream final
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IMFMediaStream2,
          IMFMediaStream, IMFMediaEventGenerator> {
  using RuntimeBase = Microsoft::WRL::RuntimeClass<
      Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IMFMediaStream2,
      IMFMediaStream, IMFMediaEventGenerator>;

 public:
  CamBridgeMediaStream() = default;
  ~CamBridgeMediaStream();

  IFACEMETHOD(QueryInterface)(REFIID, void**) override;

  HRESULT Initialize(CamBridgeMediaSource* parent);
  HRESULT Start(IMFMediaType* mediaType);
  HRESULT Stop(bool sendEvent);
  HRESULT Shutdown();
  HRESULT SetSampleAllocator(IMFVideoSampleAllocator* allocator);

  // IMFMediaEventGenerator
  IFACEMETHOD(BeginGetEvent)(IMFAsyncCallback*, IUnknown*) override;
  IFACEMETHOD(EndGetEvent)(IMFAsyncResult*, IMFMediaEvent**) override;
  IFACEMETHOD(GetEvent)(DWORD, IMFMediaEvent**) override;
  IFACEMETHOD(QueueEvent)(MediaEventType, REFGUID, HRESULT, const PROPVARIANT*) override;

  // IMFMediaStream
  IFACEMETHOD(GetMediaSource)(IMFMediaSource**) override;
  IFACEMETHOD(GetStreamDescriptor)(IMFStreamDescriptor**) override;
  IFACEMETHOD(RequestSample)(IUnknown*) override;

  // IMFMediaStream2
  IFACEMETHOD(SetStreamState)(MF_STREAM_STATE) override;
  IFACEMETHOD(GetStreamState)(MF_STREAM_STATE*) override;

  IMFAttributes* attributes() const { return attributes_.Get(); }

 private:
  HRESULT CheckState() const;
  HRESULT CreateSample(IMFSample** sample, bool requireNewIpcFrame = false);
  void StopSamplePump();
  void SamplePumpLoop();
  void LogAllocatorState(const wchar_t* eventName, HRESULT hr,
                         IMFMediaType* mediaType = nullptr) const;
  void ResetPacingDiagnosticsLocked();
  void MaybeLogPacingSummaryLocked(const wchar_t* eventName, HRESULT hr, bool force);

  Microsoft::WRL::ComPtr<IMFMediaSource> parent_;
  Microsoft::WRL::ComPtr<IMFMediaEventQueue> events_;
  Microsoft::WRL::ComPtr<IMFStreamDescriptor> descriptor_;
  Microsoft::WRL::ComPtr<IMFAttributes> attributes_;
  Microsoft::WRL::ComPtr<IMFMediaType> mediaType_;
  Microsoft::WRL::ComPtr<IMFVideoSampleAllocator> sampleAllocator_;
  const wchar_t* allocatorSource_ = L"none";
  SharedFrameReader reader_;
  Nv12Frame lastFrame_;
  std::mutex mutex_;
  MF_STREAM_STATE state_ = MF_STREAM_STATE_STOPPED;
  bool shutdown_ = false;
  std::int64_t nextTimestamp100ns_ = 0;
  std::uint64_t lastSequence_ = 0;
  std::uint64_t requestSampleCount_ = 0;
  std::uint64_t requestSampleSuccessCount_ = 0;
  std::uint64_t requestSampleFailureCount_ = 0;
  std::uint64_t samplesProduced_ = 0;
  std::uint64_t samplesDelivered_ = 0;
  std::uint64_t firstRequestUtc100ns_ = 0;
  LONGLONG lastSampleTimestamp100ns_ = 0;
  LONGLONG lastSampleDuration100ns_ = 0;
  MFSampleAllocatorUsage allocatorUsage_ = MFSampleAllocatorUsage_UsesProvidedAllocator;
  std::uint32_t width_ = 1920;
  std::uint32_t height_ = 1080;
  std::uint32_t stride_ = 1920;
  std::chrono::steady_clock::time_point streamStartSteady_{};
  std::chrono::steady_clock::time_point pacingWindowStart_{};
  std::chrono::steady_clock::time_point lastPacingSampleLogSteady_{};
  LONGLONG streamStartSystemTime100ns_ = 0;
  bool hasLastSampleTimestamp_ = false;
  std::uint64_t pacingRequestSamples_ = 0;
  std::uint64_t pacingAllocateSampleCalls_ = 0;
  std::uint64_t pacingSamplesCreated_ = 0;
  std::uint64_t pacingMediaSampleQueued_ = 0;
  std::uint64_t pacingMediaSampleEndGetEvent_ = 0;
  std::uint64_t pacingBeginGetEvent_ = 0;
  std::uint64_t pacingIpcReadAttempts_ = 0;
  std::uint64_t pacingIpcNewFrames_ = 0;
  std::uint64_t pacingUniqueIpcSequences_ = 0;
  std::uint64_t pacingDuplicateIpcSequenceSamples_ = 0;
  std::uint64_t pacingLatestIpcSequence_ = 0;
  std::uint64_t pacingWindowLastIpcSequence_ = 0;
  std::uint64_t pacingMediaSampleQueuedTotal_ = 0;
  std::uint64_t pacingMediaSampleEndGetEventTotal_ = 0;
  std::uint32_t lastIpcWidth_ = 0;
  std::uint32_t lastIpcHeight_ = 0;
  std::uint32_t lastIpcStride_ = 0;
  std::uint32_t lastIpcPayloadBytes_ = 0;
  std::uint32_t lastCopiedBytes_ = 0;
  std::condition_variable samplePumpCondition_;
  std::thread samplePump_;
  bool samplePumpStop_ = true;
  bool pendingSampleRequest_ = false;
  Microsoft::WRL::ComPtr<IUnknown> pendingSampleToken_;
};

class CamBridgeMediaSource final
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IMFMediaSourceEx,
          IMFMediaSource, IMFMediaEventGenerator, IMFGetService, IKsControl,
          IMFSampleAllocatorControl> {
  using RuntimeBase = Microsoft::WRL::RuntimeClass<
      Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IMFMediaSourceEx,
      IMFMediaSource, IMFMediaEventGenerator, IMFGetService, IKsControl,
      IMFSampleAllocatorControl>;

 public:
  CamBridgeMediaSource() = default;

  IFACEMETHOD(QueryInterface)(REFIID, void**) override;

  HRESULT Initialize(IMFAttributes* activationAttributes);

  // IMFMediaEventGenerator / IMFMediaSource
  IFACEMETHOD(BeginGetEvent)(IMFAsyncCallback*, IUnknown*) override;
  IFACEMETHOD(EndGetEvent)(IMFAsyncResult*, IMFMediaEvent**) override;
  IFACEMETHOD(GetEvent)(DWORD, IMFMediaEvent**) override;
  IFACEMETHOD(QueueEvent)(MediaEventType, REFGUID, HRESULT, const PROPVARIANT*) override;
  IFACEMETHOD(CreatePresentationDescriptor)(IMFPresentationDescriptor**) override;
  IFACEMETHOD(GetCharacteristics)(DWORD*) override;
  IFACEMETHOD(Pause)() override;
  IFACEMETHOD(Shutdown)() override;
  IFACEMETHOD(Start)(IMFPresentationDescriptor*, const GUID*, const PROPVARIANT*) override;
  IFACEMETHOD(Stop)() override;

  // IMFMediaSourceEx
  IFACEMETHOD(GetSourceAttributes)(IMFAttributes**) override;
  IFACEMETHOD(GetStreamAttributes)(DWORD, IMFAttributes**) override;
  IFACEMETHOD(SetD3DManager)(IUnknown*) override;

  // IMFGetService
  IFACEMETHOD(GetService)(REFGUID, REFIID, LPVOID*) override;

  // IKsControl
  IFACEMETHOD(KsProperty)(PKSPROPERTY, ULONG, LPVOID, ULONG, ULONG*) override;
  IFACEMETHOD(KsMethod)(PKSMETHOD, ULONG, LPVOID, ULONG, ULONG*) override;
  IFACEMETHOD(KsEvent)(PKSEVENT, ULONG, LPVOID, ULONG, ULONG*) override;

  // IMFSampleAllocatorControl
  IFACEMETHOD(SetDefaultAllocator)(DWORD, IUnknown*) override;
  IFACEMETHOD(GetAllocatorUsage)(DWORD, DWORD*, MFSampleAllocatorUsage*) override;

  CamBridgeMediaStream* stream() const { return stream_.Get(); }

 private:
  Microsoft::WRL::ComPtr<IMFMediaEventQueue> events_;
  Microsoft::WRL::ComPtr<IMFPresentationDescriptor> presentation_;
  Microsoft::WRL::ComPtr<IMFAttributes> attributes_;
  Microsoft::WRL::ComPtr<CamBridgeMediaStream> stream_;
  std::mutex mutex_;
  bool initialized_ = false;
  bool shutdown_ = false;
  bool streamAnnounced_ = false;
};

class CamBridgeMediaSourceActivate final
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IMFActivate> {
  using RuntimeBase = Microsoft::WRL::RuntimeClass<
      Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IMFActivate>;

 public:
  IFACEMETHOD(QueryInterface)(REFIID, void**) override;
  HRESULT Initialize();
  IFACEMETHOD(ActivateObject)(REFIID, void**) override;
  IFACEMETHOD(ShutdownObject)() override;
  IFACEMETHOD(DetachObject)() override;

  // IMFAttributes forwarding
  IFACEMETHOD(GetItem)(REFGUID, PROPVARIANT*) override;
  IFACEMETHOD(GetItemType)(REFGUID, MF_ATTRIBUTE_TYPE*) override;
  IFACEMETHOD(CompareItem)(REFGUID, REFPROPVARIANT, BOOL*) override;
  IFACEMETHOD(Compare)(IMFAttributes*, MF_ATTRIBUTES_MATCH_TYPE, BOOL*) override;
  IFACEMETHOD(GetUINT32)(REFGUID, UINT32*) override;
  IFACEMETHOD(GetUINT64)(REFGUID, UINT64*) override;
  IFACEMETHOD(GetDouble)(REFGUID, double*) override;
  IFACEMETHOD(GetGUID)(REFGUID, GUID*) override;
  IFACEMETHOD(GetStringLength)(REFGUID, UINT32*) override;
  IFACEMETHOD(GetString)(REFGUID, LPWSTR, UINT32, UINT32*) override;
  IFACEMETHOD(GetAllocatedString)(REFGUID, LPWSTR*, UINT32*) override;
  IFACEMETHOD(GetBlobSize)(REFGUID, UINT32*) override;
  IFACEMETHOD(GetBlob)(REFGUID, UINT8*, UINT32, UINT32*) override;
  IFACEMETHOD(GetAllocatedBlob)(REFGUID, UINT8**, UINT32*) override;
  IFACEMETHOD(GetUnknown)(REFGUID, REFIID, LPVOID*) override;
  IFACEMETHOD(SetItem)(REFGUID, REFPROPVARIANT) override;
  IFACEMETHOD(DeleteItem)(REFGUID) override;
  IFACEMETHOD(DeleteAllItems)() override;
  IFACEMETHOD(SetUINT32)(REFGUID, UINT32) override;
  IFACEMETHOD(SetUINT64)(REFGUID, UINT64) override;
  IFACEMETHOD(SetDouble)(REFGUID, double) override;
  IFACEMETHOD(SetGUID)(REFGUID, REFGUID) override;
  IFACEMETHOD(SetString)(REFGUID, LPCWSTR) override;
  IFACEMETHOD(SetBlob)(REFGUID, const UINT8*, UINT32) override;
  IFACEMETHOD(SetUnknown)(REFGUID, IUnknown*) override;
  IFACEMETHOD(LockStore)() override;
  IFACEMETHOD(UnlockStore)() override;
  IFACEMETHOD(GetCount)(UINT32*) override;
  IFACEMETHOD(GetItemByIndex)(UINT32, GUID*, PROPVARIANT*) override;
  IFACEMETHOD(CopyAllItems)(IMFAttributes*) override;

 private:
  Microsoft::WRL::ComPtr<IMFAttributes> attributes_;
};

class CamBridgeClassFactory final
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IClassFactory> {
  using RuntimeBase = Microsoft::WRL::RuntimeClass<
      Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IClassFactory>;

 public:
  IFACEMETHOD(QueryInterface)(REFIID, void**) override;
  IFACEMETHOD(CreateInstance)(IUnknown*, REFIID, void**) override;
  IFACEMETHOD(LockServer)(BOOL) override;
};

HRESULT RegisterCamBridgeMediaSource(bool perUser);
HRESULT UnregisterCamBridgeMediaSource(bool perUser);

}  // namespace cambridge::native
