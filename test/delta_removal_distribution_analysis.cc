#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "Perf_expr_config.hpp"
#include "utils/adaptive_qt_codec.h"

namespace {

constexpr uint32_t kRiceQuotientCap = 16;
constexpr uint64_t kEscape = std::numeric_limits<uint64_t>::max();
constexpr uint32_t kShift = 4;

struct State {
  uint64_t delta_cost = 0;
  uint64_t rice_cost = 0;
  uint64_t magnitude_sum = 0;
  uint64_t sample_count = 0;
};

struct Stats {
  uint64_t residuals = 0;
  uint64_t repeat_or_zero_modes = 0;
  uint64_t delta_selected = 0;
  uint64_t rice_selected = 0;
  uint64_t rice_wins_pointwise = 0;
  uint64_t delta_wins_pointwise = 0;
  uint64_t rice_fallbacks = 0;
  uint64_t sum_mapped = 0;
  uint64_t sum_abs_q = 0;
  uint64_t sum_selected_extra_bits_without_delta = 0;
  uint64_t sum_pointwise_delta_saving = 0;
  std::vector<uint64_t> mapped_values;
  std::vector<uint32_t> rice_parameters;
};

uint64_t DecayAndAdd(uint64_t cost, uint64_t value) {
  return AdaptiveQtCodec::SaturatingAdd(cost - (cost >> kShift), value);
}

uint32_t EstimateRiceParameter(const State &state) {
  if (state.magnitude_sum == 0 || state.sample_count == 0) return 0;
  const uint32_t magnitude_log = AdaptiveQtCodec::FloorLog2(state.magnitude_sum);
  const uint32_t sample_log = AdaptiveQtCodec::FloorLog2(state.sample_count);
  return magnitude_log > sample_log ? magnitude_log - sample_log : 0;
}

uint64_t CappedRiceLength(uint64_t mapped, uint32_t rice_parameter, bool raw_escape) {
  const uint64_t quotient = (mapped - 1) >> rice_parameter;
  const uint64_t direct = quotient + rice_parameter + 1;
  return quotient < kRiceQuotientCap
             ? direct
             : kRiceQuotientCap + static_cast<uint64_t>(raw_escape) +
                   AdaptiveQtCodec::DeltaLength(mapped);
}

void UpdateState(uint64_t mapped, uint64_t delta_length, uint64_t rice_length, State *state) {
  state->delta_cost = DecayAndAdd(state->delta_cost, delta_length);
  state->rice_cost = DecayAndAdd(state->rice_cost, rice_length);
  state->magnitude_sum =
      DecayAndAdd(state->magnitude_sum,
                  std::min(mapped - 1, AdaptiveQtCodec::kAdaptiveRiceMagnitudeCap));
  state->sample_count = DecayAndAdd(state->sample_count, 1);
}

bool QuantizeAbsolute(double value, double prediction, double max_diff, double step,
                      double inverse_step, int64_t *q, double *recovered) {
  if (!std::isfinite(value) || !std::isfinite(prediction)) return false;
  const double scaled = (value - prediction) * inverse_step;
  if (std::isfinite(scaled) && std::abs(scaled) <= 0x1p52) {
    *q = static_cast<int64_t>(std::round(scaled));
    if (AdaptiveQtCodec::ZigZagEncode(*q) < kEscape - 1) {
      *recovered = prediction + step * static_cast<double>(*q);
      if (std::isfinite(*recovered) && std::abs(value - *recovered) <= max_diff) return true;
    }
  }
  return false;
}

bool QuantizeLog(double magnitude, double prediction, double log_max_diff, double inverse_step,
                 double lower_error, double upper_error, int64_t *q, double *target_log,
                 double *recovered_log) {
  if (!std::isfinite(magnitude) || magnitude == 0) return false;
  *target_log = std::log(magnitude);
  const double scaled = (*target_log - prediction) * inverse_step;
  if (!std::isfinite(scaled) || std::abs(scaled) > 0x1p52) return false;
  *q = static_cast<int64_t>(std::round(scaled));
  *recovered_log = prediction + 2 * log_max_diff * static_cast<double>(*q);
  const double error = *recovered_log - *target_log;
  return error >= lower_error && error <= upper_error;
}

std::vector<double> ReadValues(const std::filesystem::path &path) {
  std::ifstream input(path);
  std::vector<double> values;
  double value = 0;
  while (input >> value) values.push_back(value);
  return values;
}

void AddResidual(uint64_t mapped, uint64_t abs_q, bool raw_escape, State *state, Stats *stats) {
  const uint32_t rice_parameter = EstimateRiceParameter(*state);
  const uint64_t delta_length = AdaptiveQtCodec::DeltaLength(mapped);
  const uint64_t rice_length = CappedRiceLength(mapped, rice_parameter, raw_escape);
  const bool choose_rice = state->rice_cost < state->delta_cost;

  ++stats->residuals;
  stats->sum_mapped += mapped;
  stats->sum_abs_q += abs_q;
  stats->mapped_values.push_back(mapped);
  stats->rice_parameters.push_back(rice_parameter);

  if (choose_rice) {
    ++stats->rice_selected;
  } else {
    ++stats->delta_selected;
    if (rice_length > delta_length) {
      stats->sum_selected_extra_bits_without_delta += rice_length - delta_length;
    }
  }
  if (rice_length < delta_length) {
    ++stats->rice_wins_pointwise;
  } else if (delta_length < rice_length) {
    ++stats->delta_wins_pointwise;
    stats->sum_pointwise_delta_saving += rice_length - delta_length;
  }
  if (((mapped - 1) >> rice_parameter) >= kRiceQuotientCap) ++stats->rice_fallbacks;
  UpdateState(mapped, delta_length, rice_length, state);
}

Stats AnalyzeAbsolute(const std::vector<std::vector<double>> &datasets, double requested_max_diff) {
  Stats stats;
  for (const auto &values : datasets) {
    State state;
    double previous = 2;
    const double max_diff = requested_max_diff * 0.999;
    const double step = 2 * max_diff;
    const double inverse_step = 1.0 / step;
    for (size_t begin = 0; begin + kBlockSizeOverall <= values.size(); begin += kBlockSizeOverall) {
      for (size_t i = begin; i < begin + kBlockSizeOverall; ++i) {
        int64_t q = 0;
        double recovered = 0;
        if (QuantizeAbsolute(values[i], previous, max_diff, step, inverse_step, &q, &recovered)) {
          AddResidual(AdaptiveQtCodec::ZigZagEncode(q) + 1, static_cast<uint64_t>(std::llabs(q)),
                      true, &state, &stats);
          previous = recovered;
        } else {
          previous = std::isfinite(values[i]) ? values[i] : 2;
        }
      }
    }
  }
  return stats;
}

Stats AnalyzeRelative(const std::vector<std::vector<double>> &datasets, double relative_error) {
  Stats stats;
  for (const auto &values : datasets) {
    State state;
    double previous_log = 0;
    double previous_value = 1;
    bool previous_sign = false;
    const double upper_error = std::log1p(relative_error);
    const double lower_error = relative_error < 1 ? std::log1p(-relative_error)
                                                  : -std::numeric_limits<double>::infinity();
    const double log_max_diff = upper_error * 0.999;
    const double inverse_step = 1.0 / (2 * log_max_diff);
    for (size_t begin = 0; begin + kBlockSizeOverall <= values.size(); begin += kBlockSizeOverall) {
      for (size_t i = begin; i < begin + kBlockSizeOverall; ++i) {
        const double value = values[i];
        const bool sign = std::signbit(value);
        if (value == 0) {
          ++stats.repeat_or_zero_modes;
          continue;
        }
        const double magnitude = std::abs(value);
        if (std::isfinite(value) && sign == previous_sign &&
            std::abs(previous_value - value) <= relative_error * magnitude) {
          ++stats.repeat_or_zero_modes;
          continue;
        }
        if (std::isfinite(value) && sign != previous_sign &&
            std::abs(std::abs(previous_value) - magnitude) <= relative_error * magnitude) {
          ++stats.repeat_or_zero_modes;
          previous_sign = sign;
          previous_value = sign ? -std::abs(previous_value) : std::abs(previous_value);
          continue;
        }

        int64_t q = 0;
        double target_log = 0;
        double recovered_log = 0;
        if (QuantizeLog(magnitude, previous_log, log_max_diff, inverse_step, lower_error,
                        upper_error, &q, &target_log, &recovered_log) &&
            q != 0) {
          AddResidual(AdaptiveQtCodec::ZigZagEncode(q), static_cast<uint64_t>(std::llabs(q)),
                      false, &state, &stats);
          previous_log = recovered_log;
          const double recovered_magnitude = std::exp(recovered_log);
          previous_value = sign ? -recovered_magnitude : recovered_magnitude;
          previous_sign = sign;
        } else {
          if (std::isfinite(value) && value != 0) {
            previous_log = std::isfinite(target_log) && target_log != 0 ? target_log : std::log(magnitude);
            previous_value = value;
            previous_sign = sign;
          }
        }
      }
    }
  }
  return stats;
}

uint64_t Percentile(std::vector<uint64_t> values, double p) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  const size_t index = static_cast<size_t>(std::min<double>(
      values.size() - 1, std::floor(p * static_cast<double>(values.size() - 1))));
  return values[index];
}

