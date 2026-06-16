#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "Perf_expr_config.hpp"
#include "utils/adaptive_qt_codec.h"
#include "utils/array.h"
#include "utils/double.h"
#include "utils/input_bit_stream.h"
#include "utils/output_bit_stream.h"

namespace {

constexpr uint32_t kRiceQuotientCap = 16;
constexpr uint64_t kEscape = std::numeric_limits<uint64_t>::max();
constexpr int kWarmups = 1;
constexpr int kRepetitions = 5;

struct CodecCounts {
  uint64_t gamma = 0;
  uint64_t delta = 0;
  uint64_t rice = 0;
  uint64_t raw = 0;

  CodecCounts &operator+=(const CodecCounts &other) {
    gamma += other.gamma;
    delta += other.delta;
    rice += other.rice;
    raw += other.raw;
    return *this;
  }
};

struct BenchmarkResult {
  uint64_t values = 0;
  uint64_t bits = 0;
  double compression_ms = 0;
  double decompression_ms = 0;
  CodecCounts counts;
  bool valid = true;
};

template <bool UseGamma, bool UseDelta, bool UseRice>
struct CodecState {
  uint64_t gamma_cost = 0;
  uint64_t delta_cost = 0;
  uint64_t rice_cost = 0;
  uint64_t magnitude_sum = 0;
  uint64_t sample_count = 0;
};

struct Choice {
  AdaptiveQtCodec::IntegerCodec codec;
  uint32_t rice_parameter;
};

struct Lengths {
  uint64_t gamma = 0;
  uint64_t delta = 0;
  uint64_t rice = 0;
};

template <bool UseGamma, bool UseDelta, bool UseRice>
uint32_t EstimateRiceParameter(const CodecState<UseGamma, UseDelta, UseRice> &state) {
  if constexpr (!UseRice) return 0;
  if (state.magnitude_sum == 0 || state.sample_count == 0) return 0;
  const uint32_t magnitude_log = AdaptiveQtCodec::FloorLog2(state.magnitude_sum);
  const uint32_t sample_log = AdaptiveQtCodec::FloorLog2(state.sample_count);
  return magnitude_log > sample_log ? magnitude_log - sample_log : 0;
}

template <bool UseGamma, bool UseDelta, bool UseRice>
Choice SelectCodec(const CodecState<UseGamma, UseDelta, UseRice> &state) {
  Choice choice{AdaptiveQtCodec::IntegerCodec::kRaw, EstimateRiceParameter(state)};
  uint64_t selected_cost = std::numeric_limits<uint64_t>::max();
  if constexpr (UseDelta) {
    choice.codec = AdaptiveQtCodec::IntegerCodec::kDelta;
    selected_cost = state.delta_cost;
  }
  if constexpr (UseGamma) {
    if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRaw ||
        state.gamma_cost < selected_cost) {
      choice.codec = AdaptiveQtCodec::IntegerCodec::kGamma;
      selected_cost = state.gamma_cost;
    }
  }
  if constexpr (UseRice) {
    if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRaw ||
        state.rice_cost < selected_cost) {
      choice.codec = AdaptiveQtCodec::IntegerCodec::kRice;
    }
  }
  return choice;
}

template <bool UseGamma, bool UseDelta, bool UseRice, bool RawEscape>
Lengths CalculateLengths(uint64_t mapped, uint32_t rice_parameter) {
  Lengths lengths;
  const uint64_t mapped_log = AdaptiveQtCodec::FloorLog2(mapped);
  const uint64_t value_bits = mapped_log + 1;
  if constexpr (UseGamma) lengths.gamma = 2 * mapped_log + 1;
  if constexpr (UseDelta || UseRice) {
    lengths.delta = AdaptiveQtCodec::GammaLength(value_bits) + value_bits - 1;
  }
  if constexpr (UseRice) {
    const uint64_t quotient = (mapped - 1) >> rice_parameter;
    const uint64_t direct = quotient + rice_parameter + 1;
    lengths.rice = quotient < kRiceQuotientCap
                       ? direct
                       : kRiceQuotientCap + static_cast<uint64_t>(RawEscape) + lengths.delta;
  }
  return lengths;
}

