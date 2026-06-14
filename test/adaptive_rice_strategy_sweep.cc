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

constexpr uint32_t kMaxK = AdaptiveQtCodec::kAdaptiveRiceMagnitudeBits;

enum class Strategy {
  kCurrent,
  kCurrentMinusOne,
  kExactMean,
  kExactMeanMinusOne,
  kRiceBank,
  kCappedRice1,
  kCappedRice2,
  kCappedRice4,
  kCappedRice8,
  kCappedRice16,
  kCappedRice32,
  kCappedRice64,
  kCappedRice128
};

const char *StrategyName(Strategy strategy) {
  switch (strategy) {
    case Strategy::kCurrent:
      return "Current";
    case Strategy::kCurrentMinusOne:
      return "CurrentMinusOne";
    case Strategy::kExactMean:
      return "ExactMean";
    case Strategy::kExactMeanMinusOne:
      return "ExactMeanMinusOne";
    case Strategy::kRiceBank:
      return "RiceBank";
    case Strategy::kCappedRice1:
      return "CappedRice1";
    case Strategy::kCappedRice2:
      return "CappedRice2";
    case Strategy::kCappedRice4:
      return "CappedRice4";
    case Strategy::kCappedRice8:
      return "CappedRice8";
    case Strategy::kCappedRice16:
      return "CappedRice16";
    case Strategy::kCappedRice32:
      return "CappedRice32";
    case Strategy::kCappedRice64:
      return "CappedRice64";
    case Strategy::kCappedRice128:
      return "CappedRice128";
  }
  return "Unknown";
}

uint32_t RiceCap(Strategy strategy) {
  switch (strategy) {
    case Strategy::kCappedRice1:
      return 1;
    case Strategy::kCappedRice2:
      return 2;
    case Strategy::kCappedRice4:
      return 4;
    case Strategy::kCappedRice8:
      return 8;
    case Strategy::kCappedRice16:
      return 16;
    case Strategy::kCappedRice32:
      return 32;
    case Strategy::kCappedRice64:
      return 64;
    case Strategy::kCappedRice128:
      return 128;
    default:
      return 0;
  }
}

struct State {
  uint64_t delta_cost = 0;
  uint64_t gamma_cost = 0;
  uint64_t rice_cost = 0;
  uint64_t magnitude_sum = 0;
  uint64_t sample_count = 0;
  std::array<uint64_t, kMaxK + 1> rice_costs{};
};

struct Choice {
  AdaptiveQtCodec::IntegerCodec codec = AdaptiveQtCodec::IntegerCodec::kDelta;
  uint32_t rice_parameter = 0;
};

struct Result {
  uint64_t values = 0;
  uint64_t bits = 0;
  uint64_t gamma_values = 0;
  uint64_t delta_values = 0;
  uint64_t rice_values = 0;
  uint64_t raw_values = 0;
  std::array<uint64_t, 8> log_modes{};
};

uint32_t CurrentEstimate(const State &state) {
  if (state.magnitude_sum == 0 || state.sample_count == 0) return 0;
  const uint32_t magnitude_log = AdaptiveQtCodec::FloorLog2(state.magnitude_sum);
  const uint32_t sample_log = AdaptiveQtCodec::FloorLog2(state.sample_count);
  return magnitude_log > sample_log ? magnitude_log - sample_log : 0;
}

uint32_t ExactMeanEstimate(const State &state) {
  if (state.magnitude_sum == 0 || state.sample_count == 0) return 0;
  const uint64_t mean = state.magnitude_sum / state.sample_count;
  return mean == 0 ? 0 : AdaptiveQtCodec::FloorLog2(mean);
}

uint32_t EstimateK(const State &state, Strategy strategy) {
  uint32_t result = strategy == Strategy::kExactMean ||
                            strategy == Strategy::kExactMeanMinusOne
                        ? ExactMeanEstimate(state)
                        : CurrentEstimate(state);
  if ((strategy == Strategy::kCurrentMinusOne ||
       strategy == Strategy::kExactMeanMinusOne) &&
      result > 0) {
    --result;
  }
  return result;
}

