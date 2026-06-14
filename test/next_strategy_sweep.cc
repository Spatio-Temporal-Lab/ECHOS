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

enum class PredictorStrategy {
  kLast,
  kLinear,
  kHalfLinear,
  kAdaptiveLastLinear,
  kAdaptiveThree
};

const char *StrategyName(PredictorStrategy strategy) {
  switch (strategy) {
    case PredictorStrategy::kLast:
      return "Last";
    case PredictorStrategy::kLinear:
      return "Linear";
    case PredictorStrategy::kHalfLinear:
      return "HalfLinear";
    case PredictorStrategy::kAdaptiveLastLinear:
      return "AdaptiveLastLinear";
    case PredictorStrategy::kAdaptiveThree:
      return "AdaptiveThree";
  }
  return "Unknown";
}

struct PredictorState {
  double last;
  double second_last;
  std::array<uint64_t, 3> costs{};
};

struct Result {
  uint64_t values = 0;
  uint64_t bits = 0;
  uint64_t raw_values = 0;
  uint64_t predictor_switches = 0;
};

double Prediction(const PredictorState &state, int predictor) {
  if (predictor == 0) return state.last;
  const double slope = state.last - state.second_last;
  const double prediction = predictor == 1 ? state.last + slope : state.last + 0.5 * slope;
  return std::isfinite(prediction) ? prediction : state.last;
}

int SelectPredictor(const PredictorState &state, PredictorStrategy strategy) {
  if (strategy == PredictorStrategy::kLast) return 0;
  if (strategy == PredictorStrategy::kLinear) return 1;
  if (strategy == PredictorStrategy::kHalfLinear) return 2;
  const int count = strategy == PredictorStrategy::kAdaptiveLastLinear ? 2 : 3;
  int selected = 0;
  for (int i = 1; i < count; ++i) {
    if (state.costs[i] < state.costs[selected]) selected = i;
  }
  return selected;
}

bool QuantizedResidual(double value, double prediction, double step, double max_diff, int64_t *q,
                       double *recovered) {
  if (!std::isfinite(value) || !std::isfinite(prediction)) return false;
  const double scaled = (value - prediction) / step;
  if (std::isfinite(scaled) && std::abs(scaled) <= 0x1p52) {
    *q = static_cast<int64_t>(std::round(scaled));
    if (AdaptiveQtCodec::ZigZagEncode(*q) < std::numeric_limits<uint64_t>::max() - 1) {
      *recovered = prediction + step * static_cast<double>(*q);
      if (std::isfinite(*recovered) && std::abs(value - *recovered) <= max_diff) return true;
    }
  }
  return false;
}

uint64_t PredictorScore(double value, double prediction, double step, bool signed_residual) {
  if (!std::isfinite(value) || !std::isfinite(prediction)) return 65;
  const double scaled = (value - prediction) / step;
  if (!std::isfinite(scaled) || std::abs(scaled) > 0x1p52) return 65;
  const int64_t q = static_cast<int64_t>(std::round(scaled));
  if (signed_residual) {
    const uint64_t mapped = AdaptiveQtCodec::ZigZagEncode(q) + 1;
    return std::min<uint64_t>(AdaptiveQtCodec::DeltaLength(mapped), 65);
  }
  if (q == 0) return 1;
  const uint64_t magnitude = q < 0 ? static_cast<uint64_t>(-q) : static_cast<uint64_t>(q);
  return std::min<uint64_t>((q < 0 ? 2 : 3) + AdaptiveQtCodec::DeltaLength(magnitude), 65);
}

void UpdatePredictors(double current, double step, bool signed_residual, PredictorState *state) {
  for (int predictor = 0; predictor < 3; ++predictor) {
    state->costs[predictor] = AdaptiveQtCodec::DecayAndAdd(
        state->costs[predictor],
        PredictorScore(current, Prediction(*state, predictor), step, signed_residual));
  }
  state->second_last = state->last;
  state->last = current;
}

uint64_t SelectedAbsoluteLength(const AdaptiveQtCodec::AdaptiveRiceChoice &choice,
                                const AdaptiveQtCodec::AdaptiveRiceFormatLengths &lengths) {
  if (choice.codec == AdaptiveQtCodec::IntegerCodec::kGamma) return lengths.legacy.gamma;
  if (choice.codec == AdaptiveQtCodec::IntegerCodec::kDelta) return lengths.legacy.delta;
  return choice.capped_rice ? lengths.capped_rice : lengths.legacy.rice;
}

