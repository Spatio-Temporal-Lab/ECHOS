#ifndef SERF_ADAPTIVE_QT_CODEC_H_
#define SERF_ADAPTIVE_QT_CODEC_H_

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "input_bit_stream.h"
#include "output_bit_stream.h"

namespace AdaptiveQtCodec {

enum class IntegerCodec : uint32_t {
  kGamma = 0,
  kDelta = 1,
  kRice = 2,
  kRaw = 3
};

constexpr uint32_t kAdaptiveRiceDecayShift = 4;
constexpr uint32_t kAdaptiveRiceMaxParameter = 12;
constexpr uint64_t kAdaptiveRiceMagnitudeCap = 1ULL << 20;

struct AdaptiveRiceChoice {
  IntegerCodec codec = IntegerCodec::kDelta;
  uint32_t rice_parameter = 0;
};

struct AdaptiveRiceState {
  uint64_t delta_cost = 0;
  uint64_t gamma_cost = 0;
  uint64_t rice_cost = 0;
  uint64_t magnitude_sum = 0;
  uint64_t sample_count = 0;
};

struct AdaptiveCodeLengths {
  uint64_t gamma;
  uint64_t delta;
  uint64_t rice_direct;
  uint64_t rice;
};

inline uint64_t ZigZagEncode(int64_t value) {
  return (static_cast<uint64_t>(value) << 1) ^ static_cast<uint64_t>(-(value < 0));
}

inline int64_t ZigZagDecode(uint64_t value) {
  const uint64_t sign_mask = 0 - (value & 1);
  return static_cast<int64_t>((value >> 1) ^ sign_mask);
}

inline uint32_t FloorLog2(uint64_t value) {
  if (value == 0) throw std::invalid_argument("FloorLog2 requires a positive value");
#if defined(__GNUC__) || defined(__clang__)
  return 63U - static_cast<uint32_t>(__builtin_clzll(value));
#else
  uint32_t result = 0;
  while (value >>= 1) ++result;
  return result;
#endif
}

inline uint64_t SaturatingAdd(uint64_t lhs, uint64_t rhs) {
  if (rhs > std::numeric_limits<uint64_t>::max() - lhs) {
    return std::numeric_limits<uint64_t>::max();
  }
  return lhs + rhs;
}

inline uint64_t GammaLength(uint64_t positive) {
  const uint64_t log = FloorLog2(positive);
  return 2 * log + 1;
}

inline uint64_t DeltaLength(uint64_t positive) {
  const uint64_t value_bits = FloorLog2(positive) + 1;
  return GammaLength(value_bits) + value_bits - 1;
}

inline uint64_t RiceLength(uint64_t value, uint32_t parameter) {
  const uint64_t quotient = value >> parameter;
  return SaturatingAdd(quotient, static_cast<uint64_t>(parameter) + 1);
}

inline uint64_t DecayAndAdd(uint64_t cost, uint64_t length) {
  return SaturatingAdd(cost - (cost >> kAdaptiveRiceDecayShift), length);
}

inline uint32_t EstimateRiceParameter(const AdaptiveRiceState &state) {
  if (state.magnitude_sum == 0 || state.sample_count == 0) return 0;
  const int estimate =
      static_cast<int>(FloorLog2(state.magnitude_sum)) - static_cast<int>(FloorLog2(state.sample_count));
  return static_cast<uint32_t>(std::clamp(estimate, 0, static_cast<int>(kAdaptiveRiceMaxParameter)));
}

inline uint64_t AdaptiveRiceLength(uint64_t mapped, uint32_t rice_parameter) {
  // Prefix 0 carries Rice; prefix 10 falls back to Delta for an unexpectedly large residual.
  return std::min(1 + RiceLength(mapped - 1, rice_parameter), 2 + DeltaLength(mapped));
}

inline AdaptiveCodeLengths CalculateAdaptiveCodeLengths(uint64_t mapped, uint32_t rice_parameter) {
  const uint64_t mapped_log = FloorLog2(mapped);
  const uint64_t value_bits = mapped_log + 1;
  const uint64_t gamma_length = 2 * mapped_log + 1;
  const uint64_t delta_length = GammaLength(value_bits) + value_bits - 1;
  const uint64_t rice_direct_length = 1 + RiceLength(mapped - 1, rice_parameter);
  return {gamma_length, delta_length, rice_direct_length,
          std::min(rice_direct_length, 2 + delta_length)};
}

inline bool ShouldUseRiceDeltaFallback(const AdaptiveCodeLengths &lengths) {
  return 2 + lengths.delta < lengths.rice_direct;
}

inline bool ShouldUseRiceDeltaFallback(uint64_t mapped, uint32_t rice_parameter) {
  return ShouldUseRiceDeltaFallback(CalculateAdaptiveCodeLengths(mapped, rice_parameter));
}

inline AdaptiveRiceChoice SelectAdaptiveRiceCodec(const AdaptiveRiceState &state) {
  AdaptiveRiceChoice choice{IntegerCodec::kDelta, EstimateRiceParameter(state)};
  uint64_t selected_cost = state.delta_cost;
  if (state.gamma_cost < selected_cost) {
    choice.codec = IntegerCodec::kGamma;
    selected_cost = state.gamma_cost;
  }
  if (state.rice_cost < selected_cost) choice.codec = IntegerCodec::kRice;
  return choice;
}

inline void UpdateAdaptiveRiceState(uint64_t mapped, const AdaptiveCodeLengths &lengths,
                                    AdaptiveRiceState *state) {
  state->delta_cost = DecayAndAdd(state->delta_cost, lengths.delta);
  state->gamma_cost = DecayAndAdd(state->gamma_cost, lengths.gamma);
  state->rice_cost = DecayAndAdd(state->rice_cost, lengths.rice);
  state->magnitude_sum =
      DecayAndAdd(state->magnitude_sum, std::min(mapped - 1, kAdaptiveRiceMagnitudeCap));
  state->sample_count = DecayAndAdd(state->sample_count, 1);
}

inline void UpdateAdaptiveRiceState(uint64_t mapped, uint32_t rice_parameter, AdaptiveRiceState *state) {
  UpdateAdaptiveRiceState(mapped, CalculateAdaptiveCodeLengths(mapped, rice_parameter), state);
}

inline void WriteZeros(uint64_t count, OutputBitStream *output) {
  while (count >= 32) {
    output->WriteInt(0, 32);
    count -= 32;
  }
  if (count > 0) output->WriteInt(0, static_cast<uint32_t>(count));
}

inline uint64_t EncodeGamma(uint64_t positive, OutputBitStream *output) {
  const uint32_t log = FloorLog2(positive);
  WriteZeros(log, output);
  output->WriteLong(positive, log + 1);
  return 2ULL * log + 1;
}

inline uint64_t DecodeGamma(InputBitStream *input) {
  const uint32_t zeros = static_cast<uint32_t>(input->ReadUnaryZeros(64));
  if (zeros == 0) return 1;
  return (1ULL << zeros) | input->ReadLong(zeros);
}

inline uint64_t EncodeDelta(uint64_t positive, OutputBitStream *output) {
  const uint32_t value_bits = FloorLog2(positive) + 1;
  uint64_t written = EncodeGamma(value_bits, output);
  if (value_bits > 1) written += output->WriteLong(positive, value_bits - 1);
  return written;
}

inline uint64_t DecodeDelta(InputBitStream *input) {
  const uint64_t value_bits = DecodeGamma(input);
  if (value_bits == 0 || value_bits > 64) throw std::runtime_error("Invalid Elias delta code");
  if (value_bits == 1) return 1;
  return (1ULL << (value_bits - 1)) | input->ReadLong(value_bits - 1);
}

inline uint64_t EncodeRice(uint64_t value, uint32_t parameter, OutputBitStream *output) {
  const uint64_t quotient = value >> parameter;
  WriteZeros(quotient, output);
  output->WriteBit(true);
  if (parameter > 0) output->WriteLong(value, parameter);
  return quotient + 1 + parameter;
}

inline uint64_t DecodeRice(uint32_t parameter, InputBitStream *input) {
  const uint64_t quotient =
      input->ReadUnaryZeros(std::numeric_limits<uint64_t>::max() >> parameter);
  const uint64_t remainder = parameter == 0 ? 0 : input->ReadLong(parameter);
  return (quotient << parameter) | remainder;
}

inline uint64_t EncodedLength(uint64_t mapped, IntegerCodec codec, uint32_t rice_parameter) {
  switch (codec) {
    case IntegerCodec::kGamma:
      return GammaLength(mapped);
    case IntegerCodec::kDelta:
      return DeltaLength(mapped);
    case IntegerCodec::kRice:
      return RiceLength(mapped - 1, rice_parameter);
    case IntegerCodec::kRaw:
      return 64;
  }
  throw std::runtime_error("Unknown integer codec");
}

inline uint64_t EncodeMapped(uint64_t mapped, IntegerCodec codec, uint32_t rice_parameter,
                             OutputBitStream *output) {
  switch (codec) {
    case IntegerCodec::kGamma:
      return EncodeGamma(mapped, output);
    case IntegerCodec::kDelta:
      return EncodeDelta(mapped, output);
    case IntegerCodec::kRice:
      return EncodeRice(mapped - 1, rice_parameter, output);
    case IntegerCodec::kRaw:
      break;
  }
  throw std::runtime_error("Raw values are not integer-coded");
}

inline uint64_t DecodeMapped(IntegerCodec codec, uint32_t rice_parameter, InputBitStream *input) {
  switch (codec) {
    case IntegerCodec::kGamma:
      return DecodeGamma(input);
    case IntegerCodec::kDelta:
      return DecodeDelta(input);
    case IntegerCodec::kRice:
      return DecodeRice(rice_parameter, input) + 1;
    case IntegerCodec::kRaw:
      break;
  }
  throw std::runtime_error("Raw values are not integer-coded");
}

}  // namespace AdaptiveQtCodec

#endif  // SERF_ADAPTIVE_QT_CODEC_H_
