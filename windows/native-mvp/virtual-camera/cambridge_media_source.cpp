#include "cambridge_media_source.h"

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

HRESULT CamBridgeMediaStream::Initialize(CamBridgeMediaSource* parent) {
  if (parent == nullptr) return E_INVALIDARG;
  std::lock_guard lock(mutex_);
  parent_ = parent;
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
  (void)reader_.Open();
  return S_OK;
}

HRESULT CamBridgeMediaStream::CheckState() const {
  return shutdown_ ? MF_E_SHUTDOWN : (events_ ? S_OK : E_UNEXPECTED);
}

HRESULT CamBridgeMediaStream::Start(IMFMediaType* mediaType) {
  if (mediaType == nullptr) return E_INVALIDARG;
  std::lock_guard lock(mutex_);
  HRESULT hr = CheckState();
  if (FAILED(hr)) return hr;
  mediaType_ = mediaType;
  UINT32 width = 0, height = 0, fps = 60, denominator = 1;
  MFGetAttributeSize(mediaType_.Get(), MF_MT_FRAME_SIZE, &width, &height);
  MFGetAttributeRatio(mediaType_.Get(), MF_MT_FRAME_RATE, &fps, &denominator);
  if (width == 0 || height == 0 || denominator == 0) return MF_E_INVALIDMEDIATYPE;
  width_ = width;
  height_ = height;
  stride_ = width;
  nextTimestamp100ns_ = 0;
  state_ = MF_STREAM_STATE_RUNNING;
  return events_->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, nullptr);
}

HRESULT CamBridgeMediaStream::Stop(bool sendEvent) {
  std::lock_guard lock(mutex_);
  HRESULT hr = CheckState();
  if (FAILED(hr)) return hr;
  state_ = MF_STREAM_STATE_STOPPED;
  if (sendEvent) return events_->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, nullptr);
  return S_OK;
}

HRESULT CamBridgeMediaStream::Shutdown() {
  std::lock_guard lock(mutex_);
  if (shutdown_) return S_OK;
  shutdown_ = true;
  state_ = MF_STREAM_STATE_STOPPED;
  parent_.Reset();
  descriptor_.Reset();
  attributes_.Reset();
  mediaType_.Reset();
  if (events_) events_->Shutdown();
  events_.Reset();
  reader_.Close();
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
  Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
  HRESULT hr = MFCreateMemoryBuffer(static_cast<DWORD>(bytes), &buffer);
  if (FAILED(hr)) return hr;
  BYTE* data = nullptr;
  DWORD maxLength = 0, currentLength = 0;
  if (FAILED(hr = buffer->Lock(&data, &maxLength, &currentLength))) return hr;
  FillBlack(data, maxLength, stride_, height_);
  Nv12Frame frame;
  if (reader_.IsOpen() && reader_.ReadLatest(frame) && frame.width == width_ &&
      frame.height == height_ && frame.stride == stride_ && frame.bytes.size() == bytes) {
    std::memcpy(data, frame.bytes.data(), bytes);
    nextTimestamp100ns_ = frame.timestamp100ns;
    lastSequence_ = frame.sequence;
  }
  buffer->Unlock();
  if (FAILED(hr = buffer->SetCurrentLength(static_cast<DWORD>(bytes)))) return hr;
  Microsoft::WRL::ComPtr<IMFSample> result;
  if (FAILED(hr = MFCreateSample(&result))) return hr;
  if (FAILED(hr = result->AddBuffer(buffer.Get()))) return hr;
  const auto fpsDuration = mediaType_ ? [&] {
    UINT32 fps = 60, den = 1;
    MFGetAttributeRatio(mediaType_.Get(), MF_MT_FRAME_RATE, &fps, &den);
    return den == 0 || fps == 0 ? kFrameDuration100ns60 : static_cast<LONGLONG>(10000000LL * den / fps);
  }() : static_cast<LONGLONG>(kFrameDuration100ns60);
  if (FAILED(hr = result->SetSampleTime(nextTimestamp100ns_))) return hr;
  if (FAILED(hr = result->SetSampleDuration(fpsDuration))) return hr;
  nextTimestamp100ns_ += fpsDuration;
  return result.CopyTo(sample);
}

HRESULT CamBridgeMediaStream::RequestSample(IUnknown* token) {
  std::lock_guard lock(mutex_);
  HRESULT hr = CheckState();
  if (FAILED(hr)) return hr;
  if (state_ != MF_STREAM_STATE_RUNNING) return MF_E_INVALIDREQUEST;
  if (!reader_.IsOpen()) (void)reader_.Open();
  Microsoft::WRL::ComPtr<IMFSample> sample;
  if (FAILED(hr = CreateSample(&sample))) return hr;
  if (token != nullptr && FAILED(hr = sample->SetUnknown(MFSampleExtension_Token, token))) return hr;
  return events_->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, sample.Get());
}

