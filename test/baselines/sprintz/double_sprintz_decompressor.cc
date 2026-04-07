#include "double_sprintz_decompressor.h"
#include <iostream>

std::vector<double> DoubleSprintzDecompressor::decompress(int16_t *src) {
  max_diff_ = *(double *)src;
  auto *start = src + (64 / 16);
  std::vector<double> decompression_output;
  auto *decompression_buffer = new uint16_t [8192];
  // decompression_size returns the len of decompression_output
  int decompression_size = sprintz_decompress_xff_16b(start, decompression_buffer);
  for (int i = 0; i < decompression_size / (64 / 16); ++i) {
    int64_t quant = *(int64_t *)(decompression_buffer + i * (64 / 16));
    double recover_value = pre_value_ + 2 * max_diff_ * static_cast<double>(quant);
    pre_value_ = recover_value;
    decompression_output.emplace_back(recover_value);
  }
  return decompression_output;
}
