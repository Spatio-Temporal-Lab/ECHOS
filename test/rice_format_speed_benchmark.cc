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
#include "compressor/adaptive_serf_qt_rice_compressor.h"
#include "decompressor/adaptive_serf_qt_rice_decompressor.h"
#include "utils/adaptive_qt_codec.h"
#include "utils/array.h"
#include "utils/double.h"
#include "utils/input_bit_stream.h"
#include "utils/output_bit_stream.h"

namespace {

constexpr uint32_t kBoundedQuotientCap = 16;
constexpr uint64_t kEscape = std::numeric_limits<uint64_t>::max();
constexpr int kWarmups = 2;
constexpr int kRepetitions = 7;

struct BoundedState {
  uint64_t delta_cost = 0;
  uint64_t gamma_cost = 0;
  uint64_t bounded_cost = 0;
  uint64_t magnitude_sum = 0;
  uint64_t sample_count = 0;
};

struct BoundedChoice {
  AdaptiveQtCodec::IntegerCodec codec = AdaptiveQtCodec::IntegerCodec::kDelta;
  uint32_t rice_parameter = 0;
};

struct BoundedLengths {
  uint64_t gamma;
  uint64_t delta;
  uint64_t bounded;
};

struct BenchmarkResult {
  uint64_t values = 0;
  uint64_t bits = 0;
  double compression_ms = 0;
  double decompression_ms = 0;
  bool valid = true;
};

bool Quantize(double value, double prediction, double max_diff, double quantization_step,
              double inverse_quantization_step, int64_t *q, double *recovered) {
  if (!std::isfinite(value) || !std::isfinite(prediction)) return false;
  const double scaled = (value - prediction) * inverse_quantization_step;
  if (std::isfinite(scaled) && std::abs(scaled) <= 0x1p52) {
    *q = static_cast<int64_t>(std::round(scaled));
    if (AdaptiveQtCodec::ZigZagEncode(*q) < std::numeric_limits<uint64_t>::max() - 1) {
      *recovered = prediction + quantization_step * static_cast<double>(*q);
      if (std::isfinite(*recovered) && std::abs(value - *recovered) <= max_diff) return true;
    }
  }

  const long double step = 2.0L * static_cast<long double>(max_diff);
  const long double precise_scaled =
      (static_cast<long double>(value) - static_cast<long double>(prediction)) / step;
  if (!std::isfinite(precise_scaled)) return false;
  const long double rounded = std::round(precise_scaled);
  if (rounded <= static_cast<long double>(std::numeric_limits<int64_t>::min()) ||
      rounded > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
    return false;
  }
  *q = static_cast<int64_t>(rounded);
  if (AdaptiveQtCodec::ZigZagEncode(*q) >= std::numeric_limits<uint64_t>::max() - 1) return false;
  *recovered = prediction + quantization_step * static_cast<double>(*q);
  return std::isfinite(*recovered) && std::abs(value - *recovered) <= max_diff;
}

uint32_t EstimateParameter(const BoundedState &state) {
  if (state.magnitude_sum == 0 || state.sample_count == 0) return 0;
  const uint32_t magnitude_log = AdaptiveQtCodec::FloorLog2(state.magnitude_sum);
  const uint32_t sample_log = AdaptiveQtCodec::FloorLog2(state.sample_count);
  return magnitude_log > sample_log ? magnitude_log - sample_log : 0;
}

BoundedChoice Select(const BoundedState &state) {
  BoundedChoice choice{AdaptiveQtCodec::IntegerCodec::kDelta, EstimateParameter(state)};
  uint64_t cost = state.delta_cost;
  if (state.gamma_cost < cost) {
    choice.codec = AdaptiveQtCodec::IntegerCodec::kGamma;
    cost = state.gamma_cost;
  }
  if (state.bounded_cost < cost) choice.codec = AdaptiveQtCodec::IntegerCodec::kRice;
  return choice;
}

BoundedLengths CalculateLengths(uint64_t mapped, uint32_t parameter) {
  const uint64_t gamma = AdaptiveQtCodec::GammaLength(mapped);
  const uint64_t delta = AdaptiveQtCodec::DeltaLength(mapped);
  const uint64_t quotient = (mapped - 1) >> parameter;
  const uint64_t bounded =
      quotient < kBoundedQuotientCap
          ? AdaptiveQtCodec::RiceLength(mapped - 1, parameter)
          : kBoundedQuotientCap + 1 + delta;
  return {gamma, delta, bounded};
}

void Update(uint64_t mapped, const BoundedLengths &lengths, BoundedState *state) {
  state->delta_cost = AdaptiveQtCodec::DecayAndAdd(state->delta_cost, lengths.delta);
  state->gamma_cost = AdaptiveQtCodec::DecayAndAdd(state->gamma_cost, lengths.gamma);
  state->bounded_cost = AdaptiveQtCodec::DecayAndAdd(state->bounded_cost, lengths.bounded);
  state->magnitude_sum = AdaptiveQtCodec::DecayAndAdd(
      state->magnitude_sum,
      std::min(mapped - 1, AdaptiveQtCodec::kAdaptiveRiceMagnitudeCap));
  state->sample_count = AdaptiveQtCodec::DecayAndAdd(state->sample_count, 1);
}

uint64_t EncodeBounded(uint64_t mapped, uint32_t parameter, OutputBitStream *output) {
  const uint64_t quotient = (mapped - 1) >> parameter;
  if (quotient < kBoundedQuotientCap) {
    AdaptiveQtCodec::WriteZeros(quotient, output);
    output->WriteBit(true);
    if (parameter > 0) output->WriteLong(mapped - 1, parameter);
    return quotient + 1 + parameter;
  }
  AdaptiveQtCodec::WriteZeros(kBoundedQuotientCap, output);
  output->WriteBit(false);
  return kBoundedQuotientCap + 1 + AdaptiveQtCodec::EncodeDelta(mapped, output);
}

uint64_t WriteBoundedRaw(OutputBitStream *output) {
  AdaptiveQtCodec::WriteZeros(kBoundedQuotientCap, output);
  output->WriteBit(true);
  return kBoundedQuotientCap + 1;
}

uint64_t DecodeBounded(uint32_t parameter, bool *raw, InputBitStream *input) {
  *raw = false;
  const uint64_t quotient = input->ReadUnaryZerosOrCap(kBoundedQuotientCap);
  if (quotient == kBoundedQuotientCap) {
    if (input->ReadBit()) {
      *raw = true;
      return 1;
    }
    return AdaptiveQtCodec::DecodeDelta(input);
  }
  const uint64_t remainder = parameter == 0 ? 0 : input->ReadLong(parameter);
  return ((quotient << parameter) | remainder) + 1;
}

class Bounded16Compressor {
 public:
  Bounded16Compressor(int block_size, double max_diff)
      : block_size_(block_size),
        max_diff_(max_diff * 0.999),
        quantization_step_(2 * max_diff_),
        inverse_quantization_step_(1.0 / quantization_step_),
        output_(std::make_unique<OutputBitStream>(static_cast<uint32_t>(24 * block_size + 32))) {}

