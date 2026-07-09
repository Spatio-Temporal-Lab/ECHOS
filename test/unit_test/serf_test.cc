#include <gtest/gtest.h>

#include "Perf_expr_config.hpp"
#include "Perf_file_utils.hpp"

#include "baselines/serf/compressor/serf_xor_compressor.h"
#include "baselines/serf/decompressor/serf_xor_decompressor.h"
#include "baselines/serf/compressor/serf_qt_compressor.h"
#include "baselines/serf/decompressor/serf_qt_decompressor.h"
#include "compressor/echos_abs_compressor.h"
#include "decompressor/echos_abs_decompressor.h"
#include "compressor/echos_rel_compressor.h"
#include "decompressor/echos_rel_decompressor.h"
#include "baselines/serf/compressor_32/serf_xor_compressor_32.h"
#include "baselines/serf/decompressor_32/serf_xor_decompressor_32.h"
#include "baselines/serf/compressor/net_serf_xor_compressor.h"
#include "baselines/serf/decompressor/net_serf_xor_decompressor.h"
#include "baselines/serf/compressor/net_serf_qt_compressor.h"
#include "baselines/serf/decompressor/net_serf_qt_decompressor.h"
#include "baselines/serf/compressor_32/serf_qt_compressor_32.h"
#include "baselines/serf/decompressor_32/serf_qt_decompressor_32.h"
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>

const std::string kDataSetDirPrefix_unitest = "../" + kDataSetDirPrefix;

// TEST(Correctness, SerfXOR) {
//   for (const auto &data_set : kDataSetList) {
//     std::ifstream data_set_input_stream(kDataSetDirPrefix + data_set);
//     if (!data_set_input_stream.is_open()) {
//       std::filesystem::path cwd = std::filesystem::current_path();
//       std::cout << "当前工作目录是: " << cwd << std::endl;
//       std::cerr << "Failed to open the file [" << data_set << "]" << std::endl;
//     }

//     int adjust_digit = kFileNameToAdjustDigit.find(data_set)->second;
//     for (const auto &max_diff : kMaxDiffList) {
//       SerfXORCompressor xor_compressor(1000, max_diff, adjust_digit);
//       SerfXORDecompressor xor_decompressor(adjust_digit);

//       std::vector<double> original_data;
//       while ((original_data = ReadBlock(data_set_input_stream, kBlockSizeOverall)).size() == kBlockSizeOverall) {
//         for (const auto &datum : original_data) {
//           xor_compressor.AddValue(datum);
//         }
//         xor_compressor.Close();
//         Array<uint8_t> result = xor_compressor.compressed_bytes_last_block();
//         std::vector<double> decompressed = xor_decompressor.Decompress(result);
//         ASSERT_EQ(original_data.size(), decompressed.size());
//         for (int i = 0; i < kBlockSizeOverall; ++i) {
//           ASSERT_NEAR(original_data[i], decompressed[i], max_diff);
//         }
//       }

//       ResetFileStream(data_set_input_stream);
//     }

//     data_set_input_stream.close();
//   }
// }

TEST(Correctness, SerfQt) {
  for (const auto &data_set : kDataSetList) {
    std::ifstream data_set_input_stream(kDataSetDirPrefix + data_set);
    if (!data_set_input_stream.is_open()) {
      std::cerr << "Failed to open the file [" << data_set << "]" << std::endl;
    }

    for (const auto &max_diff : kMaxDiffList) {
      SerfQtCompressor qt_compressor(kBlockSizeOverall, max_diff);
      SerfQtDecompressor qt_decompressor;
      std::vector<double> original_data;
      while ((original_data = ReadBlock(data_set_input_stream, kBlockSizeOverall)).size() == kBlockSizeOverall) {
        for (const auto &datum : original_data) {
          qt_compressor.AddValue(datum);
        }
        qt_compressor.Close();
        Array<uint8_t> result = qt_compressor.compressed_bytes();
        std::vector<double> decompressed = qt_decompressor.Decompress(result);
        ASSERT_EQ(original_data.size(), decompressed.size());
        for (int i = 0; i < kBlockSizeOverall; ++i) {
          ASSERT_NEAR(original_data[i], decompressed[i], max_diff) << data_set << i;
        }
      }

      ResetFileStream(data_set_input_stream);
    }

    data_set_input_stream.close();
  }
}

