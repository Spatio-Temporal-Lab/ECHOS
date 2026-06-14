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
#include "utils/adaptive_qt_codec.h"
#include "utils/array.h"
#include "utils/double.h"
#include "utils/output_bit_stream.h"

namespace {

constexpr int kRepetitions = 7;

struct RiceState {
  uint64_t magnitude_sum = 0;
  uint64_t sample_count = 0;
};

struct Result {
  uint64_t values = 0;
  uint64_t bits = 0;
  double compression_ms = 0;
};

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

uint32_t EstimateRiceParameter(const RiceState &state) {
  if (state.magnitude_sum == 0 || state.sample_count == 0) return 0;
  const uint32_t magnitude_log = AdaptiveQtCodec::FloorLog2(state.magnitude_sum);
  const uint32_t sample_log = AdaptiveQtCodec::FloorLog2(state.sample_count);
  return magnitude_log > sample_log ? magnitude_log - sample_log : 0;
}

void UpdateRiceState(uint64_t mapped, RiceState *state) {
  state->magnitude_sum = AdaptiveQtCodec::DecayAndAdd(
      state->magnitude_sum, std::min(mapped - 1, AdaptiveQtCodec::kAdaptiveRiceMagnitudeCap));
  state->sample_count = AdaptiveQtCodec::DecayAndAdd(state->sample_count, 1);
}

class RiceOnlyCompressor {
 public:
  RiceOnlyCompressor(int block_size, double max_diff, bool delta_fallback)
      : block_size_(block_size), max_diff_(max_diff * 0.999), delta_fallback_(delta_fallback) {
    output_ = std::make_unique<OutputBitStream>(static_cast<uint32_t>(24 * block_size + 32));
  }

  void AddValue(double value) {
    if (value_count_ == 0) WriteMetadata();

    const uint32_t rice_parameter = EstimateRiceParameter(state_);
    int64_t q = 0;
    double recovered = 0;
    if (Quantize(value, previous_, max_diff_, &q, &recovered)) {
      const uint64_t mapped = AdaptiveQtCodec::ZigZagEncode(q) + 1;
      const uint64_t rice_length = AdaptiveQtCodec::RiceLength(mapped - 1, rice_parameter);
      if (delta_fallback_) {
        const uint64_t delta_length = AdaptiveQtCodec::DeltaLength(mapped);
        if (2 + delta_length < 1 + rice_length) {
          bits_ += output_->WriteInt(2, 2);
          bits_ += AdaptiveQtCodec::EncodeDelta(mapped, output_.get());
        } else {
          bits_ += output_->WriteBit(false);
          bits_ += AdaptiveQtCodec::EncodeRice(mapped - 1, rice_parameter, output_.get());
        }
        UpdateRiceState(mapped, &state_);
        previous_ = recovered;
      } else if (1 + rice_length < 65) {
        bits_ += output_->WriteBit(false);
        bits_ += AdaptiveQtCodec::EncodeRice(mapped - 1, rice_parameter, output_.get());
        UpdateRiceState(mapped, &state_);
        previous_ = recovered;
      } else {
        WriteRaw(value, 1);
      }
    } else {
      WriteRaw(value, delta_fallback_ ? 2 : 1);
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

  long compressed_size_in_bits() const {
    return stored_bits_;
  }

 private:
  void WriteMetadata() {
    bits_ += output_->WriteBit(first_block_);
    bits_ += output_->WriteBit(first_block_);
    if (first_block_) {
      bits_ += output_->WriteInt(block_size_, 16);
      bits_ += output_->WriteLong(Double::DoubleToLongBits(max_diff_), 64);
      first_block_ = false;
    }
  }

  void WriteRaw(double value, uint32_t prefix_length) {
    bits_ += output_->WriteInt(prefix_length == 2 ? 3 : 1, prefix_length);
    bits_ += output_->WriteLong(Double::DoubleToLongBits(value), 64);
    previous_ = std::isfinite(value) ? value : 2;
  }

  int block_size_;
  double max_diff_;
  bool delta_fallback_;
  std::unique_ptr<OutputBitStream> output_;
  Array<uint8_t> compressed_bytes_;
  RiceState state_;
  double previous_ = 2;
  bool first_block_ = true;
  int value_count_ = 0;
  long bits_ = 0;
  long stored_bits_ = 0;
};

long CompressedBits(const AdaptiveSerfQtRiceCompressor &compressor) {
  return compressor.get_compressed_size_in_bits();
}

long CompressedBits(const RiceOnlyCompressor &compressor) {
  return compressor.compressed_size_in_bits();
}

std::vector<double> ReadValues(const std::filesystem::path &path) {
  std::ifstream input(path);
  std::vector<double> values;
  double value;
  while (input >> value) values.push_back(value);
  return values;
}

template <typename Factory>
Result Run(const std::vector<double> &values, Factory factory) {
  Result result;
  for (int repetition = 0; repetition < kRepetitions; ++repetition) {
    auto compressor = factory();
    uint64_t repetition_bits = 0;
    const auto start = std::chrono::steady_clock::now();
    for (size_t begin = 0; begin + kBlockSizeOverall <= values.size(); begin += kBlockSizeOverall) {
      for (size_t i = begin; i < begin + kBlockSizeOverall; ++i) compressor.AddValue(values[i]);
      compressor.Close();
      repetition_bits += CompressedBits(compressor);
    }
    const auto end = std::chrono::steady_clock::now();
    if (repetition == 0) {
      result.values = values.size() / kBlockSizeOverall * kBlockSizeOverall;
      result.bits = repetition_bits;
    } else if (result.bits != repetition_bits) {
      throw std::runtime_error("Non-deterministic compressed size");
    }
    result.compression_ms +=
        std::chrono::duration<double, std::milli>(end - start).count() / kRepetitions;
  }
  return result;
}

void Print(const std::string &dataset, double max_diff, const std::string &method,
           const Result &result) {
  std::cout << dataset << ',' << max_diff << ',' << method << ',' << result.values << ','
            << result.bits << ',' << static_cast<double>(result.bits) / (64.0 * result.values)
            << ',' << result.compression_ms << '\n';
}

}  // namespace

int main(int argc, char **argv) {
  const std::filesystem::path dataset_dir =
      argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("test/data_set");
  std::cout << std::setprecision(12);
  std::cout << "Dataset,MaxDiff,Method,Values,Bits,CompressionRatio,CompressionMs\n";

  for (double max_diff : kMaxDiffList) {
    for (const std::string &dataset : kDataSetList) {
      const std::vector<double> values = ReadValues(dataset_dir / dataset);
      Print(dataset, max_diff, "Adaptive-Gamma-Delta-Rice",
            Run(values, [&] { return AdaptiveSerfQtRiceCompressor(kBlockSizeOverall, max_diff); }));
      Print(dataset, max_diff, "Rice-Only-Delta-Fallback",
            Run(values, [&] { return RiceOnlyCompressor(kBlockSizeOverall, max_diff, true); }));
      Print(dataset, max_diff, "Pure-Rice-Raw-Fallback",
            Run(values, [&] { return RiceOnlyCompressor(kBlockSizeOverall, max_diff, false); }));
    }
  }
  return 0;
}
