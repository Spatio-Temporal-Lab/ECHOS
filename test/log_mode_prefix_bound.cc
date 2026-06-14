#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

#include "Perf_expr_config.hpp"
#include "Perf_file_utils.hpp"
#include "utils/adaptive_qt_codec.h"

namespace {

constexpr uint32_t kRiceQuotientCap = 16;
constexpr size_t kModeCount = 8;

enum Mode : size_t {
  kRepeat,
  kSameNegative,
  kSamePositive,
  kChangedNegative,
  kChangedPositive,
  kChangedZero,
  kZero,
  kRaw
};

constexpr std::array<uint64_t, kModeCount> kCurrentPrefixLengths = {1, 2, 3, 4, 5, 6, 7, 7};

struct Counts {
  std::array<uint64_t, kModeCount> modes{};
  std::array<std::array<uint64_t, kModeCount>, kModeCount> by_previous_mode{};
  std::array<std::array<uint64_t, kModeCount>, 2> by_previous_repeat{};
  std::array<std::array<uint64_t, kModeCount>, 2> by_previous_sign{};
};

uint64_t SelectedLength(const AdaptiveQtCodec::AdaptiveCodeLengths &lengths,
                        AdaptiveQtCodec::IntegerCodec codec) {
  if (codec == AdaptiveQtCodec::IntegerCodec::kGamma) return lengths.gamma;
  if (codec == AdaptiveQtCodec::IntegerCodec::kDelta) return lengths.delta;
  return lengths.rice;
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

void AddMode(Mode mode, Mode previous_mode, bool previous_sign, Counts *counts) {
  ++counts->modes[mode];
  ++counts->by_previous_mode[previous_mode][mode];
  ++counts->by_previous_repeat[previous_mode == kRepeat][mode];
  ++counts->by_previous_sign[previous_sign][mode];
}

void Simulate(const std::filesystem::path &path, double relative_error, Counts *counts) {
  std::ifstream input(path);
  if (!input.is_open()) throw std::runtime_error("Failed to open " + path.string());

  const double upper_error = std::log1p(relative_error);
  const double lower_error =
      relative_error < 1 ? std::log1p(-relative_error) : -std::numeric_limits<double>::infinity();
  const double step = 2 * upper_error * 0.999;
  AdaptiveQtCodec::AdaptiveRiceState state;
  double previous_log = 0;
  double previous_value = 1;
  bool previous_sign = false;
  Mode previous_mode = kRaw;
  std::vector<double> block;

  while ((block = ReadBlock(input, kBlockSizeOverall)).size() == kBlockSizeOverall) {
    for (double value : block) {
      const bool sign = std::signbit(value);
      const bool context_sign = previous_sign;
      Mode mode = kRaw;
      if (value == 0) {
        mode = kZero;
      } else {
        const double magnitude = std::abs(value);
        if (std::isfinite(value) && sign == previous_sign &&
            std::abs(previous_value - value) <= relative_error * magnitude) {
          mode = kRepeat;
        } else if (std::isfinite(value) && sign != previous_sign &&
                   std::abs(std::abs(previous_value) - magnitude) <= relative_error * magnitude) {
          mode = kChangedZero;
          previous_sign = sign;
          previous_value = sign ? -std::abs(previous_value) : std::abs(previous_value);
        } else {
          const double target_log = std::log(magnitude);
          int64_t q = 0;
          double recovered_log = 0;
          const bool quantized =
              Quantize(target_log, previous_log, step, upper_error, lower_error, &q, &recovered_log);
          const bool changed_sign = sign != previous_sign;
          if (quantized && q != 0) {
            const bool positive = q > 0;
            const uint64_t mapped = positive ? static_cast<uint64_t>(q) : static_cast<uint64_t>(-q);
            const AdaptiveQtCodec::AdaptiveRiceChoice choice =
                AdaptiveQtCodec::SelectAdaptiveRiceCodec(state);
            const AdaptiveQtCodec::AdaptiveCodeLengths lengths =
                AdaptiveQtCodec::CalculateCappedRiceCodeLengths(mapped, choice.rice_parameter,
                                                                kRiceQuotientCap);
            const uint64_t encoded =
                (changed_sign ? 4 : 2) + static_cast<uint64_t>(positive) +
                SelectedLength(lengths, choice.codec);
            if (encoded < 71) {
              mode = changed_sign ? (positive ? kChangedPositive : kChangedNegative)
                                  : (positive ? kSamePositive : kSameNegative);
              previous_log = recovered_log;
              const double recovered_magnitude = std::exp(recovered_log);
              previous_value = sign ? -recovered_magnitude : recovered_magnitude;
              previous_sign = sign;
              AdaptiveQtCodec::UpdateAdaptiveRiceState(mapped, lengths, &state);
            }
          }
          if (mode == kRaw && std::isfinite(value)) {
            previous_log = target_log;
            previous_value = value;
            previous_sign = sign;
          }
        }
      }
      AddMode(mode, previous_mode, context_sign, counts);
      previous_mode = mode;
    }
  }
}

uint64_t HuffmanCost(const std::array<uint64_t, kModeCount> &counts) {
  std::priority_queue<uint64_t, std::vector<uint64_t>, std::greater<uint64_t>> queue;
  for (uint64_t count : counts) {
    if (count != 0) queue.push(count);
  }
  if (queue.size() == 1) return queue.top();
  uint64_t cost = 0;
  while (queue.size() > 1) {
    const uint64_t first = queue.top();
    queue.pop();
    const uint64_t second = queue.top();
    queue.pop();
    cost += first + second;
    queue.push(first + second);
  }
  return cost;
}

template <size_t N>
uint64_t ContextHuffmanCost(const std::array<std::array<uint64_t, kModeCount>, N> &contexts) {
  uint64_t cost = 0;
  for (const auto &counts : contexts) cost += HuffmanCost(counts);
  return cost;
}

}  // namespace

int main(int argc, char **argv) {
  const std::filesystem::path dataset_dir =
      argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("test/data_set");
  Counts counts;
  for (double error : kMaxDiffRel) {
    for (const std::string &dataset : kDataSetList) {
      Simulate(dataset_dir / dataset, error, &counts);
    }
  }

  uint64_t current_cost = 0;
  uint64_t values = 0;
  for (size_t mode = 0; mode < kModeCount; ++mode) {
    current_cost += counts.modes[mode] * kCurrentPrefixLengths[mode];
    values += counts.modes[mode];
  }
  const uint64_t static_huffman = HuffmanCost(counts.modes);
  const uint64_t repeat_context = ContextHuffmanCost(counts.by_previous_repeat);
  const uint64_t sign_context = ContextHuffmanCost(counts.by_previous_sign);
  const uint64_t mode_context = ContextHuffmanCost(counts.by_previous_mode);

  std::cout << std::setprecision(12);
  std::cout << "Scheme,PrefixBits,BitsPerValue,VsCurrentPrefixPct\n";
  for (const auto &[name, bits] :
       std::array<std::pair<const char *, uint64_t>, 5>{
           std::pair{"Current", current_cost}, std::pair{"StaticHuffman", static_huffman},
           std::pair{"PreviousRepeatHuffman", repeat_context},
           std::pair{"PreviousSignHuffman", sign_context},
           std::pair{"PreviousModeHuffman", mode_context}}) {
    std::cout << name << ',' << bits << ',' << static_cast<double>(bits) / values << ','
              << 100.0 * (static_cast<double>(bits) / current_cost - 1.0) << '\n';
  }
  std::cout << "Mode,Count\n";
  for (size_t mode = 0; mode < kModeCount; ++mode) {
    std::cout << mode << ',' << counts.modes[mode] << '\n';
  }
}