  void AddValue(double value) {
    if (value_count_ == 0) WriteMetadata();
    const BoundedChoice choice = Select(state_);
    int64_t q = 0;
    double recovered = 0;
    if (Quantize(value, previous_, max_diff_, quantization_step_, inverse_quantization_step_, &q,
                 &recovered)) {
      const uint64_t mapped = AdaptiveQtCodec::ZigZagEncode(q) + 1;
      const BoundedLengths lengths = CalculateLengths(mapped, choice.rice_parameter);
      if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
        bits_ += EncodeBounded(mapped, choice.rice_parameter, output_.get());
      } else {
        bits_ +=
            AdaptiveQtCodec::EncodeMapped(mapped, choice.codec, choice.rice_parameter, output_.get());
      }
      Update(mapped, lengths, &state_);
      previous_ = recovered;
    } else {
      if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
        bits_ += WriteBoundedRaw(output_.get());
      } else {
        bits_ +=
            AdaptiveQtCodec::EncodeMapped(kEscape, choice.codec, choice.rice_parameter, output_.get());
      }
      bits_ += output_->WriteLong(Double::DoubleToLongBits(value), 64);
      previous_ = std::isfinite(value) ? value : 2;
    }
    ++value_count_;
  }

  void Close() {
    output_->Flush();
    compressed_bytes_ =
        output_->GetBuffer(static_cast<uint32_t>(std::ceil(bits_ / 8.0)));
    output_->Refresh();
    stored_bits_ = bits_;
    bits_ = 0;
    value_count_ = 0;
  }

  Array<uint8_t> compressed_bytes() const { return compressed_bytes_; }
  long get_compressed_size_in_bits() const { return stored_bits_; }

 private:
  void WriteMetadata() {
    const uint64_t max_diff_bits = Double::DoubleToLongBits(max_diff_);
    const bool block_size_changed = !metadata_initialized_;
    const bool max_diff_changed = !metadata_initialized_;
    bits_ += output_->WriteBit(block_size_changed);
    bits_ += output_->WriteBit(max_diff_changed);
    if (block_size_changed) bits_ += output_->WriteInt(block_size_, 16);
    if (max_diff_changed) bits_ += output_->WriteLong(max_diff_bits, 64);
    metadata_initialized_ = true;
  }

  int block_size_;
  double max_diff_;
  double quantization_step_;
  double inverse_quantization_step_;
  std::unique_ptr<OutputBitStream> output_;
  Array<uint8_t> compressed_bytes_;
  double previous_ = 2;
  BoundedState state_;
  bool metadata_initialized_ = false;
  int value_count_ = 0;
  long bits_ = 0;
  long stored_bits_ = 0;
};

