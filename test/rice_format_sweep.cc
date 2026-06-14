#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "Perf_expr_config.hpp"
#include "Perf_file_utils.hpp"
#include "utils/adaptive_qt_codec.h"

namespace {

enum class Strategy {
  kDual,
  kLegacy,
  kBounded1,
  kBounded2,
  kBounded4,
  kBounded8,
  kBounded10,
  kBounded12,
  kBounded14,
  kBounded15,
  kBounded16,
  kBounded17,
  kBounded18,
  kBounded20,
  kBounded24,
  kBounded28,
  kBounded32,
  kBounded64,
  kBounded128
};

const char *Name(Strategy strategy) {
  switch (strategy) {
    case Strategy::kDual:
      return "DualLegacyBounded32";
    case Strategy::kLegacy:
      return "LegacyOnly";
    case Strategy::kBounded1:
      return "Bounded1Only";
    case Strategy::kBounded2:
      return "Bounded2Only";
    case Strategy::kBounded4:
      return "Bounded4Only";
    case Strategy::kBounded8:
      return "Bounded8Only";
    case Strategy::kBounded10:
      return "Bounded10Only";
    case Strategy::kBounded12:
      return "Bounded12Only";
    case Strategy::kBounded14:
      return "Bounded14Only";
    case Strategy::kBounded15:
      return "Bounded15Only";
    case Strategy::kBounded16:
      return "Bounded16Only";
    case Strategy::kBounded17:
      return "Bounded17Only";
    case Strategy::kBounded18:
      return "Bounded18Only";
    case Strategy::kBounded20:
      return "Bounded20Only";
    case Strategy::kBounded24:
      return "Bounded24Only";
    case Strategy::kBounded28:
      return "Bounded28Only";
    case Strategy::kBounded32:
      return "Bounded32Only";
    case Strategy::kBounded64:
      return "Bounded64Only";
    case Strategy::kBounded128:
      return "Bounded128Only";
  }
  return "Unknown";
}

uint32_t QuotientCap(Strategy strategy) {
  switch (strategy) {
    case Strategy::kBounded1:
      return 1;
    case Strategy::kBounded2:
      return 2;
    case Strategy::kBounded4:
      return 4;
    case Strategy::kBounded8:
      return 8;
    case Strategy::kBounded10:
      return 10;
    case Strategy::kBounded12:
      return 12;
    case Strategy::kBounded14:
      return 14;
    case Strategy::kBounded15:
      return 15;
    case Strategy::kBounded16:
      return 16;
    case Strategy::kBounded17:
      return 17;
    case Strategy::kBounded18:
      return 18;
    case Strategy::kBounded20:
      return 20;
    case Strategy::kBounded24:
      return 24;
    case Strategy::kBounded28:
      return 28;
    case Strategy::kBounded32:
    case Strategy::kDual:
      return 32;
    case Strategy::kBounded64:
      return 64;
    case Strategy::kBounded128:
      return 128;
    case Strategy::kLegacy:
      return 0;
  }
  return 0;
}

enum class Codec { kGamma, kDelta, kLegacy, kBounded };

struct State {
  uint64_t delta_cost = 0;
  uint64_t gamma_cost = 0;
  uint64_t legacy_cost = 0;
  uint64_t bounded_cost = 0;
  uint64_t magnitude_sum = 0;
  uint64_t sample_count = 0;
};

struct Choice {
  Codec codec = Codec::kDelta;
  uint32_t rice_parameter = 0;
};

struct Result {
  uint64_t values = 0;
  uint64_t bits = 0;
  uint64_t gamma_values = 0;
  uint64_t delta_values = 0;
  uint64_t legacy_values = 0;
  uint64_t bounded_values = 0;
  uint64_t legacy_fallbacks = 0;
  uint64_t bounded_fallbacks = 0;
  uint64_t raw_values = 0;
};

uint32_t EstimateParameter(const State &state) {
  if (state.magnitude_sum == 0 || state.sample_count == 0) return 0;
  const uint32_t magnitude_log = AdaptiveQtCodec::FloorLog2(state.magnitude_sum);
  const uint32_t sample_log = AdaptiveQtCodec::FloorLog2(state.sample_count);
  return magnitude_log > sample_log ? magnitude_log - sample_log : 0;
}

Choice Select(const State &state, Strategy strategy) {
  Choice choice;
  choice.rice_parameter = EstimateParameter(state);
  uint64_t cost = state.delta_cost;
  if (state.gamma_cost < cost) {
    choice.codec = Codec::kGamma;
    cost = state.gamma_cost;
  }
  if (strategy == Strategy::kLegacy || strategy == Strategy::kDual) {
    if (state.legacy_cost < cost) {
      choice.codec = Codec::kLegacy;
      cost = state.legacy_cost;
    }
  }
  if (strategy != Strategy::kLegacy && state.bounded_cost < cost) {
    choice.codec = Codec::kBounded;
  }
  return choice;
}

uint64_t LegacyLength(uint64_t mapped, uint32_t parameter, bool *fallback) {
  const uint64_t direct = 1 + AdaptiveQtCodec::RiceLength(mapped - 1, parameter);
  const uint64_t delta = 2 + AdaptiveQtCodec::DeltaLength(mapped);
  *fallback = delta < direct;
  return std::min(direct, delta);
}

uint64_t BoundedLength(uint64_t mapped, uint32_t parameter, uint32_t cap, bool *fallback) {
  const uint64_t quotient = (mapped - 1) >> parameter;
  *fallback = quotient >= cap;
  return *fallback ? cap + 1 + AdaptiveQtCodec::DeltaLength(mapped)
                   : AdaptiveQtCodec::RiceLength(mapped - 1, parameter);
}

void Update(uint64_t mapped, uint32_t parameter, Strategy strategy, State *state) {
  bool fallback = false;
  state->delta_cost =
      AdaptiveQtCodec::DecayAndAdd(state->delta_cost, AdaptiveQtCodec::DeltaLength(mapped));
  state->gamma_cost =
      AdaptiveQtCodec::DecayAndAdd(state->gamma_cost, AdaptiveQtCodec::GammaLength(mapped));
  if (strategy == Strategy::kLegacy || strategy == Strategy::kDual) {
    state->legacy_cost =
        AdaptiveQtCodec::DecayAndAdd(state->legacy_cost, LegacyLength(mapped, parameter, &fallback));
  }
  if (strategy != Strategy::kLegacy) {
    state->bounded_cost = AdaptiveQtCodec::DecayAndAdd(
        state->bounded_cost, BoundedLength(mapped, parameter, QuotientCap(strategy), &fallback));
  }
  state->magnitude_sum = AdaptiveQtCodec::DecayAndAdd(
      state->magnitude_sum, std::min(mapped - 1, AdaptiveQtCodec::kAdaptiveRiceMagnitudeCap));
  state->sample_count = AdaptiveQtCodec::DecayAndAdd(state->sample_count, 1);
}

bool Quantize(double value, double prediction, double max_diff, double step, double inverse_step,
              int64_t *q, double *recovered) {
  if (!std::isfinite(value) || !std::isfinite(prediction)) return false;
  const double scaled = (value - prediction) * inverse_step;
  if (std::isfinite(scaled) && std::abs(scaled) <= 0x1p52) {
    *q = static_cast<int64_t>(std::round(scaled));
    if (AdaptiveQtCodec::ZigZagEncode(*q) < std::numeric_limits<uint64_t>::max() - 1) {
      *recovered = prediction + step * static_cast<double>(*q);
      if (std::isfinite(*recovered) && std::abs(value - *recovered) <= max_diff) return true;
    }
  }
  const long double precise_scaled =
      (static_cast<long double>(value) - static_cast<long double>(prediction)) /
      (2.0L * static_cast<long double>(max_diff));
  if (!std::isfinite(precise_scaled)) return false;
  const long double rounded = std::round(precise_scaled);
  if (rounded <= static_cast<long double>(std::numeric_limits<int64_t>::min()) ||
      rounded > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
    return false;
  }
  *q = static_cast<int64_t>(rounded);
  if (AdaptiveQtCodec::ZigZagEncode(*q) >= std::numeric_limits<uint64_t>::max() - 1) return false;
  *recovered = prediction + step * static_cast<double>(*q);
  return std::isfinite(*recovered) && std::abs(value - *recovered) <= max_diff;
}

Result Simulate(const std::filesystem::path &path, double requested_max_diff, Strategy strategy) {
  std::ifstream input(path);
  if (!input.is_open()) throw std::runtime_error("Failed to open " + path.string());
  constexpr uint64_t kEscape = std::numeric_limits<uint64_t>::max();
  const double max_diff = requested_max_diff * 0.999;
  const double step = 2 * max_diff;
  const double inverse_step = 1.0 / step;
  State state;
  double previous = 2;
  bool first_block = true;
  Result result;
  std::vector<double> block;
  while ((block = ReadBlock(input, kBlockSizeOverall)).size() == kBlockSizeOverall) {
    result.bits += first_block ? 82 : 2;
    first_block = false;
    for (double value : block) {
      const Choice choice = Select(state, strategy);
      int64_t q = 0;
      double recovered = 0;
      if (Quantize(value, previous, max_diff, step, inverse_step, &q, &recovered)) {
        const uint64_t mapped = AdaptiveQtCodec::ZigZagEncode(q) + 1;
        bool fallback = false;
        switch (choice.codec) {
          case Codec::kGamma:
            result.bits += AdaptiveQtCodec::GammaLength(mapped);
            ++result.gamma_values;
            break;
          case Codec::kDelta:
            result.bits += AdaptiveQtCodec::DeltaLength(mapped);
            ++result.delta_values;
            break;
          case Codec::kLegacy:
            result.bits += LegacyLength(mapped, choice.rice_parameter, &fallback);
            ++result.legacy_values;
            result.legacy_fallbacks += fallback;
            break;
          case Codec::kBounded:
            result.bits +=
                BoundedLength(mapped, choice.rice_parameter, QuotientCap(strategy), &fallback);
            ++result.bounded_values;
            result.bounded_fallbacks += fallback;
            break;
        }
        Update(mapped, choice.rice_parameter, strategy, &state);
        previous = recovered;
      } else {
        switch (choice.codec) {
          case Codec::kGamma:
            result.bits += AdaptiveQtCodec::GammaLength(kEscape) + 64;
            break;
          case Codec::kDelta:
            result.bits += AdaptiveQtCodec::DeltaLength(kEscape) + 64;
            break;
          case Codec::kLegacy:
            result.bits += 66;
            break;
          case Codec::kBounded:
            result.bits += QuotientCap(strategy) + 65;
            break;
        }
        ++result.raw_values;
        previous = std::isfinite(value) ? value : 2;
      }
      ++result.values;
    }
  }
  return result;
}

}  // namespace

