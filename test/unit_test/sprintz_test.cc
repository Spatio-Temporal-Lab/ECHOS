#include <gtest/gtest.h>

#include "Perf_expr_config.hpp"
#include "Perf_file_utils.hpp"

#include "baselines/sprintz/double_sprintz_compressor.h"
#include "baselines/sprintz/double_sprintz_decompressor.h"

TEST(Correctness, Sprintz) {
  for (const auto &data_set : kDataSetList) {
    std::ifstream data_input_stream(kDataSetDirPrefix + data_set);
    if (!data_input_stream.is_open()) {
      std::cerr << "Failed to open the file [" << data_set << "]" << std::endl;
    }

    std::vector<double> original_data;
    while ((original_data = ReadBlock(data_input_stream, kBlockSizeOverall)).size() == kBlockSizeOverall) {
      auto *pack = new int16_t [original_data.size() * 8];
      DoubleSprintzCompressor sprintz_compressor(kMaxDiffOverall);
      sprintz_compressor.compress(original_data, pack);
      DoubleSprintzDecompressor sprintz_decompressor;
      auto decompressed = sprintz_decompressor.decompress(pack);
      ASSERT_EQ(original_data.size(), decompressed.size());
      for (int i = 0; i < original_data.size(); ++i) {
        if (!(std::abs(original_data[i] - decompressed[i]) <= kMaxDiffOverall)) {
          std::cout << original_data[i] << " " << decompressed[i] << std::endl;
        }
        ASSERT_NEAR(original_data[i], decompressed[i], kMaxDiffOverall) << i;
      }
      delete[] pack;
    }
  }
}