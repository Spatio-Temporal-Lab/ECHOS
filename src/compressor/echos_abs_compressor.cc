#include "compressor/echos_abs_compressor.h"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "utils/double.h"

namespace {

constexpr uint64_t kEscape = std::numeric_limits<uint64_t>::max();

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

inline uint32_t EstimateRiceParameterFast(
    const AdaptiveQtCodec::AdaptiveDeltaRiceState &state) {
  if (state.magnitude_sum == 0 || state.sample_count == 0) return 0;
  const uint32_t magnitude_log = FloorLog2NonZero(state.magnitude_sum);
  const uint32_t sample_log = FloorLog2NonZero(state.sample_count);
  return magnitude_log > sample_log ? magnitude_log - sample_log : 0;
}

inline uint64_t DeltaLengthFast(uint64_t mapped) {
  const uint64_t value_bits = FloorLog2NonZero(mapped) + 1;
  const uint64_t gamma_length = 2ULL * FloorLog2NonZero(value_bits) + 1;
  return gamma_length + value_bits - 1;
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

inline uint64_t EncodeCappedRiceWithRawFast(uint64_t mapped, uint32_t parameter,
                                            EchosOutputBitStream *output) {
  const uint64_t quotient = (mapped - 1) >> parameter;
  if (quotient < AdaptiveQtCodec::kBoundedRiceQuotientCap) {
    WriteZerosFast(quotient, output);
    output->WriteBit(true);
    if (parameter > 0) output->WriteLong(mapped - 1, parameter);
    return quotient + 1 + parameter;
  }
  WriteZerosFast(AdaptiveQtCodec::kBoundedRiceQuotientCap, output);
  output->WriteBit(false);
  return AdaptiveQtCodec::kBoundedRiceQuotientCap + 1 + EncodeDeltaFast(mapped, output);
}

inline uint64_t WriteCappedRiceRawFast(EchosOutputBitStream *output) {
  WriteZerosFast(AdaptiveQtCodec::kBoundedRiceQuotientCap, output);
  output->WriteBit(true);
  return AdaptiveQtCodec::kBoundedRiceQuotientCap + 1;
}

inline void UpdateAbsStateFast(uint64_t mapped, uint32_t rice_parameter,
                               AdaptiveQtCodec::AdaptiveDeltaRiceState *state) {
  const uint64_t delta_length = DeltaLengthFast(mapped);
  const uint64_t quotient = (mapped - 1) >> rice_parameter;
  const uint64_t rice_direct_length = quotient + rice_parameter + 1;
  const uint64_t rice_length =
      quotient < AdaptiveQtCodec::kBoundedRiceQuotientCap
          ? rice_direct_length
          : AdaptiveQtCodec::kBoundedRiceQuotientCap + 1 + delta_length;
  state->delta_cost = DecayAndAddFast(state->delta_cost, delta_length);
  state->rice_cost = DecayAndAddFast(state->rice_cost, rice_length);
  const uint64_t magnitude = mapped - 1;
  state->magnitude_sum = DecayAndAddFast(
      state->magnitude_sum,
      magnitude < AdaptiveQtCodec::kAdaptiveRiceMagnitudeCap
          ? magnitude
          : AdaptiveQtCodec::kAdaptiveRiceMagnitudeCap);
  state->sample_count = DecayAndAddFast(state->sample_count, 1);
}

bool Quantize(double value, double prediction, double max_diff, double quantization_step,
              double inverse_quantization_step, uint64_t *mapped, double *recovered) {
  if (!std::isfinite(value) || !std::isfinite(prediction)) return false;
  const double scaled = (value - prediction) * inverse_quantization_step;
  if (std::isfinite(scaled) && std::abs(scaled) <= 0x1p52) {
    const int64_t q = static_cast<int64_t>(std::round(scaled));
    const uint64_t encoded = AdaptiveQtCodec::ZigZagEncode(q);
    if (encoded < std::numeric_limits<uint64_t>::max() - 1) {
      *recovered = prediction + quantization_step * static_cast<double>(q);
      if (std::isfinite(*recovered) && std::abs(value - *recovered) <= max_diff) {
        *mapped = encoded + 1;
        return true;
      }
    }
  }

  const long double step = 2.0L * static_cast<long double>(max_diff);
  const long double precise_scaled =
      (static_cast<long double>(value) - static_cast<long double>(prediction)) / step;
  if (!std::isfinite(precise_scaled)) return false;
  const long double rounded = std::round(precise_scaled);
  if (rounded <= static_cast<long double>(std::numeric_limits<int64_t>::min()) ||
      rounded > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
    return false;
  }
  const int64_t q = static_cast<int64_t>(rounded);
  const uint64_t encoded = AdaptiveQtCodec::ZigZagEncode(q);
  if (encoded >= std::numeric_limits<uint64_t>::max() - 1) return false;
  *mapped = encoded + 1;
  *recovered = prediction + quantization_step * static_cast<double>(q);
  return std::isfinite(*recovered) && std::abs(value - *recovered) <= max_diff;
}

}  // namespace

EchosAbsCompressor::EchosAbsCompressor(int block_size, double max_diff)
    : block_size_(block_size),
      max_diff_(max_diff * 0.999),
      quantization_step_(2 * max_diff_),
      inverse_quantization_step_(1.0 / quantization_step_) {
  if (block_size <= 0 || block_size > 65535) throw std::invalid_argument("Invalid block size");
  if (!std::isfinite(max_diff) || max_diff <= 0) throw std::invalid_argument("Invalid error bound");
  output_ = std::make_unique<EchosOutputBitStream>(static_cast<uint32_t>(24 * block_size + 32));
}

void EchosAbsCompressor::SetBlockConfig(int block_size, double max_diff) {
  if (value_count_ != 0) throw std::runtime_error("Cannot change block config inside a block");
  if (block_size <= 0 || block_size > 65535) throw std::invalid_argument("Invalid block size");
  if (!std::isfinite(max_diff) || max_diff <= 0) throw std::invalid_argument("Invalid error bound");
  block_size_ = block_size;
  max_diff_ = max_diff * 0.999;
  quantization_step_ = 2 * max_diff_;
  inverse_quantization_step_ = 1.0 / quantization_step_;
  output_ = std::make_unique<EchosOutputBitStream>(static_cast<uint32_t>(24 * block_size + 32));
}

void EchosAbsCompressor::WriteMetadata() {
  const uint64_t max_diff_bits = Double::DoubleToLongBits(max_diff_);
  const bool block_size_changed = !metadata_initialized_ || block_size_ != previous_block_size_;
  const bool max_diff_changed = !metadata_initialized_ || max_diff_bits != previous_max_diff_bits_;
  compressed_size_in_bits_ += output_->WriteBit(block_size_changed);
  compressed_size_in_bits_ += output_->WriteBit(max_diff_changed);
  if (block_size_changed) compressed_size_in_bits_ += output_->WriteInt(block_size_, 16);
  if (max_diff_changed) compressed_size_in_bits_ += output_->WriteLong(max_diff_bits, 64);
  previous_block_size_ = block_size_;
  previous_max_diff_bits_ = max_diff_bits;
  metadata_initialized_ = true;
}

void EchosAbsCompressor::UpdatePrediction(double recovered) {
  previous_ = std::isfinite(recovered) ? recovered : 2;
}

void EchosAbsCompressor::AddValue(double value) {
  if (value_count_ >= block_size_) {
    throw std::runtime_error("ECHOS absolute block is full");
  }
  if (value_count_ == 0) WriteMetadata();

  const bool use_rice = adaptive_state_.rice_cost < adaptive_state_.delta_cost;
  const uint32_t rice_parameter = EstimateRiceParameterFast(adaptive_state_);
  uint64_t mapped = 1;
  double recovered = 0;
  if (Quantize(value, previous_, max_diff_, quantization_step_, inverse_quantization_step_,
               &mapped, &recovered)) {
    if (use_rice) {
      compressed_size_in_bits_ +=
          EncodeCappedRiceWithRawFast(mapped, rice_parameter, output_.get());
    } else {
      compressed_size_in_bits_ += EncodeDeltaFast(mapped, output_.get());
    }
    UpdateAbsStateFast(mapped, rice_parameter, &adaptive_state_);
    UpdatePrediction(recovered);
  } else {
    if (use_rice) {
      compressed_size_in_bits_ += WriteCappedRiceRawFast(output_.get());
    } else {
      compressed_size_in_bits_ += EncodeDeltaFast(kEscape, output_.get());
    }
    compressed_size_in_bits_ += output_->WriteLong(Double::DoubleToLongBits(value), 64);
    UpdatePrediction(value);
  }
  ++value_count_;
}

void EchosAbsCompressor::Close() {
  if (value_count_ != block_size_) {
    throw std::runtime_error("ECHOS absolute block is incomplete");
  }
  output_->Flush();
  compressed_bytes_ =
      output_->GetUsedBuffer(static_cast<uint32_t>((compressed_size_in_bits_ + 7) >> 3));
  output_->Refresh();
  stored_compressed_size_in_bits_ = compressed_size_in_bits_;
  compressed_size_in_bits_ = 0;
  value_count_ = 0;
}

Array<uint8_t> EchosAbsCompressor::compressed_bytes() const {
  return compressed_bytes_;
}

long EchosAbsCompressor::get_compressed_size_in_bits() const {
  return stored_compressed_size_in_bits_;
}