template <bool UseGamma, bool UseDelta, bool UseRice>
void UpdateState(uint64_t mapped, const Lengths &lengths,
                 CodecState<UseGamma, UseDelta, UseRice> *state) {
  if constexpr (UseGamma) {
    state->gamma_cost = AdaptiveQtCodec::DecayAndAdd(state->gamma_cost, lengths.gamma);
  }
  if constexpr (UseDelta) {
    state->delta_cost = AdaptiveQtCodec::DecayAndAdd(state->delta_cost, lengths.delta);
  }
  if constexpr (UseRice) {
    state->rice_cost = AdaptiveQtCodec::DecayAndAdd(state->rice_cost, lengths.rice);
    state->magnitude_sum = AdaptiveQtCodec::DecayAndAdd(
        state->magnitude_sum,
        std::min(mapped - 1, AdaptiveQtCodec::kAdaptiveRiceMagnitudeCap));
    state->sample_count = AdaptiveQtCodec::DecayAndAdd(state->sample_count, 1);
  }
}

uint64_t SelectedLength(const Lengths &lengths, AdaptiveQtCodec::IntegerCodec codec) {
  if (codec == AdaptiveQtCodec::IntegerCodec::kGamma) return lengths.gamma;
  if (codec == AdaptiveQtCodec::IntegerCodec::kDelta) return lengths.delta;
  return lengths.rice;
}

void CountCodec(AdaptiveQtCodec::IntegerCodec codec, CodecCounts *counts) {
  if (codec == AdaptiveQtCodec::IntegerCodec::kGamma) {
    ++counts->gamma;
  } else if (codec == AdaptiveQtCodec::IntegerCodec::kDelta) {
    ++counts->delta;
  } else if (codec == AdaptiveQtCodec::IntegerCodec::kRice) {
    ++counts->rice;
  }
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

  const long double precise_step = 2.0L * static_cast<long double>(max_diff);
  const long double precise_scaled =
      (static_cast<long double>(value) - static_cast<long double>(prediction)) / precise_step;
  if (!std::isfinite(precise_scaled)) return false;
  const long double rounded = std::round(precise_scaled);
  if (rounded <= static_cast<long double>(std::numeric_limits<int64_t>::min()) ||
      rounded > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
    return false;
  }
  *q = static_cast<int64_t>(rounded);
  if (AdaptiveQtCodec::ZigZagEncode(*q) >= kEscape - 1) return false;
  *recovered = prediction + step * static_cast<double>(*q);
  return std::isfinite(*recovered) && std::abs(value - *recovered) <= max_diff;
}

bool QuantizeLog(double magnitude, double prediction, double log_max_diff, double inverse_step,
                 double lower_error, double upper_error, int64_t *q, double *target_log,
                 double *recovered_log) {
  if (!std::isfinite(magnitude) || magnitude == 0) return false;
  *target_log = std::log(magnitude);
  const double scaled = (*target_log - prediction) * inverse_step;
  if (!std::isfinite(scaled)) return false;
  if (std::abs(scaled) > 0x1p52) {
    const long double step = 2.0L * static_cast<long double>(log_max_diff);
    const long double precise_scaled =
        (static_cast<long double>(*target_log) - static_cast<long double>(prediction)) / step;
    if (!std::isfinite(precise_scaled)) return false;
    const long double rounded = std::round(precise_scaled);
    if (rounded <= static_cast<long double>(std::numeric_limits<int64_t>::min()) ||
        rounded > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
      return false;
    }
    *q = static_cast<int64_t>(rounded);
  } else {
    *q = static_cast<int64_t>(std::round(scaled));
  }
  if (*q == std::numeric_limits<int64_t>::min()) return false;
  *recovered_log = prediction + 2 * log_max_diff * static_cast<double>(*q);
  const double error = *recovered_log - *target_log;
  return error >= lower_error && error <= upper_error;
}

template <bool UseGamma, bool UseDelta, bool UseRice>
class AbsoluteCompressor {
 public:
  AbsoluteCompressor(int block_size, double requested_max_diff)
      : block_size_(block_size),
        max_diff_(requested_max_diff * 0.999),
        step_(2 * max_diff_),
        inverse_step_(1.0 / step_),
        output_(std::make_unique<OutputBitStream>(static_cast<uint32_t>(24 * block_size + 32))) {}

