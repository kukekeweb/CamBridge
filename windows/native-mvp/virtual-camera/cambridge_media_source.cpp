#include "cambridge_media_source.h"
#include "diagnostic_log.h"

#include <ks.h>
#include <ksmedia.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include <shlwapi.h>

#include <algorithm>
#include <array>
#include <cstring>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

namespace cambridge::native {
HMODULE g_camBridgeModule = nullptr;
namespace {

constexpr std::uint32_t kFrameDuration100ns60 = 166667;
constexpr std::uint32_t kFrameDuration100ns30 = 333333;
constexpr std::uint32_t kDefaultWidth = 1920;
constexpr std::uint32_t kDefaultHeight = 1080;
constexpr std::uint32_t kDefaultStride = 1920;

HRESULT MakeVideoType(std::uint32_t width, std::uint32_t height, std::uint32_t fps,
                      IMFMediaType** result) {
  if (result == nullptr) return E_POINTER;
  *result = nullptr;
  Microsoft::WRL::ComPtr<IMFMediaType> type;
  HRESULT hr = MFCreateMediaType(&type);
  if (FAILED(hr)) return hr;
  if (FAILED(hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video))) return hr;
  if (FAILED(hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12))) return hr;
  if (FAILED(hr = type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive))) return hr;
  if (FAILED(hr = type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE))) return hr;
  if (FAILED(hr = MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, width, height))) return hr;
  if (FAILED(hr = MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, fps, 1))) return hr;
  if (FAILED(hr = MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1))) return hr;
  if (FAILED(hr = type->SetUINT32(MF_MT_AVG_BITRATE, width * height * 12 * fps / 10))) return hr;
  return type.CopyTo(result);
}

void FillBlack(BYTE* data, std::size_t bytes, std::uint32_t stride, std::uint32_t height) {
  if (data == nullptr || bytes < static_cast<std::size_t>(stride) * height * 3 / 2) return;
  std::memset(data, 16, static_cast<std::size_t>(stride) * height);
  std::memset(data + static_cast<std::size_t>(stride) * height, 128,
              static_cast<std::size_t>(stride) * height / 2);
}

}  // namespace

HRESULT CamBridgeMediaStream::QueryInterface(REFIID iid, void** result) {
  const HRESULT hr = RuntimeBase::QueryInterface(iid, result);
  LogQueryInterface(L"CamBridgeMediaStream", iid, hr);
  return hr;
}

HRESULT CamBridgeMediaSource::QueryInterface(REFIID iid, void** result) {
  const HRESULT hr = RuntimeBase::QueryInterface(iid, result);
  LogQueryInterface(L"CamBridgeMediaSource", iid, hr);
  return hr;
}

HRESULT CamBridgeMediaSourceActivate::QueryInterface(REFIID iid, void** result) {
  const HRESULT hr = RuntimeBase::QueryInterface(iid, result);
  LogQueryInterface(L"CamBridgeMediaSourceActivate", iid, hr);
  return hr;
}

HRESULT CamBridgeClassFactory::QueryInterface(REFIID iid, void** result) {
  const HRESULT hr = RuntimeBase::QueryInterface(iid, result);
  LogQueryInterface(L"CamBridgeClassFactory", iid, hr);
  return hr;
}

HRESULT CamBridgeMediaStream::Initialize(CamBridgeMediaSource* parent) {
  LogControlEvent(L"CamBridgeMediaStream", L"Initialize.begin", S_OK);
  if (parent == nullptr) return E_INVALIDARG;
  std::lock_guard lock(mutex_);
  auto* mediaSource = static_cast<IMFMediaSource*>(
      static_cast<IMFMediaSourceEx*>(parent));
  parent_ = mediaSource;
  HRESULT hr = MFCreateEventQueue(&events_);
  if (FAILED(hr)) return hr;
  Microsoft::WRL::ComPtr<IMFMediaType> type60;
  if (FAILED(hr = MakeVideoType(kDefaultWidth, kDefaultHeight, 60, &type60))) return hr;
  Microsoft::WRL::ComPtr<IMFMediaType> type30;
  if (FAILED(hr = MakeVideoType(kDefaultWidth, kDefaultHeight, 30, &type30))) return hr;
  Microsoft::WRL::ComPtr<IMFMediaType> type720;
  if (FAILED(hr = MakeVideoType(1280, 720, 60, &type720))) return hr;
  std::array<IMFMediaType*, 3> types{type60.Get(), type30.Get(), type720.Get()};
  if (FAILED(hr = MFCreateStreamDescriptor(0, static_cast<DWORD>(types.size()), types.data(),
                                           &descriptor_))) return hr;
  if (FAILED(hr = descriptor_->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY,
                                       PINNAME_VIDEO_CAPTURE))) return hr;
  if (FAILED(hr = descriptor_->SetUINT32(MF_DEVICESTREAM_STREAM_ID, 0))) return hr;
  if (FAILED(hr = descriptor_->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1))) return hr;
  if (FAILED(hr = descriptor_->SetUINT32(
                MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES,
                MFFrameSourceTypes::MFFrameSourceTypes_Color))) return hr;
  if (FAILED(hr = MFCreateAttributes(&attributes_, 4))) return hr;
  attributes_->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE);
  attributes_->SetUINT32(MF_DEVICESTREAM_STREAM_ID, 0);
  attributes_->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1);
  attributes_->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES,
                          MFFrameSourceTypes::MFFrameSourceTypes_Color);
  Microsoft::WRL::ComPtr<IMFMediaTypeHandler> handler;
  if (FAILED(hr = descriptor_->GetMediaTypeHandler(&handler))) return hr;
  if (FAILED(hr = handler->SetCurrentMediaType(type60.Get()))) return hr;
  mediaType_ = type60;
  const bool readerOpen = reader_.Open();
  SharedFrameStatus ipcStatus;
  (void)reader_.GetStatus(&ipcStatus);
  LogIpcStatus(L"CamBridgeMediaStream", L"Initialize.ipc", readerOpen ? S_OK : E_FAIL,
               ipcStatus.mappingOpen, ipcStatus.openError, ipcStatus.producerState,
               ipcStatus.publishedSequence, ipcStatus.lastReadSequence);
  LogControlEvent(L"CamBridgeMediaStream", L"Initialize.end", S_OK);
  return S_OK;
}

