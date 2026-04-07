#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <vector>

#include "baselines/chimp128/chimp_compressor.h"
#include "baselines/chimp128/chimp_decompressor.h"

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
constexpr static double kMaxDiff[] = {1.0E-1, 1.0E-2, 1.0E-3, 1.0E-4, 1.0E-5, 1.0E-6};

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

TEST(TestChimp, CorrectnessTest) {
  for (const auto &data_set : kDataSetList) {
    std::ifstream data_set_input_stream(kDataSetDirPrefix + data_set);
    if (!data_set_input_stream.is_open()) {
      std::cerr << "Failed to open the file [" << data_set << "]" << std::endl;
    }

    std::vector<double> original_data;
    while ((original_data = ReadBlock(data_set_input_stream)).size() == kBlockSize) {
      ChimpCompressor compressor(128);
      for (const auto &item : original_data) {
        compressor.addValue(item);
      }
      compressor.close();
      Array<uint8_t> compressed = compressor.get_compress_pack();
      ChimpDecompressor decompressor(compressed, 128);
      std::vector<double> decompressed = decompressor.decompress();
      EXPECT_EQ(original_data.size(), decompressed.size());
      for (int i = 0; i < kBlockSize; ++i) {
        if (original_data[i] - decompressed[i] != 0) {
          GTEST_LOG_(INFO) << " " << original_data[i] << " " << decompressed[i];
        }
        EXPECT_TRUE(original_data[i] - decompressed[i] == 0);
      }
    }

    data_set_input_stream.close();
  }
}

TEST(TestChimp32, CorrectnessTest) {
  for (const auto &data_set : kDataSetList) {
    std::ifstream data_set_input_stream(kDataSetDirPrefix + data_set);
    if (!data_set_input_stream.is_open()) {
      std::cerr << "Failed to open the file [" << data_set << "]" << std::endl;
    }

    std::vector<double> original_data;
    while ((original_data = ReadBlock(data_set_input_stream)).size() == kBlockSize) {
      ChimpCompressor compressor(128);
      for (const auto &item : original_data) {
        compressor.addValue(item);
      }
      compressor.close();
      Array<uint8_t> compressed = compressor.get_compress_pack();
      ChimpDecompressor decompressor(compressed, 128);
      std::vector<double> decompressed = decompressor.decompress();
      EXPECT_EQ(original_data.size(), decompressed.size());
      for (int i = 0; i < kBlockSize; ++i) {
        if (original_data[i] - decompressed[i] != 0) {
          GTEST_LOG_(INFO) << " " << original_data[i] << " " << decompressed[i];
        }
        EXPECT_TRUE(original_data[i] - decompressed[i] == 0);
      }
    }

    data_set_input_stream.close();
  }
}