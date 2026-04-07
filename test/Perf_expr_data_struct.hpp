#ifndef SERF_ALL_TEST_PERF_EXPR_DATA_STRUCT_HPP_
#define SERF_ALL_TEST_PERF_EXPR_DATA_STRUCT_HPP_

#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <chrono>

static std::string double_to_string_with_precision(double val, size_t precision) {
  std::ostringstream stringBuffer;
  stringBuffer << std::fixed << std::setprecision(precision) << val;
  return stringBuffer.str();
}

class ExprConf {
 public:
  struct hash {
    std::size_t operator()(const ExprConf &conf) const {
      return std::hash<std::string>()(
          conf.method_ + conf.data_set_ + conf.block_size_ + conf.max_diff_ + conf.float_len_);
    }
  };

  ExprConf() = delete;

  ExprConf(std::string method, std::string data_set, int block_size, double max_diff, bool enable_32 = false)
      : method_(method), data_set_(data_set), block_size_(std::to_string(block_size)),
        max_diff_(double_to_string_with_precision(max_diff, 8)), float_len_(enable_32 ? "32" : "64") {}

  bool operator==(const ExprConf &otherConf) const {
    return method_ == otherConf.method_ && data_set_ == otherConf.data_set_ && block_size_ == otherConf.block_size_
        && max_diff_ == otherConf.max_diff_ && float_len_ == otherConf.float_len_;
  }

  std::string method() const {
    return method_;
  }

  std::string data_set() const {
    return data_set_;
  }

  std::string max_diff() const {
    return max_diff_;
  }

  std::string block_size() const {
    return block_size_;
  }

  std::string float_len() const {
    return float_len_;
  }

 private:
  const std::string method_;
  const std::string data_set_;
  const std::string max_diff_;
  const std::string block_size_;
  const std::string float_len_;
};

class PerfRecord {
 public:
  PerfRecord() = default;

  void IncreaseCompressionTime(std::chrono::microseconds &duration) {
    compression_time_ += duration;
  }

  auto &compression_time() {
    return compression_time_;
  }

  auto AvgCompressionTimePerBlock() {
    return static_cast<double>(compression_time_.count()) / block_count_;
  }

  void IncreaseDecompressionTime(std::chrono::microseconds &duration) {
    decompression_time_ += duration;
  }

  auto &decompression_time() {
    return decompression_time_;
  }

  auto AvgDecompressionTimePerBlock() {
    return static_cast<double>(decompression_time_.count()) / block_count_;
  }

  long compressed_size_in_bits() {
    return compressed_size_in_bits_;
  }

  void AddCompressedSize(long size) {
    compressed_size_in_bits_ += size;
  }

  void set_block_count(int blockCount_) {
    block_count_ = blockCount_;
  }

  int block_count() {
    return block_count_;
  }

  double CalCompressionRatio(ExprConf &expr_conf) {
    return (double) compressed_size_in_bits_
        / (double) (block_count_ * std::stoi(expr_conf.block_size()) * std::stoi(expr_conf.float_len()));
  }

 private:
  std::chrono::microseconds compression_time_ = std::chrono::microseconds::zero();
  std::chrono::microseconds decompression_time_ = std::chrono::microseconds::zero();
  long compressed_size_in_bits_ = 0;
  int block_count_ = 0;
};

using ExprTable = std::unordered_map<ExprConf, PerfRecord, ExprConf::hash>;

#endif //SERF_ALL_TEST_PERF_EXPR_DATA_STRUCT_HPP_
