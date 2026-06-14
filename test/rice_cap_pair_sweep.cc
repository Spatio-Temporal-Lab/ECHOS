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

constexpr uint32_t kMinCap = 0;
constexpr uint32_t kMaxCap = 20;
constexpr size_t kCapCount = kMaxCap - kMinCap + 1;
constexpr size_t kPairCount = kCapCount * (kCapCount + 1) / 2;

enum class Codec { kGamma, kDelta, kLegacy, kCapped };

struct Pair {
  uint32_t low;
  uint32_t high;
};

struct Choice {
  Codec codec = Codec::kDelta;
  uint32_t cap = kMinCap;
};

struct State {
  uint64_t delta = 0;
  uint64_t gamma = 0;
  std::array<uint64_t, kCapCount> legacy{};
  std::array<uint64_t, kCapCount> capped{};
  uint64_t magnitude_sum = 0;
  uint64_t sample_count = 0;
  uint32_t max_unclamped_parameter = 0;
};

struct Result {
  uint64_t values = 0;
  std::array<uint64_t, kPairCount> bits{};
  uint32_t max_unclamped_parameter = 0;
};

constexpr size_t CapIndex(uint32_t cap) { return cap - kMinCap; }

std::array<Pair, kPairCount> MakePairs() {
  std::array<Pair, kPairCount> pairs{};
  size_t index = 0;
  for (uint32_t low = kMinCap; low <= kMaxCap; ++low) {
    for (uint32_t high = low; high <= kMaxCap; ++high) pairs[index++] = {low, high};
  }
  return pairs;
}

uint32_t UnclampedParameter(const State &state) {
  if (state.magnitude_sum == 0 || state.sample_count == 0) return 0;
  const int estimate =
      static_cast<int>(AdaptiveQtCodec::FloorLog2(state.magnitude_sum)) -
      static_cast<int>(AdaptiveQtCodec::FloorLog2(state.sample_count));
  return static_cast<uint32_t>(std::max(estimate, 0));
}

Choice Select(const State &state, const Pair &pair) {
  Choice choice;
  uint64_t cost = state.delta;
  if (state.gamma < cost) {
    choice.codec = Codec::kGamma;
    cost = state.gamma;
  }
  for (uint32_t cap : {pair.low, pair.high}) {
    const size_t index = CapIndex(cap);
    if (state.legacy[index] < cost) {
      choice.codec = Codec::kLegacy;
      choice.cap = cap;
      cost = state.legacy[index];
    }
    if (state.capped[index] < cost) {
      choice.codec = Codec::kCapped;
      choice.cap = cap;
      cost = state.capped[index];
    }
  }
  return choice;
}

uint64_t Length(uint64_t mapped, const Choice &choice, uint32_t unclamped_parameter) {
  if (choice.codec == Codec::kGamma) return AdaptiveQtCodec::GammaLength(mapped);
  if (choice.codec == Codec::kDelta) return AdaptiveQtCodec::DeltaLength(mapped);
  const uint32_t parameter = std::min(unclamped_parameter, choice.cap);
  const AdaptiveQtCodec::AdaptiveRiceFormatLengths lengths =
      AdaptiveQtCodec::CalculateAdaptiveRiceFormatLengths(mapped, parameter);
  return choice.codec == Codec::kCapped ? lengths.capped_rice : lengths.legacy.rice;
}