  void AddValue(double value) {
    if (value_count_ == 0) WriteMetadata();
    const Choice choice = SelectCodec(state_);
    int64_t q = 0;
    double recovered = 0;
    if (QuantizeAbsolute(value, previous_, max_diff_, step_, inverse_step_, &q, &recovered)) {
      const uint64_t mapped = AdaptiveQtCodec::ZigZagEncode(q) + 1;
      const Lengths lengths = CalculateLengths<UseGamma, UseDelta, UseRice, true>(
          mapped, choice.rice_parameter);
      if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
        bits_ += AdaptiveQtCodec::EncodeCappedRiceWithRaw(
            mapped, choice.rice_parameter, kRiceQuotientCap, output_.get());
      } else {
        bits_ += AdaptiveQtCodec::EncodeMapped(mapped, choice.codec, choice.rice_parameter,
                                               output_.get());
      }
      CountCodec(choice.codec, &counts_);
      UpdateState(mapped, lengths, &state_);
      previous_ = recovered;
    } else {
      if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
        bits_ += AdaptiveQtCodec::WriteCappedRiceRaw(kRiceQuotientCap, output_.get());
      } else {
        bits_ += AdaptiveQtCodec::EncodeMapped(kEscape, choice.codec, choice.rice_parameter,
                                               output_.get());
      }
      bits_ += output_->WriteLong(Double::DoubleToLongBits(value), 64);
      ++counts_.raw;
      previous_ = std::isfinite(value) ? value : 2;
    }
    ++value_count_;
  }

  void Close() {
    output_->Flush();
    compressed_bytes_ = output_->GetBuffer(static_cast<uint32_t>(std::ceil(bits_ / 8.0)));
    output_->Refresh();
    stored_bits_ = bits_;
    bits_ = 0;
    value_count_ = 0;
  }

  Array<uint8_t> compressed_bytes() const { return compressed_bytes_; }
  long get_compressed_size_in_bits() const { return stored_bits_; }
  CodecCounts counts() const { return counts_; }

 private:
  void WriteMetadata() {
    bits_ += output_->WriteBit(!metadata_initialized_);
    bits_ += output_->WriteBit(!metadata_initialized_);
    if (!metadata_initialized_) {
      bits_ += output_->WriteInt(block_size_, 16);
      bits_ += output_->WriteLong(Double::DoubleToLongBits(max_diff_), 64);
      metadata_initialized_ = true;
    }
  }

  int block_size_;
  double max_diff_;
  double step_;
  double inverse_step_;
  std::unique_ptr<OutputBitStream> output_;
  Array<uint8_t> compressed_bytes_;
  double previous_ = 2;
  CodecState<UseGamma, UseDelta, UseRice> state_{};
  CodecCounts counts_{};
  bool metadata_initialized_ = false;
  int value_count_ = 0;
  long bits_ = 0;
  long stored_bits_ = 0;
};

template <bool UseGamma, bool UseDelta, bool UseRice>
class AbsoluteDecompressor {
 public:
  std::vector<double> Decompress(const Array<uint8_t> &bytes) {
    InputBitStream input;
    input.SetBuffer(bytes);
    const bool block_size_changed = input.ReadBit();
    const bool max_diff_changed = input.ReadBit();
    if (block_size_changed) block_size_ = input.ReadInt(16);
    if (max_diff_changed) {
      max_diff_ = Double::LongBitsToDouble(input.ReadLong(64));
      step_ = 2 * max_diff_;
    }
    std::vector<double> result;
    result.reserve(block_size_);
    for (int index = 0; index < block_size_; ++index) {
      const Choice choice = SelectCodec(state_);
      bool raw = false;
      uint64_t mapped;
      if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
        mapped = AdaptiveQtCodec::DecodeCappedRiceWithRaw(
            choice.rice_parameter, kRiceQuotientCap, &raw, &input);
      } else {
        mapped = AdaptiveQtCodec::DecodeMapped(choice.codec, choice.rice_parameter, &input);
        raw = mapped == kEscape;
      }
      double value;
      if (raw) {
        value = Double::LongBitsToDouble(input.ReadLong(64));
      } else {
        const int64_t q = AdaptiveQtCodec::ZigZagDecode(mapped - 1);
        value = previous_ + step_ * static_cast<double>(q);
        const Lengths lengths = CalculateLengths<UseGamma, UseDelta, UseRice, true>(
            mapped, choice.rice_parameter);
        UpdateState(mapped, lengths, &state_);
      }
      result.push_back(value);
      previous_ = std::isfinite(value) ? value : 2;
    }
    return result;
  }

 private:
  double previous_ = 2;
  CodecState<UseGamma, UseDelta, UseRice> state_{};
  int block_size_ = 0;
  double max_diff_ = 0;
  double step_ = 0;
};