Choice Select(const State &state, Strategy strategy) {
  Choice choice;
  uint64_t selected_cost = state.delta_cost;
  if (state.gamma_cost < selected_cost) {
    choice.codec = AdaptiveQtCodec::IntegerCodec::kGamma;
    selected_cost = state.gamma_cost;
  }
  if (strategy == Strategy::kRiceBank) {
    for (uint32_t k = 0; k <= kMaxK; ++k) {
      if (state.rice_costs[k] < selected_cost) {
        choice.codec = AdaptiveQtCodec::IntegerCodec::kRice;
        choice.rice_parameter = k;
        selected_cost = state.rice_costs[k];
      }
    }
  } else {
    choice.rice_parameter = EstimateK(state, strategy);
    if (state.rice_cost < selected_cost) choice.codec = AdaptiveQtCodec::IntegerCodec::kRice;
  }
  return choice;
}

uint64_t RiceLength(uint64_t mapped, const Choice &choice, Strategy strategy,
                    bool needs_raw_escape) {
  const uint32_t cap = RiceCap(strategy);
  if (cap == 0) {
    return AdaptiveQtCodec::AdaptiveRiceLength(mapped, choice.rice_parameter);
  }
  const uint64_t quotient = (mapped - 1) >> choice.rice_parameter;
  if (quotient < cap) {
    return AdaptiveQtCodec::RiceLength(mapped - 1, choice.rice_parameter);
  }
  return cap + (needs_raw_escape ? 1 : 0) + AdaptiveQtCodec::DeltaLength(mapped);
}

void Update(uint64_t mapped, const Choice &choice, Strategy strategy, bool needs_raw_escape,
            State *state) {
  state->delta_cost =
      AdaptiveQtCodec::DecayAndAdd(state->delta_cost, AdaptiveQtCodec::DeltaLength(mapped));
  state->gamma_cost =
      AdaptiveQtCodec::DecayAndAdd(state->gamma_cost, AdaptiveQtCodec::GammaLength(mapped));
  if (strategy == Strategy::kRiceBank) {
    for (uint32_t k = 0; k <= kMaxK; ++k) {
      state->rice_costs[k] = AdaptiveQtCodec::DecayAndAdd(
          state->rice_costs[k], AdaptiveQtCodec::AdaptiveRiceLength(mapped, k));
    }
  } else {
    state->rice_cost = AdaptiveQtCodec::DecayAndAdd(
        state->rice_cost, RiceLength(mapped, choice, strategy, needs_raw_escape));
  }
  state->magnitude_sum = AdaptiveQtCodec::DecayAndAdd(
      state->magnitude_sum, std::min(mapped - 1, AdaptiveQtCodec::kAdaptiveRiceMagnitudeCap));
  state->sample_count = AdaptiveQtCodec::DecayAndAdd(state->sample_count, 1);
}

uint64_t ResidualLength(uint64_t mapped, const Choice &choice, Strategy strategy,
                        bool needs_raw_escape) {
  if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
    return RiceLength(mapped, choice, strategy, needs_raw_escape);
  }
  return AdaptiveQtCodec::EncodedLength(mapped, choice.codec, choice.rice_parameter);
}

void AddResidual(uint64_t mapped, const Choice &choice, Strategy strategy, bool needs_raw_escape,
                 Result *result) {
  result->bits += ResidualLength(mapped, choice, strategy, needs_raw_escape);
  switch (choice.codec) {
    case AdaptiveQtCodec::IntegerCodec::kGamma:
      ++result->gamma_values;
      return;
    case AdaptiveQtCodec::IntegerCodec::kDelta:
      ++result->delta_values;
      return;
    case AdaptiveQtCodec::IntegerCodec::kRice:
      ++result->rice_values;
      return;
    case AdaptiveQtCodec::IntegerCodec::kRaw:
      return;
  }
}

bool Quantize(double value, double prediction, double max_diff, int64_t *q, double *recovered) {
  if (!std::isfinite(value) || !std::isfinite(prediction)) return false;
  const long double step = 2.0L * static_cast<long double>(max_diff);
  const long double scaled =
      (static_cast<long double>(value) - static_cast<long double>(prediction)) / step;
  if (!std::isfinite(scaled)) return false;
  const long double rounded = std::round(scaled);
  if (rounded <= static_cast<long double>(std::numeric_limits<int64_t>::min()) ||
      rounded > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
    return false;
  }
  *q = static_cast<int64_t>(rounded);
  if (AdaptiveQtCodec::ZigZagEncode(*q) >= std::numeric_limits<uint64_t>::max() - 1) return false;
  *recovered = prediction + 2 * max_diff * static_cast<double>(*q);
  return std::isfinite(*recovered) && std::abs(value - *recovered) <= max_diff;
}