int main(int argc, char **argv) {
  const std::filesystem::path dataset_dir =
      argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("test/data_set");
  std::cout << std::setprecision(12);
  std::cout << "Strategy,Error,Dataset,Values,Bits,CompressionRatio,GammaValues,DeltaValues,"
               "LegacyValues,BoundedValues,LegacyFallbacks,BoundedFallbacks,RawValues\n";
  for (Strategy strategy :
       {Strategy::kDual, Strategy::kLegacy, Strategy::kBounded1, Strategy::kBounded2,
        Strategy::kBounded4, Strategy::kBounded8, Strategy::kBounded10, Strategy::kBounded12,
        Strategy::kBounded14, Strategy::kBounded15, Strategy::kBounded16, Strategy::kBounded17,
        Strategy::kBounded18, Strategy::kBounded20, Strategy::kBounded24, Strategy::kBounded28,
        Strategy::kBounded32, Strategy::kBounded64, Strategy::kBounded128}) {
    for (double error : kMaxDiffList) {
      for (const std::string &dataset : kDataSetList) {
        const Result result = Simulate(dataset_dir / dataset, error, strategy);
        std::cout << Name(strategy) << ',' << error << ',' << dataset << ',' << result.values << ','
                  << result.bits << ','
                  << static_cast<double>(result.bits) / (64.0 * result.values) << ','
                  << result.gamma_values << ',' << result.delta_values << ','
                  << result.legacy_values << ',' << result.bounded_values << ','
                  << result.legacy_fallbacks << ',' << result.bounded_fallbacks << ','
                  << result.raw_values << '\n';
      }
    }
  }
}
