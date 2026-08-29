#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <iostream>
#include <string>

using Microsoft::WRL::ComPtr;

int wmain() {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return 1;
  hr = MFStartup(MF_VERSION);
  if (FAILED(hr)) return 1;

  ComPtr<IMFAttributes> attributes;
  hr = MFCreateAttributes(&attributes, 2);
  if (SUCCEEDED(hr)) hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  IMFActivate** devices = nullptr;
  UINT32 count = 0;
  if (SUCCEEDED(hr)) hr = MFEnumDeviceSources(attributes.Get(), &devices, &count);
  std::wcout << L"Video input count: " << count << L"\n";
  bool cambridgeFound = false;
  int maximumSamplesReceived = 0;
  for (UINT32 i = 0; i < count; ++i) {
    WCHAR* name = nullptr;
    WCHAR* link = nullptr;
    UINT32 nameLength = 0;
    UINT32 linkLength = 0;
    devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &nameLength);
    devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                                    &link, &linkLength);
    std::wcout << L"[" << i << L"] " << (name ? name : L"<no name>") << L"\n"
               << L"    identity: " << (link ? link : L"<no symbolic link>") << L"\n";
    if (name && wcsstr(name, L"CamBridge") != nullptr) {
      cambridgeFound = true;
      ComPtr<IMFMediaSource> source;
      HRESULT openHr = devices[i]->ActivateObject(IID_PPV_ARGS(&source));
      std::wcout << L"    ActivateObject: 0x" << std::hex << static_cast<unsigned long>(openHr)
                 << std::dec << L"\n";
      if (SUCCEEDED(openHr)) {
        ComPtr<IMFAttributes> readerAttributes;
        MFCreateAttributes(&readerAttributes, 2);
        readerAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, FALSE);
        ComPtr<IMFSourceReader> reader;
        openHr = MFCreateSourceReaderFromMediaSource(source.Get(), readerAttributes.Get(), &reader);
        std::wcout << L"    SourceReader: 0x" << std::hex << static_cast<unsigned long>(openHr)
                   << std::dec << L"\n";
        if (SUCCEEDED(openHr)) {
          DWORD stream = 0, flags = 0, actualStream = 0;
          LONGLONG timestamp = 0;
          ComPtr<IMFSample> sample;
          int frames = 0;
          for (int attempt = 0; attempt < 180 && frames < 120; ++attempt) {
            openHr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &actualStream,
                                        &flags, &timestamp, &sample);
            if (FAILED(openHr)) break;
            if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) break;
            if (sample) ++frames;
          }
          std::wcout << L"    Samples received: " << frames << L"\n";
          if (frames > maximumSamplesReceived) maximumSamplesReceived = frames;
        }
        source->Shutdown();
      }
    }
    if (name) CoTaskMemFree(name);
    if (link) CoTaskMemFree(link);
    devices[i]->Release();
  }
  if (devices) CoTaskMemFree(devices);
  std::wcout << L"CamBridge camera found: " << (cambridgeFound ? L"YES" : L"NO") << L"\n";
  std::wcout << L"Synthetic/sample probe: " << maximumSamplesReceived << L" samples\n";
  MFShutdown();
  if (hr != RPC_E_CHANGED_MODE) CoUninitialize();
  return cambridgeFound && maximumSamplesReceived >= 120 ? 0 : 1;
}
