#ifndef SERF_ADAPTIVE_QT_CODEC_H_
#define SERF_ADAPTIVE_QT_CODEC_H_

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace AdaptiveQtCodec {

enum class IntegerCodec : uint32_t {
  kGamma = 0,
  kDelta = 1,
  kRice = 2,
  kRaw = 3
};

constexpr uint32_t kAdaptiveRiceDecayShift = 3;
constexpr uint32_t kBoundedRiceQuotientCap = 16;
constexpr uint32_t kAdaptiveRiceFormatQuotientCap = 32;
constexpr uint32_t kAdaptiveRiceMagnitudeBits = 20;
constexpr uint64_t kAdaptiveRiceMagnitudeCap = 1ULL << kAdaptiveRiceMagnitudeBits;

struct AdaptiveRiceChoice {
  IntegerCodec codec = IntegerCodec::kDelta;
  uint32_t rice_parameter = 0;
  bool capped_rice = false;
};

struct AdaptiveRiceState {
  uint64_t delta_cost = 0;
  uint64_t gamma_cost = 0;
  uint64_t rice_cost = 0;
  uint64_t capped_rice_cost = 0;
  uint64_t magnitude_sum = 0;
  uint64_t sample_count = 0;
};

struct AdaptiveBoundedRiceState {
  uint64_t delta_cost = 0;
  uint64_t gamma_cost = 0;
  uint64_t rice_cost = 0;
  uint64_t magnitude_sum = 0;
  uint64_t sample_count = 0;
};

struct AdaptiveDeltaRiceState {
  uint64_t delta_cost = 0;
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

struct AdaptiveDeltaRiceCodeLengths {
  uint64_t delta;
  uint64_t rice;
};

struct AdaptiveRiceFormatLengths {
  AdaptiveCodeLengths legacy;
  uint64_t capped_rice;
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
  const uint32_t magnitude_log = FloorLog2(state.magnitude_sum);
  const uint32_t sample_log = FloorLog2(state.sample_count);
  return magnitude_log > sample_log ? magnitude_log - sample_log : 0;
}

inline uint32_t EstimateRiceParameter(const AdaptiveBoundedRiceState &state) {
  if (state.magnitude_sum == 0 || state.sample_count == 0) return 0;
  const uint32_t magnitude_log = FloorLog2(state.magnitude_sum);
  const uint32_t sample_log = FloorLog2(state.sample_count);
  return magnitude_log > sample_log ? magnitude_log - sample_log : 0;
}

inline uint32_t EstimateRiceParameter(const AdaptiveDeltaRiceState &state) {
  if (state.magnitude_sum == 0 || state.sample_count == 0) return 0;
  const uint32_t magnitude_log = FloorLog2(state.magnitude_sum);
  const uint32_t sample_log = FloorLog2(state.sample_count);
  return magnitude_log > sample_log ? magnitude_log - sample_log : 0;
}

inline uint64_t AdaptiveRiceLength(uint64_t mapped, uint32_t rice_parameter) {
  // Prefix 0 carries Rice; prefix 10 falls back to Delta for an unexpectedly large residual.
  return std::min(1 + RiceLength(mapped - 1, rice_parameter), 2 + DeltaLength(mapped));
}

inline uint64_t CappedRiceLength(uint64_t mapped, uint32_t rice_parameter, uint32_t quotient_cap) {
  const uint64_t quotient = (mapped - 1) >> rice_parameter;
  return quotient < quotient_cap ? RiceLength(mapped - 1, rice_parameter)
                                 : quotient_cap + DeltaLength(mapped);
}

inline uint64_t CappedRiceWithRawLength(uint64_t mapped, uint32_t rice_parameter,
                                       uint32_t quotient_cap) {
  const uint64_t quotient = (mapped - 1) >> rice_parameter;
  return quotient < quotient_cap ? RiceLength(mapped - 1, rice_parameter)
                                 : quotient_cap + 1 + DeltaLength(mapped);
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

inline AdaptiveCodeLengths CalculateCappedRiceCodeLengths(uint64_t mapped, uint32_t rice_parameter,
                                                          uint32_t quotient_cap) {
  const uint64_t mapped_log = FloorLog2(mapped);
  const uint64_t value_bits = mapped_log + 1;
  const uint64_t gamma_length = 2 * mapped_log + 1;
  const uint64_t delta_length = GammaLength(value_bits) + value_bits - 1;
  const uint64_t quotient = (mapped - 1) >> rice_parameter;
  const uint64_t rice_direct_length = quotient + rice_parameter + 1;
  return {gamma_length, delta_length, rice_direct_length,
          quotient < quotient_cap ? rice_direct_length : quotient_cap + delta_length};
}

inline AdaptiveCodeLengths CalculateCappedRiceWithRawCodeLengths(uint64_t mapped,
                                                                 uint32_t rice_parameter,
                                                                 uint32_t quotient_cap) {
  const uint64_t mapped_log = FloorLog2(mapped);
  const uint64_t value_bits = mapped_log + 1;
  const uint64_t gamma_length = 2 * mapped_log + 1;
  const uint64_t delta_length = GammaLength(value_bits) + value_bits - 1;
  const uint64_t quotient = (mapped - 1) >> rice_parameter;
  const uint64_t rice_direct_length = quotient + rice_parameter + 1;
  return {gamma_length, delta_length, rice_direct_length,
          quotient < quotient_cap ? rice_direct_length : quotient_cap + 1 + delta_length};
}

inline AdaptiveDeltaRiceCodeLengths CalculateCappedDeltaRiceCodeLengths(
    uint64_t mapped, uint32_t rice_parameter, uint32_t quotient_cap) {
  const uint64_t value_bits = FloorLog2(mapped) + 1;
  const uint64_t delta_length = GammaLength(value_bits) + value_bits - 1;
  const uint64_t quotient = (mapped - 1) >> rice_parameter;
  const uint64_t rice_direct_length = quotient + rice_parameter + 1;
  return {delta_length,
          quotient < quotient_cap ? rice_direct_length : quotient_cap + delta_length};
}

inline AdaptiveDeltaRiceCodeLengths CalculateCappedDeltaRiceWithRawCodeLengths(
    uint64_t mapped, uint32_t rice_parameter, uint32_t quotient_cap) {
  const uint64_t value_bits = FloorLog2(mapped) + 1;
  const uint64_t delta_length = GammaLength(value_bits) + value_bits - 1;
  const uint64_t quotient = (mapped - 1) >> rice_parameter;
  const uint64_t rice_direct_length = quotient + rice_parameter + 1;
  return {delta_length,
          quotient < quotient_cap ? rice_direct_length : quotient_cap + 1 + delta_length};
}

inline AdaptiveRiceFormatLengths CalculateAdaptiveRiceFormatLengths(uint64_t mapped,
                                                                    uint32_t rice_parameter) {
  const uint64_t mapped_log = FloorLog2(mapped);
  const uint64_t value_bits = mapped_log + 1;
  const uint64_t gamma_length = 2 * mapped_log + 1;
  const uint64_t delta_length = GammaLength(value_bits) + value_bits - 1;
  const uint64_t quotient = (mapped - 1) >> rice_parameter;
  const uint64_t capped_direct_length =
      SaturatingAdd(quotient, static_cast<uint64_t>(rice_parameter) + 1);
  const uint64_t legacy_direct_length = SaturatingAdd(capped_direct_length, 1);
  return {{gamma_length, delta_length, legacy_direct_length,
           std::min(legacy_direct_length, 2 + delta_length)},
          quotient < kAdaptiveRiceFormatQuotientCap
              ? capped_direct_length
              : kAdaptiveRiceFormatQuotientCap + 1 + delta_length};
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

inline AdaptiveRiceChoice SelectAdaptiveBoundedRiceCodec(const AdaptiveBoundedRiceState &state) {
  AdaptiveRiceChoice choice{IntegerCodec::kDelta, EstimateRiceParameter(state)};
  uint64_t selected_cost = state.delta_cost;
  if (state.gamma_cost < selected_cost) {
    choice.codec = IntegerCodec::kGamma;
    selected_cost = state.gamma_cost;
  }
  if (state.rice_cost < selected_cost) choice.codec = IntegerCodec::kRice;
  return choice;
}

inline AdaptiveRiceChoice SelectAdaptiveDeltaRiceCodec(const AdaptiveDeltaRiceState &state) {
  AdaptiveRiceChoice choice{IntegerCodec::kDelta, EstimateRiceParameter(state)};
  if (state.rice_cost < state.delta_cost) choice.codec = IntegerCodec::kRice;
  return choice;
}

inline AdaptiveRiceChoice SelectAdaptiveRiceCodecAndFormat(const AdaptiveRiceState &state) {
  AdaptiveRiceChoice choice{IntegerCodec::kDelta, EstimateRiceParameter(state), false};
  uint64_t selected_cost = state.delta_cost;
  if (state.gamma_cost < selected_cost) {
    choice.codec = IntegerCodec::kGamma;
    selected_cost = state.gamma_cost;
  }
  if (state.rice_cost < selected_cost) {
    choice.codec = IntegerCodec::kRice;
    selected_cost = state.rice_cost;
  }
  if (state.capped_rice_cost < selected_cost) {
    choice.codec = IntegerCodec::kRice;
    choice.capped_rice = true;
  }
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

inline void UpdateAdaptiveBoundedRiceState(uint64_t mapped, const AdaptiveCodeLengths &lengths,
                                           AdaptiveBoundedRiceState *state) {
  state->delta_cost = DecayAndAdd(state->delta_cost, lengths.delta);
  state->gamma_cost = DecayAndAdd(state->gamma_cost, lengths.gamma);
  state->rice_cost = DecayAndAdd(state->rice_cost, lengths.rice);
  state->magnitude_sum =
      DecayAndAdd(state->magnitude_sum, std::min(mapped - 1, kAdaptiveRiceMagnitudeCap));
  state->sample_count = DecayAndAdd(state->sample_count, 1);
}

inline void UpdateAdaptiveDeltaRiceState(uint64_t mapped,
                                         const AdaptiveDeltaRiceCodeLengths &lengths,
                                         AdaptiveDeltaRiceState *state) {
  state->delta_cost = DecayAndAdd(state->delta_cost, lengths.delta);
  state->rice_cost = DecayAndAdd(state->rice_cost, lengths.rice);
  state->magnitude_sum =
      DecayAndAdd(state->magnitude_sum, std::min(mapped - 1, kAdaptiveRiceMagnitudeCap));
  state->sample_count = DecayAndAdd(state->sample_count, 1);
}

inline void UpdateAdaptiveRiceFormatState(uint64_t mapped,
                                          const AdaptiveRiceFormatLengths &lengths,
                                          AdaptiveRiceState *state) {
  state->delta_cost = DecayAndAdd(state->delta_cost, lengths.legacy.delta);
  state->gamma_cost = DecayAndAdd(state->gamma_cost, lengths.legacy.gamma);
  state->rice_cost = DecayAndAdd(state->rice_cost, lengths.legacy.rice);
  state->capped_rice_cost = DecayAndAdd(state->capped_rice_cost, lengths.capped_rice);
  state->magnitude_sum =
      DecayAndAdd(state->magnitude_sum, std::min(mapped - 1, kAdaptiveRiceMagnitudeCap));
  state->sample_count = DecayAndAdd(state->sample_count, 1);
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

}  // namespace AdaptiveQtCodec

#endif  // SERF_ADAPTIVE_QT_CODEC_H_
