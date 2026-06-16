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
constexpr size_t kModeCount = 6;

enum Mode : size_t {
  kRepeat,
  kSameSignResidual,
  kChangedSignResidual,
  kChangedSignZeroResidual,
  kZero,
  kRaw
};

constexpr std::array<const char *, kModeCount> kModeNames = {
    "Repeat", "SameSignResidual", "ChangedSignResidual", "ChangedSignZeroResidual", "Zero",
    "Raw"};
constexpr std::array<uint64_t, kModeCount> kCurrentPrefixLengths = {1, 2, 3, 4, 5, 5};

struct Counts {
  std::array<uint64_t, kModeCount> modes{};
};

bool QuantizeLog(double magnitude, double prediction, double log_max_diff, double inverse_step,
                 double lower_error, double upper_error, int64_t *q, double *target_log,
                 double *recovered_log) {
  if (!std::isfinite(magnitude) || magnitude == 0) return false;
  *target_log = std::log(magnitude);
  const double scaled = (*target_log - prediction) * inverse_step;
  if (!std::isfinite(scaled)) return false;
  if (std::abs(scaled) > 0x1p52) return false;
  *q = static_cast<int64_t>(std::round(scaled));
  if (*q == std::numeric_limits<int64_t>::min()) return false;
  *recovered_log = prediction + 2 * log_max_diff * static_cast<double>(*q);
  const double error = *recovered_log - *target_log;
  return error >= lower_error && error <= upper_error;
}

AdaptiveQtCodec::AdaptiveDeltaRiceCodeLengths Lengths(uint64_t mapped, uint32_t rice_parameter) {
  const uint64_t delta = AdaptiveQtCodec::DeltaLength(mapped);
  const uint64_t quotient = (mapped - 1) >> rice_parameter;
  const uint64_t rice_direct = quotient + rice_parameter + 1;
  return {delta, quotient < kRiceQuotientCap ? rice_direct : kRiceQuotientCap + delta};
}

void Simulate(const std::filesystem::path &path, double relative_error, Counts *counts) {
  std::ifstream input(path);
  if (!input.is_open()) throw std::runtime_error("Failed to open " + path.string());

  const double upper_error = std::log1p(relative_error);
  const double lower_error =
      relative_error < 1 ? std::log1p(-relative_error) : -std::numeric_limits<double>::infinity();
  const double log_max_diff = upper_error * 0.999;
  const double inverse_step = 1.0 / (2 * log_max_diff);
  AdaptiveQtCodec::AdaptiveDeltaRiceState state;
  double previous_log = 0;
  double previous_value = 1;
  bool previous_sign = false;
  std::vector<double> block;

  while ((block = ReadBlock(input, kBlockSizeOverall)).size() == kBlockSizeOverall) {
    for (double value : block) {
      const bool sign = std::signbit(value);
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
          mode = kChangedSignZeroResidual;
          previous_sign = sign;
          previous_value = sign ? -std::abs(previous_value) : std::abs(previous_value);
        } else {
          int64_t q = 0;
          double target_log = 0;
          double recovered_log = 0;
          const bool quantized =
              QuantizeLog(magnitude, previous_log, log_max_diff, inverse_step, lower_error,
                          upper_error, &q, &target_log, &recovered_log);
          if (quantized && q != 0) {
            const uint32_t rice_parameter = AdaptiveQtCodec::EstimateRiceParameter(state);
            const uint64_t mapped = AdaptiveQtCodec::ZigZagEncode(q);
            const AdaptiveQtCodec::AdaptiveDeltaRiceCodeLengths lengths =
                Lengths(mapped, rice_parameter);
            const uint64_t prefix_bits = sign == previous_sign ? 2 : 3;
            if (prefix_bits + lengths.rice < 69) {
              mode = sign == previous_sign ? kSameSignResidual : kChangedSignResidual;
              previous_log = recovered_log;
              const double recovered_magnitude = std::exp(recovered_log);
              previous_value = sign ? -recovered_magnitude : recovered_magnitude;
              previous_sign = sign;
              AdaptiveQtCodec::UpdateAdaptiveDeltaRiceState(mapped, lengths, &state);
            }
          }
          if (mode == kRaw && std::isfinite(value) && value != 0) {
            previous_log = quantized ? target_log : std::log(magnitude);
            previous_value = value;
            previous_sign = sign;
          }
        }
      }
      ++counts->modes[mode];
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

  uint64_t values = 0;
  uint64_t current_cost = 0;
  for (size_t mode = 0; mode < kModeCount; ++mode) {
    values += counts.modes[mode];
    current_cost += counts.modes[mode] * kCurrentPrefixLengths[mode];
  }
  const uint64_t static_huffman = HuffmanCost(counts.modes);

  std::cout << std::setprecision(12);
  std::cout << "Scheme,PrefixBits,BitsPerValue,VsCurrentPrefixPct\n";
  std::cout << "Current," << current_cost << ',' << static_cast<double>(current_cost) / values
            << ",0\n";
  std::cout << "StaticHuffmanLowerBound," << static_huffman << ','
            << static_cast<double>(static_huffman) / values << ','
            << 100.0 * (static_cast<double>(static_huffman) / current_cost - 1.0) << '\n';
  std::cout << "Mode,Count,Share\n";
  for (size_t mode = 0; mode < kModeCount; ++mode) {
    std::cout << kModeNames[mode] << ',' << counts.modes[mode] << ','
              << static_cast<double>(counts.modes[mode]) / values << '\n';
  }
}
