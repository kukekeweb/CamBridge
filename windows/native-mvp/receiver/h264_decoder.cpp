#include "h264_decoder.h"

#include <Windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <sstream>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace cambridge::native {
namespace {

constexpr DWORD kHardwareFlags = MFT_ENUM_FLAG_HARDWARE;
constexpr DWORD kSoftwareFlags = MFT_ENUM_FLAG_SORTANDFILTER | MFT_ENUM_FLAG_SYNCMFT |
                                 MFT_ENUM_FLAG_ASYNCMFT | MFT_ENUM_FLAG_LOCALMFT;

bool ValidConfig(const H264DecoderConfig& config) {
  return config.width > 0 && config.height > 0 && (config.height % 2) == 0 &&
         config.width <= kMaxWidth && config.height <= kMaxHeight &&
         config.fpsNumerator > 0 && config.fpsDenominator > 0;
}

HRESULT SetVideoAttributes(IMFMediaType* type, const GUID& subtype,
                           const H264DecoderConfig& config) {
  if (type == nullptr) return E_POINTER;
  HRESULT hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_SUBTYPE, subtype);
  if (SUCCEEDED(hr)) hr = MFSetAttributeSize(type, MF_MT_FRAME_SIZE, config.width, config.height);
  if (SUCCEEDED(hr)) {
    hr = MFSetAttributeRatio(type, MF_MT_FRAME_RATE, config.fpsNumerator,
                             config.fpsDenominator);
  }
  if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  return hr;
}

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) return {};
  const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0, nullptr,
                                           nullptr);
  if (required <= 0) return {};
  std::string result(static_cast<std::size_t>(required), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), result.data(), required, nullptr,
                          nullptr) != required) {
    return {};
  }
  return result;
}

std::string FriendlyName(IMFActivate* activate) {
  if (activate == nullptr) return {};
  WCHAR* name = nullptr;
  UINT32 length = 0;
  const HRESULT hr = activate->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &length);
  std::wstring wide;
  if (SUCCEEDED(hr) && name != nullptr) wide.assign(name, length);
  if (name != nullptr) CoTaskMemFree(name);
  return WideToUtf8(wide);
}

std::string HresultText(const char* operation, HRESULT hr) {
  std::ostringstream output;
  output << operation << " hr=0x" << std::hex
         << static_cast<unsigned long>(hr);
  return output.str();
}

std::string GuidText(const GUID& value) {
  WCHAR buffer[64]{};
  const int length = StringFromGUID2(value, buffer, ARRAYSIZE(buffer));
  if (length <= 1) return "<invalid-guid>";
  return WideToUtf8(std::wstring(buffer, static_cast<std::size_t>(length - 1)));
}

std::string MediaTypeSummary(IMFMediaType* type) {
  if (type == nullptr) return "<null>";
  GUID subtype{};
  UINT32 width = 0;
  UINT32 height = 0;
  UINT32 fpsNumerator = 0;
  UINT32 fpsDenominator = 0;
  (void)type->GetGUID(MF_MT_SUBTYPE, &subtype);
  (void)MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height);
  (void)MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &fpsNumerator, &fpsDenominator);
  std::ostringstream result;
  result << "subtype=" << GuidText(subtype) << " size=" << width << "x" << height
         << " fps=" << fpsNumerator << "/" << fpsDenominator;
  return result.str();
}

std::string ByteHex(const std::uint8_t* bytes, std::size_t count) {
  if (bytes == nullptr || count == 0) return {};
  static constexpr char kHex[] = "0123456789abcdef";
  std::string result;
  result.reserve(count * 2);
  for (std::size_t index = 0; index < count; ++index) {
    result.push_back(kHex[(bytes[index] >> 4) & 0x0f]);
    result.push_back(kHex[bytes[index] & 0x0f]);
  }
  return result;
}