bool QuantizeLog(double value, double prediction, double log_max_diff, double inverse_log_step,
                 double lower_log_error_bound, double upper_log_error_bound, int64_t *q,
                 double *recovered_log, double *recovered_magnitude) {
  if (!std::isfinite(value) || value == 0) return false;
  const double target_log = std::log(std::abs(value));
  const double scaled = (target_log - prediction) * inverse_log_step;
  if (!std::isfinite(scaled)) return false;
  if (std::abs(scaled) > 0x1p52) {
    const long double precise_scaled =
        (static_cast<long double>(target_log) - static_cast<long double>(prediction)) /
        (2.0L * static_cast<long double>(log_max_diff));
    if (!std::isfinite(precise_scaled)) return false;
    const long double rounded = std::round(precise_scaled);
    if (rounded <= static_cast<long double>(std::numeric_limits<int64_t>::min()) ||
        rounded > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
      return false;
    }
    *q = static_cast<int64_t>(rounded);
  } else {
    *q = static_cast<int64_t>(std::round(scaled));
  }
  if (*q == std::numeric_limits<int64_t>::min()) return false;
  *recovered_log = prediction + 2 * log_max_diff * static_cast<double>(*q);
  const double error = *recovered_log - target_log;
  if (error < lower_log_error_bound || error > upper_log_error_bound) return false;
  *recovered_magnitude = std::exp(*recovered_log);
  return std::isfinite(*recovered_magnitude);
}

Result SimulateAbsolute(const std::filesystem::path &path, double requested_max_diff,
                        Strategy strategy) {
  std::ifstream input(path);
  if (!input.is_open()) throw std::runtime_error("Failed to open " + path.string());
  constexpr uint64_t kEscape = std::numeric_limits<uint64_t>::max();
  const double max_diff = requested_max_diff * 0.999;
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
      if (Quantize(value, previous, max_diff, &q, &recovered)) {
        const uint64_t mapped = AdaptiveQtCodec::ZigZagEncode(q) + 1;
        AddResidual(mapped, choice, strategy, true, &result);
        Update(mapped, choice, strategy, true, &state);
        previous = recovered;
      } else {
        if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
          const uint32_t cap = RiceCap(strategy);
          result.bits += cap == 0 ? 66 : cap + 1 + 64;
        } else {
          result.bits += AdaptiveQtCodec::EncodedLength(
                             kEscape, choice.codec, choice.rice_parameter) +
                         64;
        }
        ++result.raw_values;
        previous = std::isfinite(value) ? value : 2;
      }
      ++result.values;
    }
  }
  return result;
}