HRESULT CamBridgeMediaStream::CheckState() const {
  return shutdown_ ? MF_E_SHUTDOWN : (events_ ? S_OK : E_UNEXPECTED);
}

void CamBridgeMediaStream::LogAllocatorState(const wchar_t* eventName, HRESULT hr,
                                             IMFMediaType* mediaType) const {
  GUID subtype = GUID_NULL;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t fps = 0;
  std::uint32_t denominator = 0;
  IMFMediaType* type = mediaType != nullptr ? mediaType : mediaType_.Get();
  if (type != nullptr) {
    (void)type->GetGUID(MF_MT_SUBTYPE, &subtype);
    (void)MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height);
    (void)MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &fps, &denominator);
  }
  LogAllocatorEvent(L"CamBridgeMediaStream", eventName, hr, allocatorSource_, this,
                    sampleAllocator_.Get(), type, subtype, width, height, fps, denominator);
}

HRESULT CamBridgeMediaStream::Start(IMFMediaType* mediaType) {
  LogControlEvent(L"CamBridgeMediaStream", L"Start.begin", S_OK);
  if (mediaType == nullptr) return E_INVALIDARG;
  std::lock_guard lock(mutex_);
  HRESULT hr = CheckState();
  if (FAILED(hr)) return hr;
  mediaType_ = mediaType;
  UINT32 width = 0, height = 0, fps = 60, denominator = 1;
  MFGetAttributeSize(mediaType_.Get(), MF_MT_FRAME_SIZE, &width, &height);
  MFGetAttributeRatio(mediaType_.Get(), MF_MT_FRAME_RATE, &fps, &denominator);
  if (width == 0 || height == 0 || denominator == 0) return MF_E_INVALIDMEDIATYPE;
  LogFormatEvent(L"CamBridgeMediaStream", L"Start.format", S_OK, width, height, fps,
                 denominator);
  width_ = width;
  height_ = height;
  stride_ = width;
  LogAllocatorState(L"Start.allocator.before", S_OK, mediaType_.Get());
  if (!sampleAllocator_) {
    hr = MFCreateVideoSampleAllocatorEx(IID_PPV_ARGS(&sampleAllocator_));
    allocatorSource_ = L"internal";
    LogAllocatorState(L"MFCreateVideoSampleAllocatorEx", hr, mediaType_.Get());
    if (FAILED(hr)) return hr;
  }
  hr = sampleAllocator_->InitializeSampleAllocator(10, mediaType_.Get());
  LogAllocatorState(L"InitializeSampleAllocator", hr, mediaType_.Get());
  if (FAILED(hr)) return hr;
  nextTimestamp100ns_ = 0;
  state_ = MF_STREAM_STATE_RUNNING;
  hr = events_->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, nullptr);
  LogControlEvent(L"CamBridgeMediaStream", L"Start.end", hr);
  return hr;
}

HRESULT CamBridgeMediaStream::Stop(bool sendEvent) {
  LogControlEvent(L"CamBridgeMediaStream", L"Stop.begin", S_OK);
  std::lock_guard lock(mutex_);
  HRESULT hr = CheckState();
  if (FAILED(hr)) return hr;
  state_ = MF_STREAM_STATE_STOPPED;
  if (sendEvent) {
    hr = events_->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, nullptr);
  }
  LogControlEvent(L"CamBridgeMediaStream", L"Stop.end", hr);
  return hr;
}

HRESULT CamBridgeMediaStream::Shutdown() {
  LogControlEvent(L"CamBridgeMediaStream", L"Shutdown.begin", S_OK);
  std::lock_guard lock(mutex_);
  if (shutdown_) return S_OK;
  LogStreamSummary(L"CamBridgeMediaStream", L"Shutdown.summary", S_OK,
                   requestSampleCount_, samplesProduced_, samplesDelivered_, lastSequence_);
  shutdown_ = true;
  state_ = MF_STREAM_STATE_STOPPED;
  parent_.Reset();
  descriptor_.Reset();
  attributes_.Reset();
  mediaType_.Reset();
  sampleAllocator_.Reset();
  if (events_) events_->Shutdown();
  events_.Reset();
  reader_.Close();
  LogControlEvent(L"CamBridgeMediaStream", L"Shutdown.end", S_OK);
  return S_OK;
}

HRESULT CamBridgeMediaStream::SetSampleAllocator(IMFVideoSampleAllocator* allocator) {
  LogControlEvent(L"CamBridgeMediaStream", L"SetSampleAllocator.begin", S_OK);
  if (allocator == nullptr) return E_POINTER;
  std::lock_guard lock(mutex_);
  HRESULT hr = CheckState();
  if (FAILED(hr)) return hr;
  if (state_ == MF_STREAM_STATE_RUNNING) return MF_E_INVALIDREQUEST;
  sampleAllocator_ = allocator;
  allocatorSource_ = L"provided";
  LogAllocatorState(L"SetSampleAllocator.end", S_OK, mediaType_.Get());
  return S_OK;
}

