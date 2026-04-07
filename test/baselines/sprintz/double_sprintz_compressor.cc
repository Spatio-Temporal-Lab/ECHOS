#include "double_sprintz_compressor.h"

DoubleSprintzCompressor::DoubleSprintzCompressor(double max_diff): kMaxDiff(max_diff * 0.999) {}

int DoubleSprintzCompressor::compress(std::vector<double> data, int16_t *compression_output) {
  // store error-bound
  std::memcpy(compression_output, &kMaxDiff, sizeof(double));
  auto *quant_buffer = new int64_t [data.size()];
  for (int i = 0; i < data.size(); ++i) {
    auto quant = static_cast<int64_t>(std::round((data[i] - pre_value_) / (2 * kMaxDiff)));
    quant_buffer[i] = quant;
    double recover_value = pre_value_ + 2 * kMaxDiff * static_cast<double>(quant);
    pre_value_ = recover_value;
  }
  // compression_size returns the len of compression_output
  int compression_size = sprintz_compress_xff_16b((uint16_t *)quant_buffer, data.size() * (64 / 16),
                                                  compression_output + (64 / 16), (64 / 16));
  return compression_size * 2;
}