// TEST(Correctness, NetSerfXOR) {
//   for (const auto &data_set : kDataSetList) {
//     std::ifstream data_set_input_stream(kDataSetDirPrefix_unitest + data_set);
//     if (!data_set_input_stream.is_open()) {
//       std::cerr << "Failed to open the file [" << data_set << "]" << std::endl;
//     }

//     int adjust_digit = kFileNameToAdjustDigit.find(data_set)->second;
//     for (const auto &max_diff : kMaxDiffList) {
//       NetSerfXORCompressor net_serf_xor_compressor(kBlockSizeOverall, max_diff, adjust_digit);
//       NetSerfXORDecompressor net_serf_xor_decompressor(kBlockSizeOverall, adjust_digit);

//       double originalData;
//       while (!data_set_input_stream.eof()) {
//         data_set_input_stream >> originalData;
//         Array<uint8_t> result = net_serf_xor_compressor.Compress(originalData);
//         double decompressed = net_serf_xor_decompressor.Decompress(result);
//         if (std::abs(originalData - decompressed) > max_diff) {
//           GTEST_LOG_(INFO) << originalData << " " << decompressed << " " << max_diff;
//         }
//         ASSERT_TRUE(std::abs(originalData - decompressed) <= max_diff);
//       }

//       ResetFileStream(data_set_input_stream);
//     }

//     data_set_input_stream.close();
//   }
// }

// TEST(Correctness, TestNetSerfQt) {
//   for (const auto &data_set : kDataSetList) {
//     std::ifstream data_set_input_stream(kDataSetDirPrefix_unitest + data_set);
//     if (!data_set_input_stream.is_open()) {
//       std::cerr << "Failed to open the file [" << data_set << "]" << std::endl;
//     }

//     for (const auto &max_diff : kMaxDiffList) {
//       NetSerfQtCompressor net_serf_qt_compressor(max_diff);
//       NetSerfQtDecompressor net_serf_qt_decompressor(max_diff);

//       double originalData;
//       while (!data_set_input_stream.eof()) {
//         data_set_input_stream >> originalData;
//         Array<uint8_t> result = net_serf_qt_compressor.Compress(originalData);
//         double decompressed = net_serf_qt_decompressor.Decompress(result);
//         if (std::abs(originalData - decompressed) > max_diff) {
//           GTEST_LOG_(INFO) << originalData << " " << decompressed << " " << max_diff;
//         }
//         ASSERT_TRUE(std::abs(originalData - decompressed) <= max_diff);
//       }

//       ResetFileStream(data_set_input_stream);
//     }

//     data_set_input_stream.close();
//   }
// }

// TEST(Correctness, SerfXOR32) {
//   for (const auto &data_set : kDataSetList32) {
//     std::ifstream data_set_input_stream(kDataSetDirPrefix + data_set);
//     if (!data_set_input_stream.is_open()) {
//       std::cerr << "Failed to open the file [" << data_set << "]" << std::endl;
//     }

//     SerfXORCompressor32 xor_compressor_32(1000, kMaxDiff32);
//     SerfXORDecompressor32 xor_decompressor_32;

//     std::vector<float> original_data;
//     int block_cnt = 0;
//     while ((original_data = ReadBlock32(data_set_input_stream, kBlockSize32)).size() == kBlockSize32) {
//       for (const auto &datum : original_data) {
//         xor_compressor_32.AddValue(datum);
//       }
//       xor_compressor_32.Close();
//       Array<uint8_t> result = xor_compressor_32.compressed_bytes_last_block();
//       std::vector<float> decompressed = xor_decompressor_32.Decompress(result);
//       EXPECT_EQ(original_data.size(), decompressed.size());
//       for (int i = 0; i < kBlockSize32; ++i) {
//         if (std::abs(original_data[i] - decompressed[i]) > kMaxDiff32) {
//           GTEST_LOG_(INFO) << original_data[i] << " " << decompressed[i] << " " << kMaxDiff32;
//         }
//         ASSERT_TRUE(std::abs(original_data[i] - decompressed[i]) <= kMaxDiff32);
//       }
//       block_cnt++;
//     }

//     ResetFileStream(data_set_input_stream);

//     data_set_input_stream.close();
//   }
// }

// TEST(Correctness, SerfQt32) {
//   for (const auto &data_set : kDataSetList32) {
//     std::ifstream data_set_input_stream(kDataSetDirPrefix + data_set);
//     if (!data_set_input_stream.is_open()) {
//       std::cerr << "Failed to open the file [" << data_set << "]" << std::endl;
//     }

