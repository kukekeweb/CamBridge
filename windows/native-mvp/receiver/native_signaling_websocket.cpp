#include "native_signaling_websocket.h"

#include <rtc/rtc.hpp>

#include <utility>

namespace cambridge::native::receiver {

struct NativeSignalingWebSocket::Impl {
  std::shared_ptr<rtc::WebSocket> webSocket;
};

NativeSignalingWebSocket::NativeSignalingWebSocket(NativeSignalingWebSocketConfig config)
    : config_(std::move(config)),
      session_(std::make_unique<NativeSignalingSession>(config_.session)) {}

NativeSignalingWebSocket::~NativeSignalingWebSocket() { Close(); }

bool NativeSignalingWebSocket::Start() {
  if (config_.url.rfind("wss://", 0) != 0) {
    lastError_ = "signaling URL must use wss://";
    return false;
  }
  if (config_.caCertificatePemFile.empty() && !config_.allowInsecureTlsForLocalProbe) {
    lastError_ = "a local CA certificate is required for WSS";
    return false;
  }
  if (!session_->Start()) {
    lastError_ = session_->lastError();
    return false;
  }

  try {
    rtc::WebSocketConfiguration webSocketConfig;
    webSocketConfig.maxMessageSize = 512 * 1024;
    webSocketConfig.disableTlsVerification = config_.allowInsecureTlsForLocalProbe;
    if (!config_.caCertificatePemFile.empty()) {
      webSocketConfig.caCertificatePemFile = config_.caCertificatePemFile;
    }

    impl_ = std::make_unique<Impl>();
    impl_->webSocket = std::make_shared<rtc::WebSocket>(webSocketConfig);
    session_->SetSendHandler([this](const std::string& message) {
      if (!impl_ || !impl_->webSocket || !impl_->webSocket->send(message)) {
        lastError_ = "signaling message send failed";
      }
    });
    impl_->webSocket->onOpen([this] {
      if (!session_->OnSocketOpen()) {
        lastError_ = session_->lastError();
        if (impl_ && impl_->webSocket) impl_->webSocket->close();
      }
    });
    impl_->webSocket->onMessage(
        [](rtc::binary) {},
        [this](std::string message) {
          if (!session_->OnSocketMessage(message)) {
            lastError_ = session_->lastError();
            if (impl_ && impl_->webSocket) impl_->webSocket->close();
          }
        });
    impl_->webSocket->onError([this](std::string error) { lastError_ = std::move(error); });
    impl_->webSocket->onClosed([this] { session_->Close(); });
    impl_->webSocket->open(config_.url);
  } catch (const std::exception& error) {
    impl_.reset();
    session_->Close();
    lastError_ = error.what();
    return false;
  }

  lastError_.clear();
  return true;
}

void NativeSignalingWebSocket::Close() {
  if (impl_ && impl_->webSocket) {
    impl_->webSocket->resetCallbacks();
    impl_->webSocket->close();
  }
  impl_.reset();
  if (session_) session_->Close();
}

}  // namespace cambridge::native::receiver
