#include "decompressor/echos_rel_decompressor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "utils/double.h"
#include "utils/input_bit_stream.h"

namespace {

enum class Mode {
  kRepeat,
  kSameSignResidual,
  kChangedSignResidual,
  kChangedSignZeroResidual,
  kZero,
  kRaw
};

Mode ReadMode(InputBitStream *input) {
  if (!input->ReadBit()) return Mode::kRepeat;
  if (!input->ReadBit()) return Mode::kSameSignResidual;
  if (!input->ReadBit()) return Mode::kChangedSignResidual;
  if (!input->ReadBit()) return Mode::kChangedSignZeroResidual;
  return !input->ReadBit() ? Mode::kZero : Mode::kRaw;
}

uint32_t EstimateRiceParameter(uint64_t magnitude_sum, uint64_t sample_count) {
  if (magnitude_sum == 0 || sample_count == 0) return 0;
  const uint32_t magnitude_log = AdaptiveQtCodec::FloorLog2(magnitude_sum);
  const uint32_t sample_log = AdaptiveQtCodec::FloorLog2(sample_count);
  return magnitude_log > sample_log ? magnitude_log - sample_log : 0;
}

void UpdateRiceParameterState(uint64_t mapped, uint64_t *magnitude_sum, uint64_t *sample_count) {
  *magnitude_sum = AdaptiveQtCodec::DecayAndAdd(
      *magnitude_sum, std::min(mapped - 1, AdaptiveQtCodec::kAdaptiveRiceMagnitudeCap));
  *sample_count = AdaptiveQtCodec::DecayAndAdd(*sample_count, 1);
}

}  // namespace

std::vector<double> EchosRelDecompressor::Decompress(const Array<uint8_t> &bytes) {
  InputBitStream input;
  input.SetBuffer(bytes);
  const bool block_size_changed = input.ReadBit();
  const bool log_max_diff_changed = input.ReadBit();
  if (!metadata_initialized_ && (!block_size_changed || !log_max_diff_changed)) {
    throw std::runtime_error("First ECHOS relative block must contain full metadata");
  }
  if (block_size_changed) block_size_ = input.ReadInt(16);
  if (log_max_diff_changed) log_max_diff_ = Double::LongBitsToDouble(input.ReadLong(64));
  if (block_size_ <= 0 || block_size_ > 65535 || !std::isfinite(log_max_diff_) ||
      log_max_diff_ <= 0) {
    throw std::runtime_error("Invalid ECHOS relative metadata");
  }
  metadata_initialized_ = true;

  std::vector<double> result;
  result.reserve(block_size_);
  for (int index = 0; index < block_size_; ++index) {
    const Mode mode = ReadMode(&input);
    if (mode == Mode::kRaw) {
      const double value = Double::LongBitsToDouble(input.ReadLong(64));
      result.push_back(value);
      if (std::isfinite(value) && value != 0) {
        previous_log_ = std::log(std::abs(value));
        previous_value_ = value;
        previous_sign_ = std::signbit(value);
      }
      continue;
    }
    if (mode == Mode::kZero) {
      result.push_back(0.0);
      continue;
    }
    if (mode == Mode::kRepeat) {
      result.push_back(previous_value_);
      continue;
    }
    if (mode == Mode::kChangedSignZeroResidual) {
      previous_sign_ = !previous_sign_;
      previous_value_ = previous_sign_ ? -std::abs(previous_value_) : std::abs(previous_value_);
      result.push_back(previous_value_);
      continue;
    }

    const uint32_t rice_parameter =
        EstimateRiceParameter(adaptive_magnitude_sum_, adaptive_sample_count_);
    const uint64_t mapped =
        AdaptiveQtCodec::DecodeCappedRice(rice_parameter, AdaptiveQtCodec::kBoundedRiceQuotientCap,
                                          &input);
    if (mapped == std::numeric_limits<uint64_t>::max()) {
      throw std::runtime_error("Invalid ECHOS relative residual");
    }

    if (mode == Mode::kChangedSignResidual) previous_sign_ = !previous_sign_;
    const int64_t q = AdaptiveQtCodec::ZigZagDecode(mapped);
    previous_log_ += 2 * log_max_diff_ * static_cast<double>(q);
    const double magnitude = std::exp(previous_log_);
    previous_value_ = previous_sign_ ? -magnitude : magnitude;
    result.push_back(previous_value_);
    UpdateRiceParameterState(mapped, &adaptive_magnitude_sum_, &adaptive_sample_count_);
  }
  return result;
}