HRESULT CamBridgeMediaStream::BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) {
  if (callback == nullptr) return E_POINTER;
  std::lock_guard lock(mutex_);
  HRESULT hr = CheckState();
  if (FAILED(hr)) return hr;
  return events_->BeginGetEvent(callback, state);
}

HRESULT CamBridgeMediaStream::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) {
  if (result == nullptr || event == nullptr) return E_POINTER;
  std::lock_guard lock(mutex_);
  HRESULT hr = CheckState();
  if (FAILED(hr)) return hr;
  return events_->EndGetEvent(result, event);
}

HRESULT CamBridgeMediaStream::GetEvent(DWORD flags, IMFMediaEvent** event) {
  if (event == nullptr) return E_POINTER;
  Microsoft::WRL::ComPtr<IMFMediaEventQueue> queue;
  {
    std::lock_guard lock(mutex_);
    HRESULT hr = CheckState();
    if (FAILED(hr)) return hr;
    queue = events_;
  }
  return queue->GetEvent(flags, event);
}

HRESULT CamBridgeMediaStream::QueueEvent(MediaEventType type, REFGUID extendedType,
                                         HRESULT status, const PROPVARIANT* value) {
  std::lock_guard lock(mutex_);
  HRESULT hr = CheckState();
  if (FAILED(hr)) return hr;
  return events_->QueueEventParamVar(type, extendedType, status, value);
}

HRESULT CamBridgeMediaStream::GetMediaSource(IMFMediaSource** source) {
  if (source == nullptr) return E_POINTER;
  std::lock_guard lock(mutex_);
  HRESULT hr = CheckState();
  if (FAILED(hr)) return hr;
  return parent_.CopyTo(source);
}

HRESULT CamBridgeMediaStream::GetStreamDescriptor(IMFStreamDescriptor** descriptor) {
  if (descriptor == nullptr) return E_POINTER;
  std::lock_guard lock(mutex_);
  HRESULT hr = CheckState();
  if (FAILED(hr)) return hr;
  return descriptor_.CopyTo(descriptor);
}

HRESULT CamBridgeMediaStream::CreateSample(IMFSample** sample) {
  if (sample == nullptr) return E_POINTER;
  *sample = nullptr;
  const auto bytes = static_cast<std::size_t>(stride_) * height_ * 3 / 2;
  Microsoft::WRL::ComPtr<IMFSample> result;
  HRESULT hr = S_OK;
  const bool logSampleDetails = requestSampleCount_ <= 3;
  if (sampleAllocator_) {
    hr = sampleAllocator_->AllocateSample(&result);
    if (logSampleDetails) LogAllocatorState(L"AllocateSample", hr, mediaType_.Get());
    if (FAILED(hr)) return hr;
  } else {
    hr = MFCreateSample(&result);
    if (logSampleDetails) LogAllocatorState(L"MFCreateSample", hr, mediaType_.Get());
    if (FAILED(hr)) return hr;
    Microsoft::WRL::ComPtr<IMFMediaBuffer> newBuffer;
    hr = MFCreateMemoryBuffer(static_cast<DWORD>(bytes), &newBuffer);
    if (FAILED(hr)) return hr;
    hr = result->AddBuffer(newBuffer.Get());
    if (FAILED(hr)) return hr;
  }
  Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
  hr = result->GetBufferByIndex(0, &buffer);
  if (logSampleDetails) LogAllocatorState(L"GetBufferByIndex", hr, mediaType_.Get());
  if (FAILED(hr)) return hr;
  BYTE* data = nullptr;
  DWORD maxLength = 0, currentLength = 0;
  if (FAILED(hr = buffer->Lock(&data, &maxLength, &currentLength))) {
    if (logSampleDetails) LogAllocatorState(L"IMFMediaBuffer::Lock", hr, mediaType_.Get());
    return hr;
  }
  if (logSampleDetails) LogAllocatorState(L"IMFMediaBuffer::Lock", hr, mediaType_.Get());
  FillBlack(data, maxLength, stride_, height_);
  Nv12Frame frame;
  const bool readLatest = reader_.IsOpen() && reader_.ReadLatest(frame);
  SharedFrameStatus ipcStatus;
  (void)reader_.GetStatus(&ipcStatus);
  const auto sampleIndex = samplesProduced_ + 1;
  if (sampleIndex <= 3) {
    LogIpcStatus(L"CamBridgeMediaStream", L"CreateSample.ipc", readLatest ? S_OK : S_FALSE,
                 ipcStatus.mappingOpen, ipcStatus.openError, ipcStatus.producerState,
                 ipcStatus.publishedSequence, ipcStatus.lastReadSequence);
  }
  if (readLatest && frame.width == width_ &&
      frame.height == height_ && frame.stride == stride_ && frame.bytes.size() == bytes) {
    std::memcpy(data, frame.bytes.data(), bytes);
    nextTimestamp100ns_ = frame.timestamp100ns;
    lastSequence_ = frame.sequence;
  }
  hr = buffer->Unlock();
  if (logSampleDetails) LogAllocatorState(L"IMFMediaBuffer::Unlock", hr, mediaType_.Get());
  if (FAILED(hr)) return hr;
  if (FAILED(hr = buffer->SetCurrentLength(static_cast<DWORD>(bytes)))) return hr;
  const auto fpsDuration = mediaType_ ? [&] {
    UINT32 fps = 60, den = 1;
    MFGetAttributeRatio(mediaType_.Get(), MF_MT_FRAME_RATE, &fps, &den);
    return den == 0 || fps == 0 ? kFrameDuration100ns60 : static_cast<LONGLONG>(10000000LL * den / fps);
  }() : static_cast<LONGLONG>(kFrameDuration100ns60);
  if (FAILED(hr = result->SetSampleTime(nextTimestamp100ns_))) return hr;
  if (FAILED(hr = result->SetSampleDuration(fpsDuration))) return hr;
  lastSampleTimestamp100ns_ = nextTimestamp100ns_;
  lastSampleDuration100ns_ = fpsDuration;
  nextTimestamp100ns_ += fpsDuration;
  ++samplesProduced_;
  if (sampleIndex <= 3) {
    LogSampleEvent(L"CamBridgeMediaStream", L"SampleCreated", S_OK, sampleIndex,
                   lastSequence_, nextTimestamp100ns_ - fpsDuration,
                   static_cast<DWORD>(bytes));
  }
  return result.CopyTo(sample);
}