//     SerfQtCompressor32 qt_compressor_32(kBlockSize32, kMaxDiff32);
//     SerfQtDecompressor32 qt_decompressor_32;

//     std::vector<float> original_data;
//     while ((original_data = ReadBlock32(data_set_input_stream, kBlockSize32)).size() == kBlockSize32) {
//       for (const auto &datum : original_data) {
//         qt_compressor_32.AddValue(datum);
//       }
//       qt_compressor_32.Close();
//       Array<uint8_t> result = qt_compressor_32.compressed_bytes();
//       std::vector<float> decompressed = qt_decompressor_32.Decompress(result);
//       EXPECT_EQ(original_data.size(), decompressed.size());
//       for (int i = 0; i < kBlockSize32; ++i) {
//         if (std::abs(original_data[i] - decompressed[i]) > kMaxDiff32) {
//           GTEST_LOG_(INFO) << original_data[i] << " " << decompressed[i] << " " << kMaxDiff32;
//         }
//         ASSERT_TRUE(std::abs(original_data[i] - decompressed[i]) <= kMaxDiff32);
//       }
//     }

//     ResetFileStream(data_set_input_stream);

//     data_set_input_stream.close();
//   }
// }

TEST(Correctness, EchosAbsStreaming) {
  for (const auto &data_set : kDataSetList) {
    std::ifstream data_set_input_stream(kDataSetDirPrefix + data_set);
    ASSERT_TRUE(data_set_input_stream.is_open()) << "Failed to open " << data_set;

    for (const auto &max_diff : kMaxDiffList) {
      EchosAbsCompressor compressor(kBlockSizeOverall, max_diff);
      EchosAbsDecompressor decompressor;
      std::vector<double> original_data;
      size_t block_index = 0;

      while ((original_data = ReadBlock(data_set_input_stream, kBlockSizeOverall)).size() == kBlockSizeOverall) {
        for (double value : original_data) compressor.AddValue(value);
        compressor.Close();
        const std::vector<double> recovered = decompressor.Decompress(compressor.compressed_bytes());

        ASSERT_EQ(original_data.size(), recovered.size()) << data_set << " max_diff=" << max_diff;
        for (int i = 0; i < kBlockSizeOverall; ++i) {
          ASSERT_LE(std::abs(original_data[i] - recovered[i]), max_diff)
              << data_set << " max_diff=" << max_diff
              << " index=" << block_index * kBlockSizeOverall + i;
        }
        ++block_index;
      }
      ResetFileStream(data_set_input_stream);
    }
    data_set_input_stream.close();
  }
}

TEST(Correctness, EchosAbsMetadataChanges) {
  EchosAbsCompressor compressor(4, 1.0E-3);
  EchosAbsDecompressor decompressor;

  const auto verify_block = [&](const std::vector<double> &original, double error_bound) {
    for (double value : original) compressor.AddValue(value);
    compressor.Close();
    const std::vector<double> recovered = decompressor.Decompress(compressor.compressed_bytes());
    EXPECT_EQ(original.size(), recovered.size());
    for (size_t i = 0; i < original.size(); ++i) {
      EXPECT_LE(std::abs(original[i] - recovered[i]), error_bound);
    }
    return compressor.get_compressed_size_in_bits();
  };

  const long first_bits = verify_block({2.0, 2.0, 2.0, 2.0}, 1.0E-3);
  const long unchanged_bits = verify_block({2.0, 2.0, 2.0, 2.0}, 1.0E-3);
  EXPECT_GE(first_bits - unchanged_bits, 70);
  EchosAbsDecompressor fresh_decompressor;
  EXPECT_THROW(fresh_decompressor.Decompress(compressor.compressed_bytes()), std::runtime_error);

  compressor.SetBlockConfig(3, 1.0E-3);
  verify_block({2.0, 2.0, 2.0}, 1.0E-3);

  compressor.SetBlockConfig(3, 1.0E-4);
  verify_block({2.0, 2.0, 2.0}, 1.0E-4);
}

