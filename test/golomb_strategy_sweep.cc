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

constexpr uint32_t kAbsoluteGolombCap = 32;
constexpr uint32_t kLogGolombCap = 16;

enum class Strategy { kCurrent, kGolomb };
enum class Codec { kGamma, kDelta, kLegacyRice, kCappedRice, kGolomb };

const char *StrategyName(Strategy strategy) {
  return strategy == Strategy::kCurrent ? "Current" : "Golomb";
}

struct State {
  AdaptiveQtCodec::AdaptiveRiceState adaptive;
  uint64_t golomb_cost = 0;
};

struct Choice {
  Codec codec = Codec::kDelta;
  uint32_t rice_parameter = 0;
  uint64_t golomb_divisor = 1;
};

struct Result {
  uint64_t values = 0;
  uint64_t bits = 0;
  uint64_t golomb_values = 0;
  uint64_t raw_values = 0;
};

uint64_t GolombDivisor(const State &state) {
  if (state.adaptive.magnitude_sum == 0 || state.adaptive.sample_count == 0) return 1;
  const long double mean = static_cast<long double>(state.adaptive.magnitude_sum) /
                           static_cast<long double>(state.adaptive.sample_count);
  return std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(mean * 0.6931471805599453L)));
}

uint64_t GolombLength(uint64_t value, uint64_t divisor) {
  const uint64_t quotient = value / divisor;
  if (divisor == 1) return quotient + 1;
  const uint32_t bits = AdaptiveQtCodec::FloorLog2(divisor - 1) + 1;
  const uint64_t cutoff = (1ULL << bits) - divisor;
  const uint64_t remainder = value - quotient * divisor;
  return quotient + 1 + bits - static_cast<uint64_t>(remainder < cutoff);
}

uint64_t CappedGolombLength(uint64_t mapped, uint64_t divisor, uint32_t cap, bool raw_escape) {
  const uint64_t quotient = (mapped - 1) / divisor;
  if (quotient < cap) return GolombLength(mapped - 1, divisor);
  return cap + static_cast<uint64_t>(raw_escape) + AdaptiveQtCodec::DeltaLength(mapped);
}

Choice SelectAbsolute(const State &state, Strategy strategy) {
  const AdaptiveQtCodec::AdaptiveRiceChoice current =
      AdaptiveQtCodec::SelectAdaptiveRiceCodecAndFormat(state.adaptive);
  Choice choice;
  choice.rice_parameter = current.rice_parameter;
  if (current.codec == AdaptiveQtCodec::IntegerCodec::kGamma) {
    choice.codec = Codec::kGamma;
  } else if (current.codec == AdaptiveQtCodec::IntegerCodec::kDelta) {
    choice.codec = Codec::kDelta;
  } else {
    choice.codec = current.capped_rice ? Codec::kCappedRice : Codec::kLegacyRice;
  }
  if (strategy == Strategy::kGolomb) {
    choice.golomb_divisor = GolombDivisor(state);
    uint64_t selected_cost = state.adaptive.delta_cost;
    if (state.adaptive.gamma_cost < selected_cost) selected_cost = state.adaptive.gamma_cost;
    if (state.adaptive.rice_cost < selected_cost) selected_cost = state.adaptive.rice_cost;
    if (state.adaptive.capped_rice_cost < selected_cost) selected_cost = state.adaptive.capped_rice_cost;
    if (state.golomb_cost < selected_cost) choice.codec = Codec::kGolomb;
  }
  return choice;
}

Choice SelectLog(const State &state, Strategy strategy) {
  const AdaptiveQtCodec::AdaptiveRiceChoice current =
      AdaptiveQtCodec::SelectAdaptiveRiceCodec(state.adaptive);
  Choice choice;
  choice.rice_parameter = current.rice_parameter;
  choice.codec = current.codec == AdaptiveQtCodec::IntegerCodec::kGamma
                     ? Codec::kGamma
                     : current.codec == AdaptiveQtCodec::IntegerCodec::kDelta ? Codec::kDelta
                                                                              : Codec::kCappedRice;
  if (strategy == Strategy::kGolomb) {
    choice.golomb_divisor = GolombDivisor(state);
    uint64_t selected_cost = state.adaptive.delta_cost;
    if (state.adaptive.gamma_cost < selected_cost) selected_cost = state.adaptive.gamma_cost;
    if (state.adaptive.rice_cost < selected_cost) selected_cost = state.adaptive.rice_cost;
    if (state.golomb_cost < selected_cost) choice.codec = Codec::kGolomb;
  }
  return choice;
}