enum class LogMode {
  kRepeat,
  kSamePositive,
  kSameNegative,
  kChangedZero,
  kChangedPositive,
  kChangedNegative,
  kZero,
  kRaw
};

LogMode ReadLogMode(InputBitStream *input) {
  if (!input->ReadBit()) return LogMode::kRepeat;
  if (!input->ReadBit()) return LogMode::kSameNegative;
  if (!input->ReadBit()) return LogMode::kSamePositive;
  if (!input->ReadBit()) return LogMode::kChangedNegative;
  if (!input->ReadBit()) return LogMode::kChangedPositive;
  if (!input->ReadBit()) return LogMode::kChangedZero;
  return !input->ReadBit() ? LogMode::kZero : LogMode::kRaw;
}

template <bool UseGamma, bool UseDelta, bool UseRice>
class LogCompressor {
 public:
  LogCompressor(int block_size, double relative_error)
      : block_size_(block_size),
        relative_error_(relative_error),
        upper_error_(std::log1p(relative_error)),
        lower_error_(relative_error < 1 ? std::log1p(-relative_error)
                                        : -std::numeric_limits<double>::infinity()),
        log_max_diff_(upper_error_ * 0.999),
        inverse_step_(1.0 / (2 * log_max_diff_)),
        output_(std::make_unique<OutputBitStream>(static_cast<uint32_t>(16 * block_size + 32))) {}

  void AddValue(double value) {
    if (value_count_ == 0) WriteMetadata();
    const bool sign = std::signbit(value);
    if (value == 0) {
      bits_ += output_->WriteInt(126, 7);
      ++value_count_;
      return;
    }
    const double magnitude = std::abs(value);
    if (std::isfinite(value) && sign == previous_sign_ &&
        std::abs(previous_value_ - value) <= relative_error_ * magnitude) {
      bits_ += output_->WriteBit(false);
      ++value_count_;
      return;
    }
    if (std::isfinite(value) && sign != previous_sign_ &&
        std::abs(std::abs(previous_value_) - magnitude) <= relative_error_ * magnitude) {
      bits_ += output_->WriteInt(62, 6);
      previous_sign_ = sign;
      previous_value_ = sign ? -std::abs(previous_value_) : std::abs(previous_value_);
      ++value_count_;
      return;
    }

    const Choice choice = SelectCodec(state_);
    int64_t q = 0;
    double target_log = 0;
    double recovered_log = 0;
    const bool quantized =
        QuantizeLog(magnitude, previous_log_, log_max_diff_, inverse_step_, lower_error_,
                    upper_error_, &q, &target_log, &recovered_log);
    const bool changed_sign = sign != previous_sign_;
    if (quantized && q != 0) {
      const bool positive = q > 0;
      const uint64_t mapped = positive ? static_cast<uint64_t>(q) : static_cast<uint64_t>(-q);
      const Lengths lengths = CalculateLengths<UseGamma, UseDelta, UseRice, false>(
          mapped, choice.rice_parameter);
      const uint64_t prefix = (changed_sign ? 4 : 2) + static_cast<uint64_t>(positive);
      if (prefix + SelectedLength(lengths, choice.codec) < 71) {
        if (changed_sign) {
          bits_ += positive ? output_->WriteInt(30, 5) : output_->WriteInt(14, 4);
        } else {
          bits_ += positive ? output_->WriteInt(6, 3) : output_->WriteInt(2, 2);
        }
        if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
          bits_ += AdaptiveQtCodec::EncodeCappedRice(
              mapped, choice.rice_parameter, kRiceQuotientCap, output_.get());
        } else {
          bits_ += AdaptiveQtCodec::EncodeMapped(mapped, choice.codec, choice.rice_parameter,
                                                 output_.get());
        }
        CountCodec(choice.codec, &counts_);
        UpdateState(mapped, lengths, &state_);
        previous_log_ = recovered_log;
        const double recovered_magnitude = std::exp(recovered_log);
        previous_value_ = sign ? -recovered_magnitude : recovered_magnitude;
        previous_sign_ = sign;
        ++value_count_;
        return;
      }
    }