std::string H264InputSummary(const std::vector<std::uint8_t>& accessUnit) {
  std::ostringstream result;
  result << "bytes=" << accessUnit.size() << " prefix=";
  if (accessUnit.size() >= 4 && accessUnit[0] == 0 && accessUnit[1] == 0 &&
      accessUnit[2] == 0 && accessUnit[3] == 1) {
    result << "annexb4";
  } else if (accessUnit.size() >= 3 && accessUnit[0] == 0 && accessUnit[1] == 0 &&
             accessUnit[2] == 1) {
    result << "annexb3";
  } else {
    result << "other";
  }
  result << " first=" << ByteHex(accessUnit.data(), std::min<std::size_t>(16, accessUnit.size()));
  result << " nalTypes=";
  std::size_t offset = 0;
  unsigned int nalCount = 0;
  while (offset + 4 <= accessUnit.size() && nalCount < 12) {
    std::size_t prefix = 0;
    if (offset + 4 <= accessUnit.size() && accessUnit[offset] == 0 && accessUnit[offset + 1] == 0 &&
        accessUnit[offset + 2] == 0 && accessUnit[offset + 3] == 1) {
      prefix = 4;
    } else if (offset + 3 <= accessUnit.size() && accessUnit[offset] == 0 &&
               accessUnit[offset + 1] == 0 && accessUnit[offset + 2] == 1) {
      prefix = 3;
    } else {
      ++offset;
      continue;
    }
    const std::size_t payload = offset + prefix;
    if (payload >= accessUnit.size()) break;
    if (nalCount++ != 0) result << ",";
    result << static_cast<unsigned int>(accessUnit[payload] & 0x1f);
    offset = payload + 1;
  }
  if (nalCount == 0) result << "none";
  return result.str();
}

std::string StreamChangeDescription(IMFTransform* transform) {
  if (transform == nullptr) return "transform=<null>";
  std::ostringstream result;
  result << "available=";
  bool found = false;
  for (DWORD index = 0; index < 8; ++index) {
    ComPtr<IMFMediaType> type;
    const HRESULT hr = transform->GetOutputAvailableType(0, index, &type);
    if (hr == MF_E_NO_MORE_TYPES) break;
    if (FAILED(hr)) {
      result << "[index=" << index << " hr=0x" << std::hex
             << static_cast<unsigned long>(hr) << std::dec << "]";
      break;
    }
    if (found) result << ";";
    result << "[index=" << index << " " << MediaTypeSummary(type.Get()) << "]";
    found = true;
  }
  if (!found) result << "<none>";
  return result.str();
}

}  // namespace

struct MediaFoundationH264Decoder::Impl {
  ComPtr<IMFTransform> transform;
  ComPtr<IMFMediaType> outputType;
  MFT_OUTPUT_STREAM_INFO outputInfo{};
  bool mfStarted = false;
  bool comInitialized = false;
  std::int64_t lastInputTimestamp = 0;
  std::int64_t lastInputDuration = 0;
};

bool UpdateOutputMetadata(IMFMediaType* type, const MFT_OUTPUT_STREAM_INFO& streamInfo,
                          H264DecoderMetrics* metrics, std::string* error) {
  if (type == nullptr || metrics == nullptr || error == nullptr) return false;
  UINT32 codedWidth = 0;
  UINT32 codedHeight = 0;
  if (FAILED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &codedWidth, &codedHeight)) ||
      codedWidth == 0 || codedHeight == 0) {
    *error = "H264 decoder output type has no valid coded frame size";
    return false;
  }

  UINT32 displayWidth = codedWidth;
  UINT32 displayHeight = codedHeight;
  MFVideoArea displayAperture{};
  UINT32 apertureBytes = 0;
  const HRESULT apertureHr = type->GetBlob(
      MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&displayAperture),
      sizeof(displayAperture), &apertureBytes);
  if (SUCCEEDED(apertureHr) && apertureBytes == sizeof(displayAperture)) {
    if (displayAperture.OffsetX.value != 0 || displayAperture.OffsetX.fract != 0 ||
        displayAperture.OffsetY.value != 0 || displayAperture.OffsetY.fract != 0 ||
        displayAperture.Area.cx <= 0 || displayAperture.Area.cy <= 0) {
      *error = "H264 decoder output has an unsupported non-zero display aperture offset";
      return false;
    }
    displayWidth = static_cast<UINT32>(displayAperture.Area.cx);
    displayHeight = static_cast<UINT32>(displayAperture.Area.cy);
  }
  if (displayWidth > codedWidth || displayHeight > codedHeight || (displayHeight % 2) != 0 ||
      displayWidth == 0 || displayHeight == 0) {
    *error = "H264 decoder output display aperture exceeds coded frame";
    return false;
  }

  metrics->codedWidth = codedWidth;
  metrics->codedHeight = codedHeight;
  metrics->outputWidth = displayWidth;
  metrics->outputHeight = displayHeight;
  metrics->outputStreamFlags = streamInfo.dwFlags;
  metrics->outputBufferSize = streamInfo.cbSize;
  metrics->outputBufferAlignment = streamInfo.cbAlignment;
  UINT32 stride = 0;
  if (FAILED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) || stride == 0) {
    stride = codedWidth;
  }
  if (stride < displayWidth) {
    *error = "H264 decoder output stride is smaller than display width";
    return false;
  }
  metrics->outputStride = stride;
  return true;
}

