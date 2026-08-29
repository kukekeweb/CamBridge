#pragma once

#include <Windows.h>

namespace cambridge::native {

// Control-path diagnostics only. This must never be called for every video frame.
void LogControlEvent(const wchar_t* component, const wchar_t* eventName, HRESULT hr);
void LogQueryInterface(const wchar_t* component, REFIID requestedIid, HRESULT hr);

}  // namespace cambridge::native