uint32_t Percentile32(std::vector<uint32_t> values, double p) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  const size_t index = static_cast<size_t>(std::min<double>(
      values.size() - 1, std::floor(p * static_cast<double>(values.size() - 1))));
  return values[index];
}

void Print(const char *domain, double error, const Stats &stats) {
  std::cout << domain << ',' << error << ',' << stats.residuals << ','
            << stats.repeat_or_zero_modes << ',' << stats.delta_selected << ','
            << stats.rice_selected << ','
            << static_cast<double>(stats.delta_selected) / std::max<uint64_t>(1, stats.residuals)
            << ',' << static_cast<double>(stats.rice_fallbacks) / std::max<uint64_t>(1, stats.residuals)
            << ',' << static_cast<double>(stats.delta_wins_pointwise) /
                   std::max<uint64_t>(1, stats.residuals)
            << ',' << static_cast<double>(stats.sum_selected_extra_bits_without_delta) /
                   std::max<uint64_t>(1, stats.delta_selected)
            << ',' << static_cast<double>(stats.sum_pointwise_delta_saving) /
                   std::max<uint64_t>(1, stats.delta_wins_pointwise)
            << ',' << static_cast<double>(stats.sum_abs_q) / std::max<uint64_t>(1, stats.residuals)
            << ',' << Percentile(stats.mapped_values, 0.5) << ','
            << Percentile(stats.mapped_values, 0.9) << ',' << Percentile(stats.mapped_values, 0.99)
            << ',' << Percentile(stats.mapped_values, 0.999) << ','
            << Percentile32(stats.rice_parameters, 0.5) << ','
            << Percentile32(stats.rice_parameters, 0.9) << '\n';
}

}  // namespace

int main(int argc, char **argv) {
  const std::filesystem::path dataset_dir =
      argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("test/data_set");
  std::vector<std::vector<double>> datasets;
  for (const std::string &dataset : kDataSetList) {
    datasets.push_back(ReadValues(dataset_dir / dataset));
  }

  std::cout << std::setprecision(12);
  std::cout << "Domain,Error,Residuals,RepeatOrZeroModes,DeltaSelected,RiceSelected,"
               "DeltaSelectedRate,RiceFallbackRate,PointwiseDeltaWinRate,"
               "AvgExtraBitsWhenSelectedDeltaRemoved,AvgPointwiseDeltaSaving,"
               "AvgAbsQ,MappedP50,MappedP90,MappedP99,MappedP999,RiceK_P50,RiceK_P90\n";
  for (double error : kMaxDiffList) Print("Absolute", error, AnalyzeAbsolute(datasets, error));
  for (double error : kMaxDiffRel) Print("RelativeZigZag", error, AnalyzeRelative(datasets, error));
}