HRESULT CamBridgeMediaStream::RequestSample(IUnknown* token) {
  std::lock_guard lock(mutex_);
  const auto requestIndex = ++requestSampleCount_;
  if (requestIndex <= 3) {
    LogControlEvent(L"CamBridgeMediaStream", L"RequestSample.begin", S_OK);
    LogAllocatorState(L"RequestSample.allocator", S_OK, mediaType_.Get());
  }
  HRESULT hr = CheckState();
  if (FAILED(hr)) {
    if (requestIndex <= 3) LogControlEvent(L"CamBridgeMediaStream", L"RequestSample.state", hr);
    return hr;
  }
  if (state_ != MF_STREAM_STATE_RUNNING) {
    if (requestIndex <= 3) {
      LogControlEvent(L"CamBridgeMediaStream", L"RequestSample.not-running",
                      MF_E_INVALIDREQUEST);
    }
    return MF_E_INVALIDREQUEST;
  }
  if (!reader_.IsOpen()) (void)reader_.Open();
  Microsoft::WRL::ComPtr<IMFSample> sample;
  if (FAILED(hr = CreateSample(&sample))) {
    if (requestIndex <= 3) LogControlEvent(L"CamBridgeMediaStream", L"RequestSample.CreateSample", hr);
    return hr;
  }
  if (token != nullptr && FAILED(hr = sample->SetUnknown(MFSampleExtension_Token, token))) {
    if (requestIndex <= 3) LogControlEvent(L"CamBridgeMediaStream", L"RequestSample.SetToken", hr);
    return hr;
  }
  hr = events_->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, sample.Get());
  if (SUCCEEDED(hr)) {
    ++samplesDelivered_;
    if (requestIndex <= 3) {
      LogSampleEvent(L"CamBridgeMediaStream", L"SampleDelivered", hr, samplesDelivered_,
                     lastSequence_, lastSampleTimestamp100ns_,
                     static_cast<DWORD>(static_cast<std::size_t>(stride_) * height_ * 3 / 2));
    }
  } else if (requestIndex <= 3) {
    LogControlEvent(L"CamBridgeMediaStream", L"RequestSample.QueueEvent", hr);
  }
  return hr;
}

HRESULT CamBridgeMediaStream::SetStreamState(MF_STREAM_STATE state) {
  LogControlEvent(L"CamBridgeMediaStream", L"SetStreamState.begin", S_OK);
  std::lock_guard lock(mutex_);
  HRESULT hr = CheckState();
  if (FAILED(hr)) return hr;
  if (state == MF_STREAM_STATE_RUNNING) state_ = state;
  else if (state == MF_STREAM_STATE_STOPPED || state == MF_STREAM_STATE_PAUSED) state_ = state;
  else return MF_E_INVALID_STATE_TRANSITION;
  LogControlEvent(L"CamBridgeMediaStream", L"SetStreamState.end", S_OK);
  return S_OK;
}

HRESULT CamBridgeMediaStream::GetStreamState(MF_STREAM_STATE* state) {
  if (state == nullptr) return E_POINTER;
  std::lock_guard lock(mutex_);
  HRESULT hr = CheckState();
  if (FAILED(hr)) return hr;
  *state = state_;
  return S_OK;
}

