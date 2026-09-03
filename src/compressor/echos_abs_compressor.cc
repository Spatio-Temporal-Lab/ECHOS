#include "compressor/echos_abs_compressor.h"

#include <cmath>
#include <cstddef>
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

inline uint32_t RiceParameterFromMean(long double magnitude_sum,
                                      std::size_t sample_count) {
  if (sample_count == 0) return 0;
  const long double mean =
      magnitude_sum / static_cast<long double>(sample_count);
  if (mean < 1) return 0;
  int exponent = 0;
  std::frexp(mean, &exponent);
  return exponent > 64 ? 63 : static_cast<uint32_t>(exponent - 1);
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

inline uint64_t CappedRiceLengthFast(uint64_t mapped, uint32_t rice_parameter) {
  const uint64_t quotient = (mapped - 1) >> rice_parameter;
  return quotient < AdaptiveQtCodec::kBoundedRiceQuotientCap
             ? quotient + rice_parameter + 1
             : AdaptiveQtCodec::kBoundedRiceQuotientCap + 1 + DeltaLengthFast(mapped);
}

inline std::array<uint64_t, 3> MakeAbsObservation(uint64_t mapped,
                                                  uint32_t rice_parameter) {
  const uint64_t delta_length = DeltaLengthFast(mapped);
  const uint64_t rice_length = CappedRiceLengthFast(mapped, rice_parameter);
  const uint64_t magnitude = mapped - 1;
  return {delta_length, rice_length,
          magnitude < AdaptiveQtCodec::kAdaptiveRiceMagnitudeCap
              ? magnitude
              : AdaptiveQtCodec::kAdaptiveRiceMagnitudeCap};
}

inline void UpdateAbsState(uint64_t mapped, uint32_t rice_parameter,
                           EchosAbsMode mode, std::size_t window_size,
                           std::deque<std::array<uint64_t, 3>> *window,
                           AdaptiveQtCodec::AdaptiveDeltaRiceState *state) {
  const auto observation = MakeAbsObservation(mapped, rice_parameter);
  const uint64_t delta_length = observation[0];
  const uint64_t rice_length = observation[1];
  const uint64_t magnitude = observation[2];
  if (mode == EchosAbsMode::kFullHistory) {
    state->delta_cost += delta_length;
    state->rice_cost += rice_length;
    state->magnitude_sum += magnitude;
    ++state->sample_count;
    return;
  }
  if (mode == EchosAbsMode::kSlidingWindow) {
    state->delta_cost += delta_length;
    state->rice_cost += rice_length;
    state->magnitude_sum += magnitude;
    ++state->sample_count;
    window->push_back(observation);
    if (window->size() > window_size) {
      const auto oldest = window->front();
      window->pop_front();
      state->delta_cost -= oldest[0];
      state->rice_cost -= oldest[1];
      state->magnitude_sum -= oldest[2];
      --state->sample_count;
    }
    return;
  }
  if (mode != EchosAbsMode::kAdaptive) return;
  state->delta_cost = DecayAndAddFast(state->delta_cost, delta_length);
  state->rice_cost = DecayAndAddFast(state->rice_cost, rice_length);
  state->magnitude_sum = DecayAndAddFast(state->magnitude_sum, magnitude);
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

struct QuantizedAbsValue {
  bool raw;
  uint64_t mapped;
  double reconstructed;
  double original;
};

}  // namespace

EchosAbsCompressor::EchosAbsCompressor(int block_size, double max_diff,
                                       EchosAbsMode mode,
                                       std::size_t sliding_window)
    : block_size_(block_size),
      max_diff_(max_diff * 0.999),
      quantization_step_(2 * max_diff_),
      inverse_quantization_step_(1.0 / quantization_step_),
      mode_(mode),
      sliding_window_(sliding_window) {
  if (block_size <= 0 || block_size > 65535) throw std::invalid_argument("Invalid block size");
  if (!std::isfinite(max_diff) || max_diff <= 0) throw std::invalid_argument("Invalid error bound");
  if (mode_ == EchosAbsMode::kSlidingWindow && sliding_window_ == 0) {
    throw std::invalid_argument("Sliding window must be nonzero");
  }
  if (mode_ == EchosAbsMode::kBatchOracle) pending_values_.reserve(block_size_);
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

void EchosAbsCompressor::EncodeBatchOracle() {
  std::vector<QuantizedAbsValue> values;
  values.reserve(pending_values_.size());
  double prediction = previous_;
  uint64_t delta_total = 0;
  long double magnitude_sum = 0;
  std::size_t sample_count = 0;
  for (double value : pending_values_) {
    uint64_t mapped = 1;
    double reconstructed = 0;
    const bool quantized =
        Quantize(value, prediction, max_diff_, quantization_step_,
                 inverse_quantization_step_, &mapped, &reconstructed);
    values.push_back({!quantized, mapped, reconstructed, value});
    if (quantized) {
      delta_total += DeltaLengthFast(mapped);
      magnitude_sum += static_cast<long double>(mapped - 1);
      ++sample_count;
      prediction = reconstructed;
    } else {
      delta_total += DeltaLengthFast(kEscape) + 64;
      prediction = std::isfinite(value) ? value : 2;
    }
  }

  const uint32_t batch_parameter =
      RiceParameterFromMean(magnitude_sum, sample_count);
  uint64_t batch_rice_total = 0;
  for (const auto &item : values) {
    batch_rice_total +=
        item.raw
            ? AdaptiveQtCodec::kBoundedRiceQuotientCap + 1 + 64
            : CappedRiceLengthFast(item.mapped, batch_parameter);
  }
  const bool use_rice = batch_rice_total < delta_total;
  compressed_size_in_bits_ += output_->WriteBit(use_rice);
  ++decision_metadata_size_in_bits_;
  if (use_rice) {
    compressed_size_in_bits_ += output_->WriteInt(batch_parameter, 6);
    decision_metadata_size_in_bits_ += 6;
  }

  for (const auto &item : values) {
    if (item.raw) {
      compressed_size_in_bits_ += use_rice
                                      ? WriteCappedRiceRawFast(output_.get())
                                      : EncodeDeltaFast(kEscape, output_.get());
      compressed_size_in_bits_ +=
          output_->WriteLong(Double::DoubleToLongBits(item.original), 64);
      UpdatePrediction(item.original);
    } else {
      compressed_size_in_bits_ +=
          use_rice
              ? EncodeCappedRiceWithRawFast(item.mapped, batch_parameter, output_.get())
              : EncodeDeltaFast(item.mapped, output_.get());
      UpdatePrediction(item.reconstructed);
    }
  }
  pending_values_.clear();
}

void EchosAbsCompressor::AddValue(double value) {
  if (value_count_ >= block_size_) {
    throw std::runtime_error("ECHOS absolute block is full");
  }
  if (value_count_ == 0) WriteMetadata();
  if (mode_ == EchosAbsMode::kBatchOracle) {
    pending_values_.push_back(value);
    ++value_count_;
    return;
  }

  bool use_rice = adaptive_state_.rice_cost < adaptive_state_.delta_cost;
  uint32_t rice_parameter = EstimateRiceParameterFast(adaptive_state_);
  uint64_t mapped = 1;
  double recovered = 0;
  if (Quantize(value, previous_, max_diff_, quantization_step_, inverse_quantization_step_,
               &mapped, &recovered)) {
    if (mode_ == EchosAbsMode::kPointwiseOracle) {
      const uint64_t magnitude = mapped - 1;
      rice_parameter =
          magnitude == 0 ? 0 : FloorLog2NonZero(magnitude);
      const uint64_t delta_length = DeltaLengthFast(mapped);
      use_rice =
          CappedRiceLengthFast(mapped, rice_parameter) < delta_length;
      compressed_size_in_bits_ += output_->WriteBit(use_rice);
      ++decision_metadata_size_in_bits_;
      if (use_rice) {
        compressed_size_in_bits_ += output_->WriteInt(rice_parameter, 6);
        decision_metadata_size_in_bits_ += 6;
      }
    }
    if (use_rice) {
      compressed_size_in_bits_ +=
          EncodeCappedRiceWithRawFast(mapped, rice_parameter, output_.get());
    } else {
      compressed_size_in_bits_ += EncodeDeltaFast(mapped, output_.get());
    }
    UpdateAbsState(mapped, rice_parameter, mode_, sliding_window_,
                   &context_window_, &adaptive_state_);
    UpdatePrediction(recovered);
  } else {
    if (mode_ == EchosAbsMode::kPointwiseOracle) {
      use_rice = true;
      rice_parameter = 0;
      compressed_size_in_bits_ += output_->WriteBit(true);
      compressed_size_in_bits_ += output_->WriteInt(rice_parameter, 6);
      decision_metadata_size_in_bits_ += 7;
    }
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
  if (mode_ == EchosAbsMode::kBatchOracle) EncodeBatchOracle();
  output_->Flush();
  compressed_bytes_ =
      output_->GetUsedBuffer(static_cast<uint32_t>((compressed_size_in_bits_ + 7) >> 3));
  output_->Refresh();
  stored_compressed_size_in_bits_ = compressed_size_in_bits_;
  compressed_size_in_bits_ = 0;
  value_count_ = 0;
  stored_decision_metadata_size_in_bits_ = decision_metadata_size_in_bits_;
  decision_metadata_size_in_bits_ = 0;
}

Array<uint8_t> EchosAbsCompressor::compressed_bytes() const {
  return compressed_bytes_;
}

long EchosAbsCompressor::get_compressed_size_in_bits() const {
  return stored_compressed_size_in_bits_;
}

long EchosAbsCompressor::get_decision_metadata_size_in_bits() const {
  return stored_decision_metadata_size_in_bits_;
}