MediaFoundationH264Decoder::MediaFoundationH264Decoder() = default;

MediaFoundationH264Decoder::~MediaFoundationH264Decoder() { Stop(); }

bool MediaFoundationH264Decoder::Fail(std::string message) {
  lastError_ = std::move(message);
  ++metrics_.decodeErrors;
  return false;
}

bool MediaFoundationH264Decoder::Start(const H264DecoderConfig& config) {
  Stop();
  metrics_ = {};
  lastError_.clear();
  if (!ValidConfig(config)) return Fail("invalid decoder dimensions");
  config_ = config;

  impl_ = std::make_unique<Impl>();
  const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  impl_->comInitialized = SUCCEEDED(comHr);
  if (FAILED(comHr) && comHr != RPC_E_CHANGED_MODE) {
    impl_.reset();
    return Fail("CoInitializeEx failed");
  }
  const HRESULT mfHr = MFStartup(MF_VERSION);
  if (FAILED(mfHr)) {
    Stop();
    return Fail("MFStartup failed");
  }
  impl_->mfStarted = true;

  const MFT_REGISTER_TYPE_INFO inputInfo{MFMediaType_Video, MFVideoFormat_H264};
  const MFT_REGISTER_TYPE_INFO outputInfo{MFMediaType_Video, MFVideoFormat_NV12};
  IMFActivate** activates = nullptr;
  UINT32 count = 0;

  auto tryCandidates = [&](DWORD flags, bool hardware) -> bool {
    IMFActivate** localActivates = nullptr;
    UINT32 localCount = 0;
    HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, flags, &inputInfo, &outputInfo,
                           &localActivates, &localCount);
    if (FAILED(hr)) return false;
    for (UINT32 index = 0; index < localCount; ++index) {
      ComPtr<IMFTransform> transform;
      hr = localActivates[index]->ActivateObject(IID_PPV_ARGS(&transform));
      if (FAILED(hr)) continue;

      ComPtr<IMFMediaType> inputType;
      hr = MFCreateMediaType(&inputType);
      if (SUCCEEDED(hr)) hr = SetVideoAttributes(inputType.Get(), MFVideoFormat_H264, config_);
      if (SUCCEEDED(hr)) hr = transform->SetInputType(0, inputType.Get(), 0);
      if (FAILED(hr)) continue;

      ComPtr<IMFMediaType> selectedOutput;
      for (DWORD typeIndex = 0; ; ++typeIndex) {
        ComPtr<IMFMediaType> candidate;
        const HRESULT typeHr = transform->GetOutputAvailableType(0, typeIndex, &candidate);
        if (FAILED(typeHr)) break;
        GUID subtype{};
        if (FAILED(candidate->GetGUID(MF_MT_SUBTYPE, &subtype)) ||
            subtype != MFVideoFormat_NV12) {
          continue;
        }
        if (FAILED(MFSetAttributeSize(candidate.Get(), MF_MT_FRAME_SIZE, config_.width,
                                      config_.height)) ||
            FAILED(MFSetAttributeRatio(candidate.Get(), MF_MT_FRAME_RATE,
                                        config_.fpsNumerator, config_.fpsDenominator)) ||
            FAILED(transform->SetOutputType(0, candidate.Get(), 0))) {
          continue;
        }
        selectedOutput = candidate;
        break;
      }
      if (!selectedOutput) {
        if (FAILED(MFCreateMediaType(&selectedOutput)) ||
            FAILED(SetVideoAttributes(selectedOutput.Get(), MFVideoFormat_NV12, config_)) ||
            FAILED(transform->SetOutputType(0, selectedOutput.Get(), 0))) {
          continue;
        }
      }

      MFT_OUTPUT_STREAM_INFO streamInfo{};
      hr = transform->GetOutputStreamInfo(0, &streamInfo);
      if (FAILED(hr)) continue;
      impl_->transform = transform;
      impl_->outputType = selectedOutput;
      impl_->outputInfo = streamInfo;
      metrics_.selectedTransform = FriendlyName(localActivates[index]);
      metrics_.hardware = hardware;
      if (!UpdateOutputMetadata(selectedOutput.Get(), streamInfo, &metrics_, &lastError_)) {
        continue;
      }
      (void)transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
      (void)transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
      CoTaskMemFree(localActivates);
      return true;
    }
    if (localActivates != nullptr) CoTaskMemFree(localActivates);
    return false;
  };

  const bool configured = config_.preferHardware && tryCandidates(kHardwareFlags, true);
  const bool configuredWithSoftware = configured || tryCandidates(kSoftwareFlags, false);
  if (!configuredWithSoftware) {
    Stop();
    return Fail("no H264 to NV12 Media Foundation decoder is available");
  }
  started_ = true;
  return true;
}

