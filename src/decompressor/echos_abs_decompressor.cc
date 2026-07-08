#include "decompressor/echos_abs_decompressor.h"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "utils/double.h"

namespace {

constexpr uint64_t kEscape = std::numeric_limits<uint64_t>::max();

class AbsBitReader {
 public:
  explicit AbsBitReader(const Array<uint8_t> &bytes)
      : data_(bytes.begin()), length_(bytes.length()) {
    Fill(32);
  }

  uint32_t ReadBit() {
    Fill(1);
    const uint32_t result = static_cast<uint32_t>(buffer_ >> 63);
    ForwardLoaded(1);
    return result;
  }

  uint32_t ReadInt(size_t len) {
    return static_cast<uint32_t>(ReadLong(len));
  }

  uint64_t ReadLong(size_t len) {
    uint64_t result = 0;
    while (len > 32) {
      result = (result << 32) | ReadBitsAtMost32(32);
      len -= 32;
    }
    return len == 0 ? result : (result << len) | ReadBitsAtMost32(len);
  }

  uint64_t ReadUnaryZeros(uint64_t max_zeros) {
    uint64_t zeros = 0;
    while (true) {
      const uint32_t next = Peek32();
      if (next != 0) {
        const uint32_t next_zeros = CountLeadingZeros(next);
        if (next_zeros >= max_zeros - zeros) throw std::runtime_error("Invalid unary code");
        ForwardLoaded(next_zeros + 1);
        return zeros + next_zeros;
      }
      if (max_zeros - zeros <= 32) throw std::runtime_error("Invalid unary code");
      ForwardLoaded(32);
      zeros += 32;
    }
  }

  uint64_t ReadUnaryZerosOrCap(uint32_t max_zeros) {
    const uint32_t next = Peek32();
    const uint32_t next_zeros = next == 0 ? 32 : CountLeadingZeros(next);
    if (next_zeros >= max_zeros) {
      ForwardLoaded(max_zeros);
      return max_zeros;
    }
    ForwardLoaded(next_zeros + 1);
    return next_zeros;
  }

 private:
  static uint32_t CountLeadingZeros(uint32_t value) {
#if defined(__GNUC__) || defined(__clang__)
    return static_cast<uint32_t>(__builtin_clz(value));
#else
    uint32_t zeros = 0;
    while ((value & 0x80000000U) == 0) {
      value <<= 1;
      ++zeros;
    }
    return zeros;
#endif
  }

  void Fill(size_t target_bits) {
    while (bits_in_buffer_ < target_bits && cursor_ < length_ && bits_in_buffer_ + 8 <= 64) {
      buffer_ |= static_cast<uint64_t>(data_[cursor_++]) << (56 - bits_in_buffer_);
      bits_in_buffer_ += 8;
    }
  }

  uint32_t Peek32() {
    Fill(32);
    return static_cast<uint32_t>(buffer_ >> 32);
  }

  uint64_t ReadBitsAtMost32(size_t len) {
    Fill(len);
    if (len == 0) return 0;
    const uint64_t result = buffer_ >> (64 - len);
    ForwardLoaded(len);
    return result;
  }

  void ForwardLoaded(size_t len) {
    if (len == 64) {
      buffer_ = 0;
      bits_in_buffer_ = 0;
    } else {
      buffer_ <<= len;
      bits_in_buffer_ -= len;
    }
    Fill(32);
  }

  const uint8_t *data_;
  int length_;
  int cursor_ = 0;
  uint64_t buffer_ = 0;
  size_t bits_in_buffer_ = 0;
};

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

inline uint64_t DecodeGammaFast(AbsBitReader *input) {
  const uint32_t zeros = static_cast<uint32_t>(input->ReadUnaryZeros(64));
  if (zeros == 0) return 1;
  return (1ULL << zeros) | input->ReadLong(zeros);
}

inline uint64_t DecodeDeltaFast(AbsBitReader *input) {
  const uint64_t value_bits = DecodeGammaFast(input);
  if (value_bits == 0 || value_bits > 64) throw std::runtime_error("Invalid Elias delta code");
  if (value_bits == 1) return 1;
  return (1ULL << (value_bits - 1)) | input->ReadLong(value_bits - 1);
}

inline uint64_t DecodeCappedRiceWithRawFast(uint32_t parameter, bool *raw,
                                            AbsBitReader *input) {
  *raw = false;
  const uint64_t quotient = input->ReadUnaryZerosOrCap(AdaptiveQtCodec::kBoundedRiceQuotientCap);
  if (quotient == AdaptiveQtCodec::kBoundedRiceQuotientCap) {
    if (input->ReadBit()) {
      *raw = true;
      return 1;
    }
    return DecodeDeltaFast(input);
  }
  const uint64_t remainder = parameter == 0 ? 0 : input->ReadLong(parameter);
  return ((quotient << parameter) | remainder) + 1;
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

}  // namespace

std::vector<double> EchosAbsDecompressor::Decompress(const Array<uint8_t> &bytes) {
  AbsBitReader input(bytes);
  const bool block_size_changed = input.ReadBit();
  const bool max_diff_changed = input.ReadBit();
  if (!metadata_initialized_ && (!block_size_changed || !max_diff_changed)) {
    throw std::runtime_error("First ECHOS absolute block must contain full metadata");
  }
  if (block_size_changed) block_size_ = input.ReadInt(16);
  if (max_diff_changed) {
    max_diff_ = Double::LongBitsToDouble(input.ReadLong(64));
    quantization_step_ = 2 * max_diff_;
  }
  if (block_size_ <= 0 || block_size_ > 65535 || !std::isfinite(max_diff_) || max_diff_ <= 0) {
    throw std::runtime_error("Invalid ECHOS absolute metadata");
  }
  metadata_initialized_ = true;

  std::vector<double> result(block_size_);
  double previous = previous_;
  AdaptiveQtCodec::AdaptiveDeltaRiceState state = adaptive_state_;
  const double quantization_step = quantization_step_;
  for (int index = 0; index < block_size_; ++index) {
    const bool use_rice = state.rice_cost < state.delta_cost;
    uint32_t rice_parameter = 0;
    bool raw = false;
    uint64_t mapped = 1;
    if (use_rice) {
      rice_parameter = EstimateRiceParameterFast(state);
      mapped = DecodeCappedRiceWithRawFast(rice_parameter, &raw, &input);
    } else {
      mapped = DecodeDeltaFast(&input);
      raw = mapped == kEscape;
    }

    double value;
    if (raw) {
      value = Double::LongBitsToDouble(input.ReadLong(64));
      previous = std::isfinite(value) ? value : 2;
    } else {
      if (!use_rice) rice_parameter = EstimateRiceParameterFast(state);
      const int64_t q = AdaptiveQtCodec::ZigZagDecode(mapped - 1);
      value = previous + quantization_step * static_cast<double>(q);
      UpdateAbsStateFast(mapped, rice_parameter, &state);
      previous = value;
    }
    result[index] = value;
  }
  previous_ = previous;
  adaptive_state_ = state;
  return result;
}
