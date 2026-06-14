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

enum class Strategy { kCap12, kCap20, kDual };
enum class Codec { kGamma, kDelta, kLegacy12, kCapped12, kLegacy20, kCapped20 };

const char *Name(Strategy strategy) {
  if (strategy == Strategy::kCap12) return "Cap12";
  if (strategy == Strategy::kCap20) return "Cap20";
  return "Dual12_20";
}

struct State {
  uint64_t delta = 0;
  uint64_t gamma = 0;
  uint64_t legacy12 = 0;
  uint64_t capped12 = 0;
  uint64_t legacy20 = 0;
  uint64_t capped20 = 0;
  uint64_t magnitude_sum = 0;
  uint64_t sample_count = 0;
};

struct Choice {
  Codec codec = Codec::kDelta;
  uint32_t parameter = 0;
};

struct Result {
  uint64_t values = 0;
  uint64_t bits = 0;
};

uint32_t Estimate(const State &state, uint32_t cap) {
  if (state.magnitude_sum == 0 || state.sample_count == 0) return 0;
  const int estimate =
      static_cast<int>(AdaptiveQtCodec::FloorLog2(state.magnitude_sum)) -
      static_cast<int>(AdaptiveQtCodec::FloorLog2(state.sample_count));
  return static_cast<uint32_t>(std::clamp(estimate, 0, static_cast<int>(cap)));
}

Choice Select(const State &state, Strategy strategy) {
  Choice choice;
  uint64_t cost = state.delta;
  if (state.gamma < cost) {
    choice.codec = Codec::kGamma;
    cost = state.gamma;
  }
  if (strategy != Strategy::kCap20) {
    choice.parameter = Estimate(state, 12);
    if (state.legacy12 < cost) {
      choice.codec = Codec::kLegacy12;
      cost = state.legacy12;
    }
    if (state.capped12 < cost) {
      choice.codec = Codec::kCapped12;
      cost = state.capped12;
    }
  }
  if (strategy != Strategy::kCap12) {
    const uint32_t parameter20 = Estimate(state, 20);
    if (state.legacy20 < cost) {
      choice.codec = Codec::kLegacy20;
      choice.parameter = parameter20;
      cost = state.legacy20;
    }
    if (state.capped20 < cost) {
      choice.codec = Codec::kCapped20;
      choice.parameter = parameter20;
    }
  }
  return choice;
}

bool IsCapped(Codec codec) {
  return codec == Codec::kCapped12 || codec == Codec::kCapped20;
}

bool IsLegacy(Codec codec) {
  return codec == Codec::kLegacy12 || codec == Codec::kLegacy20;
}

void Update(uint64_t mapped, Strategy strategy, State *state) {
  const uint32_t parameter12 = Estimate(*state, 12);
  const uint32_t parameter20 = Estimate(*state, 20);
  const AdaptiveQtCodec::AdaptiveRiceFormatLengths lengths12 =
      AdaptiveQtCodec::CalculateAdaptiveRiceFormatLengths(mapped, parameter12);
  const AdaptiveQtCodec::AdaptiveRiceFormatLengths lengths20 =
      parameter20 == parameter12 ? lengths12
                                 : AdaptiveQtCodec::CalculateAdaptiveRiceFormatLengths(mapped,
                                                                                       parameter20);
  state->delta = AdaptiveQtCodec::DecayAndAdd(state->delta, lengths12.legacy.delta);
  state->gamma = AdaptiveQtCodec::DecayAndAdd(state->gamma, lengths12.legacy.gamma);
  if (strategy != Strategy::kCap20) {
    state->legacy12 = AdaptiveQtCodec::DecayAndAdd(state->legacy12, lengths12.legacy.rice);
    state->capped12 = AdaptiveQtCodec::DecayAndAdd(state->capped12, lengths12.capped_rice);
  }
  if (strategy != Strategy::kCap12) {
    state->legacy20 = AdaptiveQtCodec::DecayAndAdd(state->legacy20, lengths20.legacy.rice);
    state->capped20 = AdaptiveQtCodec::DecayAndAdd(state->capped20, lengths20.capped_rice);
  }
  state->magnitude_sum = AdaptiveQtCodec::DecayAndAdd(
      state->magnitude_sum, std::min(mapped - 1, AdaptiveQtCodec::kAdaptiveRiceMagnitudeCap));
  state->sample_count = AdaptiveQtCodec::DecayAndAdd(state->sample_count, 1);
}

uint64_t Length(uint64_t mapped, const Choice &choice) {
  const AdaptiveQtCodec::AdaptiveRiceFormatLengths lengths =
      AdaptiveQtCodec::CalculateAdaptiveRiceFormatLengths(mapped, choice.parameter);
  if (choice.codec == Codec::kGamma) return lengths.legacy.gamma;
  if (choice.codec == Codec::kDelta) return lengths.legacy.delta;
  return IsCapped(choice.codec) ? lengths.capped_rice : lengths.legacy.rice;
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

Result Simulate(const std::filesystem::path &path, double requested_max_diff, Strategy strategy) {
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
      const Choice choice = Select(state, strategy);
      int64_t q = 0;
      double recovered = 0;
      if (Quantize(value, previous, step, max_diff, &q, &recovered)) {
        const uint64_t mapped = AdaptiveQtCodec::ZigZagEncode(q) + 1;
        result.bits += Length(mapped, choice);
        Update(mapped, strategy, &state);
        previous = recovered;
      } else {
        if (IsLegacy(choice.codec)) {
          result.bits += 66;
        } else if (IsCapped(choice.codec)) {
          result.bits += AdaptiveQtCodec::kAdaptiveRiceFormatQuotientCap + 65;
        } else {
          const AdaptiveQtCodec::IntegerCodec codec =
              choice.codec == Codec::kGamma ? AdaptiveQtCodec::IntegerCodec::kGamma
                                             : AdaptiveQtCodec::IntegerCodec::kDelta;
          result.bits += AdaptiveQtCodec::EncodedLength(kEscape, codec, 0) + 64;
        }
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
  std::cout << "Strategy,Error,Dataset,Values,Bits,CompressionRatio\n";
  for (Strategy strategy : {Strategy::kCap12, Strategy::kCap20, Strategy::kDual}) {
    for (double error : kMaxDiffList) {
      for (const std::string &dataset : kDataSetList) {
        const Result result = Simulate(dataset_dir / dataset, error, strategy);
        std::cout << Name(strategy) << ',' << error << ',' << dataset << ',' << result.values << ','
                  << result.bits << ','
                  << static_cast<double>(result.bits) / (64.0 * result.values) << '\n';
      }
    }
  }
}