    bits_ += output_->WriteInt(127, 7);
    bits_ += output_->WriteLong(Double::DoubleToLongBits(value), 64);
    ++counts_.raw;
    if (std::isfinite(value) && value != 0) {
      previous_log_ = quantized ? target_log : std::log(magnitude);
      previous_value_ = value;
      previous_sign_ = sign;
    }
    ++value_count_;
  }

  void Close() {
    output_->Flush();
    compressed_bytes_ = output_->GetBuffer(static_cast<uint32_t>(std::ceil(bits_ / 8.0)));
    output_->Refresh();
    stored_bits_ = bits_;
    bits_ = 0;
    value_count_ = 0;
  }

  Array<uint8_t> compressed_bytes() const { return compressed_bytes_; }
  long get_compressed_size_in_bits() const { return stored_bits_; }
  CodecCounts counts() const { return counts_; }

 private:
  void WriteMetadata() {
    bits_ += output_->WriteBit(!metadata_initialized_);
    bits_ += output_->WriteBit(!metadata_initialized_);
    if (!metadata_initialized_) {
      bits_ += output_->WriteInt(block_size_, 16);
      bits_ += output_->WriteLong(Double::DoubleToLongBits(log_max_diff_), 64);
      metadata_initialized_ = true;
    }
  }

  int block_size_;
  double relative_error_;
  double upper_error_;
  double lower_error_;
  double log_max_diff_;
  double inverse_step_;
  std::unique_ptr<OutputBitStream> output_;
  Array<uint8_t> compressed_bytes_;
  double previous_log_ = 0;
  double previous_value_ = 1;
  bool previous_sign_ = false;
  CodecState<UseGamma, UseDelta, UseRice> state_{};
  CodecCounts counts_{};
  bool metadata_initialized_ = false;
  int value_count_ = 0;
  long bits_ = 0;
  long stored_bits_ = 0;
};

template <bool UseGamma, bool UseDelta, bool UseRice>
class LogDecompressor {
 public:
  std::vector<double> Decompress(const Array<uint8_t> &bytes) {
    InputBitStream input;
    input.SetBuffer(bytes);
    const bool block_size_changed = input.ReadBit();
    const bool log_max_diff_changed = input.ReadBit();
    if (block_size_changed) block_size_ = input.ReadInt(16);
    if (log_max_diff_changed) log_max_diff_ = Double::LongBitsToDouble(input.ReadLong(64));
    std::vector<double> result;
    result.reserve(block_size_);
    for (int index = 0; index < block_size_; ++index) {
      const LogMode mode = ReadLogMode(&input);
      if (mode == LogMode::kRaw) {
        const double value = Double::LongBitsToDouble(input.ReadLong(64));
        result.push_back(value);
        if (std::isfinite(value) && value != 0) {
          previous_log_ = std::log(std::abs(value));
          previous_value_ = value;
          previous_sign_ = std::signbit(value);
        }
        continue;
      }
      if (mode == LogMode::kZero) {
        result.push_back(0.0);
        continue;
      }
      if (mode == LogMode::kRepeat) {
        result.push_back(previous_value_);
        continue;
      }
      if (mode == LogMode::kChangedZero) {
        previous_sign_ = !previous_sign_;
        previous_value_ = previous_sign_ ? -std::abs(previous_value_) : std::abs(previous_value_);
        result.push_back(previous_value_);
        continue;
      }

      const Choice choice = SelectCodec(state_);
      uint64_t mapped;
      if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
        mapped = AdaptiveQtCodec::DecodeCappedRice(choice.rice_parameter, kRiceQuotientCap, &input);
      } else {
        mapped = AdaptiveQtCodec::DecodeMapped(choice.codec, choice.rice_parameter, &input);
      }
      const bool changed_sign =
          mode == LogMode::kChangedPositive || mode == LogMode::kChangedNegative;
      const bool negative = mode == LogMode::kSameNegative || mode == LogMode::kChangedNegative;
      if (changed_sign) previous_sign_ = !previous_sign_;
      const int64_t q = negative ? -static_cast<int64_t>(mapped) : static_cast<int64_t>(mapped);
      previous_log_ += 2 * log_max_diff_ * static_cast<double>(q);
      const double magnitude = std::exp(previous_log_);
      previous_value_ = previous_sign_ ? -magnitude : magnitude;
      result.push_back(previous_value_);
      const Lengths lengths = CalculateLengths<UseGamma, UseDelta, UseRice, false>(
          mapped, choice.rice_parameter);
      UpdateState(mapped, lengths, &state_);
    }
    return result;
  }

 private:
  double previous_log_ = 0;
  double previous_value_ = 1;
  bool previous_sign_ = false;
  CodecState<UseGamma, UseDelta, UseRice> state_{};
  int block_size_ = 0;
  double log_max_diff_ = 0;
};