void Update(uint64_t mapped, uint32_t unclamped_parameter, State *state) {
  const uint64_t gamma = AdaptiveQtCodec::GammaLength(mapped);
  const uint64_t delta = AdaptiveQtCodec::DeltaLength(mapped);
  state->gamma = AdaptiveQtCodec::DecayAndAdd(state->gamma, gamma);
  state->delta = AdaptiveQtCodec::DecayAndAdd(state->delta, delta);
  for (uint32_t cap = kMinCap; cap <= kMaxCap; ++cap) {
    const AdaptiveQtCodec::AdaptiveRiceFormatLengths lengths =
        AdaptiveQtCodec::CalculateAdaptiveRiceFormatLengths(mapped,
                                                            std::min(unclamped_parameter, cap));
    const size_t index = CapIndex(cap);
    state->legacy[index] = AdaptiveQtCodec::DecayAndAdd(state->legacy[index], lengths.legacy.rice);
    state->capped[index] = AdaptiveQtCodec::DecayAndAdd(state->capped[index], lengths.capped_rice);
  }
  state->magnitude_sum = AdaptiveQtCodec::DecayAndAdd(
      state->magnitude_sum, std::min(mapped - 1, AdaptiveQtCodec::kAdaptiveRiceMagnitudeCap));
  state->sample_count = AdaptiveQtCodec::DecayAndAdd(state->sample_count, 1);
  state->max_unclamped_parameter = std::max(state->max_unclamped_parameter, unclamped_parameter);
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

Result Simulate(const std::filesystem::path &path, double requested_max_diff,
                const std::array<Pair, kPairCount> &pairs) {
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
    const uint64_t metadata_bits = first_block ? 82 : 2;
    for (uint64_t &bits : result.bits) bits += metadata_bits;
    first_block = false;
    for (double value : block) {
      std::array<Choice, kPairCount> choices{};
      for (size_t index = 0; index < pairs.size(); ++index) choices[index] = Select(state, pairs[index]);
      int64_t q = 0;
      double recovered = 0;
      const uint32_t parameter = UnclampedParameter(state);
      if (Quantize(value, previous, step, max_diff, &q, &recovered)) {
        const uint64_t mapped = AdaptiveQtCodec::ZigZagEncode(q) + 1;
        for (size_t index = 0; index < pairs.size(); ++index) {
          result.bits[index] += Length(mapped, choices[index], parameter);
        }
        Update(mapped, parameter, &state);
        previous = recovered;
      } else {
        for (size_t index = 0; index < pairs.size(); ++index) {
          const Choice choice = choices[index];
          if (choice.codec == Codec::kLegacy) {
            result.bits[index] += 66;
          } else if (choice.codec == Codec::kCapped) {
            result.bits[index] += AdaptiveQtCodec::kAdaptiveRiceFormatQuotientCap + 65;
          } else {
            result.bits[index] +=
                (choice.codec == Codec::kGamma ? AdaptiveQtCodec::GammaLength(kEscape)
                                                : AdaptiveQtCodec::DeltaLength(kEscape)) +
                64;
          }
        }
        previous = std::isfinite(value) ? value : 2;
      }
      ++result.values;
    }
  }
  result.max_unclamped_parameter = state.max_unclamped_parameter;
  return result;
}

}  // namespace

int main(int argc, char **argv) {
  const std::filesystem::path dataset_dir =
      argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("test/data_set");
  const std::array<Pair, kPairCount> pairs = MakePairs();
  std::cout << std::setprecision(12);
  std::cout << "Error,LowCap,HighCap,Values,Bits,CompressionRatio,MaxUnclampedParameter\n";
  for (double error : kMaxDiffList) {
    Result aggregate;
    for (const std::string &dataset : kDataSetList) {
      const Result result = Simulate(dataset_dir / dataset, error, pairs);
      aggregate.values += result.values;
      aggregate.max_unclamped_parameter =
          std::max(aggregate.max_unclamped_parameter, result.max_unclamped_parameter);
      for (size_t index = 0; index < pairs.size(); ++index) aggregate.bits[index] += result.bits[index];
    }
    for (size_t index = 0; index < pairs.size(); ++index) {
      std::cout << error << ',' << pairs[index].low << ',' << pairs[index].high << ','
                << aggregate.values << ',' << aggregate.bits[index] << ','
                << static_cast<double>(aggregate.bits[index]) / (64.0 * aggregate.values) << ','
                << aggregate.max_unclamped_parameter << '\n';
    }
  }
}