HRESULT CamBridgeMediaSource::Initialize(IMFAttributes* activationAttributes) {
  LogControlEvent(L"CamBridgeMediaSource", L"Initialize.begin", S_OK);
  std::lock_guard lock(mutex_);
  if (initialized_) return MF_E_ALREADY_INITIALIZED;
  HRESULT hr = MFCreateEventQueue(&events_);
  if (FAILED(hr)) return hr;
  hr = MFCreateAttributes(&attributes_, 8);
  if (FAILED(hr)) return hr;
  if (activationAttributes != nullptr) {
    if (FAILED(hr = activationAttributes->CopyAllItems(attributes_.Get()))) return hr;
  }
  attributes_->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE);
  attributes_->SetUINT32(MF_DEVICESTREAM_STREAM_ID, 0);
  attributes_->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1);
  attributes_->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES,
                          MFFrameSourceTypes::MFFrameSourceTypes_Color);

  Microsoft::WRL::ComPtr<IMFSensorProfileCollection> profileCollection;
  Microsoft::WRL::ComPtr<IMFSensorProfile> profile;
  if (FAILED(hr = MFCreateSensorProfileCollection(&profileCollection))) return hr;
  if (FAILED(hr = MFCreateSensorProfile(KSCAMERAPROFILE_Legacy, 0, nullptr, &profile))) return hr;
  if (FAILED(hr = profile->AddProfileFilter(0, L"((RES==;FRT<=30,1;SUT==))"))) return hr;
  if (FAILED(hr = profileCollection->AddProfile(profile.Get()))) return hr;
  profile.Reset();
  if (FAILED(hr = MFCreateSensorProfile(KSCAMERAPROFILE_HighFrameRate, 0, nullptr, &profile))) return hr;
  if (FAILED(hr = profile->AddProfileFilter(0, L"((RES==;FRT>=60,1;SUT==))"))) return hr;
  if (FAILED(hr = profileCollection->AddProfile(profile.Get()))) return hr;
  if (FAILED(hr = attributes_->SetUnknown(MF_DEVICEMFT_SENSORPROFILE_COLLECTION,
                                           profileCollection.Get()))) return hr;
  stream_ = Microsoft::WRL::Make<CamBridgeMediaStream>();
  if (!stream_) return E_OUTOFMEMORY;
  if (FAILED(hr = stream_->Initialize(this))) return hr;
  Microsoft::WRL::ComPtr<IMFStreamDescriptor> descriptor;
  if (FAILED(hr = stream_->GetStreamDescriptor(&descriptor))) return hr;
  IMFStreamDescriptor* descriptors[] = {descriptor.Get()};
  if (FAILED(hr = MFCreatePresentationDescriptor(1, descriptors, &presentation_))) return hr;
  presentation_->SelectStream(0);
  initialized_ = true;
  LogControlEvent(L"CamBridgeMediaSource", L"Initialize.end", S_OK);
  return S_OK;
}

HRESULT CamBridgeMediaSource::BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) {
  if (callback == nullptr) return E_POINTER;
  std::lock_guard lock(mutex_);
  if (shutdown_ || !events_) return MF_E_SHUTDOWN;
  return events_->BeginGetEvent(callback, state);
}

HRESULT CamBridgeMediaSource::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) {
  if (result == nullptr || event == nullptr) return E_POINTER;
  std::lock_guard lock(mutex_);
  if (shutdown_ || !events_) return MF_E_SHUTDOWN;
  return events_->EndGetEvent(result, event);
}

HRESULT CamBridgeMediaSource::GetEvent(DWORD flags, IMFMediaEvent** event) {
  if (event == nullptr) return E_POINTER;
  Microsoft::WRL::ComPtr<IMFMediaEventQueue> queue;
  {
    std::lock_guard lock(mutex_);
    if (shutdown_ || !events_) return MF_E_SHUTDOWN;
    queue = events_;
  }
  return queue->GetEvent(flags, event);
}

HRESULT CamBridgeMediaSource::QueueEvent(MediaEventType type, REFGUID extendedType,
                                         HRESULT status, const PROPVARIANT* value) {
  std::lock_guard lock(mutex_);
  if (shutdown_ || !events_) return MF_E_SHUTDOWN;
  return events_->QueueEventParamVar(type, extendedType, status, value);
}

HRESULT CamBridgeMediaSource::CreatePresentationDescriptor(IMFPresentationDescriptor** descriptor) {
  LogControlEvent(L"CamBridgeMediaSource", L"CreatePresentationDescriptor.begin", S_OK);
  if (descriptor == nullptr) return E_POINTER;
  std::lock_guard lock(mutex_);
  if (shutdown_ || !presentation_) return MF_E_SHUTDOWN;
  const HRESULT hr = presentation_->Clone(descriptor);
  LogControlEvent(L"CamBridgeMediaSource", L"CreatePresentationDescriptor.end", hr);
  return hr;
}

HRESULT CamBridgeMediaSource::GetCharacteristics(DWORD* characteristics) {
  if (characteristics == nullptr) return E_POINTER;
  *characteristics = MFMEDIASOURCE_IS_LIVE;
  LogControlEvent(L"CamBridgeMediaSource", L"GetCharacteristics", S_OK);
  return S_OK;
}

HRESULT CamBridgeMediaSource::Pause() { return MF_E_INVALID_STATE_TRANSITION;
}

HRESULT CamBridgeMediaSource::Shutdown() {
  LogControlEvent(L"CamBridgeMediaSource", L"Shutdown.begin", S_OK);
  std::lock_guard lock(mutex_);
  if (shutdown_) return S_OK;
  shutdown_ = true;
  if (stream_) stream_->Shutdown();
  if (events_) events_->Shutdown();
  stream_.Reset();
  presentation_.Reset();
  attributes_.Reset();
  events_.Reset();
  LogControlEvent(L"CamBridgeMediaSource", L"Shutdown.end", S_OK);
  return S_OK;
}