class Bounded16Decompressor {
 public:
  std::vector<double> Decompress(const Array<uint8_t> &bytes) {
    InputBitStream input;
    input.SetBuffer(bytes);
    const bool block_size_changed = input.ReadBit();
    const bool max_diff_changed = input.ReadBit();
    if (block_size_changed) block_size_ = input.ReadInt(16);
    if (max_diff_changed) {
      max_diff_ = Double::LongBitsToDouble(input.ReadLong(64));
      quantization_step_ = 2 * max_diff_;
    }
    metadata_initialized_ = true;

    std::vector<double> result;
    result.reserve(block_size_);
    for (int index = 0; index < block_size_; ++index) {
      const BoundedChoice choice = Select(state_);
      bool raw = false;
      uint64_t mapped = 1;
      if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
        mapped = DecodeBounded(choice.rice_parameter, &raw, &input);
      } else {
        mapped = AdaptiveQtCodec::DecodeMapped(choice.codec, choice.rice_parameter, &input);
        raw = mapped == kEscape;
      }

      double value;
      if (raw) {
        value = Double::LongBitsToDouble(input.ReadLong(64));
      } else {
        const int64_t q = AdaptiveQtCodec::ZigZagDecode(mapped - 1);
        value = previous_ + quantization_step_ * static_cast<double>(q);
        Update(mapped, CalculateLengths(mapped, choice.rice_parameter), &state_);
      }
      result.push_back(value);
      previous_ = std::isfinite(value) ? value : 2;
    }
    return result;
  }

 private:
  double previous_ = 2;
  BoundedState state_;
  bool metadata_initialized_ = false;
  int block_size_ = 0;
  double max_diff_ = 0;
  double quantization_step_ = 0;
};

std::vector<double> ReadValues(const std::filesystem::path &path) {
  std::ifstream input(path);
  std::vector<double> values;
  double value;
  while (input >> value) values.push_back(value);
  return values;
}

template <typename Compressor>
uint64_t CompressDatasets(const std::vector<std::vector<double>> &datasets, double max_diff,
                          std::vector<std::vector<Array<uint8_t>>> *blocks) {
  uint64_t bits = 0;
  if (blocks != nullptr) blocks->clear();
  for (const std::vector<double> &values : datasets) {
    Compressor compressor(kBlockSizeOverall, max_diff);
    if (blocks != nullptr) blocks->emplace_back();
    for (size_t begin = 0; begin + kBlockSizeOverall <= values.size(); begin += kBlockSizeOverall) {
      for (size_t i = begin; i < begin + kBlockSizeOverall; ++i) compressor.AddValue(values[i]);
      compressor.Close();
      bits += compressor.get_compressed_size_in_bits();
      if (blocks != nullptr) blocks->back().push_back(compressor.compressed_bytes());
    }
  }
  return bits;
}

