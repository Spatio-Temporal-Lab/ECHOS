#include <gtest/gtest.h>
#include "Perf_file_utils.hpp"
#include "Perf_expr_config.hpp"

#include "baselines/elf/elf.h"

TEST(TestElf, CorrectnessTest) {
  for (const auto &data_set : kDataSetList) {
    std::ifstream data_set_input_stream(kDataSetDirPrefix + data_set);
    if (!data_set_input_stream.is_open()) {
      std::cerr << "Failed to open the file [" << data_set << "]" << std::endl;
    }

    std::vector<double> original_data;
    while ((original_data = ReadBlock(data_set_input_stream, kBlockSizeOverall)).size() == kBlockSizeOverall) {
      uint8_t *compression_output_buffer;
      double decompression_output[kBlockSizeOverall];
      ssize_t compression_output_len_in_bytes = elf_encode(original_data.data(), original_data.size(),
                                                           &compression_output_buffer, 0);
      elf_decode(compression_output_buffer, compression_output_len_in_bytes, decompression_output, 0);
      for (int i = 0; i < kBlockSizeOverall; ++i) {
        if (original_data[i] - decompression_output[i] != 0) {
          GTEST_LOG_(INFO) << " " << original_data[i] << " " << decompression_output[i];
        }
        EXPECT_TRUE(original_data[i] - decompression_output[i] == 0);
      }
    }

    data_set_input_stream.close();
  }
}