TEST(Correctness, EchosAbsRawEscape) {
  EchosAbsCompressor compressor(4, 1.0E-3);
  EchosAbsDecompressor decompressor;
  const std::vector<double> original = {
      1.0,
      std::numeric_limits<double>::infinity(),
      -2.5,
      std::numeric_limits<double>::quiet_NaN()};

  for (double value : original) compressor.AddValue(value);
  compressor.Close();
  const std::vector<double> recovered = decompressor.Decompress(compressor.compressed_bytes());

  ASSERT_EQ(original.size(), recovered.size());
  EXPECT_NEAR(original[0], recovered[0], 1.0E-3);
  EXPECT_TRUE(std::isinf(recovered[1]));
  EXPECT_EQ(std::signbit(original[1]), std::signbit(recovered[1]));
  EXPECT_NEAR(original[2], recovered[2], 1.0E-3);
  EXPECT_TRUE(std::isnan(recovered[3]));
}

TEST(Correctness, EchosRelStreamingRelativeError) {
  for (const auto &data_set : kDataSetList) {
    std::ifstream data_set_input_stream(kDataSetDirPrefix + data_set);
    ASSERT_TRUE(data_set_input_stream.is_open()) << "Failed to open " << data_set;

    for (const auto &relative_error : kMaxDiffRel) {
      EchosRelCompressor compressor(kBlockSizeOverall, relative_error);
      EchosRelDecompressor decompressor;
      std::vector<double> original_data;
      size_t block_index = 0;

      while ((original_data = ReadBlock(data_set_input_stream, kBlockSizeOverall)).size() == kBlockSizeOverall) {
        for (double value : original_data) compressor.AddValue(value);
        compressor.Close();
        const std::vector<double> recovered = decompressor.Decompress(compressor.compressed_bytes());

        ASSERT_EQ(original_data.size(), recovered.size()) << data_set << " relative_error=" << relative_error;
        for (int i = 0; i < kBlockSizeOverall; ++i) {
          const double original = original_data[i];
          const double actual_error =
              original == 0 ? (recovered[i] == 0 ? 0 : std::numeric_limits<double>::infinity())
                            : std::abs(original - recovered[i]) / std::abs(original);
          if (original == 0) {
            ASSERT_FALSE(std::signbit(recovered[i]))
                << data_set << " relative_error=" << relative_error
                << " index=" << block_index * kBlockSizeOverall + i;
          } else {
            ASSERT_EQ(std::signbit(original), std::signbit(recovered[i]))
                << data_set << " relative_error=" << relative_error
                << " index=" << block_index * kBlockSizeOverall + i;
          }
          ASSERT_LE(actual_error, relative_error)
              << data_set << " relative_error=" << relative_error
              << " index=" << block_index * kBlockSizeOverall + i;
        }
        ++block_index;
      }
      ResetFileStream(data_set_input_stream);
    }
    data_set_input_stream.close();
  }
}

TEST(Correctness, EchosRelMetadataChangesAndZeroMode) {
  EchosRelCompressor compressor(4, 1.0E-2);
  EchosRelDecompressor decompressor;

  const auto verify_block = [&](const std::vector<double> &original, double relative_error) {
    for (double value : original) compressor.AddValue(value);
    compressor.Close();
    const std::vector<double> recovered = decompressor.Decompress(compressor.compressed_bytes());
    EXPECT_EQ(original.size(), recovered.size());
    for (size_t i = 0; i < original.size(); ++i) {
      if (original[i] == 0) {
        EXPECT_EQ(recovered[i], 0.0);
        EXPECT_FALSE(std::signbit(recovered[i]));
      } else {
        EXPECT_EQ(std::signbit(original[i]), std::signbit(recovered[i]));
        EXPECT_LE(std::abs(original[i] - recovered[i]) / std::abs(original[i]), relative_error);
      }
    }
    return compressor.get_compressed_size_in_bits();
  };

  const long first_bits = verify_block({1.0, 1.0, 1.0, 1.0}, 1.0E-2);
  const long unchanged_bits = verify_block({1.0, 1.0, 1.0, 1.0}, 1.0E-2);
  EXPECT_GE(first_bits - unchanged_bits, 70);
  EchosRelDecompressor fresh_decompressor;
  EXPECT_THROW(fresh_decompressor.Decompress(compressor.compressed_bytes()), std::runtime_error);

  compressor.SetBlockConfig(3, 1.0E-2);
  const long zero_bits = verify_block({0.0, -0.0, 0.0}, 1.0E-2);
  EXPECT_LE(zero_bits, 2 + 16 + 3 * 5);

  compressor.SetBlockConfig(3, 1.0E-3);
  verify_block({1.0, -1.0, 1.001}, 1.0E-3);
}