HRESULT CamBridgeMediaSource::Start(IMFPresentationDescriptor* descriptor, const GUID*,
                                    const PROPVARIANT*) {
  LogControlEvent(L"CamBridgeMediaSource", L"Start.begin", S_OK);
  if (descriptor == nullptr) return E_POINTER;
  std::lock_guard lock(mutex_);
  if (shutdown_ || !stream_ || !events_) return MF_E_SHUTDOWN;
  BOOL selected = FALSE;
  HRESULT hr = S_OK;
  Microsoft::WRL::ComPtr<IMFStreamDescriptor> streamDescriptor;
  if (FAILED(hr = descriptor->GetStreamDescriptorByIndex(0, &selected, &streamDescriptor))) return hr;
  if (!selected) return MF_E_INVALIDREQUEST;
  Microsoft::WRL::ComPtr<IMFMediaTypeHandler> handler;
  if (FAILED(hr = streamDescriptor->GetMediaTypeHandler(&handler))) return hr;
  Microsoft::WRL::ComPtr<IMFMediaType> type;
  if (FAILED(hr = handler->GetCurrentMediaType(&type))) return hr;
  Microsoft::WRL::ComPtr<IUnknown> unknownStream;
  if (FAILED(hr = stream_->QueryInterface(IID_PPV_ARGS(&unknownStream)))) return hr;
  const auto streamEventType = streamAnnounced_ ? MEUpdatedStream : MENewStream;
  if (FAILED(hr = events_->QueueEventParamUnk(streamEventType, GUID_NULL, S_OK,
                                               unknownStream.Get()))) {
    LogControlEvent(L"CamBridgeMediaSource", L"Start.StreamAnnouncement", hr);
    return hr;
  }
  streamAnnounced_ = true;
  if (FAILED(hr = stream_->Start(type.Get()))) return hr;
  hr = events_->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, nullptr);
  LogControlEvent(L"CamBridgeMediaSource", L"Start.end", hr);
  return hr;
}

HRESULT CamBridgeMediaSource::Stop() {
  LogControlEvent(L"CamBridgeMediaSource", L"Stop.begin", S_OK);
  std::lock_guard lock(mutex_);
  if (shutdown_ || !stream_ || !events_) return MF_E_SHUTDOWN;
  HRESULT hr = stream_->Stop(true);
  if (FAILED(hr)) return hr;
  hr = events_->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, nullptr);
  LogControlEvent(L"CamBridgeMediaSource", L"Stop.end", hr);
  return hr;
}

HRESULT CamBridgeMediaSource::GetSourceAttributes(IMFAttributes** attributes) {
  LogControlEvent(L"CamBridgeMediaSource", L"GetSourceAttributes.begin", S_OK);
  if (attributes == nullptr) return E_POINTER;
  std::lock_guard lock(mutex_);
  if (shutdown_ || !attributes_) return MF_E_SHUTDOWN;
  const HRESULT hr = attributes_.CopyTo(attributes);
  LogControlEvent(L"CamBridgeMediaSource", L"GetSourceAttributes.end", hr);
  return hr;
}

HRESULT CamBridgeMediaSource::GetStreamAttributes(DWORD id, IMFAttributes** attributes) {
  LogControlEvent(L"CamBridgeMediaSource", L"GetStreamAttributes.begin", S_OK);
  if (id != 0 || attributes == nullptr) return E_INVALIDARG;
  std::lock_guard lock(mutex_);
  if (shutdown_ || !stream_ || stream_->attributes() == nullptr) return MF_E_SHUTDOWN;
  const HRESULT hr = stream_->attributes()->QueryInterface(IID_PPV_ARGS(attributes));
  LogControlEvent(L"CamBridgeMediaSource", L"GetStreamAttributes.end", hr);
  return hr;
}

HRESULT CamBridgeMediaSource::SetD3DManager(IUnknown*) { return S_OK; }

HRESULT CamBridgeMediaSource::GetService(REFGUID, REFIID, LPVOID* object) {
  if (object == nullptr) return E_POINTER;
  *object = nullptr;
  return MF_E_UNSUPPORTED_SERVICE;
}

