#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <vector>

#include "baselines/sim_piece/sim_piece.h"

const static int kBlockSize = 50;
const static std::string kDataSetDirPrefix = "../../test/data_set/";
const static std::string kDataSetList[] = {
    "Air-pressure.csv",
    "Air-sensor.csv",
    "Bird-migration.csv",
    "Basel-temp.csv",
    "Basel-wind.csv",
    "City-temp.csv",
    "Dew-point-temp.csv",
    "IR-bio-temp.csv",
    "PM10-dust.csv",
    "Stocks-DE.csv",
    "Stocks-UK.csv",
    "Stocks-USA.csv",
    "Wind-Speed.csv"
};
constexpr static double kMaxDiff[] = {1.0E-1, 1.0E-2, 1.0E-3};

/**
 * @brief Read a block of double from file input stream, whose size is equal to BLOCK_SIZE
 * @param file_input_stream_ref Input steam where this function reads
 * @return A vector of doubles, whose size may be less than BLOCK_SIZE
 */
std::vector<double> ReadBlock(std::ifstream &file_input_stream_ref) {
  std::vector<double> ret;
  ret.reserve(kBlockSize);
  int entry_count = 0;
  double buffer;
  while (!file_input_stream_ref.eof() && entry_count < kBlockSize) {
    file_input_stream_ref >> buffer;
    ret.emplace_back(buffer);
    ++entry_count;
  }
  return ret;
}

/**
 * @brief Read a block of float from file input stream, whose size is equal to BLOCK_SIZE
 * @param fileInputStreamRef Input steam where this function reads
 * @return A vector of floats, whose size may be less than BLOCK_SIZE
 */
std::vector<float> ReadBlock32(std::ifstream &file_input_stream_ref) {
  std::vector<float> ret;
  ret.reserve(kBlockSize);
  int entry_count = 0;
  float buffer;
  while (!file_input_stream_ref.eof() && entry_count < kBlockSize) {
    file_input_stream_ref >> buffer;
    ret.emplace_back(buffer);
    ++entry_count;
  }
  return ret;
}

void ResetFileStream(std::ifstream &data_set_input_stream_ref) {
  data_set_input_stream_ref.clear();
  data_set_input_stream_ref.seekg(0, std::ios::beg);
}

TEST(TestSimPiece, CorrectnessTest) {
  for (const auto &data_set : kDataSetList) {
    std::ifstream data_set_input_stream(kDataSetDirPrefix + data_set);
    if (!data_set_input_stream.is_open()) {
      std::cerr << "Failed to open the file [" << data_set << "]" << std::endl;
    }

    for (auto &max_diff : kMaxDiff) {
      std::vector<double> original_data;
      while ((original_data = ReadBlock(data_set_input_stream)).size() == kBlockSize) {
        std::vector<Point> points;
        for (int idx = 0; idx < original_data.size(); ++idx) {
          points.emplace_back(idx, original_data[idx]);
        }
        SimPiece sim_piece_compressor(points, max_diff);
        int timestamp_store_size;
        char *compression_output = new char [kBlockSize * 8];
        int compression_len = sim_piece_compressor.toByteArray(compression_output, true, &timestamp_store_size);
        SimPiece sim_piece_decompressor(compression_output, compression_len, true);
        std::vector<Point> decompressed = sim_piece_decompressor.decompress();
        for (int i = 0; i < kBlockSize; ++i) {
          if (std::abs(original_data[i] - decompressed[i].getValue()) > max_diff * 1.1) {
            std::cout << original_data[i] << " " << decompressed[i].getValue() << " " << max_diff << std::endl;
          }
        }
      }

      ResetFileStream(data_set_input_stream);
    }

    data_set_input_stream.close();
  }
}

TEST(TestSimPiece, Simple) {
  std::vector<Point> points;
  for (int i = 0; i < 10; ++i) {
    points.emplace_back(i, i);
  }
  SimPiece sim_piece_compress(points, 0.1);
  char compression_output[15];
  int timestamp_store_size;
  int compression_len = sim_piece_compress.toByteArray(compression_output, true, &timestamp_store_size);
  SimPiece sim_piece_decompress(compression_output, compression_len, true);
  std::vector<Point> decompressed = sim_piece_decompress.decompress();
  for (const auto &item : decompressed) std::cout << item.getValue() << std::endl;
}