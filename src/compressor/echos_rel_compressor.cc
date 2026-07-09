#include "compressor/echos_rel_compressor.h"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "utils/double.h"

namespace {

bool QuantizeLog(double magnitude, double prediction, double log_max_diff, double inverse_log_step,
                 double lower_log_error_bound, double upper_log_error_bound, int64_t *q,
                 double *target_log, double *recovered_log) {
  if (!std::isfinite(magnitude) || magnitude == 0) return false;
  *target_log = std::log(magnitude);
  const double scaled = (*target_log - prediction) * inverse_log_step;
  if (!std::isfinite(scaled)) return false;

  if (std::abs(scaled) > 0x1p52) {
    const long double step = 2.0L * static_cast<long double>(log_max_diff);
    const long double precise_scaled =
        (static_cast<long double>(*target_log) - static_cast<long double>(prediction)) / step;
    if (!std::isfinite(precise_scaled)) return false;
    const long double precise_rounded = std::round(precise_scaled);
    if (precise_rounded <= static_cast<long double>(std::numeric_limits<int64_t>::min()) ||
        precise_rounded > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
      return false;
    }
    *q = static_cast<int64_t>(precise_rounded);
  } else {
    *q = static_cast<int64_t>(std::round(scaled));
  }
  if (*q == std::numeric_limits<int64_t>::min()) return false;

  *recovered_log = prediction + 2 * log_max_diff * static_cast<double>(*q);
  const double log_error = *recovered_log - *target_log;
  return log_error >= lower_log_error_bound && log_error <= upper_log_error_bound;
}

inline uint32_t FloorLog2NonZero(uint64_t value) {
#if defined(__GNUC__) || defined(__clang__)
  return 63U - static_cast<uint32_t>(__builtin_clzll(value));
#else
  uint32_t result = 0;
  while (value >>= 1) ++result;
  return result;
#endif
}

inline uint64_t DecayAndAddFast(uint64_t cost, uint64_t length) {
  return cost - (cost >> AdaptiveQtCodec::kAdaptiveRiceDecayShift) + length;
}

inline void WriteZerosFast(uint64_t count, EchosOutputBitStream *output) {
  while (count >= 32) {
    output->WriteInt(0, 32);
    count -= 32;
  }
  if (count > 0) output->WriteInt(0, static_cast<uint32_t>(count));
}

uint32_t EstimateRiceParameter(uint64_t magnitude_sum, uint64_t sample_count) {
  if (magnitude_sum == 0 || sample_count == 0) return 0;
  const uint32_t magnitude_log = FloorLog2NonZero(magnitude_sum);
  const uint32_t sample_log = FloorLog2NonZero(sample_count);
  return magnitude_log > sample_log ? magnitude_log - sample_log : 0;
}

inline uint64_t GammaLengthFast(uint64_t positive) {
  return 2ULL * FloorLog2NonZero(positive) + 1;
}

inline uint64_t DeltaLengthFast(uint64_t positive) {
  const uint64_t value_bits = FloorLog2NonZero(positive) + 1;
  return GammaLengthFast(value_bits) + value_bits - 1;
}

inline uint64_t CappedRiceLengthFast(uint64_t mapped, uint32_t rice_parameter) {
  const uint64_t quotient = (mapped - 1) >> rice_parameter;
  return quotient < AdaptiveQtCodec::kBoundedRiceQuotientCap
             ? quotient + rice_parameter + 1
             : AdaptiveQtCodec::kBoundedRiceQuotientCap + DeltaLengthFast(mapped);
}

inline uint64_t EncodeGammaFast(uint64_t positive, EchosOutputBitStream *output) {
  const uint32_t log = FloorLog2NonZero(positive);
  WriteZerosFast(log, output);
  output->WriteLong(positive, log + 1);
  return 2ULL * log + 1;
}

inline uint64_t EncodeDeltaFast(uint64_t positive, EchosOutputBitStream *output) {
  const uint32_t value_bits = FloorLog2NonZero(positive) + 1;
  uint64_t written = EncodeGammaFast(value_bits, output);
  if (value_bits > 1) written += output->WriteLong(positive, value_bits - 1);
  return written;
}

inline uint64_t EncodeCappedRiceFast(uint64_t mapped, uint32_t parameter,
                                     EchosOutputBitStream *output) {
  const uint64_t quotient = (mapped - 1) >> parameter;
  if (quotient < AdaptiveQtCodec::kBoundedRiceQuotientCap) {
    WriteZerosFast(quotient, output);
    output->WriteBit(true);
    if (parameter > 0) output->WriteLong(mapped - 1, parameter);
    return quotient + 1 + parameter;
  }
  WriteZerosFast(AdaptiveQtCodec::kBoundedRiceQuotientCap, output);
  return AdaptiveQtCodec::kBoundedRiceQuotientCap + EncodeDeltaFast(mapped, output);
}

void UpdateRiceParameterState(uint64_t mapped, uint64_t *magnitude_sum, uint64_t *sample_count) {
  const uint64_t magnitude = mapped - 1;
  *magnitude_sum = DecayAndAddFast(
      *magnitude_sum,
      magnitude < AdaptiveQtCodec::kAdaptiveRiceMagnitudeCap
          ? magnitude
          : AdaptiveQtCodec::kAdaptiveRiceMagnitudeCap);
  *sample_count = DecayAndAddFast(*sample_count, 1);
}

}  // namespace

