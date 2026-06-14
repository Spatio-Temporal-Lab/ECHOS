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

constexpr uint32_t kLogRiceQuotientCap = 16;
constexpr uint32_t kNoCapExponent = 64;
constexpr std::array<uint32_t, 16> kMagnitudeCapExponents = {
    8, 12, 16, 18, 19, 20, 21, 22, 23, 24, 25, 26, 28, 32, 48, kNoCapExponent};

struct Result {
  uint64_t values = 0;
  uint64_t bits = 0;
  uint64_t clipped_values = 0;
  uint64_t raw_values = 0;
  uint32_t max_parameter = 0;
};

uint64_t MagnitudeCap(uint32_t exponent) {
  return exponent == kNoCapExponent ? std::numeric_limits<uint64_t>::max() : 1ULL << exponent;
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

void UpdateMagnitude(uint64_t mapped, uint64_t magnitude_cap,
                     AdaptiveQtCodec::AdaptiveRiceState *state, Result *result) {
  const uint64_t magnitude = mapped - 1;
  if (magnitude > magnitude_cap) ++result->clipped_values;
  state->magnitude_sum =
      AdaptiveQtCodec::DecayAndAdd(state->magnitude_sum, std::min(magnitude, magnitude_cap));
  state->sample_count = AdaptiveQtCodec::DecayAndAdd(state->sample_count, 1);
}

void UpdateAbsolute(uint64_t mapped, uint64_t magnitude_cap,
                    const AdaptiveQtCodec::AdaptiveRiceFormatLengths &lengths,
                    AdaptiveQtCodec::AdaptiveRiceState *state, Result *result) {
  state->delta_cost = AdaptiveQtCodec::DecayAndAdd(state->delta_cost, lengths.legacy.delta);
  state->gamma_cost = AdaptiveQtCodec::DecayAndAdd(state->gamma_cost, lengths.legacy.gamma);
  state->rice_cost = AdaptiveQtCodec::DecayAndAdd(state->rice_cost, lengths.legacy.rice);
  state->capped_rice_cost =
      AdaptiveQtCodec::DecayAndAdd(state->capped_rice_cost, lengths.capped_rice);
  UpdateMagnitude(mapped, magnitude_cap, state, result);
}

void UpdateLog(uint64_t mapped, uint64_t magnitude_cap,
               const AdaptiveQtCodec::AdaptiveCodeLengths &lengths,
               AdaptiveQtCodec::AdaptiveRiceState *state, Result *result) {
  state->delta_cost = AdaptiveQtCodec::DecayAndAdd(state->delta_cost, lengths.delta);
  state->gamma_cost = AdaptiveQtCodec::DecayAndAdd(state->gamma_cost, lengths.gamma);
  state->rice_cost = AdaptiveQtCodec::DecayAndAdd(state->rice_cost, lengths.rice);
  UpdateMagnitude(mapped, magnitude_cap, state, result);
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
                        uint64_t magnitude_cap) {
  std::ifstream input(path);
  if (!input.is_open()) throw std::runtime_error("Failed to open " + path.string());
  constexpr uint64_t kEscape = std::numeric_limits<uint64_t>::max();
  const double max_diff = requested_max_diff * 0.999;
  const double step = 2 * max_diff;
  AdaptiveQtCodec::AdaptiveRiceState state;
  double previous = 2;
  bool first_block = true;
  Result result;
  std::vector<double> block;
  while ((block = ReadBlock(input, kBlockSizeOverall)).size() == kBlockSizeOverall) {
    result.bits += first_block ? 82 : 2;
    first_block = false;
    for (double value : block) {
      const AdaptiveQtCodec::AdaptiveRiceChoice choice =
          AdaptiveQtCodec::SelectAdaptiveRiceCodecAndFormat(state);
      result.max_parameter = std::max(result.max_parameter, choice.rice_parameter);
      int64_t q = 0;
      double recovered = 0;
      if (Quantize(value, previous, step, max_diff, &q, &recovered)) {
        const uint64_t mapped = AdaptiveQtCodec::ZigZagEncode(q) + 1;
        const AdaptiveQtCodec::AdaptiveRiceFormatLengths lengths =
            AdaptiveQtCodec::CalculateAdaptiveRiceFormatLengths(mapped, choice.rice_parameter);
        result.bits += SelectedAbsoluteLength(choice, lengths);
        UpdateAbsolute(mapped, magnitude_cap, lengths, &state, &result);
        previous = recovered;
      } else {
        if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
          result.bits += choice.capped_rice ? AdaptiveQtCodec::kAdaptiveRiceFormatQuotientCap + 65
                                           : 66;
        } else {
          result.bits += AdaptiveQtCodec::EncodedLength(kEscape, choice.codec, choice.rice_parameter) +
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

Result SimulateLog(const std::filesystem::path &path, double relative_error, uint64_t magnitude_cap) {
  std::ifstream input(path);
  if (!input.is_open()) throw std::runtime_error("Failed to open " + path.string());
  const double upper_error = std::log1p(relative_error);
  const double lower_error =
      relative_error < 1 ? std::log1p(-relative_error) : -std::numeric_limits<double>::infinity();
  const double log_max_diff = upper_error * 0.999;
  const double step = 2 * log_max_diff;
  AdaptiveQtCodec::AdaptiveRiceState state;
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

      const AdaptiveQtCodec::AdaptiveRiceChoice choice = AdaptiveQtCodec::SelectAdaptiveRiceCodec(state);
      result.max_parameter = std::max(result.max_parameter, choice.rice_parameter);
      const double target_log = std::log(magnitude);
      int64_t q = 0;
      double recovered_log = 0;
      const bool quantized =
          Quantize(target_log, previous_log, step, upper_error, &q, &recovered_log) &&
          recovered_log - target_log >= lower_error;
      const bool changed_sign = sign != previous_sign;
      if (quantized && q != 0) {
        const bool positive = q > 0;
        const uint64_t mapped = positive ? static_cast<uint64_t>(q) : static_cast<uint64_t>(-q);
        const AdaptiveQtCodec::AdaptiveCodeLengths lengths =
            AdaptiveQtCodec::CalculateCappedRiceCodeLengths(mapped, choice.rice_parameter,
                                                            kLogRiceQuotientCap);
        const uint64_t encoded =
            (changed_sign ? 4 : 2) + static_cast<uint64_t>(positive) +
            SelectedLogLength(choice, lengths);
        if (encoded < 71) {
          result.bits += encoded;
          previous_log = recovered_log;
          const double recovered_magnitude = std::exp(recovered_log);
          previous_value = sign ? -recovered_magnitude : recovered_magnitude;
          previous_sign = sign;
          UpdateLog(mapped, magnitude_cap, lengths, &state, &result);
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

void Print(const char *method, uint32_t cap_exponent, double error, const std::string &dataset,
           const Result &result) {
  std::cout << method << ',' << (cap_exponent == kNoCapExponent ? "None" : std::to_string(cap_exponent))
            << ',' << error << ',' << dataset << ',' << result.values << ',' << result.bits << ','
            << static_cast<double>(result.bits) / (64.0 * result.values) << ','
            << result.clipped_values << ',' << result.raw_values << ',' << result.max_parameter
            << '\n';
}

}  // namespace

int main(int argc, char **argv) {
  const std::filesystem::path dataset_dir =
      argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("test/data_set");
  const std::string method = argc > 2 ? argv[2] : "all";
  std::cout << std::setprecision(12);
  std::cout << "Method,CapExponent,Error,Dataset,Values,Bits,CompressionRatio,ClippedValues,"
               "RawValues,MaxParameter\n";
  for (uint32_t cap_exponent : kMagnitudeCapExponents) {
    const uint64_t magnitude_cap = MagnitudeCap(cap_exponent);
    if (method != "log") {
      for (double error : kMaxDiffList) {
        for (const std::string &dataset : kDataSetList) {
          Print("AdaptiveRice", cap_exponent, error, dataset,
                SimulateAbsolute(dataset_dir / dataset, error, magnitude_cap));
        }
      }
    }
    if (method != "absolute") {
      for (double error : kMaxDiffRel) {
        for (const std::string &dataset : kDataSetList) {
          Print("LogSerfQt", cap_exponent, error, dataset,
                SimulateLog(dataset_dir / dataset, error, magnitude_cap));
        }
      }
    }
  }
}