bool MediaFoundationH264Decoder::DrainOutput() {
  if (!started_ || !impl_ || !impl_->transform) return Fail("decoder is not started");

  for (int outputIndex = 0; outputIndex < 16; ++outputIndex) {
    MFT_OUTPUT_DATA_BUFFER output{};
    DWORD status = 0;
    ComPtr<IMFSample> suppliedSample;
    if ((impl_->outputInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) == 0) {
      DWORD bufferBytes = impl_->outputInfo.cbSize;
      if (bufferBytes == 0) {
        bufferBytes = metrics_.outputStride * metrics_.outputHeight * 3 / 2;
      }
      ComPtr<IMFMediaBuffer> outputBuffer;
      HRESULT hr = MFCreateMemoryBuffer(bufferBytes, &outputBuffer);
      if (SUCCEEDED(hr)) hr = MFCreateSample(&suppliedSample);
      if (SUCCEEDED(hr)) hr = suppliedSample->AddBuffer(outputBuffer.Get());
      output.pSample = suppliedSample.Get();
      if (FAILED(hr)) return Fail(HresultText("create H264 output sample", hr));
    }

    ++metrics_.processOutputCalls;
    HRESULT hr = impl_->transform->ProcessOutput(0, 1, &output, &status);
    metrics_.lastProcessOutputHr = static_cast<std::int32_t>(hr);
    metrics_.lastProcessOutputStatus = status;
    if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
      ++metrics_.processOutputNeedMoreInput;
      return true;
    }
    if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
      ++metrics_.processOutputStreamChanges;
      metrics_.lastStreamChangeDescription = StreamChangeDescription(impl_->transform.Get());
      ComPtr<IMFMediaType> changedOutput;
      for (DWORD typeIndex = 0; ; ++typeIndex) {
        ComPtr<IMFMediaType> candidate;
        const HRESULT typeHr =
            impl_->transform->GetOutputAvailableType(0, typeIndex, &candidate);
        if (typeHr == MF_E_NO_MORE_TYPES) break;
        if (FAILED(typeHr)) continue;
        GUID subtype{};
        if (FAILED(candidate->GetGUID(MF_MT_SUBTYPE, &subtype)) ||
            subtype != MFVideoFormat_NV12) {
          continue;
        }
        if (SUCCEEDED(impl_->transform->SetOutputType(0, candidate.Get(), 0))) {
          changedOutput = candidate;
          break;
        }
      }
      if (!changedOutput) {
        return Fail("H264 decoder output stream change has no usable NV12 type: " +
                    metrics_.lastStreamChangeDescription);
      }
      MFT_OUTPUT_STREAM_INFO changedInfo{};
      hr = impl_->transform->GetOutputStreamInfo(0, &changedInfo);
      if (FAILED(hr) ||
          !UpdateOutputMetadata(changedOutput.Get(), changedInfo, &metrics_, &lastError_)) {
        return Fail(HresultText("H264 decoder stream change output metadata", FAILED(hr) ? hr : E_FAIL));
      }
      impl_->outputType = changedOutput;
      impl_->outputInfo = changedInfo;
      lastError_.clear();
      continue;
    }
    if (FAILED(hr)) return Fail(HresultText("H264 decoder ProcessOutput", hr));
    if (output.pSample == nullptr) continue;

    ComPtr<IMFSample> producedSample;
    if (output.pSample != suppliedSample.Get()) producedSample.Attach(output.pSample);
    IMFMediaBuffer* sourceBuffer = nullptr;
    if (FAILED(output.pSample->ConvertToContiguousBuffer(&sourceBuffer)) ||
        sourceBuffer == nullptr) {
      if (producedSample) producedSample.Reset();
      return Fail(HresultText("H264 decoder output buffer unavailable", E_FAIL));
    }
    ComPtr<IMFMediaBuffer> contiguousBuffer;
    contiguousBuffer.Attach(sourceBuffer);
    BYTE* source = nullptr;
    DWORD maxLength = 0;
    DWORD length = 0;
    hr = contiguousBuffer->Lock(&source, &maxLength, &length);
    if (FAILED(hr)) return Fail(HresultText("H264 decoder output buffer lock", hr));
    const std::size_t codedBytes = static_cast<std::size_t>(metrics_.outputStride) *
                                   metrics_.codedHeight * 3 / 2;
    const std::size_t displayBytes = static_cast<std::size_t>(metrics_.outputStride) *
                                     metrics_.outputHeight * 3 / 2;
    if (metrics_.outputStride < metrics_.outputWidth || metrics_.outputHeight > metrics_.codedHeight ||
        codedBytes == 0 || displayBytes == 0 || length < codedBytes) {
      (void)contiguousBuffer->Unlock();
      return Fail("H264 decoder output is not a complete NV12 frame");
    }
    if (metrics_.outputFrames < 3 && source != nullptr) {
      const auto scan = [](const BYTE* data, std::size_t count,
                           std::uint32_t* minimum, std::uint32_t* maximum) {
        if (data == nullptr || count == 0 || minimum == nullptr || maximum == nullptr) return;
        BYTE minValue = 255;
        BYTE maxValue = 0;
        for (std::size_t index = 0; index < count; ++index) {
          minValue = (std::min)(minValue, data[index]);
          maxValue = (std::max)(maxValue, data[index]);
        }
        *minimum = minValue;
        *maximum = maxValue;
      };
      const std::size_t lumaScanBytes = (std::min<std::size_t>)(
          static_cast<std::size_t>(metrics_.outputStride) * metrics_.outputHeight, 64 * 1024);
      const std::size_t chromaOffset = static_cast<std::size_t>(metrics_.outputStride) *
                                       metrics_.codedHeight;
      const std::size_t chromaScanBytes =
          (chromaOffset < length)
              ? (std::min<std::size_t>)(static_cast<std::size_t>(metrics_.outputStride) *
                                            (metrics_.outputHeight / 2),
                                        64 * 1024)
              : 0;
      scan(source, lumaScanBytes, &metrics_.firstOutputLumaMin, &metrics_.firstOutputLumaMax);
      scan(chromaOffset < length ? source + chromaOffset : nullptr, chromaScanBytes,
           &metrics_.firstOutputChromaMin, &metrics_.firstOutputChromaMax);
      metrics_.firstOutputBufferLength = length;
      metrics_.firstOutputBufferMaxLength = maxLength;
      DWORD sampleFlags = 0;
      if (SUCCEEDED(output.pSample->GetSampleFlags(&sampleFlags))) {
        metrics_.firstOutputSampleFlags = sampleFlags;
      }
      LONGLONG sampleTime = 0;
      if (SUCCEEDED(output.pSample->GetSampleTime(&sampleTime))) {
        metrics_.firstOutputSampleTime100ns = sampleTime;
      }
    }
    Nv12Frame frame;
    frame.width = metrics_.outputWidth;
    frame.height = metrics_.outputHeight;
    frame.stride = metrics_.outputStride;
    frame.timestamp100ns = impl_->lastInputTimestamp;
    LONGLONG sampleTime = 0;
    if (SUCCEEDED(output.pSample->GetSampleTime(&sampleTime))) frame.timestamp100ns = sampleTime;
    frame.bytes.resize(displayBytes);
    const std::size_t lumaBytes = static_cast<std::size_t>(metrics_.outputStride) * metrics_.outputHeight;
    for (std::uint32_t row = 0; row < metrics_.outputHeight; ++row) {
      std::memcpy(frame.bytes.data() + static_cast<std::size_t>(row) * metrics_.outputStride,
                  source + static_cast<std::size_t>(row) * metrics_.outputStride,
                  metrics_.outputStride);
    }
    const BYTE* sourceChroma = source + static_cast<std::size_t>(metrics_.outputStride) *
                               metrics_.codedHeight;
    BYTE* destinationChroma = reinterpret_cast<BYTE*>(frame.bytes.data() + lumaBytes);
    for (std::uint32_t row = 0; row < metrics_.outputHeight / 2; ++row) {
      std::memcpy(destinationChroma + static_cast<std::size_t>(row) * metrics_.outputStride,
                  sourceChroma + static_cast<std::size_t>(row) * metrics_.outputStride,
                  metrics_.outputStride);
    }
    (void)contiguousBuffer->Unlock();
    ++metrics_.outputFrames;
    if (frameHandler_) frameHandler_(std::move(frame));
  }
  return true;
}

