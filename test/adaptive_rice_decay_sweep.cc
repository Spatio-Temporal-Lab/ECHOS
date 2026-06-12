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
#include "Perf_file_utils.hpp"
#include "utils/adaptive_qt_codec.h"

namespace {

constexpr int kMinShift = 1;
constexpr int kMaxShift = 10;

struct Result {
  uint64_t values = 0;
  uint64_t bits = 0;
  uint64_t gamma_values = 0;
  uint64_t delta_values = 0;
  uint64_t rice_values = 0;
  uint64_t raw_values = 0;
  uint64_t codec_switches = 0;
};

uint64_t DecayAndAdd(uint64_t cost, uint64_t value, uint32_t shift) {
  return AdaptiveQtCodec::SaturatingAdd(cost - (cost >> shift), value);
}

void UpdateState(uint64_t mapped, uint32_t rice_parameter, uint32_t shift,
                 AdaptiveQtCodec::AdaptiveRiceState *state) {
  const AdaptiveQtCodec::AdaptiveCodeLengths lengths =
      AdaptiveQtCodec::CalculateAdaptiveCodeLengths(mapped, rice_parameter);
  state->delta_cost = DecayAndAdd(state->delta_cost, lengths.delta, shift);
  state->gamma_cost = DecayAndAdd(state->gamma_cost, lengths.gamma, shift);
  state->rice_cost = DecayAndAdd(state->rice_cost, lengths.rice, shift);
  state->magnitude_sum =
      DecayAndAdd(state->magnitude_sum,
                  std::min(mapped - 1, AdaptiveQtCodec::kAdaptiveRiceMagnitudeCap), shift);
  state->sample_count = DecayAndAdd(state->sample_count, 1, shift);
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

Result Simulate(const std::filesystem::path &path, double requested_max_diff, uint32_t shift) {
  std::ifstream input(path);
  if (!input.is_open()) throw std::runtime_error("Failed to open " + path.string());

  constexpr uint64_t kEscape = std::numeric_limits<uint64_t>::max();
  const double max_diff = requested_max_diff * 0.999;
  AdaptiveQtCodec::AdaptiveRiceState state;
  AdaptiveQtCodec::IntegerCodec previous_codec = AdaptiveQtCodec::IntegerCodec::kRaw;
  double previous = 2;
  bool first_block = true;
  Result result;
  std::vector<double> block;

  while ((block = ReadBlock(input, kBlockSizeOverall)).size() == kBlockSizeOverall) {
    result.bits += first_block ? 82 : 2;
    first_block = false;

    for (double value : block) {
      const AdaptiveQtCodec::AdaptiveRiceChoice choice =
          AdaptiveQtCodec::SelectAdaptiveRiceCodec(state);
      if (result.values > 0 && choice.codec != previous_codec) ++result.codec_switches;
      previous_codec = choice.codec;

      int64_t q = 0;
      double recovered = 0;
      if (Quantize(value, previous, max_diff, &q, &recovered)) {
        const uint64_t mapped = AdaptiveQtCodec::ZigZagEncode(q) + 1;
        switch (choice.codec) {
          case AdaptiveQtCodec::IntegerCodec::kGamma:
            result.bits += AdaptiveQtCodec::GammaLength(mapped);
            ++result.gamma_values;
            break;
          case AdaptiveQtCodec::IntegerCodec::kDelta:
            result.bits += AdaptiveQtCodec::DeltaLength(mapped);
            ++result.delta_values;
            break;
          case AdaptiveQtCodec::IntegerCodec::kRice:
            result.bits += AdaptiveQtCodec::AdaptiveRiceLength(mapped, choice.rice_parameter);
            ++result.rice_values;
            break;
          case AdaptiveQtCodec::IntegerCodec::kRaw:
            throw std::runtime_error("Unexpected raw adaptive codec");
        }
        UpdateState(mapped, choice.rice_parameter, shift, &state);
        previous = recovered;
      } else {
        result.bits +=
            choice.codec == AdaptiveQtCodec::IntegerCodec::kRice
                ? 66
                : AdaptiveQtCodec::EncodedLength(kEscape, choice.codec, choice.rice_parameter) + 64;
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
  std::cout << "Shift,MaxDiff,Dataset,Values,Bits,CompressionRatio,GammaValues,DeltaValues,"
               "RiceValues,RawValues,CodecSwitches\n";

  for (int shift = kMinShift; shift <= kMaxShift; ++shift) {
    for (double max_diff : kMaxDiffList) {
      for (const std::string &dataset : kDataSetList) {
        const Result result = Simulate(dataset_dir / dataset, max_diff, shift);
        std::cout << shift << ',' << max_diff << ',' << dataset << ',' << result.values << ','
                  << result.bits << ','
                  << static_cast<double>(result.bits) / (64.0 * result.values) << ','
                  << result.gamma_values << ',' << result.delta_values << ',' << result.rice_values
                  << ',' << result.raw_values << ',' << result.codec_switches << '\n';
      }
    }
  }
  return 0;
}