void UpdateMagnitude(uint64_t mapped, State *state) {
  state->adaptive.magnitude_sum = AdaptiveQtCodec::DecayAndAdd(
      state->adaptive.magnitude_sum,
      std::min(mapped - 1, AdaptiveQtCodec::kAdaptiveRiceMagnitudeCap));
  state->adaptive.sample_count =
      AdaptiveQtCodec::DecayAndAdd(state->adaptive.sample_count, 1);
}

void UpdateAbsolute(uint64_t mapped, uint32_t rice_parameter, uint64_t golomb_divisor,
                    Strategy strategy, State *state) {
  const AdaptiveQtCodec::AdaptiveRiceFormatLengths lengths =
      AdaptiveQtCodec::CalculateAdaptiveRiceFormatLengths(mapped, rice_parameter);
  state->adaptive.delta_cost =
      AdaptiveQtCodec::DecayAndAdd(state->adaptive.delta_cost, lengths.legacy.delta);
  state->adaptive.gamma_cost =
      AdaptiveQtCodec::DecayAndAdd(state->adaptive.gamma_cost, lengths.legacy.gamma);
  state->adaptive.rice_cost =
      AdaptiveQtCodec::DecayAndAdd(state->adaptive.rice_cost, lengths.legacy.rice);
  state->adaptive.capped_rice_cost =
      AdaptiveQtCodec::DecayAndAdd(state->adaptive.capped_rice_cost, lengths.capped_rice);
  if (strategy == Strategy::kGolomb) {
    state->golomb_cost = AdaptiveQtCodec::DecayAndAdd(
        state->golomb_cost, CappedGolombLength(mapped, golomb_divisor, kAbsoluteGolombCap, true));
  }
  UpdateMagnitude(mapped, state);
}

void UpdateLog(uint64_t mapped, uint32_t rice_parameter, uint64_t golomb_divisor,
               Strategy strategy, State *state) {
  const AdaptiveQtCodec::AdaptiveCodeLengths lengths =
      AdaptiveQtCodec::CalculateCappedRiceCodeLengths(mapped, rice_parameter, 16);
  state->adaptive.delta_cost =
      AdaptiveQtCodec::DecayAndAdd(state->adaptive.delta_cost, lengths.delta);
  state->adaptive.gamma_cost =
      AdaptiveQtCodec::DecayAndAdd(state->adaptive.gamma_cost, lengths.gamma);
  state->adaptive.rice_cost =
      AdaptiveQtCodec::DecayAndAdd(state->adaptive.rice_cost, lengths.rice);
  if (strategy == Strategy::kGolomb) {
    state->golomb_cost = AdaptiveQtCodec::DecayAndAdd(
        state->golomb_cost, CappedGolombLength(mapped, golomb_divisor, kLogGolombCap, false));
  }
  UpdateMagnitude(mapped, state);
}

uint64_t AbsoluteLength(uint64_t mapped, const Choice &choice) {
  const AdaptiveQtCodec::AdaptiveRiceFormatLengths lengths =
      AdaptiveQtCodec::CalculateAdaptiveRiceFormatLengths(mapped, choice.rice_parameter);
  switch (choice.codec) {
    case Codec::kGamma:
      return lengths.legacy.gamma;
    case Codec::kDelta:
      return lengths.legacy.delta;
    case Codec::kLegacyRice:
      return lengths.legacy.rice;
    case Codec::kCappedRice:
      return lengths.capped_rice;
    case Codec::kGolomb:
      return CappedGolombLength(mapped, choice.golomb_divisor, kAbsoluteGolombCap, true);
  }
  return 0;
}

uint64_t LogLength(uint64_t mapped, const Choice &choice) {
  const AdaptiveQtCodec::AdaptiveCodeLengths lengths =
      AdaptiveQtCodec::CalculateCappedRiceCodeLengths(mapped, choice.rice_parameter, 16);
  switch (choice.codec) {
    case Codec::kGamma:
      return lengths.gamma;
    case Codec::kDelta:
      return lengths.delta;
    case Codec::kCappedRice:
    case Codec::kLegacyRice:
      return lengths.rice;
    case Codec::kGolomb:
      return CappedGolombLength(mapped, choice.golomb_divisor, kLogGolombCap, false);
  }
  return 0;
}

bool Quantize(double value, double prediction, double step, double max_diff, int64_t *q,
              double *recovered) {
  if (!std::isfinite(value) || !std::isfinite(prediction)) return false;
  const double scaled = (value - prediction) / step;
  if (!std::isfinite(scaled) || std::abs(scaled) > 0x1p52) return false;
  *q = static_cast<int64_t>(std::round(scaled));
  if (AdaptiveQtCodec::ZigZagEncode(*q) >= std::numeric_limits<uint64_t>::max() - 1) return false;
  *recovered = prediction + step * static_cast<double>(*q);
  return std::isfinite(*recovered) && std::abs(value - *recovered) <= max_diff;
}