uint64_t SelectedLogLength(const AdaptiveQtCodec::AdaptiveRiceChoice &choice,
                           const AdaptiveQtCodec::AdaptiveCodeLengths &lengths) {
  if (choice.codec == AdaptiveQtCodec::IntegerCodec::kGamma) return lengths.gamma;
  if (choice.codec == AdaptiveQtCodec::IntegerCodec::kDelta) return lengths.delta;
  return lengths.rice;
}

Result SimulateAbsolute(const std::filesystem::path &path, double requested_max_diff,
                        PredictorStrategy strategy) {
  std::ifstream input(path);
  if (!input.is_open()) throw std::runtime_error("Failed to open " + path.string());
  constexpr uint64_t kEscape = std::numeric_limits<uint64_t>::max();
  const double max_diff = requested_max_diff * 0.999;
  const double step = 2 * max_diff;
  AdaptiveQtCodec::AdaptiveRiceState codec_state;
  PredictorState predictor_state{2, 2};
  int previous_predictor = -1;
  bool first_block = true;
  Result result;
  std::vector<double> block;
  while ((block = ReadBlock(input, kBlockSizeOverall)).size() == kBlockSizeOverall) {
    result.bits += first_block ? 82 : 2;
    first_block = false;
    for (double value : block) {
      const int predictor = SelectPredictor(predictor_state, strategy);
      if (previous_predictor >= 0 && predictor != previous_predictor) ++result.predictor_switches;
      previous_predictor = predictor;
      const double prediction = Prediction(predictor_state, predictor);
      const AdaptiveQtCodec::AdaptiveRiceChoice choice =
          AdaptiveQtCodec::SelectAdaptiveRiceCodecAndFormat(codec_state);
      int64_t q = 0;
      double recovered = 0;
      if (QuantizedResidual(value, prediction, step, max_diff, &q, &recovered)) {
        const uint64_t mapped = AdaptiveQtCodec::ZigZagEncode(q) + 1;
        const AdaptiveQtCodec::AdaptiveRiceFormatLengths lengths =
            AdaptiveQtCodec::CalculateAdaptiveRiceFormatLengths(mapped, choice.rice_parameter);
        result.bits += SelectedAbsoluteLength(choice, lengths);
        AdaptiveQtCodec::UpdateAdaptiveRiceFormatState(mapped, lengths, &codec_state);
        UpdatePredictors(recovered, step, true, &predictor_state);
      } else {
        if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
          result.bits += choice.capped_rice ? AdaptiveQtCodec::kAdaptiveRiceFormatQuotientCap + 65
                                           : 66;
        } else {
          result.bits +=
              AdaptiveQtCodec::EncodedLength(kEscape, choice.codec, choice.rice_parameter) + 64;
        }
        ++result.raw_values;
        const double current = std::isfinite(value) ? value : 2;
        UpdatePredictors(current, step, true, &predictor_state);
      }
      ++result.values;
    }
  }
  return result;
}

bool QuantizeLog(double target_log, double prediction, double step, double lower_error,
                 double upper_error, int64_t *q, double *recovered_log) {
  const double scaled = (target_log - prediction) / step;
  if (!std::isfinite(scaled) || std::abs(scaled) > 0x1p52) return false;
  *q = static_cast<int64_t>(std::round(scaled));
  if (*q == std::numeric_limits<int64_t>::min()) return false;
  *recovered_log = prediction + step * static_cast<double>(*q);
  const double error = *recovered_log - target_log;
  return error >= lower_error && error <= upper_error;
}