EchosRelCompressor::EchosRelCompressor(int block_size, double relative_error_bound)
    : block_size_(block_size) {
  if (block_size <= 0 || block_size > 65535) throw std::invalid_argument("Invalid block size");
  UpdateErrorConfig(relative_error_bound);
  output_ = std::make_unique<EchosOutputBitStream>(static_cast<uint32_t>(16 * block_size + 32));
}

void EchosRelCompressor::UpdateErrorConfig(double relative_error_bound) {
  if (!std::isfinite(relative_error_bound) || relative_error_bound <= 0) {
    throw std::invalid_argument("Invalid relative error bound");
  }
  relative_error_bound_ = relative_error_bound;
  upper_log_error_bound_ = std::log1p(relative_error_bound);
  lower_log_error_bound_ =
      relative_error_bound < 1 ? std::log1p(-relative_error_bound)
                               : -std::numeric_limits<double>::infinity();
  log_max_diff_ = upper_log_error_bound_ * 0.999;
  inverse_log_step_ = 1.0 / (2 * log_max_diff_);
}

void EchosRelCompressor::SetBlockConfig(int block_size, double relative_error_bound) {
  if (value_count_ != 0) throw std::runtime_error("Cannot change block config inside a block");
  if (block_size <= 0 || block_size > 65535) throw std::invalid_argument("Invalid block size");
  block_size_ = block_size;
  UpdateErrorConfig(relative_error_bound);
  output_ = std::make_unique<EchosOutputBitStream>(static_cast<uint32_t>(16 * block_size + 32));
}

void EchosRelCompressor::WriteMetadata() {
  const uint64_t log_max_diff_bits = Double::DoubleToLongBits(log_max_diff_);
  const bool block_size_changed = !metadata_initialized_ || block_size_ != previous_block_size_;
  const bool log_max_diff_changed =
      !metadata_initialized_ || log_max_diff_bits != previous_log_max_diff_bits_;
  compressed_size_in_bits_ += output_->WriteBit(block_size_changed);
  compressed_size_in_bits_ += output_->WriteBit(log_max_diff_changed);
  if (block_size_changed) compressed_size_in_bits_ += output_->WriteInt(block_size_, 16);
  if (log_max_diff_changed) compressed_size_in_bits_ += output_->WriteLong(log_max_diff_bits, 64);
  previous_block_size_ = block_size_;
  previous_log_max_diff_bits_ = log_max_diff_bits;
  metadata_initialized_ = true;
}

