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

constexpr uint32_t kRiceQuotientCap = 16;

enum class Strategy {
  kShared,
  kResidualDirection,
  kSignChange,
  kSharedOrSignChange,
  kValueSign,
  kDirectionSignChange,
  kDirectionValueSign
};

const char *StrategyName(Strategy strategy) {
  switch (strategy) {
    case Strategy::kShared:
      return "Shared";
    case Strategy::kResidualDirection:
      return "ResidualDirection";
    case Strategy::kSignChange:
      return "SignChange";
    case Strategy::kSharedOrSignChange:
      return "SharedOrSignChange";
    case Strategy::kValueSign:
      return "ValueSign";
    case Strategy::kDirectionSignChange:
      return "DirectionSignChange";
    case Strategy::kDirectionValueSign:
      return "DirectionValueSign";
  }
  return "Unknown";
}

struct Result {
  uint64_t values = 0;
  uint64_t bits = 0;
  uint64_t residual_values = 0;
  uint64_t raw_values = 0;
};

size_t ContextIndex(Strategy strategy, bool positive_residual, bool changed_sign,
                    bool value_sign) {
  switch (strategy) {
    case Strategy::kShared:
      return 0;
    case Strategy::kResidualDirection:
      return positive_residual;
    case Strategy::kSignChange:
      return changed_sign;
    case Strategy::kSharedOrSignChange:
      return 0;
    case Strategy::kValueSign:
      return value_sign;
    case Strategy::kDirectionSignChange:
      return 2 * static_cast<size_t>(changed_sign) + positive_residual;
    case Strategy::kDirectionValueSign:
      return 2 * static_cast<size_t>(value_sign) + positive_residual;
  }
  return 0;
}

uint64_t SelectedLength(const AdaptiveQtCodec::AdaptiveCodeLengths &lengths,
                        AdaptiveQtCodec::IntegerCodec codec) {
  switch (codec) {
    case AdaptiveQtCodec::IntegerCodec::kGamma:
      return lengths.gamma;
    case AdaptiveQtCodec::IntegerCodec::kDelta:
      return lengths.delta;
    case AdaptiveQtCodec::IntegerCodec::kRice:
      return lengths.rice;
    case AdaptiveQtCodec::IntegerCodec::kRaw:
      return 64;
  }
  return 64;
}

uint64_t SelectedCost(const AdaptiveQtCodec::AdaptiveRiceState &state,
                      AdaptiveQtCodec::IntegerCodec codec) {
  switch (codec) {
    case AdaptiveQtCodec::IntegerCodec::kGamma:
      return state.gamma_cost;
    case AdaptiveQtCodec::IntegerCodec::kDelta:
      return state.delta_cost;
    case AdaptiveQtCodec::IntegerCodec::kRice:
      return state.rice_cost;
    case AdaptiveQtCodec::IntegerCodec::kRaw:
      return std::numeric_limits<uint64_t>::max();
  }
  return std::numeric_limits<uint64_t>::max();
}

bool Quantize(double target_log, double prediction, double step, double upper_error,
              double lower_error, int64_t *q, double *recovered_log) {
  if (!std::isfinite(target_log) || !std::isfinite(prediction)) return false;
  const double scaled = (target_log - prediction) / step;
  if (!std::isfinite(scaled) || std::abs(scaled) > 0x1p52) return false;
  *q = static_cast<int64_t>(std::round(scaled));
  if (*q == std::numeric_limits<int64_t>::min()) return false;
  *recovered_log = prediction + step * static_cast<double>(*q);
  const double error = *recovered_log - target_log;
  return std::isfinite(*recovered_log) && error >= lower_error && error <= upper_error;
}

