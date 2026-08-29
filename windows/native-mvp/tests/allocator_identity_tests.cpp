#include <Windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <wrl/client.h>

#include <iostream>

using Microsoft::WRL::ComPtr;

namespace {

HRESULT MakeTestType(IMFMediaType** result) {
  if (result == nullptr) return E_POINTER;
  *result = nullptr;
  ComPtr<IMFMediaType> type;
  HRESULT hr = MFCreateMediaType(&type);
  if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
  if (SUCCEEDED(hr)) hr = MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, 1920, 1080);
  if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, 60, 1);
  if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
  return SUCCEEDED(hr) ? type.CopyTo(result) : hr;
}

bool Check(const wchar_t* label, HRESULT actual, HRESULT expected) {
  if (actual == expected) return true;
  std::wcerr << label << L": expected 0x" << std::hex
             << static_cast<unsigned long>(expected) << L", got 0x"
             << static_cast<unsigned long>(actual) << std::dec << L"\n";
  return false;
}

}  // namespace

int wmain() {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool comInitialized = SUCCEEDED(hr);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return 1;

  bool passed = true;
  hr = MFStartup(MF_VERSION);
  passed = Check(L"MFStartup", hr, S_OK);
  if (SUCCEEDED(hr)) {
    ComPtr<IMFMediaType> mediaType;
    passed = MakeTestType(&mediaType) == S_OK && passed;

    ComPtr<IMFVideoSampleAllocator> allocatorA;
    hr = MFCreateVideoSampleAllocatorEx(IID_PPV_ARGS(&allocatorA));
    passed = Check(L"allocator A creation", hr, S_OK) && passed;
    if (SUCCEEDED(hr)) {
      hr = allocatorA->InitializeSampleAllocator(10, mediaType.Get());
      passed = Check(L"allocator A initialization", hr, S_OK) && passed;
      ComPtr<IMFSample> sampleA;
      hr = allocatorA->AllocateSample(&sampleA);
      passed = Check(L"allocator A AllocateSample", hr, S_OK) && passed;

      ComPtr<IMFVideoSampleAllocator> allocatorB;
      hr = MFCreateVideoSampleAllocatorEx(IID_PPV_ARGS(&allocatorB));
      passed = Check(L"allocator B creation", hr, S_OK) && passed;
      if (SUCCEEDED(hr)) {
        // Model a stream whose initialized allocator A is replaced by B before its
        // first request. B is intentionally not initialized.
        ComPtr<IMFVideoSampleAllocator> activeAllocator = allocatorB;
        ComPtr<IMFSample> sampleB;
        hr = activeAllocator->AllocateSample(&sampleB);
        passed = Check(L"uninitialized replacement allocator B AllocateSample",
                       hr, MF_E_NOT_INITIALIZED) && passed;
      }
    }
    MFShutdown();
  }
  if (comInitialized) CoUninitialize();
  if (!passed) return 1;
  std::wcout << L"Allocator identity tests: PASS\n";
  return 0;
}