HRESULT CamBridgeMediaSource::KsProperty(PKSPROPERTY property, ULONG propertyLength,
                                          LPVOID, ULONG, ULONG*) {
  if (property == nullptr || propertyLength < sizeof(KSPROPERTY)) return E_INVALIDARG;
  return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

HRESULT CamBridgeMediaSource::KsMethod(PKSMETHOD, ULONG, LPVOID, ULONG, ULONG*) {
  return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

HRESULT CamBridgeMediaSource::KsEvent(PKSEVENT, ULONG, LPVOID, ULONG, ULONG*) {
  return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

HRESULT CamBridgeMediaSource::SetDefaultAllocator(DWORD outputStreamId, IUnknown* allocator) {
  LogControlEvent(L"CamBridgeMediaSource", L"SetDefaultAllocator.begin", S_OK);
  if (outputStreamId != 0 || allocator == nullptr || stream_ == nullptr) return E_INVALIDARG;
  Microsoft::WRL::ComPtr<IMFVideoSampleAllocator> videoAllocator;
  HRESULT hr = allocator->QueryInterface(IID_PPV_ARGS(&videoAllocator));
  if (FAILED(hr)) {
    LogControlEvent(L"CamBridgeMediaSource", L"SetDefaultAllocator.QueryInterface", hr);
  }
  if (FAILED(hr)) return hr;
  LogAllocatorEvent(L"CamBridgeMediaSource", L"SetDefaultAllocator.allocator", S_OK,
                    L"provided", stream_.Get(), videoAllocator.Get(), nullptr,
                    GUID_NULL, 0, 0, 0, 0);
  hr = stream_->SetSampleAllocator(videoAllocator.Get());
  LogControlEvent(L"CamBridgeMediaSource", L"SetDefaultAllocator.end", hr);
  return hr;
}

HRESULT CamBridgeMediaSource::GetAllocatorUsage(DWORD outputStreamId, DWORD* inputStreamId,
                                                MFSampleAllocatorUsage* usage) {
  if (outputStreamId != 0 || inputStreamId == nullptr || usage == nullptr) return E_INVALIDARG;
  *inputStreamId = 0;
  *usage = MFSampleAllocatorUsage_UsesProvidedAllocator;
  LogAllocatorEvent(L"CamBridgeMediaSource", L"GetAllocatorUsage", S_OK,
                    L"provided", stream_.Get(), nullptr, nullptr, GUID_NULL, 0, 0, 0, 0);
  return S_OK;
}

#define CAMBRIDGE_ATTR_FORWARD(method, args) \
  do { if (!attributes_) { HRESULT hr = MFCreateAttributes(&attributes_, 2); if (FAILED(hr)) return hr; } return attributes_->method args; } while (false)

HRESULT CamBridgeMediaSourceActivate::Initialize() {
  LogControlEvent(L"CamBridgeMediaSourceActivate", L"Initialize.begin", S_OK);
  HRESULT hr = MFCreateAttributes(&attributes_, 2);
  if (FAILED(hr)) return hr;
  hr = attributes_->SetUINT32(MF_VIRTUALCAMERA_PROVIDE_ASSOCIATED_CAMERA_SOURCES, 1);
  LogControlEvent(L"CamBridgeMediaSourceActivate", L"Initialize.end", hr);
  return hr;
}
HRESULT CamBridgeMediaSourceActivate::ActivateObject(REFIID riid, void** result) {
  LogQueryInterface(L"CamBridgeMediaSourceActivate.ActivateObject", riid, S_OK);
  if (result == nullptr) return E_POINTER;
  *result = nullptr;
  auto source = Microsoft::WRL::Make<CamBridgeMediaSource>();
  if (!source) return E_OUTOFMEMORY;
  HRESULT hr = source->Initialize(attributes_.Get());
  if (FAILED(hr)) {
    LogControlEvent(L"CamBridgeMediaSourceActivate", L"ActivateObject.Initialize", hr);
    return hr;
  }
  if (riid == IID_IMFMediaSource) {
    auto* mediaSource = static_cast<IMFMediaSource*>(
        static_cast<IMFMediaSourceEx*>(source.Get()));
    mediaSource->AddRef();
    *result = mediaSource;
    LogControlEvent(L"CamBridgeMediaSourceActivate", L"ActivateObject.end", S_OK);
    return S_OK;
  }
  hr = source->QueryInterface(riid, result);
  LogControlEvent(L"CamBridgeMediaSourceActivate", L"ActivateObject.end", hr);
  return hr;
}
HRESULT CamBridgeMediaSourceActivate::ShutdownObject() {
  LogControlEvent(L"CamBridgeMediaSourceActivate", L"ShutdownObject", S_OK);
  return S_OK;
}
HRESULT CamBridgeMediaSourceActivate::DetachObject() {
  LogControlEvent(L"CamBridgeMediaSourceActivate", L"DetachObject", S_OK);
  return S_OK;
}
HRESULT CamBridgeMediaSourceActivate::GetItem(REFGUID k, PROPVARIANT* v) { CAMBRIDGE_ATTR_FORWARD(GetItem, (k, v)); }
HRESULT CamBridgeMediaSourceActivate::GetItemType(REFGUID k, MF_ATTRIBUTE_TYPE* v) { CAMBRIDGE_ATTR_FORWARD(GetItemType, (k, v)); }
HRESULT CamBridgeMediaSourceActivate::CompareItem(REFGUID k, REFPROPVARIANT v, BOOL* r) { CAMBRIDGE_ATTR_FORWARD(CompareItem, (k, v, r)); }
HRESULT CamBridgeMediaSourceActivate::Compare(IMFAttributes* a, MF_ATTRIBUTES_MATCH_TYPE t, BOOL* r) { CAMBRIDGE_ATTR_FORWARD(Compare, (a, t, r)); }
HRESULT CamBridgeMediaSourceActivate::GetUINT32(REFGUID k, UINT32* v) { CAMBRIDGE_ATTR_FORWARD(GetUINT32, (k, v)); }
HRESULT CamBridgeMediaSourceActivate::GetUINT64(REFGUID k, UINT64* v) { CAMBRIDGE_ATTR_FORWARD(GetUINT64, (k, v)); }
HRESULT CamBridgeMediaSourceActivate::GetDouble(REFGUID k, double* v) { CAMBRIDGE_ATTR_FORWARD(GetDouble, (k, v)); }
HRESULT CamBridgeMediaSourceActivate::GetGUID(REFGUID k, GUID* v) { CAMBRIDGE_ATTR_FORWARD(GetGUID, (k, v)); }
HRESULT CamBridgeMediaSourceActivate::GetStringLength(REFGUID k, UINT32* v) { CAMBRIDGE_ATTR_FORWARD(GetStringLength, (k, v)); }
HRESULT CamBridgeMediaSourceActivate::GetString(REFGUID k, LPWSTR v, UINT32 n, UINT32* l) { CAMBRIDGE_ATTR_FORWARD(GetString, (k, v, n, l)); }
HRESULT CamBridgeMediaSourceActivate::GetAllocatedString(REFGUID k, LPWSTR* v, UINT32* l) { CAMBRIDGE_ATTR_FORWARD(GetAllocatedString, (k, v, l)); }
HRESULT CamBridgeMediaSourceActivate::GetBlobSize(REFGUID k, UINT32* v) { CAMBRIDGE_ATTR_FORWARD(GetBlobSize, (k, v)); }
HRESULT CamBridgeMediaSourceActivate::GetBlob(REFGUID k, UINT8* b, UINT32 n, UINT32* l) { CAMBRIDGE_ATTR_FORWARD(GetBlob, (k, b, n, l)); }
HRESULT CamBridgeMediaSourceActivate::GetAllocatedBlob(REFGUID k, UINT8** b, UINT32* n) { CAMBRIDGE_ATTR_FORWARD(GetAllocatedBlob, (k, b, n)); }
HRESULT CamBridgeMediaSourceActivate::GetUnknown(REFGUID k, REFIID i, LPVOID* v) { CAMBRIDGE_ATTR_FORWARD(GetUnknown, (k, i, v)); }
HRESULT CamBridgeMediaSourceActivate::SetItem(REFGUID k, REFPROPVARIANT v) { CAMBRIDGE_ATTR_FORWARD(SetItem, (k, v)); }
HRESULT CamBridgeMediaSourceActivate::DeleteItem(REFGUID k) { CAMBRIDGE_ATTR_FORWARD(DeleteItem, (k)); }
HRESULT CamBridgeMediaSourceActivate::DeleteAllItems() { CAMBRIDGE_ATTR_FORWARD(DeleteAllItems, ()); }
HRESULT CamBridgeMediaSourceActivate::SetUINT32(REFGUID k, UINT32 v) { CAMBRIDGE_ATTR_FORWARD(SetUINT32, (k, v)); }
HRESULT CamBridgeMediaSourceActivate::SetUINT64(REFGUID k, UINT64 v) { CAMBRIDGE_ATTR_FORWARD(SetUINT64, (k, v)); }
HRESULT CamBridgeMediaSourceActivate::SetDouble(REFGUID k, double v) { CAMBRIDGE_ATTR_FORWARD(SetDouble, (k, v)); }
HRESULT CamBridgeMediaSourceActivate::SetGUID(REFGUID k, REFGUID v) { CAMBRIDGE_ATTR_FORWARD(SetGUID, (k, v)); }
HRESULT CamBridgeMediaSourceActivate::SetString(REFGUID k, LPCWSTR v) { CAMBRIDGE_ATTR_FORWARD(SetString, (k, v)); }
HRESULT CamBridgeMediaSourceActivate::SetBlob(REFGUID k, const UINT8* b, UINT32 n) { CAMBRIDGE_ATTR_FORWARD(SetBlob, (k, b, n)); }
HRESULT CamBridgeMediaSourceActivate::SetUnknown(REFGUID k, IUnknown* v) { CAMBRIDGE_ATTR_FORWARD(SetUnknown, (k, v)); }
HRESULT CamBridgeMediaSourceActivate::LockStore() { CAMBRIDGE_ATTR_FORWARD(LockStore, ()); }
HRESULT CamBridgeMediaSourceActivate::UnlockStore() { CAMBRIDGE_ATTR_FORWARD(UnlockStore, ()); }
HRESULT CamBridgeMediaSourceActivate::GetCount(UINT32* v) { CAMBRIDGE_ATTR_FORWARD(GetCount, (v)); }
HRESULT CamBridgeMediaSourceActivate::GetItemByIndex(UINT32 n, GUID* k, PROPVARIANT* v) { CAMBRIDGE_ATTR_FORWARD(GetItemByIndex, (n, k, v)); }
HRESULT CamBridgeMediaSourceActivate::CopyAllItems(IMFAttributes* d) { CAMBRIDGE_ATTR_FORWARD(CopyAllItems, (d)); }

HRESULT CamBridgeClassFactory::CreateInstance(IUnknown* outer, REFIID riid, void** result) {
  LogQueryInterface(L"CamBridgeClassFactory.CreateInstance", riid, S_OK);
  if (outer != nullptr || result == nullptr) return outer != nullptr ? CLASS_E_NOAGGREGATION : E_POINTER;
  *result = nullptr;
  auto activate = Microsoft::WRL::Make<CamBridgeMediaSourceActivate>();
  if (!activate) return E_OUTOFMEMORY;
  HRESULT hr = activate->Initialize();
  if (FAILED(hr)) return hr;
  hr = activate->QueryInterface(riid, result);
  LogControlEvent(L"CamBridgeClassFactory", L"CreateInstance.end", hr);
  return hr;
}
HRESULT CamBridgeClassFactory::LockServer(BOOL) { return S_OK; }

HRESULT RegisterCamBridgeMediaSource(bool perUser) {
  HKEY root = perUser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
  const std::wstring key = L"Software\\Classes\\CLSID\\" +
                           std::wstring(kCamBridgeMediaSourceClsidString) + L"\\InprocServer32";
  wchar_t modulePath[MAX_PATH]{};
  if (g_camBridgeModule == nullptr) return E_UNEXPECTED;
  DWORD length = GetModuleFileNameW(g_camBridgeModule, modulePath, MAX_PATH);
  if (length == 0 || length == MAX_PATH) return HRESULT_FROM_WIN32(GetLastError());
  HKEY keyHandle = nullptr;
  LSTATUS status = RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr,
                                   &keyHandle, nullptr);
  if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
  status = RegSetValueExW(keyHandle, nullptr, 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(modulePath),
                          (length + 1) * sizeof(wchar_t));
  if (status == ERROR_SUCCESS) {
    constexpr wchar_t threading[] = L"Both";
    status = RegSetValueExW(keyHandle, L"ThreadingModel", 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(threading), sizeof(threading));
  }
  RegCloseKey(keyHandle);
  return status == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(status);
}

HRESULT UnregisterCamBridgeMediaSource(bool perUser) {
  HKEY root = perUser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
  const std::wstring key = L"Software\\Classes\\CLSID\\" +
                           std::wstring(kCamBridgeMediaSourceClsidString);
  const auto status = SHDeleteKeyW(root, key.c_str());
  return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(status);
}

}  // namespace cambridge::native
