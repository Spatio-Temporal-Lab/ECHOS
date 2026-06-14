#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "Perf_expr_config.hpp"
#include "Perf_file_utils.hpp"
#include "compressor/adaptive_serf_qt_compressor.h"
#include "compressor/adaptive_serf_qt_rice_compressor.h"
#include "compressor/adaptive_serf_qt_rice_bounded16_compressor.h"
#include "compressor/log_serf_qt_compressor.h"
#include "compressor/serf_qt_compressor.h"
#include "decompressor/adaptive_serf_qt_decompressor.h"
#include "decompressor/adaptive_serf_qt_rice_decompressor.h"
#include "decompressor/adaptive_serf_qt_rice_bounded16_decompressor.h"
#include "decompressor/log_serf_qt_decompressor.h"
#include "decompressor/serf_qt_decompressor.h"
#include "utils/adaptive_qt_codec.h"
#include "utils/double.h"
#include "utils/input_bit_stream.h"
#include "utils/output_bit_stream.h"

namespace {

bool ValidateCappedRice() {
  constexpr uint32_t kParameter = 3;
  constexpr uint32_t kQuotientCap = 16;
  const std::vector<uint64_t> mapped_values = {1, 8, 15 * 8 + 1, 16 * 8 + 1, 10000};
  OutputBitStream output(128);
  uint64_t bits = 0;
  for (uint64_t mapped : mapped_values) {
    const uint64_t written =
        AdaptiveQtCodec::EncodeCappedRice(mapped, kParameter, kQuotientCap, &output);
    if (written != AdaptiveQtCodec::CappedRiceLength(mapped, kParameter, kQuotientCap)) {
      return false;
    }
    bits += written;
  }
  output.Flush();

  InputBitStream input;
  input.SetBuffer(output.GetBuffer(static_cast<uint32_t>((bits + 7) / 8)));
  for (uint64_t expected : mapped_values) {
    if (AdaptiveQtCodec::DecodeCappedRice(kParameter, kQuotientCap, &input) != expected) {
      return false;
    }
  }

  const std::vector<uint64_t> raw_cap_values = {
      1, (AdaptiveQtCodec::kAdaptiveRiceFormatQuotientCap - 1) * 8 + 1,
      AdaptiveQtCodec::kAdaptiveRiceFormatQuotientCap * 8 + 1, 10000};
  OutputBitStream raw_cap_output(128);
  bits = 0;
  for (uint64_t mapped : raw_cap_values) {
    const uint64_t written =
        AdaptiveQtCodec::EncodeCappedRiceWithRaw(mapped, kParameter, &raw_cap_output);
    if (written != AdaptiveQtCodec::CappedRiceWithRawLength(
                       mapped, kParameter, AdaptiveQtCodec::kAdaptiveRiceFormatQuotientCap)) {
      return false;
    }
    bits += written;
  }
  bits += AdaptiveQtCodec::WriteCappedRiceRaw(&raw_cap_output);
  raw_cap_output.Flush();

  InputBitStream raw_cap_input;
  raw_cap_input.SetBuffer(raw_cap_output.GetBuffer(static_cast<uint32_t>((bits + 7) / 8)));
  for (uint64_t expected : raw_cap_values) {
    bool raw = false;
    if (AdaptiveQtCodec::DecodeCappedRiceWithRaw(kParameter, &raw, &raw_cap_input) != expected ||
        raw) {
      return false;
    }
  }
  bool raw = false;
  AdaptiveQtCodec::DecodeCappedRiceWithRaw(kParameter, &raw, &raw_cap_input);
  if (!raw) return false;

  AdaptiveQtCodec::AdaptiveRiceState state;
  state.delta_cost = 100;
  state.gamma_cost = 100;
  state.rice_cost = 50;
  state.capped_rice_cost = 10;
  AdaptiveQtCodec::AdaptiveRiceChoice choice =
      AdaptiveQtCodec::SelectAdaptiveRiceCodecAndFormat(state);
  if (choice.codec != AdaptiveQtCodec::IntegerCodec::kRice || !choice.capped_rice) return false;
  state.rice_cost = 5;
  choice = AdaptiveQtCodec::SelectAdaptiveRiceCodecAndFormat(state);
  if (choice.codec != AdaptiveQtCodec::IntegerCodec::kRice || choice.capped_rice) return false;
  return true;
}

bool ValidateNaturalRiceParameter() {
  AdaptiveQtCodec::AdaptiveRiceState state;
  state.magnitude_sum = 1ULL << 13;
  state.sample_count = 1;
  if (AdaptiveQtCodec::EstimateRiceParameter(state) != 13) return false;
  state.magnitude_sum = AdaptiveQtCodec::kAdaptiveRiceMagnitudeCap;
  if (AdaptiveQtCodec::EstimateRiceParameter(state) !=
      AdaptiveQtCodec::kAdaptiveRiceMagnitudeBits) {
    return false;
  }
  state.magnitude_sum = 1;
  state.sample_count = 16;
  return AdaptiveQtCodec::EstimateRiceParameter(state) == 0;
}

template <typename Compressor, typename Decompressor>
std::vector<double> RoundTripBlocks(const std::vector<double> &original, int block_size,
                                    double error_bound) {
  Compressor compressor(block_size, error_bound);
  Decompressor decompressor;
  std::vector<double> recovered;
  recovered.reserve(original.size());

  for (size_t begin = 0; begin < original.size(); begin += block_size) {
    for (size_t i = begin; i < begin + block_size; ++i) compressor.AddValue(original[i]);
    compressor.Close();
    const std::vector<double> block = decompressor.Decompress(compressor.compressed_bytes());
    recovered.insert(recovered.end(), block.begin(), block.end());
  }
  return recovered;
}

bool ValidateSerfQtContinuity() {
  const double max_diff = 1.0E-3;
  const std::vector<double> original = {
      1000.000, 1000.002, 1000.004, 1000.006, 1000.008,
      1000.010, 1000.012, 1000.014, 1000.016, 1000.018
  };
  const std::vector<double> recovered =
      RoundTripBlocks<SerfQtCompressor, SerfQtDecompressor>(original, 5, max_diff);
  if (recovered.size() != original.size()) return false;
  for (size_t i = 0; i < original.size(); ++i) {
    if (std::abs(original[i] - recovered[i]) > max_diff) return false;
  }
  return std::abs(recovered[5] - recovered[4]) < 0.01;
}

bool ValidateAdaptive() {
  const double max_diff = 1.0E-6;
  const std::vector<double> original = {
      2.0, 2.0, 2.000001, 2.000002, 2.000003, -1000.123456,
      -1000.123455, 1.0E12, 1.0E12 + 0.25, -1.0E12, 0.0,
      std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
      std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
      std::numeric_limits<double>::quiet_NaN()
  };

  const std::vector<double> recovered =
      RoundTripBlocks<AdaptiveSerfQtCompressor, AdaptiveSerfQtDecompressor>(original, 8, max_diff);
  if (recovered.size() != original.size()) return false;

  for (size_t i = 0; i < original.size(); ++i) {
    if (std::isnan(original[i])) {
      if (!std::isnan(recovered[i])) return false;
    } else if (std::isinf(original[i])) {
      if (original[i] != recovered[i]) return false;
    } else if (std::abs(original[i] - recovered[i]) > max_diff) {
      return false;
    }
  }
  return true;
}

bool ValidateAdaptiveRice() {
  const double max_diff = 1.0E-6;
  const std::vector<double> original = {
      2.0, 2.0, 2.000001, 2.000002, 2.000003, -1000.123456,
      -1000.123455, 1.0E12, 1.0E12 + 0.25, -1.0E12, 0.0,
      std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
      std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
      std::numeric_limits<double>::quiet_NaN()
  };

  const std::vector<double> recovered =
      RoundTripBlocks<AdaptiveSerfQtRiceCompressor, AdaptiveSerfQtRiceDecompressor>(
          original, 8, max_diff);
  if (recovered.size() != original.size()) return false;

  for (size_t i = 0; i < original.size(); ++i) {
    if (std::isnan(original[i])) {
      if (!std::isnan(recovered[i])) return false;
    } else if (std::isinf(original[i])) {
      if (original[i] != recovered[i]) return false;
    } else if (std::abs(original[i] - recovered[i]) > max_diff) {
      return false;
    }
  }
  return true;
}

bool ValidateAdaptiveRiceBounded16() {
  const double max_diff = 1.0E-6;
  const std::vector<double> original = {
      2.0, 2.0, 2.000001, 2.000002, 2.000003, -1000.123456,
      -1000.123455, 1.0E12, 1.0E12 + 0.25, -1.0E12, 0.0,
      std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
      std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
      std::numeric_limits<double>::quiet_NaN()};

  const std::vector<double> recovered =
      RoundTripBlocks<AdaptiveSerfQtRiceBounded16Compressor,
                      AdaptiveSerfQtRiceBounded16Decompressor>(original, 8, max_diff);
  if (recovered.size() != original.size()) return false;
  for (size_t i = 0; i < original.size(); ++i) {
    if (std::isnan(original[i])) {
      if (!std::isnan(recovered[i])) return false;
    } else if (std::isinf(original[i])) {
      if (original[i] != recovered[i]) return false;
    } else if (std::abs(original[i] - recovered[i]) > max_diff) {
      return false;
    }
  }
  return true;
}

bool ValidateAdaptiveRiceMetadata() {
  AdaptiveSerfQtRiceCompressor compressor(4, 1.0E-3);
  AdaptiveSerfQtRiceDecompressor decompressor;

  const auto encode_block = [&](const std::vector<double> &values, double error_bound) {
    for (double value : values) compressor.AddValue(value);
    compressor.Close();
    const long bits = compressor.get_compressed_size_in_bits();
    const std::vector<double> recovered = decompressor.Decompress(compressor.compressed_bytes());
    if (values.size() != recovered.size()) return std::pair<bool, long>{false, bits};
    for (size_t i = 0; i < values.size(); ++i) {
      if (std::abs(values[i] - recovered[i]) > error_bound) {
        return std::pair<bool, long>{false, bits};
      }
    }
    return std::pair<bool, long>{true, bits};
  };

  const auto first = encode_block({2.0, 2.0, 2.0, 2.0}, 1.0E-3);
  const auto unchanged = encode_block({2.0, 2.0, 2.0, 2.0}, 1.0E-3);
  if (!first.first || !unchanged.first || first.second - unchanged.second < 70) return false;

  compressor.SetBlockConfig(3, 1.0E-3);
  const auto changed_size = encode_block({2.0, 2.0, 2.0}, 1.0E-3);
  if (!changed_size.first) return false;

  compressor.SetBlockConfig(3, 1.0E-4);
  const auto changed_error = encode_block({2.0, 2.0, 2.0}, 1.0E-4);
  return changed_error.first;
}

bool ValidateAdaptiveRiceBounded16Metadata() {
  AdaptiveSerfQtRiceBounded16Compressor compressor(4, 1.0E-3);
  AdaptiveSerfQtRiceBounded16Decompressor decompressor;
  const auto encode_block = [&](const std::vector<double> &values, double error_bound) {
    for (double value : values) compressor.AddValue(value);
    compressor.Close();
    const long bits = compressor.get_compressed_size_in_bits();
    const std::vector<double> recovered = decompressor.Decompress(compressor.compressed_bytes());
    if (values.size() != recovered.size()) return std::pair<bool, long>{false, bits};
    for (size_t i = 0; i < values.size(); ++i) {
      if (std::abs(values[i] - recovered[i]) > error_bound) {
        return std::pair<bool, long>{false, bits};
      }
    }
    return std::pair<bool, long>{true, bits};
  };

  const auto first = encode_block({2.0, 2.0, 2.0, 2.0}, 1.0E-3);
  const auto unchanged = encode_block({2.0, 2.0, 2.0, 2.0}, 1.0E-3);
  if (!first.first || !unchanged.first || first.second - unchanged.second < 70) return false;
  compressor.SetBlockConfig(3, 1.0E-3);
  if (!encode_block({2.0, 2.0, 2.0}, 1.0E-3).first) return false;
  compressor.SetBlockConfig(3, 1.0E-4);
  return encode_block({2.0, 2.0, 2.0}, 1.0E-4).first;
}

bool ValidateLog() {
  const double relative_error = 0.01;
  const std::vector<double> original = {
      1.0, 1.001, 1.01, 1000.0, -1000.0, -0.001, 0.001,
      std::numeric_limits<double>::min(), std::numeric_limits<double>::max(),
      0.0, -0.0, std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()
  };

  const std::vector<double> recovered =
      RoundTripBlocks<LogSerfQtCompressor, LogSerfQtDecompressor>(original, 7, relative_error);
  if (recovered.size() != original.size()) return false;

  for (size_t i = 0; i < original.size(); ++i) {
    if (std::isnan(original[i])) {
      if (!std::isnan(recovered[i])) return false;
    } else if (original[i] == 0) {
      if (recovered[i] != 0 || std::signbit(recovered[i])) return false;
    } else if (!std::isfinite(original[i])) {
      if (Double::DoubleToLongBits(original[i]) != Double::DoubleToLongBits(recovered[i])) return false;
    } else {
      const double error = std::abs(original[i] - recovered[i]) / std::abs(original[i]);
      if (std::signbit(original[i]) != std::signbit(recovered[i]) || error > relative_error) return false;
    }
  }
  return true;
}

bool ValidateLogMetadata() {
  LogSerfQtCompressor compressor(4, 1.0E-2);
  LogSerfQtDecompressor decompressor;

  const auto encode_block = [&](const std::vector<double> &values, double relative_error) {
    for (double value : values) compressor.AddValue(value);
    compressor.Close();
    const long bits = compressor.get_compressed_size_in_bits();
    const std::vector<double> recovered = decompressor.Decompress(compressor.compressed_bytes());
    if (values.size() != recovered.size()) return std::pair<bool, long>{false, bits};
    for (size_t i = 0; i < values.size(); ++i) {
      if (values[i] == 0) {
        if (recovered[i] != 0 || std::signbit(recovered[i])) return std::pair<bool, long>{false, bits};
      } else if (std::signbit(values[i]) != std::signbit(recovered[i]) ||
                 std::abs(values[i] - recovered[i]) / std::abs(values[i]) > relative_error) {
        return std::pair<bool, long>{false, bits};
      }
    }
    return std::pair<bool, long>{true, bits};
  };

  const auto first = encode_block({1.0, 1.0, 1.0, 1.0}, 1.0E-2);
  const auto unchanged = encode_block({1.0, 1.0, 1.0, 1.0}, 1.0E-2);
  if (!first.first || !unchanged.first || first.second - unchanged.second < 70) return false;

  LogSerfQtDecompressor fresh_decompressor;
  try {
    fresh_decompressor.Decompress(compressor.compressed_bytes());
    return false;
  } catch (const std::runtime_error &) {
  }

  compressor.SetBlockConfig(3, 1.0E-2);
  const auto zeros = encode_block({0.0, -0.0, 0.0}, 1.0E-2);
  if (!zeros.first || zeros.second > 2 + 16 + 3 * 7) return false;

  compressor.SetBlockConfig(3, 1.0E-3);
  return encode_block({1.0, -1.0, 1.001}, 1.0E-3).first;
}

bool ValidateRealDatasets(const std::filesystem::path &dataset_dir) {
  for (const auto &data_set : kDataSetList) {
    const std::filesystem::path path = dataset_dir / data_set;
    std::ifstream input(path);
    if (!input.is_open()) {
      std::cerr << "Failed to open " << path << '\n';
      return false;
    }

    for (double max_diff : kMaxDiffList) {
      AdaptiveSerfQtCompressor compressor(kBlockSizeOverall, max_diff);
      AdaptiveSerfQtDecompressor decompressor;
      std::vector<double> original;
      size_t block_index = 0;
      while ((original = ReadBlock(input, kBlockSizeOverall)).size() == kBlockSizeOverall) {
        for (double value : original) compressor.AddValue(value);
        compressor.Close();
        const std::vector<double> recovered = decompressor.Decompress(compressor.compressed_bytes());
        for (int i = 0; i < kBlockSizeOverall; ++i) {
          if (std::abs(original[i] - recovered[i]) > max_diff) {
            std::cerr << "AdaptiveSerfQt failed: " << data_set << " max_diff=" << max_diff
                      << " index=" << block_index * kBlockSizeOverall + i << '\n';
            return false;
          }
        }
        ++block_index;
      }
      ResetFileStream(input);

      AdaptiveSerfQtRiceBounded16Compressor bounded16_compressor(kBlockSizeOverall, max_diff);
      AdaptiveSerfQtRiceBounded16Decompressor bounded16_decompressor;
      block_index = 0;
      while ((original = ReadBlock(input, kBlockSizeOverall)).size() == kBlockSizeOverall) {
        for (double value : original) bounded16_compressor.AddValue(value);
        bounded16_compressor.Close();
        const std::vector<double> recovered =
            bounded16_decompressor.Decompress(bounded16_compressor.compressed_bytes());
        for (int i = 0; i < kBlockSizeOverall; ++i) {
          if (std::abs(original[i] - recovered[i]) > max_diff) {
            std::cerr << "AdaptiveSerfQt-Rice-Bounded16 failed: " << data_set
                      << " max_diff=" << max_diff
                      << " index=" << block_index * kBlockSizeOverall + i << '\n';
            return false;
          }
        }
        ++block_index;
      }
      ResetFileStream(input);

      AdaptiveSerfQtRiceCompressor rice_compressor(kBlockSizeOverall, max_diff);
      AdaptiveSerfQtRiceDecompressor rice_decompressor;
      block_index = 0;
      while ((original = ReadBlock(input, kBlockSizeOverall)).size() == kBlockSizeOverall) {
        for (double value : original) rice_compressor.AddValue(value);
        rice_compressor.Close();
        const std::vector<double> recovered =
            rice_decompressor.Decompress(rice_compressor.compressed_bytes());
        for (int i = 0; i < kBlockSizeOverall; ++i) {
          if (std::abs(original[i] - recovered[i]) > max_diff) {
            std::cerr << "AdaptiveSerfQt-Rice failed: " << data_set << " max_diff=" << max_diff
                      << " index=" << block_index * kBlockSizeOverall + i
                      << " original=" << original[i] << " recovered=" << recovered[i]
                      << " error=" << std::abs(original[i] - recovered[i]) << '\n';
            return false;
          }
        }
        ++block_index;
      }
      ResetFileStream(input);
    }

    for (double relative_error : kMaxDiffRel) {
      LogSerfQtCompressor compressor(kBlockSizeOverall, relative_error);
      LogSerfQtDecompressor decompressor;
      std::vector<double> original;
      size_t block_index = 0;
      while ((original = ReadBlock(input, kBlockSizeOverall)).size() == kBlockSizeOverall) {
        for (double value : original) compressor.AddValue(value);
        compressor.Close();
        const std::vector<double> recovered = decompressor.Decompress(compressor.compressed_bytes());
        for (int i = 0; i < kBlockSizeOverall; ++i) {
          const double expected = original[i];
          const double actual_error =
              expected == 0 ? (recovered[i] == 0 ? 0 : std::numeric_limits<double>::infinity())
                            : std::abs(expected - recovered[i]) / std::abs(expected);
          const bool invalid_sign =
              expected == 0 ? std::signbit(recovered[i])
                            : std::signbit(expected) != std::signbit(recovered[i]);
          if (invalid_sign || actual_error > relative_error) {
            std::cerr << "LogSerfQt failed: " << data_set << " relative_error=" << relative_error
                      << " index=" << block_index * kBlockSizeOverall + i << '\n';
            return false;
          }
        }
        ++block_index;
      }
      ResetFileStream(input);
    }
  }
  return true;
}

}  // namespace

