#include "cambridge_media_source.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

void TestLandscapeToPortraitNv12Rotation() {
  cambridge::native::Nv12Frame input;
  input.width = 4;
  input.height = 2;
  input.stride = 4;
  input.sequence = 7;
  input.timestamp100ns = 11;
  input.bytes = {
      1, 2, 3, 4,
      5, 6, 7, 8,
      10, 20, 30, 40,
  };

  cambridge::native::Nv12Frame output;
  assert(cambridge::native::ConvertNv12FrameToLayout(input, 2, 4, 2, &output));
  assert(output.width == 2);
  assert(output.height == 4);
  assert(output.stride == 2);
  assert(output.sequence == input.sequence);
  assert(output.timestamp100ns == input.timestamp100ns);
  assert((output.bytes == std::vector<std::uint8_t>{5, 1, 6, 2, 7, 3, 8, 4,
                                                     10, 20, 30, 40}));
}

void TestSameLayoutPreservesNv12() {
  cambridge::native::Nv12Frame input;
  input.width = 2;
  input.height = 2;
  input.stride = 2;
  input.bytes = {1, 2, 3, 4, 10, 20};
  cambridge::native::Nv12Frame output;
  assert(cambridge::native::ConvertNv12FrameToLayout(input, 2, 2, 2, &output));
  assert(output.bytes == input.bytes);
}

void TestRejectsUnsupportedLayout() {
  cambridge::native::Nv12Frame input;
  input.width = 4;
  input.height = 2;
  input.stride = 4;
  input.bytes.resize(12, 0);
  cambridge::native::Nv12Frame output;
  assert(!cambridge::native::ConvertNv12FrameToLayout(input, 8, 8, 8, &output));
}

}  // namespace

int main() {
  TestLandscapeToPortraitNv12Rotation();
  TestSameLayoutPreservesNv12();
  TestRejectsUnsupportedLayout();
  std::cout << "CamBridge Media Source layout tests passed\n";
  return 0;
}