Result SimulateLog(const std::filesystem::path &path, double relative_error, Strategy strategy) {
  std::ifstream input(path);
  if (!input.is_open()) throw std::runtime_error("Failed to open " + path.string());
  const double upper_error = std::log1p(relative_error);
  const double lower_error =
      relative_error < 1 ? std::log1p(-relative_error) : -std::numeric_limits<double>::infinity();
  const double log_max_diff = upper_error * 0.999;
  const double inverse_log_step = 1.0 / (2 * log_max_diff);
  State state;
  double previous_log = 0;
  double previous_value = 1;
  bool previous_sign = false;
  bool first_block = true;
  Result result;
  std::vector<double> block;
  while ((block = ReadBlock(input, kBlockSizeOverall)).size() == kBlockSizeOverall) {
    result.bits += first_block ? 82 : 2;
    first_block = false;
    for (double value : block) {
      const bool sign = std::signbit(value);
      if (value == 0) {
        result.bits += 4;
        ++result.log_modes[6];
        ++result.values;
        continue;
      }
      if (std::isfinite(value) && sign == previous_sign &&
          std::abs(previous_value - value) <= relative_error * std::abs(value)) {
        ++result.bits;
        ++result.log_modes[0];
        ++result.values;
        continue;
      }
      if (std::isfinite(value) && sign != previous_sign &&
          std::abs(std::abs(previous_value) - std::abs(value)) <=
              relative_error * std::abs(value)) {
        result.bits += 4;
        previous_sign = sign;
        previous_value = sign ? -std::abs(previous_value) : std::abs(previous_value);
        ++result.log_modes[3];
        ++result.values;
        continue;
      }

      const Choice choice = Select(state, strategy);
      int64_t q = 0;
      double recovered_log = 0;
      double recovered_magnitude = 0;
      const bool quantized =
          QuantizeLog(value, previous_log, log_max_diff, inverse_log_step, lower_error, upper_error,
                      &q, &recovered_log, &recovered_magnitude);
      const bool changed_sign = sign != previous_sign;
      const uint64_t mapped =
          q < 0 ? static_cast<uint64_t>(-(q + 1)) + 1 : static_cast<uint64_t>(q);
      const uint64_t residual_bits =
          quantized && q != 0 ? ResidualLength(mapped, choice, strategy, false) : 64;
      const uint64_t prefix_bits = changed_sign ? 5 : 3;
      if (quantized && q != 0 && prefix_bits + residual_bits < 68) {
        result.bits += prefix_bits;
        AddResidual(mapped, choice, strategy, false, &result);
        if (changed_sign) {
          ++result.log_modes[q > 0 ? 4 : 5];
        } else {
          ++result.log_modes[q > 0 ? 1 : 2];
        }
        previous_log = recovered_log;
        previous_value = sign ? -recovered_magnitude : recovered_magnitude;
        previous_sign = sign;
        Update(mapped, choice, strategy, false, &state);
      } else {
        result.bits += 68;
        ++result.raw_values;
        ++result.log_modes[7];
        if (std::isfinite(value) && value != 0) {
          previous_log = std::log(std::abs(value));
          previous_value = value;
          previous_sign = sign;
        }
      }
      ++result.values;
    }
  }
  return result;
}

void Print(const char *method, Strategy strategy, double error, const std::string &dataset,
           const Result &result) {
  std::cout << method << ',' << StrategyName(strategy) << ',' << error << ',' << dataset << ','
            << result.values << ',' << result.bits << ','
            << static_cast<double>(result.bits) / (64.0 * result.values) << ','
            << result.gamma_values << ',' << result.delta_values << ',' << result.rice_values << ','
            << result.raw_values;
  for (uint64_t count : result.log_modes) std::cout << ',' << count;
  std::cout << '\n';
}

}  // namespace

int main(int argc, char **argv) {
  const std::filesystem::path dataset_dir =
      argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("test/data_set");
  const std::string strategy_filter = argc > 2 ? argv[2] : "";
  const std::array strategies = {Strategy::kCurrent, Strategy::kCurrentMinusOne,
                                 Strategy::kExactMean, Strategy::kExactMeanMinusOne,
                                 Strategy::kRiceBank, Strategy::kCappedRice1,
                                 Strategy::kCappedRice2, Strategy::kCappedRice4,
                                 Strategy::kCappedRice8, Strategy::kCappedRice16,
                                 Strategy::kCappedRice32, Strategy::kCappedRice64,
                                 Strategy::kCappedRice128};
  std::cout << std::setprecision(12);
  std::cout << "Method,Strategy,Error,Dataset,Values,Bits,CompressionRatio,GammaValues,"
               "DeltaValues,RiceValues,RawValues,RepeatMode,SamePositiveMode,SameNegativeMode,"
               "ChangedZeroMode,ChangedPositiveMode,ChangedNegativeMode,ZeroMode,RawMode\n";
  for (Strategy strategy : strategies) {
    if (!strategy_filter.empty() && strategy_filter != StrategyName(strategy)) continue;
    for (double error : kMaxDiffList) {
      for (const std::string &dataset : kDataSetList) {
        Print("AdaptiveRice", strategy, error, dataset,
              SimulateAbsolute(dataset_dir / dataset, error, strategy));
      }
    }
    for (double error : kMaxDiffRel) {
      for (const std::string &dataset : kDataSetList) {
        Print("LogSerfQt", strategy, error, dataset,
              SimulateLog(dataset_dir / dataset, error, strategy));
      }
    }
  }
  return 0;
}
