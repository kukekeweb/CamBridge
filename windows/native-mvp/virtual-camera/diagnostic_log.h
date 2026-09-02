#pragma once

#include <Windows.h>

#include <cstdint>

namespace cambridge::native {

// Control-path diagnostics only. This must never be called for every video frame.
const wchar_t* MediaEventTypeName(DWORD eventType);
void LogControlEvent(const wchar_t* component, const wchar_t* eventName, HRESULT hr);
void LogQueryInterface(const wchar_t* component, REFIID requestedIid, HRESULT hr);
void LogMediaEvent(const wchar_t* component, const wchar_t* operation,
                   DWORD eventType, HRESULT callHr, HRESULT status,
                   REFGUID extendedType, bool associatedObject,
                   const void* associatedPointer, DWORD streamId,
                   std::uint64_t sequence, HRESULT valueHr);
void LogDescriptorEvent(const wchar_t* component, const wchar_t* eventName, HRESULT hr,
                        DWORD descriptorCount, DWORD streamId, bool selected,
                        REFGUID majorType, REFGUID subtype,
                        std::uint32_t width, std::uint32_t height,
                        std::uint32_t fps, std::uint32_t denominator);
void LogIpcStatus(const wchar_t* component, const wchar_t* eventName, HRESULT hr,
                  bool mappingOpen, DWORD openError, LONG producerState,
                  std::uint64_t publishedSequence, std::uint64_t lastReadSequence);
void LogSampleEvent(const wchar_t* component, const wchar_t* eventName, HRESULT hr,
                    std::uint64_t sampleIndex, std::uint64_t sequence,
                    LONGLONG timestamp100ns, DWORD bufferBytes);
void LogFormatEvent(const wchar_t* component, const wchar_t* eventName, HRESULT hr,
                    std::uint32_t width, std::uint32_t height,
                    std::uint32_t fps, std::uint32_t denominator);
void LogAllocatorEvent(const wchar_t* component, const wchar_t* eventName, HRESULT hr,
                       const wchar_t* allocatorSource, const void* stream,
                       const void* allocator, const void* mediaType, REFGUID subtype,
                       std::uint32_t width, std::uint32_t height,
                       std::uint32_t fps, std::uint32_t denominator);
void LogStreamSummary(const wchar_t* component, const wchar_t* eventName, HRESULT hr,
                      std::uint64_t requestSamples, std::uint64_t samplesProduced,
                      std::uint64_t samplesDelivered, std::uint64_t lastSequence);
void LogRequestSampleSummary(const wchar_t* component, const wchar_t* eventName,
                             HRESULT hr, std::uint64_t requestSamples,
                             std::uint64_t requestSuccesses,
                             std::uint64_t requestFailures,
                             std::uint64_t samplesProduced,
                             std::uint64_t samplesDelivered,
                             std::uint64_t firstRequestUtc100ns,
                             std::uint64_t lastSequence);
void LogPacingSummary(const wchar_t* component, const wchar_t* eventName, HRESULT hr,
                      std::uint64_t windowElapsed100ns,
                      std::uint64_t requestSamples,
                      std::uint64_t allocateSampleCalls,
                      std::uint64_t samplesCreated,
                      std::uint64_t mediaSampleQueued,
                      std::uint64_t mediaSampleEndGetEvent,
                      std::uint64_t beginGetEvent,
                      std::uint64_t ipcReadAttempts,
                      std::uint64_t ipcNewFrames,
                      std::uint64_t uniqueIpcSequences,
                      std::uint64_t duplicateIpcSequenceSamples,
                      std::uint64_t latestIpcSequence,
                      std::uint64_t pendingMediaSamples,
                      std::uint64_t totalRequests,
                      std::uint64_t totalSamplesCreated,
                      std::uint64_t totalSamplesDelivered);
void LogPacingSample(const wchar_t* component, const wchar_t* eventName, HRESULT hr,
                     const void* sample,
                     std::uint64_t sampleIndex,
                     std::uint64_t ipcSequence,
                     LONGLONG sampleTime100ns,
                     LONGLONG previousSampleTime100ns,
                     LONGLONG sampleDelta100ns,
                     LONGLONG sampleDuration100ns,
                     std::uint64_t wallClockElapsed100ns,
                     LONGLONG sampleRelative100ns,
                     LONGLONG sampleMinusWallClock100ns,
                     std::uint32_t negotiatedWidth,
                     std::uint32_t negotiatedHeight,
                     std::uint32_t allocatedBufferCapacity,
                     std::uint32_t currentLength,
                     std::uint32_t ipcWidth,
                     std::uint32_t ipcHeight,
                     std::uint32_t ipcStride,
                     std::uint32_t ipcPayloadBytes,
                     std::uint32_t copiedBytes);

}  // namespace cambridge::native