template <typename Decompressor>
bool DecompressDatasets(const std::vector<std::vector<double>> &datasets,
                        const std::vector<std::vector<Array<uint8_t>>> &blocks, double max_diff,
                        bool validate) {
  for (size_t dataset = 0; dataset < datasets.size(); ++dataset) {
    Decompressor decompressor;
    size_t original_index = 0;
    for (const Array<uint8_t> &block : blocks[dataset]) {
      const std::vector<double> recovered = decompressor.Decompress(block);
      if (validate) {
        for (double value : recovered) {
          if (std::abs(value - datasets[dataset][original_index++]) > max_diff) return false;
        }
      }
    }
  }
  return true;
}

template <typename Compressor, typename Decompressor>
BenchmarkResult Benchmark(const std::vector<std::vector<double>> &datasets, double max_diff) {
  BenchmarkResult result;
  std::vector<std::vector<Array<uint8_t>>> blocks;
  result.bits = CompressDatasets<Compressor>(datasets, max_diff, &blocks);
  for (const auto &values : datasets) {
    result.values += values.size() / kBlockSizeOverall * kBlockSizeOverall;
  }
  result.valid = DecompressDatasets<Decompressor>(datasets, blocks, max_diff, true);

  for (int repetition = -kWarmups; repetition < kRepetitions; ++repetition) {
    const auto start = std::chrono::steady_clock::now();
    const uint64_t bits = CompressDatasets<Compressor>(datasets, max_diff, nullptr);
    const auto end = std::chrono::steady_clock::now();
    if (bits != result.bits) throw std::runtime_error("Non-deterministic compressed size");
    if (repetition >= 0) {
      result.compression_ms +=
          std::chrono::duration<double, std::milli>(end - start).count() / kRepetitions;
    }
  }

  for (int repetition = -kWarmups; repetition < kRepetitions; ++repetition) {
    const auto start = std::chrono::steady_clock::now();
    DecompressDatasets<Decompressor>(datasets, blocks, max_diff, false);
    const auto end = std::chrono::steady_clock::now();
    if (repetition >= 0) {
      result.decompression_ms +=
          std::chrono::duration<double, std::milli>(end - start).count() / kRepetitions;
    }
  }
  return result;
}

void Print(double max_diff, const char *method, const BenchmarkResult &result) {
  std::cout << max_diff << ',' << method << ',' << result.values << ',' << result.bits << ','
            << static_cast<double>(result.bits) / (64.0 * result.values) << ','
            << result.compression_ms << ',' << result.decompression_ms << ','
            << (result.valid ? "true" : "false") << '\n';
}

}  // namespace

int main(int argc, char **argv) {
  const std::filesystem::path dataset_dir =
      argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("test/data_set");
  const bool bounded_first = argc > 2 && std::string(argv[2]) == "bounded-first";
  std::vector<std::vector<double>> datasets;
  for (const std::string &dataset : kDataSetList) datasets.push_back(ReadValues(dataset_dir / dataset));

  std::cout << std::setprecision(12);
  std::cout << "MaxDiff,Method,Values,Bits,CompressionRatio,CompressionMs,DecompressionMs,Valid\n";
  for (double max_diff : kMaxDiffList) {
    if (bounded_first) {
      Print(max_diff, "Bounded16Only",
            Benchmark<Bounded16Compressor, Bounded16Decompressor>(datasets, max_diff));
      Print(max_diff, "DualRice",
            Benchmark<AdaptiveSerfQtRiceCompressor, AdaptiveSerfQtRiceDecompressor>(datasets,
                                                                                    max_diff));
    } else {
      Print(max_diff, "DualRice",
            Benchmark<AdaptiveSerfQtRiceCompressor, AdaptiveSerfQtRiceDecompressor>(datasets,
                                                                                    max_diff));
      Print(max_diff, "Bounded16Only",
            Benchmark<Bounded16Compressor, Bounded16Decompressor>(datasets, max_diff));
    }
  }
}