int main(int argc, char **argv) {
  const std::filesystem::path dataset_dir =
      argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("test/data_set");
  const bool capped_rice_valid = ValidateCappedRice();
  const bool natural_rice_parameter_valid = ValidateNaturalRiceParameter();
  const bool serf_qt_valid = ValidateSerfQtContinuity();
  const bool adaptive_valid = ValidateAdaptive();
  const bool adaptive_rice_valid = ValidateAdaptiveRice();
  const bool adaptive_rice_bounded16_valid = ValidateAdaptiveRiceBounded16();
  const bool adaptive_rice_metadata_valid = ValidateAdaptiveRiceMetadata();
  const bool adaptive_rice_bounded16_metadata_valid = ValidateAdaptiveRiceBounded16Metadata();
  const bool log_valid = ValidateLog();
  const bool log_metadata_valid = ValidateLogMetadata();
  const bool real_datasets_valid = ValidateRealDatasets(dataset_dir);
  std::cout << "CappedRice=" << (capped_rice_valid ? "PASS" : "FAIL") << '\n';
  std::cout << "NaturalRiceParameter=" << (natural_rice_parameter_valid ? "PASS" : "FAIL")
            << '\n';
  std::cout << "SerfQtContinuousBlocks=" << (serf_qt_valid ? "PASS" : "FAIL") << '\n';
  std::cout << "AdaptiveSerfQt=" << (adaptive_valid ? "PASS" : "FAIL") << '\n';
  std::cout << "AdaptiveSerfQt-Rice=" << (adaptive_rice_valid ? "PASS" : "FAIL") << '\n';
  std::cout << "AdaptiveSerfQt-Rice-Bounded16="
            << (adaptive_rice_bounded16_valid ? "PASS" : "FAIL") << '\n';
  std::cout << "AdaptiveSerfQt-Rice-Metadata=" << (adaptive_rice_metadata_valid ? "PASS" : "FAIL") << '\n';
  std::cout << "AdaptiveSerfQt-Rice-Bounded16-Metadata="
            << (adaptive_rice_bounded16_metadata_valid ? "PASS" : "FAIL") << '\n';
  std::cout << "LogSerfQt=" << (log_valid ? "PASS" : "FAIL") << '\n';
  std::cout << "LogSerfQt-Metadata=" << (log_metadata_valid ? "PASS" : "FAIL") << '\n';
  std::cout << "RealDatasetsAllBounds=" << (real_datasets_valid ? "PASS" : "FAIL") << '\n';
  return capped_rice_valid && natural_rice_parameter_valid && serf_qt_valid && adaptive_valid &&
                 adaptive_rice_valid &&
                 adaptive_rice_bounded16_valid && adaptive_rice_metadata_valid &&
                 adaptive_rice_bounded16_metadata_valid && log_valid && log_metadata_valid &&
                 real_datasets_valid
             ? 0
             : 1;
}