Result SimulateAbsolute(const std::filesystem::path &path, double requested_max_diff,
                        Strategy strategy) {
  std::ifstream input(path);
  if (!input.is_open()) throw std::runtime_error("Failed to open " + path.string());
  constexpr uint64_t kEscape = std::numeric_limits<uint64_t>::max();
  const double max_diff = requested_max_diff * 0.999;
  const double step = 2 * max_diff;
  State state;
  double previous = 2;
  bool first_block = true;
  Result result;
  std::vector<double> block;
  while ((block = ReadBlock(input, kBlockSizeOverall)).size() == kBlockSizeOverall) {
    result.bits += first_block ? 82 : 2;
    first_block = false;
    for (double value : block) {
      const Choice choice = SelectAbsolute(state, strategy);
      int64_t q = 0;
      double recovered = 0;
      if (Quantize(value, previous, step, max_diff, &q, &recovered)) {
        const uint64_t mapped = AdaptiveQtCodec::ZigZagEncode(q) + 1;
        result.bits += AbsoluteLength(mapped, choice);
        if (choice.codec == Codec::kGolomb) ++result.golomb_values;
        UpdateAbsolute(mapped, choice.rice_parameter, choice.golomb_divisor, strategy, &state);
        previous = recovered;
      } else {
        switch (choice.codec) {
          case Codec::kGamma:
            result.bits += AdaptiveQtCodec::GammaLength(kEscape) + 64;
            break;
          case Codec::kDelta:
            result.bits += AdaptiveQtCodec::DeltaLength(kEscape) + 64;
            break;
          case Codec::kLegacyRice:
            result.bits += 66;
            break;
          case Codec::kCappedRice:
          case Codec::kGolomb:
            result.bits += kAbsoluteGolombCap + 65;
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

Result SimulateLog(const std::filesystem::path &path, double relative_error, Strategy strategy) {
  std::ifstream input(path);
  if (!input.is_open()) throw std::runtime_error("Failed to open " + path.string());
  const double upper_error = std::log1p(relative_error);
  const double lower_error =
      relative_error < 1 ? std::log1p(-relative_error) : -std::numeric_limits<double>::infinity();
  const double log_max_diff = upper_error * 0.999;
  const double step = 2 * log_max_diff;
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
        result.bits += 7;
        ++result.values;
        continue;
      }
      const double magnitude = std::abs(value);
      if (std::isfinite(value) && sign == previous_sign &&
          std::abs(previous_value - value) <= relative_error * magnitude) {
        ++result.bits;
        ++result.values;
        continue;
      }
      if (std::isfinite(value) && sign != previous_sign &&
          std::abs(std::abs(previous_value) - magnitude) <= relative_error * magnitude) {
        result.bits += 6;
        previous_sign = sign;
        previous_value = sign ? -std::abs(previous_value) : std::abs(previous_value);
        ++result.values;
        continue;
      }
      const Choice choice = SelectLog(state, strategy);
      const double target_log = std::log(magnitude);
      int64_t q = 0;
      double recovered_log = 0;
      const bool quantized = Quantize(target_log, previous_log, step, upper_error, &q, &recovered_log) &&
                             recovered_log - target_log >= lower_error;
      const bool changed_sign = sign != previous_sign;
      if (quantized && q != 0) {
        const bool positive = q > 0;
        const uint64_t mapped = positive ? static_cast<uint64_t>(q) : static_cast<uint64_t>(-q);
        const uint64_t encoded =
            (changed_sign ? 4 : 2) + static_cast<uint64_t>(positive) + LogLength(mapped, choice);
        if (encoded < 71) {
          result.bits += encoded;
          if (choice.codec == Codec::kGolomb) ++result.golomb_values;
          previous_log = recovered_log;
          const double recovered_magnitude = std::exp(recovered_log);
          previous_value = sign ? -recovered_magnitude : recovered_magnitude;
          previous_sign = sign;
          UpdateLog(mapped, choice.rice_parameter, choice.golomb_divisor, strategy, &state);
        } else {
          result.bits += 71;
          ++result.raw_values;
          previous_log = target_log;
          previous_value = value;
          previous_sign = sign;
        }
      } else {
        result.bits += 71;
        ++result.raw_values;
        if (std::isfinite(value)) {
          previous_log = target_log;
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
            << result.golomb_values << ',' << result.raw_values << '\n';
}

}  // namespace

int main(int argc, char **argv) {
  const std::filesystem::path dataset_dir =
      argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("test/data_set");
  std::cout << std::setprecision(12);
  std::cout << "Method,Strategy,Error,Dataset,Values,Bits,CompressionRatio,GolombValues,RawValues\n";
  for (Strategy strategy : {Strategy::kCurrent, Strategy::kGolomb}) {
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
}
