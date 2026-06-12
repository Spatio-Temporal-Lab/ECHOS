#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <string>
#include <vector>

#include "compressor/adaptive_serf_qt_compressor.h"
#include "compressor/adaptive_serf_qt_rice_compressor.h"
#include "compressor/log_serf_qt_compressor.h"
#include "compressor/serf_qt_compressor.h"
#include "compressor/serf_xor_compressor_rel.h"
#include "decompressor/adaptive_serf_qt_decompressor.h"
#include "decompressor/adaptive_serf_qt_rice_decompressor.h"
#include "decompressor/log_serf_qt_decompressor.h"
#include "decompressor/serf_qt_decompressor.h"
#include "decompressor/serf_xor_decompressor.h"

namespace {

constexpr int kBlockSize = 50;
constexpr int kSerfXorWindowSize = 1000;
constexpr double kAbsoluteError = 1.0E-3;
constexpr double kRelativeError = 0.01;

struct Result {
  uint64_t values = 0;
  uint64_t bits = 0;
  double compression_ms = 0;
  double decompression_ms = 0;
  double max_error = 0;
  bool valid = true;
};

std::vector<double> ReadValues(const std::filesystem::path &path) {
  std::ifstream input(path);
  std::vector<double> values;
  double value;
  while (input >> value) values.push_back(value);
  return values;
}

template <typename Compressor, typename Decompressor, typename Error>
Result Run(const std::vector<double> &values, double bound, Error error) {
  Result result;
  Compressor compressor(kBlockSize, bound);
  Decompressor decompressor;
  for (size_t begin = 0; begin + kBlockSize <= values.size(); begin += kBlockSize) {
    const auto compression_start = std::chrono::steady_clock::now();
    for (size_t i = begin; i < begin + kBlockSize; ++i) compressor.AddValue(values[i]);
    compressor.Close();
    const auto compression_end = std::chrono::steady_clock::now();

    const auto decompression_start = std::chrono::steady_clock::now();
    const std::vector<double> recovered = decompressor.Decompress(compressor.compressed_bytes());
    const auto decompression_end = std::chrono::steady_clock::now();

    result.bits += compressor.get_compressed_size_in_bits();
    result.values += kBlockSize;
    result.compression_ms +=
        std::chrono::duration<double, std::milli>(compression_end - compression_start).count();
    result.decompression_ms +=
        std::chrono::duration<double, std::milli>(decompression_end - decompression_start).count();
    for (int i = 0; i < kBlockSize; ++i) {
      const double current_error = error(values[begin + i], recovered[i]);
      result.max_error = std::max(result.max_error, current_error);
      if (current_error > bound) result.valid = false;
    }
  }
  return result;
}

Result RunSerfXorRelative(const std::vector<double> &values, double bound, int adjust_digit) {
  Result result;
  SerfXORCompressorRel compressor(kSerfXorWindowSize, bound, adjust_digit);
  SerfXORDecompressor decompressor(adjust_digit);
  for (size_t begin = 0; begin + kBlockSize <= values.size(); begin += kBlockSize) {
    const auto compression_start = std::chrono::steady_clock::now();
    for (size_t i = begin; i < begin + kBlockSize; ++i) compressor.AddValue(values[i]);
    compressor.Close();
    const auto compression_end = std::chrono::steady_clock::now();

    const auto decompression_start = std::chrono::steady_clock::now();
    const std::vector<double> recovered = decompressor.Decompress(compressor.compressed_bytes_last_block());
    const auto decompression_end = std::chrono::steady_clock::now();

    result.bits += compressor.compressed_size_last_block();
    result.values += kBlockSize;
    result.compression_ms +=
        std::chrono::duration<double, std::milli>(compression_end - compression_start).count();
    result.decompression_ms +=
        std::chrono::duration<double, std::milli>(decompression_end - decompression_start).count();
    for (int i = 0; i < kBlockSize; ++i) {
      const double current_error =
          values[begin + i] == 0
              ? (values[begin + i] == recovered[i] ? 0.0 : std::numeric_limits<double>::infinity())
              : std::abs(values[begin + i] - recovered[i]) / std::abs(values[begin + i]);
      result.max_error = std::max(result.max_error, current_error);
      if (current_error > bound) result.valid = false;
    }
  }
  return result;
}

void Print(const std::string &dataset, const std::string &method, const Result &result) {
  const double ratio = static_cast<double>(result.bits) / (64.0 * result.values);
  std::cout << dataset << ',' << method << ',' << result.values << ',' << ratio << ','
            << result.compression_ms << ',' << result.decompression_ms << ','
            << result.max_error << ',' << (result.valid ? "true" : "false") << '\n';
}

}  // namespace

int main(int argc, char **argv) {
  const std::filesystem::path dataset_dir =
      argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("test/data_set");
  const double absolute_bound = argc > 2 ? std::stod(argv[2]) : kAbsoluteError;
  const double relative_bound = argc > 3 ? std::stod(argv[3]) : kRelativeError;
  std::cout << std::setprecision(10);
  std::cout << "Dataset,Method,Values,CompressionRatio,CompressionMs,DecompressionMs,MaxError,Valid\n";
  const std::unordered_map<std::string, int> adjust_digits = {
      {"Air-pressure", 0}, {"Basel-temp", 80}, {"Basel-wind", 126}, {"Chengdu-traj", 0},
      {"City-temp", 355}, {"Dew-point-temp", 109}, {"IR-bio-temp", 39}, {"Motor-temp", 109},
      {"PM10-dust", 256}, {"Smart-grid", 4}, {"Stocks-USA", 245}, {"T-drive", 0},
      {"Wind-Speed", 8}
  };

  for (const auto &entry : std::filesystem::directory_iterator(dataset_dir)) {
    if (entry.path().extension() != ".csv" || entry.path().filename().string().rfind("Tsbs-", 0) == 0) continue;
    const std::vector<double> values = ReadValues(entry.path());
    const std::string dataset = entry.path().stem().string();

    const auto absolute_error = [](double original, double recovered) {
      return std::abs(original - recovered);
    };
    Print(dataset, "SerfQt-Delta",
          Run<SerfQtCompressor, SerfQtDecompressor>(values, absolute_bound, absolute_error));
    Print(dataset, "AdaptiveSerfQt",
          Run<AdaptiveSerfQtCompressor, AdaptiveSerfQtDecompressor>(values, absolute_bound, absolute_error));
    Print(dataset, "AdaptiveSerfQt-Rice",
          Run<AdaptiveSerfQtRiceCompressor, AdaptiveSerfQtRiceDecompressor>(
              values, absolute_bound, absolute_error));

    const auto relative_error = [](double original, double recovered) {
      if (original == 0) return original == recovered ? 0.0 : std::numeric_limits<double>::infinity();
      return std::abs(original - recovered) / std::abs(original);
    };
    Print(dataset, "LogSerfQt",
          Run<LogSerfQtCompressor, LogSerfQtDecompressor>(values, relative_bound, relative_error));
    Print(dataset, "SerfXOR-Relative",
          RunSerfXorRelative(values, relative_bound, adjust_digits.at(dataset)));
  }
  return 0;
}