HRESULT CamBridgeMediaStream::SetStreamState(MF_STREAM_STATE state) {
  std::lock_guard lock(mutex_);
  HRESULT hr = CheckState();
  if (FAILED(hr)) return hr;
  if (state == MF_STREAM_STATE_RUNNING) state_ = state;
  else if (state == MF_STREAM_STATE_STOPPED || state == MF_STREAM_STATE_PAUSED) state_ = state;
  else return MF_E_INVALID_STATE_TRANSITION;
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

HRESULT CamBridgeMediaSource::Initialize(IMFAttributes*) {
  std::lock_guard lock(mutex_);
  if (initialized_) return MF_E_ALREADY_INITIALIZED;
  HRESULT hr = MFCreateEventQueue(&events_);
  if (FAILED(hr)) return hr;
  hr = MFCreateAttributes(&attributes_, 8);
  if (FAILED(hr)) return hr;
  attributes_->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE);
  attributes_->SetUINT32(MF_DEVICESTREAM_STREAM_ID, 0);
  attributes_->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1);
  attributes_->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES,
                          MFFrameSourceTypes::MFFrameSourceTypes_Color);
  stream_ = Microsoft::WRL::Make<CamBridgeMediaStream>();
  if (!stream_) return E_OUTOFMEMORY;
  if (FAILED(hr = stream_->Initialize(this))) return hr;
  Microsoft::WRL::ComPtr<IMFStreamDescriptor> descriptor;
  if (FAILED(hr = stream_->GetStreamDescriptor(&descriptor))) return hr;
  IMFStreamDescriptor* descriptors[] = {descriptor.Get()};
  if (FAILED(hr = MFCreatePresentationDescriptor(1, descriptors, &presentation_))) return hr;
  presentation_->SelectStream(0);
  initialized_ = true;
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
  if (descriptor == nullptr) return E_POINTER;
  std::lock_guard lock(mutex_);
  if (shutdown_ || !presentation_) return MF_E_SHUTDOWN;
  return presentation_->Clone(descriptor);
}

HRESULT CamBridgeMediaSource::GetCharacteristics(DWORD* characteristics) {
  if (characteristics == nullptr) return E_POINTER;
  *characteristics = MFMEDIASOURCE_IS_LIVE;
  return S_OK;
}

HRESULT CamBridgeMediaSource::Pause() { return MF_E_INVALID_STATE_TRANSITION;
}

HRESULT CamBridgeMediaSource::Shutdown() {
  std::lock_guard lock(mutex_);
  if (shutdown_) return S_OK;
  shutdown_ = true;
  if (stream_) stream_->Shutdown();
  if (events_) events_->Shutdown();
  stream_.Reset();
  presentation_.Reset();
  attributes_.Reset();
  events_.Reset();
  return S_OK;
}

HRESULT CamBridgeMediaSource::Start(IMFPresentationDescriptor* descriptor, const GUID*,
                                    const PROPVARIANT*) {
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
  if (FAILED(hr = stream_->Start(type.Get()))) return hr;
  return events_->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, nullptr);
}

HRESULT CamBridgeMediaSource::Stop() {
  std::lock_guard lock(mutex_);
  if (shutdown_ || !stream_ || !events_) return MF_E_SHUTDOWN;
  HRESULT hr = stream_->Stop(true);
  if (FAILED(hr)) return hr;
  return events_->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, nullptr);
}

HRESULT CamBridgeMediaSource::GetSourceAttributes(IMFAttributes** attributes) {
  if (attributes == nullptr) return E_POINTER;
  std::lock_guard lock(mutex_);
  if (shutdown_ || !attributes_) return MF_E_SHUTDOWN;
  return attributes_.CopyTo(attributes);
}

HRESULT CamBridgeMediaSource::GetStreamAttributes(DWORD id, IMFAttributes** attributes) {
  if (id != 0 || attributes == nullptr) return E_INVALIDARG;
  std::lock_guard lock(mutex_);
  if (shutdown_ || !stream_ || stream_->attributes() == nullptr) return MF_E_SHUTDOWN;
  return stream_->attributes()->QueryInterface(IID_PPV_ARGS(attributes));
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

HRESULT CamBridgeMediaSource::SetDefaultAllocator(DWORD, IUnknown*) { return E_NOTIMPL; }

HRESULT CamBridgeMediaSource::GetAllocatorUsage(DWORD outputStreamId, DWORD* inputStreamId,
                                                MFSampleAllocatorUsage* usage) {
  if (outputStreamId != 0 || inputStreamId == nullptr || usage == nullptr) return E_INVALIDARG;
  *inputStreamId = 0;
  *usage = MFSampleAllocatorUsage_UsesCustomAllocator;
  return S_OK;
}

#define CAMBRIDGE_ATTR_FORWARD(method, args) \
  do { if (!attributes_) { HRESULT hr = MFCreateAttributes(&attributes_, 2); if (FAILED(hr)) return hr; } return attributes_->method args; } while (false)

HRESULT CamBridgeMediaSourceActivate::Initialize() {
  HRESULT hr = MFCreateAttributes(&attributes_, 2);
  if (FAILED(hr)) return hr;
  return attributes_->SetUINT32(MF_VIRTUALCAMERA_PROVIDE_ASSOCIATED_CAMERA_SOURCES, 1);
}
HRESULT CamBridgeMediaSourceActivate::ActivateObject(REFIID riid, void** result) {
  if (result == nullptr) return E_POINTER;
  *result = nullptr;
  auto source = Microsoft::WRL::Make<CamBridgeMediaSource>();
  if (!source) return E_OUTOFMEMORY;
  HRESULT hr = source->Initialize(attributes_.Get());
  if (FAILED(hr)) return hr;
  if (riid == IID_IMFMediaSource) {
    auto* mediaSource = static_cast<IMFMediaSource*>(
        static_cast<IMFMediaSourceEx*>(source.Get()));
    mediaSource->AddRef();
    *result = mediaSource;
    return S_OK;
  }
  return source->QueryInterface(riid, result);
}
HRESULT CamBridgeMediaSourceActivate::ShutdownObject() { return S_OK; }
HRESULT CamBridgeMediaSourceActivate::DetachObject() { return S_OK; }
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
  if (outer != nullptr || result == nullptr) return outer != nullptr ? CLASS_E_NOAGGREGATION : E_POINTER;
  *result = nullptr;
  auto activate = Microsoft::WRL::Make<CamBridgeMediaSourceActivate>();
  if (!activate) return E_OUTOFMEMORY;
  HRESULT hr = activate->Initialize();
  if (FAILED(hr)) return hr;
  return activate->QueryInterface(riid, result);
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
