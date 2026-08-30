#include "native_signaling_websocket.h"
#include "receiver_media_pipeline.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic_bool g_stop = false;

BOOL WINAPI ConsoleControlHandler(DWORD controlType) {
  switch (controlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      g_stop.store(true, std::memory_order_relaxed);
      return TRUE;
    default:
      return FALSE;
  }
}

struct Options {
  std::string signalingUrl;
  std::string caCertificate;
  std::string sessionId;
  std::string bindAddress;
  unsigned int bitrateKbps = 5000;
  unsigned int durationMs = 0;
  bool allowInsecureTls = false;
  bool publishFrames = true;
  bool help = false;
};

void PrintUsage() {
  std::cout
      << "CamBridge native WebRTC receiver probe\n"
      << "Usage: cambridge_native_receiver --url <wss-url> --session-id <id> "
         "[--ca <root-ca.pem>] [options]\n"
      << "  --url <wss-url>          WSS signaling URL\n"
      << "  --session-id <id>        ID shown by the Safari Web Client\n"
      << "  --ca <path>              CamBridge Local CA PEM certificate\n"
      << "  --bind-address <IPv4>    Local ICE bind address\n"
      << "  --bitrate-kbps <n>       H.264 receive description bitrate\n"
      << "  --duration-ms <n>        Stop after n milliseconds (0 = Ctrl+C)\n"
      << "  --no-publish             Receive access units without decode/IPC output\n"
      << "  --allow-insecure-tls     Local probe only; do not use for normal runs\n"
      << "  --help                   Show this help\n";
}

bool NextValue(int* index, int argc, wchar_t** argv, std::string* value) {
  if (index == nullptr || value == nullptr || *index + 1 >= argc) return false;
  const std::wstring wide = argv[++(*index)];
  if (wide.empty()) return false;
  const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                           wide.data(), static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
  if (required <= 0) return false;
  value->assign(static_cast<std::size_t>(required), '\0');
  const int written = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()),
      value->data(), required, nullptr, nullptr);
  if (written != required) {
    value->clear();
    return false;
  }
  return true;
}

bool ParseUnsigned(const std::string& value, unsigned int* result) {
  if (result == nullptr || value.empty()) return false;
  try {
    const unsigned long parsed = std::stoul(value);
    if (parsed > 0xFFFFFFFFUL) return false;
    *result = static_cast<unsigned int>(parsed);
    return true;
  } catch (...) {
    return false;
  }
}

