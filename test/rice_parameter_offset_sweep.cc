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

enum class Strategy { kCap20Offset0, kCap20OffsetMinus1, kDualCapOffset0, kImplicitOffsets };
enum class Codec { kGamma, kDelta, kLegacy, kCapped };

struct Candidate {
  int offset;
  uint32_t cap;
};

constexpr std::array<Candidate, 4> kCandidates = {
    Candidate{0, 12}, Candidate{0, 20}, Candidate{-1, 12}, Candidate{-1, 20}};

const char *Name(Strategy strategy) {
  switch (strategy) {
    case Strategy::kCap20Offset0:
      return "Cap20Offset0";
    case Strategy::kCap20OffsetMinus1:
      return "Cap20OffsetMinus1";
    case Strategy::kDualCapOffset0:
      return "DualCapOffset0";
    case Strategy::kImplicitOffsets:
      return "ImplicitOffsets";
  }
  return "Unknown";
}

struct State {
  uint64_t delta = 0;
  uint64_t gamma = 0;
  std::array<uint64_t, 4> legacy{};
  std::array<uint64_t, 4> capped{};
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

uint32_t Estimate(const State &state, const Candidate &candidate) {
  if (state.magnitude_sum == 0 || state.sample_count == 0) return 0;
  const int estimate =
      static_cast<int>(AdaptiveQtCodec::FloorLog2(state.magnitude_sum)) -
      static_cast<int>(AdaptiveQtCodec::FloorLog2(state.sample_count)) + candidate.offset;
  return static_cast<uint32_t>(std::clamp(estimate, 0, static_cast<int>(candidate.cap)));
}

bool UsesCandidate(Strategy strategy, size_t index) {
  switch (strategy) {
    case Strategy::kCap20Offset0:
      return index == 1;
    case Strategy::kCap20OffsetMinus1:
      return index == 3;
    case Strategy::kDualCapOffset0:
      return index < 2;
    case Strategy::kImplicitOffsets:
      return true;
  }
  return false;
}

Choice Select(const State &state, Strategy strategy) {
  Choice choice;
  uint64_t cost = state.delta;
  if (state.gamma < cost) {
    choice.codec = Codec::kGamma;
    cost = state.gamma;
  }
  for (size_t index = 0; index < kCandidates.size(); ++index) {
    if (!UsesCandidate(strategy, index)) continue;
    const uint32_t parameter = Estimate(state, kCandidates[index]);
    if (state.legacy[index] < cost) {
      choice.codec = Codec::kLegacy;
      choice.parameter = parameter;
      cost = state.legacy[index];
    }
    if (state.capped[index] < cost) {
      choice.codec = Codec::kCapped;
      choice.parameter = parameter;
      cost = state.capped[index];
    }
  }
  return choice;
}

void Update(uint64_t mapped, Strategy strategy, State *state) {
  std::array<AdaptiveQtCodec::AdaptiveRiceFormatLengths, 4> lengths{};
  for (size_t index = 0; index < kCandidates.size(); ++index) {
    if (!UsesCandidate(strategy, index)) continue;
    lengths[index] =
        AdaptiveQtCodec::CalculateAdaptiveRiceFormatLengths(mapped, Estimate(*state, kCandidates[index]));
  }

  size_t first = 0;
  while (!UsesCandidate(strategy, first)) ++first;
  state->delta = AdaptiveQtCodec::DecayAndAdd(state->delta, lengths[first].legacy.delta);
  state->gamma = AdaptiveQtCodec::DecayAndAdd(state->gamma, lengths[first].legacy.gamma);
  for (size_t index = 0; index < kCandidates.size(); ++index) {
    if (!UsesCandidate(strategy, index)) continue;
    state->legacy[index] =
        AdaptiveQtCodec::DecayAndAdd(state->legacy[index], lengths[index].legacy.rice);
    state->capped[index] =
        AdaptiveQtCodec::DecayAndAdd(state->capped[index], lengths[index].capped_rice);
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
  return choice.codec == Codec::kCapped ? lengths.capped_rice : lengths.legacy.rice;
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
        if (choice.codec == Codec::kLegacy) {
          result.bits += 66;
        } else if (choice.codec == Codec::kCapped) {
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
  for (Strategy strategy : {Strategy::kCap20Offset0, Strategy::kCap20OffsetMinus1,
                            Strategy::kDualCapOffset0, Strategy::kImplicitOffsets}) {
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