bool MediaFoundationH264Decoder::SubmitAccessUnit(
    const std::vector<std::uint8_t>& accessUnit, std::int64_t timestamp100ns,
    std::int64_t duration100ns) {
  if (!started_ || !impl_ || !impl_->transform) return Fail("decoder is not started");
  if (accessUnit.empty() || accessUnit.size() > std::numeric_limits<DWORD>::max()) {
    return Fail("empty or oversized H264 access unit");
  }
  if (metrics_.inputAccessUnits == 0) metrics_.firstInputSummary = H264InputSummary(accessUnit);

  ComPtr<IMFMediaBuffer> buffer;
  HRESULT hr = MFCreateMemoryBuffer(static_cast<DWORD>(accessUnit.size()), &buffer);
  BYTE* destination = nullptr;
  DWORD maxLength = 0;
  DWORD currentLength = 0;
  if (SUCCEEDED(hr)) hr = buffer->Lock(&destination, &maxLength, &currentLength);
  if (SUCCEEDED(hr)) {
    std::memcpy(destination, accessUnit.data(), accessUnit.size());
    hr = buffer->Unlock();
  }
  if (SUCCEEDED(hr)) hr = buffer->SetCurrentLength(static_cast<DWORD>(accessUnit.size()));
  ComPtr<IMFSample> sample;
  if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
  if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.Get());
  if (SUCCEEDED(hr)) hr = sample->SetSampleTime(timestamp100ns);
  if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(duration100ns);
  if (FAILED(hr)) return Fail(HresultText("create H264 input sample", hr));

  hr = impl_->transform->ProcessInput(0, sample.Get(), 0);
  if (hr == MF_E_NOTACCEPTING) {
    // Some Windows H.264 MFTs apply back-pressure until pending output is
    // drained. Treat this as a recoverable queue condition, not a fatal
    // decode error, and retry the same input after draining output.
    if (!DrainOutput()) return false;
    hr = impl_->transform->ProcessInput(0, sample.Get(), 0);
  }
  if (FAILED(hr)) return Fail(HresultText("H264 decoder ProcessInput", hr));
  ++metrics_.inputAccessUnits;
  impl_->lastInputTimestamp = timestamp100ns;
  impl_->lastInputDuration = duration100ns;
  if (!DrainOutput()) return false;
  lastError_.clear();
  return true;
}

void MediaFoundationH264Decoder::Stop() {
  if (impl_ && impl_->transform) {
    (void)impl_->transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
    (void)impl_->transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
    // Release Media Foundation COM objects while both COM and MF are still
    // active. Resetting the PImpl after CoUninitialize would call COM
    // destructors from an invalid apartment and can crash the process.
    impl_->transform.Reset();
    impl_->outputType.Reset();
  }
  started_ = false;
  if (impl_ && impl_->mfStarted) {
    MFShutdown();
  }
  if (impl_ && impl_->comInitialized) {
    CoUninitialize();
  }
  impl_.reset();
}

}  // namespace cambridge::native
