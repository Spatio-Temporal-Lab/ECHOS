#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "baselines/fpc/fpc_compressor.h"
#include "baselines/fpc/fpc_decompressor.h"

const static int BLOCK_SIZE = 1000;
const static std::string DATA_SET_DIR = "../../test/data_set";

/**
 * @brief Scan all data set files in DATA_SET_DIR.
 * @return A vector contains all data set file path.
 */
std::vector<std::string> scanDataSet() {
    namespace fs = std::filesystem;
    std::vector<std::string> dataSetList;
    for (const auto &entry: fs::recursive_directory_iterator(DATA_SET_DIR)) {
        if (fs::is_regular_file(entry)) {
            std::string fileNameOfEntry = entry.path().filename();
            if (fileNameOfEntry.substr(fileNameOfEntry.find('.') + 1, 3) == "csv") {
                dataSetList.push_back(entry.path().string());
            }
        }
    }
    return dataSetList;
}

/**
 * @brief Read a block of double from file input stream, whose size is equal to BLOCK_SIZE
 * @param fileInputStreamRef Input steam where this function reads
 * @return A vector of doubles, whose size may be less than BLOCK_SIZE
 */
std::vector<double> readBlock(std::ifstream &fileInputStreamRef) {
    std::vector<double> returnData;
    int readDoubleCount = 0;
    double buffer;
    while (!fileInputStreamRef.eof() && readDoubleCount < BLOCK_SIZE) {
        fileInputStreamRef >> buffer;
        returnData.emplace_back(buffer);
        ++readDoubleCount;
    }
    return returnData;
}

/**
 * @brief Read a block of float from file input stream, whose size is equal to BLOCK_SIZE
 * @param fileInputStreamRef Input steam where this function reads
 * @return A vector of doubles, whose size may be less than BLOCK_SIZE
 */
std::vector<float> readBlock32(std::ifstream &fileInputStreamRef) {
    std::vector<float> returnData;
    int readDoubleCount = 0;
    float buffer;
    while (!fileInputStreamRef.eof() && readDoubleCount < BLOCK_SIZE) {
        fileInputStreamRef >> buffer;
        returnData.emplace_back(buffer);
        ++readDoubleCount;
    }
    return returnData;
}

TEST(TestFPC, CorrectnessTest) {
    std::vector<std::string> dataSetList = scanDataSet();
    for (const auto &dataSet: dataSetList) {
        std::ifstream dataSetInputStream(dataSet);
        if (!dataSetInputStream.is_open()) {
            fprintf(stderr, "[Error] Failed to open the file '%s'", dataSet.c_str());
        }

        std::vector<double> originalData;
        while ((originalData = readBlock(dataSetInputStream)).size() == BLOCK_SIZE) {
            FpcCompressor fpc_compressor = FpcCompressor(5, BLOCK_SIZE);
            FpcDecompressor fpc_decompressor = FpcDecompressor(5, BLOCK_SIZE);
            for (const auto &item: originalData) {
                fpc_compressor.addValue(item);
            }
            fpc_compressor.close();
            std::vector<char> result = fpc_compressor.getBytes();
            fpc_decompressor.setBytes(result.data(), result.size());
            std::vector<double> decompressed = fpc_decompressor.decompress();
            EXPECT_EQ(originalData.size(), decompressed.size());
            for (int i = 0; i < BLOCK_SIZE; ++i) {
                if (originalData[i] - decompressed[i] != 0) {
                    GTEST_LOG_(INFO) << " " << originalData[i] << " " << decompressed[i];
                }
                EXPECT_TRUE(originalData[i] - decompressed[i] == 0);
            }
        }

        dataSetInputStream.close();
    }
}