std::vector<double> ReadValues(const std::filesystem::path &path) {
  std::ifstream input(path);
  std::vector<double> values;
  double value;
  while (input >> value) values.push_back(value);
  return values;
}

template <typename Compressor>
uint64_t CompressDatasets(const std::vector<std::vector<double>> &datasets, double error,
                          std::vector<std::vector<Array<uint8_t>>> *blocks,
                          CodecCounts *counts) {
  uint64_t bits = 0;
  if (blocks != nullptr) blocks->clear();
  if (counts != nullptr) *counts = {};
  for (const auto &values : datasets) {
    Compressor compressor(kBlockSizeOverall, error);
    if (blocks != nullptr) blocks->emplace_back();
    for (size_t begin = 0; begin + kBlockSizeOverall <= values.size(); begin += kBlockSizeOverall) {
      for (size_t i = begin; i < begin + kBlockSizeOverall; ++i) compressor.AddValue(values[i]);
      compressor.Close();
      bits += compressor.get_compressed_size_in_bits();
      if (blocks != nullptr) blocks->back().push_back(compressor.compressed_bytes());
    }
    if (counts != nullptr) *counts += compressor.counts();
  }
  return bits;
}

template <typename Decompressor, bool Relative>
bool DecompressDatasets(const std::vector<std::vector<double>> &datasets,
                        const std::vector<std::vector<Array<uint8_t>>> &blocks, double error,
                        bool validate) {
  for (size_t dataset = 0; dataset < datasets.size(); ++dataset) {
    Decompressor decompressor;
    size_t original_index = 0;
    for (const auto &block : blocks[dataset]) {
      const std::vector<double> recovered = decompressor.Decompress(block);
      if (validate) {
        for (double value : recovered) {
          const double original = datasets[dataset][original_index++];
          if constexpr (Relative) {
            if (original == 0) {
              if (value != 0 || std::signbit(value)) return false;
            } else if (std::signbit(original) != std::signbit(value) ||
                       std::abs(original - value) / std::abs(original) > error) {
              return false;
            }
          } else if (std::abs(original - value) > error) {
            return false;
          }
        }
      }
    }
  }
  return true;
}

template <typename Compressor, typename Decompressor, bool Relative>
BenchmarkResult Benchmark(const std::vector<std::vector<double>> &datasets, double error) {
  BenchmarkResult result;
  std::vector<std::vector<Array<uint8_t>>> blocks;
  result.bits = CompressDatasets<Compressor>(datasets, error, &blocks, &result.counts);
  for (const auto &values : datasets) {
    result.values += values.size() / kBlockSizeOverall * kBlockSizeOverall;
  }
  result.valid = DecompressDatasets<Decompressor, Relative>(datasets, blocks, error, true);

  for (int repetition = -kWarmups; repetition < kRepetitions; ++repetition) {
    const auto start = std::chrono::steady_clock::now();
    const uint64_t bits = CompressDatasets<Compressor>(datasets, error, nullptr, nullptr);
    const auto end = std::chrono::steady_clock::now();
    if (bits != result.bits) throw std::runtime_error("Non-deterministic compressed size");
    if (repetition >= 0) {
      result.compression_ms +=
          std::chrono::duration<double, std::milli>(end - start).count() / kRepetitions;
    }
  }
  for (int repetition = -kWarmups; repetition < kRepetitions; ++repetition) {
    const auto start = std::chrono::steady_clock::now();
    DecompressDatasets<Decompressor, Relative>(datasets, blocks, error, false);
    const auto end = std::chrono::steady_clock::now();
    if (repetition >= 0) {
      result.decompression_ms +=
          std::chrono::duration<double, std::milli>(end - start).count() / kRepetitions;
    }
  }
  return result;
}