Result SimulateLog(const std::filesystem::path &path, double relative_error,
                   PredictorStrategy strategy) {
  std::ifstream input(path);
  if (!input.is_open()) throw std::runtime_error("Failed to open " + path.string());
  constexpr uint32_t kRiceCap = 16;
  const double upper_error = std::log1p(relative_error);
  const double lower_error =
      relative_error < 1 ? std::log1p(-relative_error) : -std::numeric_limits<double>::infinity();
  const double log_max_diff = upper_error * 0.999;
  const double step = 2 * log_max_diff;
  AdaptiveQtCodec::AdaptiveRiceState codec_state;
  PredictorState predictor_state{0, 0};
  double previous_value = 1;
  bool previous_sign = false;
  int previous_predictor = -1;
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
        UpdatePredictors(predictor_state.last, step, false, &predictor_state);
        ++result.values;
        continue;
      }
      if (std::isfinite(value) && sign != previous_sign &&
          std::abs(std::abs(previous_value) - magnitude) <= relative_error * magnitude) {
        result.bits += 6;
        previous_sign = sign;
        previous_value = sign ? -std::abs(previous_value) : std::abs(previous_value);
        UpdatePredictors(predictor_state.last, step, false, &predictor_state);
        ++result.values;
        continue;
      }

      const int predictor = SelectPredictor(predictor_state, strategy);
      if (previous_predictor >= 0 && predictor != previous_predictor) ++result.predictor_switches;
      previous_predictor = predictor;
      const double prediction = Prediction(predictor_state, predictor);
      const AdaptiveQtCodec::AdaptiveRiceChoice choice =
          AdaptiveQtCodec::SelectAdaptiveRiceCodec(codec_state);
      int64_t q = 0;
      double recovered_log = 0;
      const double target_log = std::log(magnitude);
      const bool quantized =
          QuantizeLog(target_log, prediction, step, lower_error, upper_error, &q, &recovered_log);
      const bool changed_sign = sign != previous_sign;
      if (quantized && q == 0) {
        result.bits += changed_sign ? 9 : 8;
        previous_sign = sign;
        const double recovered_magnitude = std::exp(recovered_log);
        previous_value = sign ? -recovered_magnitude : recovered_magnitude;
        UpdatePredictors(recovered_log, step, false, &predictor_state);
      } else if (quantized) {
        const bool positive = q > 0;
        const uint64_t mapped = positive ? static_cast<uint64_t>(q) : static_cast<uint64_t>(-q);
        const AdaptiveQtCodec::AdaptiveCodeLengths lengths =
            AdaptiveQtCodec::CalculateCappedRiceCodeLengths(mapped, choice.rice_parameter, kRiceCap);
        const uint64_t prefix = (changed_sign ? 4 : 2) + static_cast<uint64_t>(positive);
        const uint64_t encoded = prefix + SelectedLogLength(choice, lengths);
        if (encoded < 73) {
          result.bits += encoded;
          previous_sign = sign;
          const double recovered_magnitude = std::exp(recovered_log);
          previous_value = sign ? -recovered_magnitude : recovered_magnitude;
          AdaptiveQtCodec::UpdateAdaptiveRiceState(mapped, lengths, &codec_state);
          UpdatePredictors(recovered_log, step, false, &predictor_state);
        } else {
          result.bits += 73;
          ++result.raw_values;
          previous_sign = sign;
          previous_value = value;
          UpdatePredictors(target_log, step, false, &predictor_state);
        }
      } else {
        result.bits += 73;
        ++result.raw_values;
        if (std::isfinite(value)) {
          previous_sign = sign;
          previous_value = value;
          UpdatePredictors(target_log, step, false, &predictor_state);
        }
      }
      ++result.values;
    }
  }
  return result;
}

void Print(const char *method, PredictorStrategy strategy, double error, const std::string &dataset,
           const Result &result) {
  std::cout << method << ',' << StrategyName(strategy) << ',' << error << ',' << dataset << ','
            << result.values << ',' << result.bits << ','
            << static_cast<double>(result.bits) / (64.0 * result.values) << ','
            << result.raw_values << ',' << result.predictor_switches << '\n';
}

}  // namespace

int main(int argc, char **argv) {
  const std::filesystem::path dataset_dir =
      argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("test/data_set");
  const std::array strategies = {
      PredictorStrategy::kLast, PredictorStrategy::kLinear, PredictorStrategy::kHalfLinear,
      PredictorStrategy::kAdaptiveLastLinear, PredictorStrategy::kAdaptiveThree};
  std::cout << std::setprecision(12);
  std::cout << "Method,Strategy,Error,Dataset,Values,Bits,CompressionRatio,RawValues,"
               "PredictorSwitches\n";
  for (PredictorStrategy strategy : strategies) {
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