void EchosRelCompressor::AddValue(double value) {
  if (value_count_ >= block_size_) {
    throw std::runtime_error("ECHOS relative block is full");
  }
  if (value_count_ == 0) WriteMetadata();

  const bool sign = std::signbit(value);
  if (value == 0) {
    compressed_size_in_bits_ += output_->WriteInt(30, 5);
    ++value_count_;
    return;
  }

  const bool finite = std::isfinite(value);
  const double magnitude = std::abs(value);
  if (finite && sign == previous_sign_ &&
      std::abs(previous_value_ - value) <= relative_error_bound_ * magnitude) {
    compressed_size_in_bits_ += output_->WriteBit(false);
    ++value_count_;
    return;
  }
  if (finite && sign != previous_sign_ &&
      std::abs(std::abs(previous_value_) - magnitude) <= relative_error_bound_ * magnitude) {
    compressed_size_in_bits_ += output_->WriteInt(14, 4);
    previous_value_ = sign ? -std::abs(previous_value_) : std::abs(previous_value_);
    previous_sign_ = sign;
    ++value_count_;
    return;
  }

  const uint32_t rice_parameter =
      EstimateRiceParameter(adaptive_magnitude_sum_, adaptive_sample_count_);
  int64_t q = 0;
  double target_log = 0;
  double recovered_log = 0;
  bool has_target_log = false;
  if (QuantizeLog(magnitude, previous_log_, log_max_diff_, inverse_log_step_,
                  lower_log_error_bound_, upper_log_error_bound_, &q, &target_log,
                  &recovered_log)) {
    has_target_log = true;
    if (q != 0) {
      const bool changed_sign = sign != previous_sign_;
      const uint64_t mapped = AdaptiveQtCodec::ZigZagEncode(q);
      const uint64_t prefix_bits = changed_sign ? 3 : 2;
      if (prefix_bits + CappedRiceLengthFast(mapped, rice_parameter) < 69) {
        const double recovered_magnitude = std::exp(recovered_log);
        if (std::isfinite(recovered_magnitude)) {
          compressed_size_in_bits_ +=
              changed_sign ? output_->WriteInt(6, 3) : output_->WriteInt(2, 2);
          compressed_size_in_bits_ +=
              EncodeCappedRiceFast(mapped, rice_parameter, output_.get());
          previous_log_ = recovered_log;
          previous_value_ = sign ? -recovered_magnitude : recovered_magnitude;
          previous_sign_ = sign;
          UpdateRiceParameterState(mapped, &adaptive_magnitude_sum_, &adaptive_sample_count_);
          ++value_count_;
          return;
        }
      }
    }
  }

  compressed_size_in_bits_ += output_->WriteInt(31, 5);
  compressed_size_in_bits_ += output_->WriteLong(Double::DoubleToLongBits(value), 64);
  if (finite) {
    previous_log_ = has_target_log ? target_log : std::log(magnitude);
    previous_value_ = value;
    previous_sign_ = sign;
  }
  ++value_count_;
}

void EchosRelCompressor::Close() {
  if (value_count_ != block_size_) {
    throw std::runtime_error("ECHOS relative block is incomplete");
  }
  output_->Flush();
  compressed_bytes_ =
      output_->GetUsedBuffer(static_cast<uint32_t>((compressed_size_in_bits_ + 7) >> 3));
  output_->Refresh();
  stored_compressed_size_in_bits_ = compressed_size_in_bits_;
  compressed_size_in_bits_ = 0;
  value_count_ = 0;
}

Array<uint8_t> EchosRelCompressor::compressed_bytes() const {
  return compressed_bytes_;
}

long EchosRelCompressor::get_compressed_size_in_bits() const {
  return stored_compressed_size_in_bits_;
}
