#include <rtc/rtc.hpp>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
using Byte = std::byte;

std::vector<Byte> RtpPacket(std::uint16_t sequence,
                            std::uint32_t timestamp,
                            bool marker,
                            std::initializer_list<std::uint8_t> payload) {
  std::vector<Byte> packet(12 + payload.size());
  auto put16 = [&](std::size_t offset, std::uint16_t value) {
    packet[offset] = static_cast<Byte>(value >> 8);
    packet[offset + 1] = static_cast<Byte>(value & 0xff);
  };
  auto put32 = [&](std::size_t offset, std::uint32_t value) {
    packet[offset] = static_cast<Byte>(value >> 24);
    packet[offset + 1] = static_cast<Byte>((value >> 16) & 0xff);
    packet[offset + 2] = static_cast<Byte>((value >> 8) & 0xff);
    packet[offset + 3] = static_cast<Byte>(value & 0xff);
  };
  packet[0] = static_cast<Byte>(0x80);
  packet[1] = static_cast<Byte>((marker ? 0x80 : 0) | 96);
  put16(2, sequence);
  put32(4, timestamp);
  put32(8, 0x12345678);
  std::size_t index = 12;
  for (const auto value : payload) packet[index++] = static_cast<Byte>(value);
  return packet;
}

void TestSingleNal() {
  rtc::H264RtpDepacketizer depacketizer;
  rtc::message_vector messages;
  messages.push_back(std::make_shared<rtc::Message>(
      RtpPacket(1, 9000, true, {0x65, 0xaa, 0xbb})));
  rtc::binary output;
  std::uint32_t timestamp = 0;
  static_cast<rtc::MediaHandler&>(depacketizer).incoming(messages, [](rtc::message_ptr) {});
  assert(messages.size() == 1);
  output = *messages.front();
  assert(messages.front()->frameInfo);
  timestamp = messages.front()->frameInfo->timestamp;
  assert(output.size() == 7);
  assert(std::to_integer<std::uint8_t>(output[0]) == 0);
  assert(std::to_integer<std::uint8_t>(output[3]) == 1);
  assert(std::to_integer<std::uint8_t>(output[4]) == 0x65);
  assert(std::to_integer<std::uint8_t>(output[6]) == 0xbb);
  assert(timestamp == 9000);
}

void TestFuA() {
  rtc::H264RtpDepacketizer depacketizer;
  rtc::message_vector messages;
  messages.push_back(std::make_shared<rtc::Message>(
      RtpPacket(10, 18000, false, {0x7c, 0x85, 0xaa})));
  messages.push_back(std::make_shared<rtc::Message>(
      RtpPacket(11, 18000, true, {0x7c, 0x45, 0xbb})));
  rtc::binary output;
  static_cast<rtc::MediaHandler&>(depacketizer).incoming(messages, [](rtc::message_ptr) {});
  assert(messages.size() == 1);
  output = *messages.front();
  assert(output.size() == 7);
  assert(std::to_integer<std::uint8_t>(output[3]) == 1);
  assert(std::to_integer<std::uint8_t>(output[4]) == 0x65);
  assert(std::to_integer<std::uint8_t>(output[5]) == 0xaa);
  assert(std::to_integer<std::uint8_t>(output[6]) == 0xbb);
}
}  // namespace

int main() {
  TestSingleNal();
  TestFuA();
  std::cout << "CamBridge H264 depacketizer tests passed\n";
  return 0;
}
