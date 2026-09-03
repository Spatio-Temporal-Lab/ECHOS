#include "decompressor/echos_rel_decompressor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "utils/double.h"

namespace {

enum class Mode {
  kRepeat,
  kSameSignResidual,
  kChangedSignResidual,
  kChangedSignZeroResidual,
  kZero,
  kRaw
};

class RelBitReader {
 public:
  explicit RelBitReader(const Array<uint8_t> &bytes)
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

Mode ReadMode(RelBitReader *input) {
  if (!input->ReadBit()) return Mode::kRepeat;
  if (!input->ReadBit()) return Mode::kSameSignResidual;
  if (!input->ReadBit()) return Mode::kChangedSignResidual;
  if (!input->ReadBit()) return Mode::kChangedSignZeroResidual;
  return !input->ReadBit() ? Mode::kZero : Mode::kRaw;
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

uint32_t EstimateRiceParameter(uint64_t magnitude_sum, uint64_t sample_count) {
  if (magnitude_sum == 0 || sample_count == 0) return 0;
  const uint32_t magnitude_log = FloorLog2NonZero(magnitude_sum);
  const uint32_t sample_log = FloorLog2NonZero(sample_count);
  return magnitude_log > sample_log ? magnitude_log - sample_log : 0;
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

inline uint64_t DecodeGammaFast(RelBitReader *input) {
  const uint32_t zeros = static_cast<uint32_t>(input->ReadUnaryZeros(64));
  if (zeros == 0) return 1;
  return (1ULL << zeros) | input->ReadLong(zeros);
}

inline uint64_t DecodeDeltaFast(RelBitReader *input) {
  const uint64_t value_bits = DecodeGammaFast(input);
  if (value_bits == 0 || value_bits > 64) throw std::runtime_error("Invalid Elias delta code");
  if (value_bits == 1) return 1;
  return (1ULL << (value_bits - 1)) | input->ReadLong(value_bits - 1);
}

inline uint64_t DecodeCappedRiceFast(uint32_t parameter, RelBitReader *input) {
  const uint64_t quotient = input->ReadUnaryZerosOrCap(AdaptiveQtCodec::kBoundedRiceQuotientCap);
  if (quotient == AdaptiveQtCodec::kBoundedRiceQuotientCap) return DecodeDeltaFast(input);
  const uint64_t remainder = parameter == 0 ? 0 : input->ReadLong(parameter);
  return ((quotient << parameter) | remainder) + 1;
}

inline uint64_t DecodeCappedRiceNonnegativeFast(uint32_t parameter,
                                                RelBitReader *input) {
  const uint64_t quotient =
      input->ReadUnaryZerosOrCap(AdaptiveQtCodec::kBoundedRiceQuotientCap);
  if (quotient == AdaptiveQtCodec::kBoundedRiceQuotientCap) {
    return DecodeDeltaFast(input) - 1;
  }
  const uint64_t remainder = parameter == 0 ? 0 : input->ReadLong(parameter);
  return (quotient << parameter) | remainder;
}

}  // namespace

std::vector<double> EchosRelDecompressor::Decompress(const Array<uint8_t> &bytes) {
  RelBitReader input(bytes);
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

  std::vector<double> result(block_size_);
  double previous_log = previous_log_;
  double previous_value = previous_value_;
  bool previous_sign = previous_sign_;
  uint64_t magnitude_sum = adaptive_magnitude_sum_;
  uint64_t sample_count = adaptive_sample_count_;
  const double log_step = 2 * log_max_diff_;
  for (int index = 0; index < block_size_; ++index) {
    if (explicit_flags_) {
      const bool sign = input.ReadBit();
      const bool zero = input.ReadBit();
      const bool raw = input.ReadBit();
      if (zero) {
        if (raw) throw std::runtime_error("Invalid explicit ECHOS flags");
        result[index] = Double::LongBitsToDouble(
            static_cast<uint64_t>(sign) << 63);
        continue;
      }
      if (raw) {
        const uint64_t bits =
            (static_cast<uint64_t>(sign) << 63) | input.ReadLong(63);
        const double value = Double::LongBitsToDouble(bits);
        result[index] = value;
        if (std::isfinite(value)) {
          previous_log = std::log(std::abs(value));
          previous_value = value;
          previous_sign = sign;
        }
        continue;
      }

      const uint32_t rice_parameter =
          EstimateRiceParameter(magnitude_sum, sample_count);
      const uint64_t residual =
          DecodeCappedRiceNonnegativeFast(rice_parameter, &input);
      if (residual == std::numeric_limits<uint64_t>::max()) {
        throw std::runtime_error("Invalid explicit ECHOS residual");
      }
      const int64_t q = AdaptiveQtCodec::ZigZagDecode(residual);
      previous_log += log_step * static_cast<double>(q);
      const double magnitude = std::exp(previous_log);
      previous_sign = sign;
      previous_value = sign ? -magnitude : magnitude;
      result[index] = previous_value;
      UpdateRiceParameterState(residual + 1, &magnitude_sum, &sample_count);
      continue;
    }
    const Mode mode = ReadMode(&input);
    if (mode == Mode::kRaw) {
      const double value = Double::LongBitsToDouble(input.ReadLong(64));
      result[index] = value;
      if (std::isfinite(value) && value != 0) {
        previous_log = std::log(std::abs(value));
        previous_value = value;
        previous_sign = std::signbit(value);
      }
      continue;
    }
    if (mode == Mode::kZero) {
      result[index] = 0.0;
      continue;
    }
    if (mode == Mode::kRepeat) {
      result[index] = previous_value;
      continue;
    }
    if (mode == Mode::kChangedSignZeroResidual) {
      previous_sign = !previous_sign;
      previous_value = -previous_value;
      result[index] = previous_value;
      continue;
    }

    const uint32_t rice_parameter = EstimateRiceParameter(magnitude_sum, sample_count);
    const uint64_t mapped = DecodeCappedRiceFast(rice_parameter, &input);
    if (mapped == std::numeric_limits<uint64_t>::max()) {
      throw std::runtime_error("Invalid ECHOS relative residual");
    }

    if (mode == Mode::kChangedSignResidual) previous_sign = !previous_sign;
    const int64_t q = AdaptiveQtCodec::ZigZagDecode(mapped);
    previous_log += log_step * static_cast<double>(q);
    const double magnitude = std::exp(previous_log);
    previous_value = previous_sign ? -magnitude : magnitude;
    result[index] = previous_value;
    UpdateRiceParameterState(mapped, &magnitude_sum, &sample_count);
  }
  previous_log_ = previous_log;
  previous_value_ = previous_value;
  previous_sign_ = previous_sign;
  adaptive_magnitude_sum_ = magnitude_sum;
  adaptive_sample_count_ = sample_count;
  return result;
}
