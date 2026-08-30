#include <mfidl.h>

#include "diagnostic_log.h"

#include <iostream>

namespace {

bool Check(DWORD eventType, const wchar_t* expected) {
  const wchar_t* actual = cambridge::native::MediaEventTypeName(eventType);
  if (actual == nullptr || std::wstring(actual) != expected) {
    std::wcerr << L"event type " << eventType << L": expected " << expected
               << L", got " << (actual == nullptr ? L"<null>" : actual) << L"\n";
    return false;
  }
  return true;
}

}  // namespace

int wmain() {
  bool passed = true;
  passed = Check(MENewStream, L"MENewStream") && passed;
  passed = Check(MEUpdatedStream, L"MEUpdatedStream") && passed;
  passed = Check(MESourceStarted, L"MESourceStarted") && passed;
  passed = Check(MEStreamStarted, L"MEStreamStarted") && passed;
  passed = Check(MEMediaSample, L"MEMediaSample") && passed;
  passed = Check(MEError, L"MEError") && passed;
  if (passed) std::wcout << L"Diagnostic event trace tests: PASS\n";
  return passed ? 0 : 1;
}
