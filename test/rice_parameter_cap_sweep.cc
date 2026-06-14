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

struct Result {
  uint64_t values = 0;
  uint64_t bits = 0;
  uint64_t raw_values = 0;
};

uint32_t EstimateParameter(const AdaptiveQtCodec::AdaptiveRiceState &state, uint32_t max_parameter) {
  if (state.magnitude_sum == 0 || state.sample_count == 0) return 0;
  const int estimate =
      static_cast<int>(AdaptiveQtCodec::FloorLog2(state.magnitude_sum)) -
      static_cast<int>(AdaptiveQtCodec::FloorLog2(state.sample_count));
  return static_cast<uint32_t>(std::clamp(estimate, 0, static_cast<int>(max_parameter)));
}

AdaptiveQtCodec::AdaptiveRiceChoice Select(const AdaptiveQtCodec::AdaptiveRiceState &state,
                                           uint32_t max_parameter) {
  AdaptiveQtCodec::AdaptiveRiceChoice choice{
      AdaptiveQtCodec::IntegerCodec::kDelta, EstimateParameter(state, max_parameter), false};
  uint64_t cost = state.delta_cost;
  if (state.gamma_cost < cost) {
    choice.codec = AdaptiveQtCodec::IntegerCodec::kGamma;
    cost = state.gamma_cost;
  }
  if (state.rice_cost < cost) {
    choice.codec = AdaptiveQtCodec::IntegerCodec::kRice;
    cost = state.rice_cost;
  }
  if (state.capped_rice_cost < cost) {
    choice.codec = AdaptiveQtCodec::IntegerCodec::kRice;
    choice.capped_rice = true;
  }
  return choice;
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

uint64_t SelectedLength(const AdaptiveQtCodec::AdaptiveRiceChoice &choice,
                        const AdaptiveQtCodec::AdaptiveRiceFormatLengths &lengths) {
  if (choice.codec == AdaptiveQtCodec::IntegerCodec::kGamma) return lengths.legacy.gamma;
  if (choice.codec == AdaptiveQtCodec::IntegerCodec::kDelta) return lengths.legacy.delta;
  return choice.capped_rice ? lengths.capped_rice : lengths.legacy.rice;
}

Result Simulate(const std::filesystem::path &path, double requested_max_diff, uint32_t max_parameter) {
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
      const AdaptiveQtCodec::AdaptiveRiceChoice choice = Select(state, max_parameter);
      int64_t q = 0;
      double recovered = 0;
      if (Quantize(value, previous, step, max_diff, &q, &recovered)) {
        const uint64_t mapped = AdaptiveQtCodec::ZigZagEncode(q) + 1;
        const AdaptiveQtCodec::AdaptiveRiceFormatLengths lengths =
            AdaptiveQtCodec::CalculateAdaptiveRiceFormatLengths(mapped, choice.rice_parameter);
        result.bits += SelectedLength(choice, lengths);
        AdaptiveQtCodec::UpdateAdaptiveRiceFormatState(mapped, lengths, &state);
        previous = recovered;
      } else {
        if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
          result.bits += choice.capped_rice ? AdaptiveQtCodec::kAdaptiveRiceFormatQuotientCap + 65
                                           : 66;
        } else {
          result.bits +=
              AdaptiveQtCodec::EncodedLength(kEscape, choice.codec, choice.rice_parameter) + 64;
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
  constexpr std::array<uint32_t, 7> caps = {8, 12, 16, 20, 24, 32, 48};
  std::cout << std::setprecision(12);
  std::cout << "MaxParameter,Error,Dataset,Values,Bits,CompressionRatio,RawValues\n";
  for (uint32_t cap : caps) {
    for (double error : kMaxDiffList) {
      for (const std::string &dataset : kDataSetList) {
        const Result result = Simulate(dataset_dir / dataset, error, cap);
        std::cout << cap << ',' << error << ',' << dataset << ',' << result.values << ','
                  << result.bits << ','
                  << static_cast<double>(result.bits) / (64.0 * result.values) << ','
                  << result.raw_values << '\n';
      }
    }
  }
}