bool ParseOptions(int argc, wchar_t** argv, Options* options) {
  if (options == nullptr) return false;
  for (int index = 1; index < argc; ++index) {
    const std::wstring argument = argv[index];
    std::string value;
    if (argument == L"--help") {
      options->help = true;
    } else if (argument == L"--url") {
      if (!NextValue(&index, argc, argv, &options->signalingUrl)) return false;
    } else if (argument == L"--ca") {
      if (!NextValue(&index, argc, argv, &options->caCertificate)) return false;
    } else if (argument == L"--session-id") {
      if (!NextValue(&index, argc, argv, &options->sessionId)) return false;
    } else if (argument == L"--bind-address") {
      if (!NextValue(&index, argc, argv, &options->bindAddress)) return false;
    } else if (argument == L"--bitrate-kbps") {
      if (!NextValue(&index, argc, argv, &value) ||
          !ParseUnsigned(value, &options->bitrateKbps)) return false;
    } else if (argument == L"--duration-ms") {
      if (!NextValue(&index, argc, argv, &value) ||
          !ParseUnsigned(value, &options->durationMs)) return false;
    } else if (argument == L"--allow-insecure-tls") {
      options->allowInsecureTls = true;
    } else if (argument == L"--no-publish") {
      options->publishFrames = false;
    } else {
      return false;
    }
  }
  return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  Options options;
  if (!ParseOptions(argc, argv, &options) || options.help) {
    PrintUsage();
    return options.help ? 0 : 2;
  }
  if (options.signalingUrl.empty() || options.sessionId.empty()) {
    std::cerr << "--url and --session-id are required\n";
    PrintUsage();
    return 2;
  }
  if (options.caCertificate.empty() && !options.allowInsecureTls) {
    std::cerr << "--ca is required unless --allow-insecure-tls is explicitly set\n";
    return 2;
  }

  SetConsoleCtrlHandler(ConsoleControlHandler, TRUE);
  cambridge::native::receiver::NativeSignalingWebSocket receiver({
      options.signalingUrl,
      options.caCertificate,
      options.allowInsecureTls,
      {options.sessionId, options.bindAddress, options.bitrateKbps},
  });
  std::unique_ptr<cambridge::native::ReceiverMediaPipeline> mediaPipeline;
  if (options.publishFrames) {
    mediaPipeline = std::make_unique<cambridge::native::ReceiverMediaPipeline>();
    if (!mediaPipeline->Start()) {
      std::cerr << "Receiver media pipeline start failed: "
                << mediaPipeline->lastError() << "\n";
      return 1;
    }
    receiver.SetAccessUnitHandler(
        [&mediaPipeline](std::vector<std::uint8_t> accessUnit,
                         std::uint32_t timestamp) {
          if (!mediaPipeline->SubmitAccessUnit(accessUnit, timestamp)) {
            std::cerr << "Receiver media pipeline access unit failed: "
                      << mediaPipeline->lastError() << "\n";
          }
        });
  } else {
    receiver.SetAccessUnitHandler([](std::vector<std::uint8_t> accessUnit,
                                     std::uint32_t timestamp) {
      static std::atomic<std::uint64_t> count = 0;
      const auto sequence = count.fetch_add(1, std::memory_order_relaxed) + 1;
      if (sequence <= 3 || sequence % 60 == 0) {
        std::cout << "AccessUnit: sequence=" << sequence << " bytes=" << accessUnit.size()
                  << " rtpTimestamp=" << timestamp << "\n";
      }
    });
  }

  std::cout << "CamBridge native receiver probe\n"
            << "Signaling: " << options.signalingUrl << "\n"
            << "Session ID: " << options.sessionId << "\n"
            << "ICE: LAN host candidates only; external STUN/TURN disabled\n"
            << "Output: " << (options.publishFrames ? "H264 -> NV12 -> shared IPC" :
                              "access-unit diagnostics only") << "\n";
  if (!receiver.Start()) {
    std::cerr << "Native receiver start failed: " << receiver.lastError() << "\n";
    return 1;
  }
  std::cout << "Receiver: started; waiting for Safari Offer\n";

  const auto started = std::chrono::steady_clock::now();
  auto lastState = receiver.state();
  auto lastReport = started;
  for (;;) {
    if (g_stop.load(std::memory_order_relaxed)) break;
    const auto now = std::chrono::steady_clock::now();
    if (options.durationMs != 0 &&
        now - started >= std::chrono::milliseconds(options.durationMs)) {
      break;
    }
    const auto state = receiver.state();
    if (state != lastState) {
      std::cout << "State: "
                << cambridge::native::receiver::ReceiverStateName(state) << "\n";
      lastState = state;
    }
    if (now - lastReport >= std::chrono::seconds(1)) {
      const auto metrics = receiver.metrics();
      std::cout << "Metrics: accessUnits=" << metrics.accessUnits
                << " lastRtpTimestamp=" << metrics.lastTimestamp
                << " peerState="
                << cambridge::native::receiver::ReceiverPeerStateName(metrics.peerState)
                << " iceState="
                << cambridge::native::receiver::ReceiverIceStateName(metrics.iceState)
                << " peerStateChanges=" << metrics.peerStateChanges
                << " iceStateChanges=" << metrics.iceStateChanges
                << " remoteOfferH264=" << (metrics.remoteOfferHasH264 ? "yes" : "no")
                << " localAnswerH264=" << (metrics.localAnswerHasH264 ? "yes" : "no")
                << " accessUnitBytes=" << metrics.accessUnitBytes
                << " bytesReceived=" << metrics.bytesReceived
                << " rttMs=" << metrics.rttMilliseconds
                << " selectedLocalCandidate=" << metrics.selectedLocalCandidate
                << " selectedRemoteCandidate=" << metrics.selectedRemoteCandidate
                << "\n";
      if (mediaPipeline) {
        const auto pipelineMetrics = mediaPipeline->metrics();
        std::cout << "Pipeline: input=" << pipelineMetrics.inputAccessUnits
                  << " decoded=" << pipelineMetrics.decodedFrames
                  << " published=" << pipelineMetrics.publishedFrames
                  << " decodeErrors=" << pipelineMetrics.decoder.decodeErrors
                  << " publishErrors=" << pipelineMetrics.publishErrors << "\n";
      }
      lastReport = now;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  const auto metrics = receiver.metrics();
  std::cout << "Receiver: stopping\n"
            << "Final metrics: accessUnits=" << metrics.accessUnits
            << " lastRtpTimestamp=" << metrics.lastTimestamp
            << " peerState="
            << cambridge::native::receiver::ReceiverPeerStateName(metrics.peerState)
            << " iceState="
            << cambridge::native::receiver::ReceiverIceStateName(metrics.iceState)
            << " peerStateChanges=" << metrics.peerStateChanges
            << " iceStateChanges=" << metrics.iceStateChanges
            << " remoteOfferH264=" << (metrics.remoteOfferHasH264 ? "yes" : "no")
            << " localAnswerH264=" << (metrics.localAnswerHasH264 ? "yes" : "no")
            << " accessUnitBytes=" << metrics.accessUnitBytes
            << " bytesReceived=" << metrics.bytesReceived
            << " rttMs=" << metrics.rttMilliseconds
            << " selectedLocalCandidate=" << metrics.selectedLocalCandidate
            << " selectedRemoteCandidate=" << metrics.selectedRemoteCandidate
            << "\n";
  receiver.Close();
  if (mediaPipeline) {
    const auto pipelineMetrics = mediaPipeline->metrics();
    std::cout << "Final pipeline metrics: input=" << pipelineMetrics.inputAccessUnits
              << " decoded=" << pipelineMetrics.decodedFrames
              << " published=" << pipelineMetrics.publishedFrames
              << " decodeErrors=" << pipelineMetrics.decoder.decodeErrors
              << " publishErrors=" << pipelineMetrics.publishErrors << "\n";
    mediaPipeline->Stop();
  }
  SetConsoleCtrlHandler(ConsoleControlHandler, FALSE);
  return 0;
}
