#ifndef SERF_ALL_TEST_PERF_FILE_UTILS_HPP_
#define SERF_ALL_TEST_PERF_FILE_UTILS_HPP_

#include <vector>
#include <fstream>

std::vector<double> ReadBlock(std::ifstream &file_input_stream_ref, int block_size) {
  std::vector<double> ret;
  ret.reserve(block_size);
  int entry_count = 0;
  double buffer;
  while (!file_input_stream_ref.eof() && entry_count < block_size) {
    file_input_stream_ref >> buffer;
    ret.emplace_back(buffer);
    ++entry_count;
  }
  return ret;
}

std::vector<float> ReadBlock32(std::ifstream &file_input_stream_ref, int block_size) {
  std::vector<float> ret;
  ret.reserve(block_size);
  int entry_count = 0;
  float buffer;
  while (!file_input_stream_ref.eof() && entry_count < block_size) {
    file_input_stream_ref >> buffer;
    ret.emplace_back(buffer);
    ++entry_count;
  }
  return ret;
}

std::vector<double> ReadBlockUsingBeta(std::ifstream &file_input_stream_ref, int block_size, int beta) {
  std::vector<double> ret;
  ret.reserve(block_size);
  int entry_count = 0;
  double buffer;
  std::string raw_double_string;
  while (!file_input_stream_ref.eof() && entry_count < block_size) {
    std::getline(file_input_stream_ref, raw_double_string);
    if (raw_double_string.empty()) continue;
    buffer = std::stod(raw_double_string.substr(0, raw_double_string.find('.') + 1 + beta));
    ret.emplace_back(buffer);
    ++entry_count;
  }
  return ret;
}

void ResetFileStream(std::ifstream &data_set_input_stream_ref) {
  data_set_input_stream_ref.clear();
  data_set_input_stream_ref.seekg(0, std::ios::beg);
}

#endif //SERF_ALL_TEST_PERF_FILE_UTILS_HPP_