void Print(const char *domain, double error, const char *variant, const BenchmarkResult &result) {
  std::cout << domain << ',' << error << ',' << variant << ',' << result.values << ','
            << result.bits << ',' << static_cast<double>(result.bits) / (64.0 * result.values)
            << ',' << result.compression_ms << ',' << result.decompression_ms << ','
            << result.counts.gamma << ',' << result.counts.delta << ',' << result.counts.rice << ','
            << result.counts.raw << ',' << (result.valid ? "true" : "false") << '\n';
}

template <bool Relative>
void RunDomain(const std::vector<std::vector<double>> &datasets, double error) {
  if constexpr (Relative) {
    Print("Relative", error, "Gamma+Delta+Rice",
          Benchmark<LogCompressor<true, true, true>, LogDecompressor<true, true, true>, true>(
              datasets, error));
    Print("Relative", error, "Delta+Rice(NoGamma)",
          Benchmark<LogCompressor<false, true, true>, LogDecompressor<false, true, true>, true>(
              datasets, error));
    Print("Relative", error, "Gamma+Rice(NoDelta)",
          Benchmark<LogCompressor<true, false, true>, LogDecompressor<true, false, true>, true>(
              datasets, error));
    Print("Relative", error, "Gamma+Delta(NoRice)",
          Benchmark<LogCompressor<true, true, false>, LogDecompressor<true, true, false>, true>(
              datasets, error));
    Print("Relative", error, "GammaOnly",
          Benchmark<LogCompressor<true, false, false>, LogDecompressor<true, false, false>, true>(
              datasets, error));
    Print("Relative", error, "DeltaOnly",
          Benchmark<LogCompressor<false, true, false>, LogDecompressor<false, true, false>, true>(
              datasets, error));
    Print("Relative", error, "RiceOnly",
          Benchmark<LogCompressor<false, false, true>, LogDecompressor<false, false, true>, true>(
              datasets, error));
  } else {
    Print("Absolute", error, "Gamma+Delta+Rice",
          Benchmark<AbsoluteCompressor<true, true, true>,
                    AbsoluteDecompressor<true, true, true>, false>(datasets, error));
    Print("Absolute", error, "Delta+Rice(NoGamma)",
          Benchmark<AbsoluteCompressor<false, true, true>,
                    AbsoluteDecompressor<false, true, true>, false>(datasets, error));
    Print("Absolute", error, "Gamma+Rice(NoDelta)",
          Benchmark<AbsoluteCompressor<true, false, true>,
                    AbsoluteDecompressor<true, false, true>, false>(datasets, error));
    Print("Absolute", error, "Gamma+Delta(NoRice)",
          Benchmark<AbsoluteCompressor<true, true, false>,
                    AbsoluteDecompressor<true, true, false>, false>(datasets, error));
    Print("Absolute", error, "GammaOnly",
          Benchmark<AbsoluteCompressor<true, false, false>,
                    AbsoluteDecompressor<true, false, false>, false>(datasets, error));
    Print("Absolute", error, "DeltaOnly",
          Benchmark<AbsoluteCompressor<false, true, false>,
                    AbsoluteDecompressor<false, true, false>, false>(datasets, error));
    Print("Absolute", error, "RiceOnly",
          Benchmark<AbsoluteCompressor<false, false, true>,
                    AbsoluteDecompressor<false, false, true>, false>(datasets, error));
  }
}

}  // namespace

int main(int argc, char **argv) {
  const std::filesystem::path dataset_dir =
      argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("test/data_set");
  std::vector<std::vector<double>> datasets;
  for (const std::string &dataset : kDataSetList) datasets.push_back(ReadValues(dataset_dir / dataset));

  std::cout << std::setprecision(12);
  std::cout << "Domain,Error,Variant,Values,Bits,CompressionRatio,CompressionMs,DecompressionMs,"
               "GammaValues,DeltaValues,RiceValues,RawValues,Valid\n";
  for (double error : kMaxDiffList) RunDomain<false>(datasets, error);
  for (double error : kMaxDiffRel) RunDomain<true>(datasets, error);
}
