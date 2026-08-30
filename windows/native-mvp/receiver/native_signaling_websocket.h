#pragma once

#include "native_signaling_session.h"

#include <memory>
#include <string>

namespace cambridge::native::receiver {

struct NativeSignalingWebSocketConfig {
  std::string url;
  std::string caCertificatePemFile;
  bool allowInsecureTlsForLocalProbe = false;
  NativeSignalingSessionConfig session;
};

class NativeSignalingWebSocket {
 public:
  explicit NativeSignalingWebSocket(NativeSignalingWebSocketConfig config);
  ~NativeSignalingWebSocket();

  NativeSignalingWebSocket(const NativeSignalingWebSocket&) = delete;
  NativeSignalingWebSocket& operator=(const NativeSignalingWebSocket&) = delete;

  bool Start();
  void Close();
  const std::string& lastError() const { return lastError_; }
  ReceiverState state() const { return session_->state(); }

 private:
  struct Impl;

  NativeSignalingWebSocketConfig config_;
  std::unique_ptr<Impl> impl_;
  std::unique_ptr<NativeSignalingSession> session_;
  std::string lastError_;
};

}  // namespace cambridge::native::receiver
