#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "baselines/elf_star/elf_star.h"

namespace {

uint64_t BitPattern(double value) {
  uint64_t bits;
  std::memcpy(&bits, &value, sizeof(value));
  return bits;
}

uint32_t BitPattern(float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof(value));
  return bits;
}

TEST(ElfStarTest, DoublePrecisionRoundTrip) {
  std::vector<double> input = {
      0.0, -0.0, 1.0, -1.0, 96.29879, 98.54943, -15.20976,
      38.49025, 30.65283, 30.72649, 0.001, 150.0, 2.0499};
  uint8_t *compressed = nullptr;

  const ssize_t compressed_size =
      elf_star_encode(input.data(), input.size(), &compressed);
  ASSERT_GT(compressed_size, 0);
  ASSERT_NE(compressed, nullptr);

  std::vector<double> output(input.size());
  ASSERT_EQ(elf_star_decode(compressed, compressed_size, output.data()),
            static_cast<ssize_t>(input.size()));
  std::free(compressed);

  for (std::size_t i = 0; i < input.size(); ++i) {
    EXPECT_EQ(BitPattern(output[i]), BitPattern(input[i])) << "index " << i;
  }
}

TEST(ElfStarTest, SinglePrecisionRoundTrip) {
  std::vector<float> input = {
      0.0f, -0.0f, 1.0f, -1.0f, 96.29879f, 98.54943f, -15.20976f,
      38.49025f, 30.65283f, 30.72649f, 0.001f, 150.0f, 2.0499f};
  uint8_t *compressed = nullptr;

  const ssize_t compressed_size =
      elf_star_encode_32(input.data(), input.size(), &compressed);
  ASSERT_GT(compressed_size, 0);
  ASSERT_NE(compressed, nullptr);

  std::vector<float> output(input.size());
  ASSERT_EQ(elf_star_decode_32(compressed, compressed_size, output.data()),
            static_cast<ssize_t>(input.size()));
  std::free(compressed);

  for (std::size_t i = 0; i < input.size(); ++i) {
    EXPECT_EQ(BitPattern(output[i]), BitPattern(input[i])) << "index " << i;
  }
}

}  // namespace
