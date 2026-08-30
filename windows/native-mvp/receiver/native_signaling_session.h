#pragma once

#include "libdatachannel_receiver.h"
#include "signaling_protocol.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cambridge::native::receiver {

struct NativeSignalingSessionConfig {
  std::string sessionId;
  std::string bindAddress;
  unsigned int h264BitrateKbps = 5000;
};

class NativeSignalingSession {
 public:
  using SendHandler = std::function<void(const std::string& message)>;
  using AccessUnitHandler = LibDataChannelReceiver::AccessUnitHandler;

  explicit NativeSignalingSession(NativeSignalingSessionConfig config);
  ~NativeSignalingSession();

  NativeSignalingSession(const NativeSignalingSession&) = delete;
  NativeSignalingSession& operator=(const NativeSignalingSession&) = delete;

  bool Start();
  bool Restart(std::string sessionId);
  bool OnSocketOpen();
  bool OnSocketMessage(const std::string& message);
  void Close();

  ReceiverState state() const;
  const std::string& lastError() const { return lastError_; }
  void SetSendHandler(SendHandler handler) { sendHandler_ = std::move(handler); }
  void SetAccessUnitHandler(AccessUnitHandler handler) {
    accessUnitHandler_ = std::move(handler);
    if (receiver_) receiver_->SetAccessUnitHandler(accessUnitHandler_);
  }
  LibDataChannelReceiverMetrics metrics() const;

 private:
  bool Fail(std::string message);
  bool Send(std::string message);
  void CreateReceiver();

  NativeSignalingSessionConfig config_;
  std::unique_ptr<LibDataChannelReceiver> receiver_;
  std::vector<SignalingMessage> pendingIce_;
  SendHandler sendHandler_;
  AccessUnitHandler accessUnitHandler_;
  std::string lastError_;
  bool automaticSession_ = false;
  bool started_ = false;
  bool socketOpen_ = false;
};

}  // namespace cambridge::native::receiver