Result Simulate(const std::filesystem::path &path, double relative_error, Strategy strategy) {
  std::ifstream input(path);
  if (!input.is_open()) throw std::runtime_error("Failed to open " + path.string());

  const double upper_error = std::log1p(relative_error);
  const double lower_error =
      relative_error < 1 ? std::log1p(-relative_error) : -std::numeric_limits<double>::infinity();
  const double log_max_diff = upper_error * 0.999;
  const double step = 2 * log_max_diff;
  std::array<AdaptiveQtCodec::AdaptiveRiceState, 4> states{};
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

      const double target_log = std::log(magnitude);
      int64_t q = 0;
      double recovered_log = 0;
      const bool quantized =
          Quantize(target_log, previous_log, step, upper_error, lower_error, &q, &recovered_log);
      const bool changed_sign = sign != previous_sign;
      if (quantized && q != 0) {
        const bool positive = q > 0;
        const uint64_t mapped = positive ? static_cast<uint64_t>(q) : static_cast<uint64_t>(-q);
        AdaptiveQtCodec::AdaptiveRiceState *state =
            &states[ContextIndex(strategy, positive, changed_sign, sign)];
        AdaptiveQtCodec::AdaptiveRiceChoice choice = AdaptiveQtCodec::SelectAdaptiveRiceCodec(*state);
        if (strategy == Strategy::kSharedOrSignChange) {
          AdaptiveQtCodec::AdaptiveRiceState *context_state = &states[1 + changed_sign];
          const AdaptiveQtCodec::AdaptiveRiceChoice context_choice =
              AdaptiveQtCodec::SelectAdaptiveRiceCodec(*context_state);
          if (SelectedCost(*context_state, context_choice.codec) <
              SelectedCost(*state, choice.codec)) {
            state = context_state;
            choice = context_choice;
          }
        }
        const AdaptiveQtCodec::AdaptiveCodeLengths lengths =
            AdaptiveQtCodec::CalculateCappedRiceCodeLengths(mapped, choice.rice_parameter,
                                                            kRiceQuotientCap);
        const uint64_t encoded =
            (changed_sign ? 4 : 2) + static_cast<uint64_t>(positive) +
            SelectedLength(lengths, choice.codec);
        if (encoded < 71) {
          result.bits += encoded;
          ++result.residual_values;
          previous_log = recovered_log;
          const double recovered_magnitude = std::exp(recovered_log);
          previous_value = sign ? -recovered_magnitude : recovered_magnitude;
          previous_sign = sign;
          if (strategy == Strategy::kSharedOrSignChange) {
            AdaptiveQtCodec::AdaptiveRiceState &shared_state = states[0];
            AdaptiveQtCodec::AdaptiveRiceState &context_state = states[1 + changed_sign];
            const uint32_t shared_parameter =
                AdaptiveQtCodec::EstimateRiceParameter(shared_state);
            const uint32_t context_parameter =
                AdaptiveQtCodec::EstimateRiceParameter(context_state);
            AdaptiveQtCodec::UpdateAdaptiveRiceState(
                mapped,
                AdaptiveQtCodec::CalculateCappedRiceCodeLengths(mapped, shared_parameter,
                                                                kRiceQuotientCap),
                &shared_state);
            AdaptiveQtCodec::UpdateAdaptiveRiceState(
                mapped,
                AdaptiveQtCodec::CalculateCappedRiceCodeLengths(mapped, context_parameter,
                                                                kRiceQuotientCap),
                &context_state);
          } else {
            AdaptiveQtCodec::UpdateAdaptiveRiceState(mapped, lengths, state);
          }
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

}  // namespace

int main(int argc, char **argv) {
  const std::filesystem::path dataset_dir =
      argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("test/data_set");
  std::cout << std::setprecision(12);
  std::cout << "Strategy,Error,Dataset,Values,Bits,CompressionRatio,ResidualValues,RawValues\n";
  for (Strategy strategy :
       {Strategy::kShared, Strategy::kResidualDirection, Strategy::kSignChange,
        Strategy::kSharedOrSignChange, Strategy::kValueSign, Strategy::kDirectionSignChange,
        Strategy::kDirectionValueSign}) {
    for (double error : kMaxDiffRel) {
      for (const std::string &dataset : kDataSetList) {
        const Result result = Simulate(dataset_dir / dataset, error, strategy);
        std::cout << StrategyName(strategy) << ',' << error << ',' << dataset << ','
                  << result.values << ',' << result.bits << ','
                  << static_cast<double>(result.bits) / (64.0 * result.values) << ','
                  << result.residual_values << ',' << result.raw_values << '\n';
      }
    }
  }
}
