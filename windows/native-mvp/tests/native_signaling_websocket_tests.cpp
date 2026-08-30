#include "native_signaling_websocket.h"

#include <cassert>
#include <iostream>

using cambridge::native::receiver::NativeSignalingWebSocket;
using cambridge::native::receiver::NativeSignalingWebSocketConfig;
using cambridge::native::receiver::ReceiverState;

namespace {
void TestTlsSafetyGuards() {
  NativeSignalingWebSocket receiver({"https://192.168.11.2:8443/signaling", "", false,
                                     {"s1", "192.168.11.2", 5000}});
  assert(!receiver.Start());
  assert(receiver.lastError() == "signaling URL must use wss://");
  assert(receiver.state() == ReceiverState::Idle);

  NativeSignalingWebSocket missingCa({"wss://192.168.11.2:8443/signaling", "", false,
                                      {"s2", "192.168.11.2", 5000}});
  assert(!missingCa.Start());
  assert(missingCa.lastError() == "a local CA certificate is required for WSS");
  assert(missingCa.state() == ReceiverState::Idle);
}
}  // namespace

int main() {
  TestTlsSafetyGuards();
  std::cout << "CamBridge native signaling websocket tests passed\n";
  return 0;
}
