#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "baselines/machete/machete.h"

const static int BLOCK_SIZE = 1000;
const static std::string DATA_SET_DIR = "../../test/data_set";

constexpr static double MAX_DIFF[] = {1.0E-1, 1.0E-2, 1.0E-3, 1.0E-4, 1.0E-5, 1.0E-6, 1.0E-7, 1.0E-8};

/**
 * @brief Scan all data set files in DATA_SET_DIR.
 * @return A vector contains all data set file path.
 */
std::vector<std::string> ScanDataSet() {
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
 * @param file_input_stream_ref Input steam where this function reads
 * @return A vector of doubles, whose size may be less than BLOCK_SIZE
 */
std::vector<double> ReadBlock(std::ifstream &file_input_stream_ref) {
    std::vector<double> returnData;
    int readDoubleCount = 0;
    double buffer;
    while (!file_input_stream_ref.eof() && readDoubleCount < BLOCK_SIZE) {
        file_input_stream_ref >> buffer;
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
std::vector<float> ReadBlock32(std::ifstream &fileInputStreamRef) {
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

TEST(TestMachete, CorrectnessTest) {
    std::vector<std::string> dataSetList = ScanDataSet();
    for (const auto &dataSet: dataSetList) {
        std::string fileName = dataSet.substr(dataSet.find_last_of('/') + 1, dataSet.size());
        for (const auto &max_diff: MAX_DIFF) {
            std::ifstream dataSetInputStream(dataSet);
            if (!dataSetInputStream.is_open()) {
                fprintf(stderr, "[Error] Failed to open the file '%s'", dataSet.c_str());
            }

            std::vector<double> originalData;
            while ((originalData = ReadBlock(dataSetInputStream)).size() == BLOCK_SIZE) {
                auto *compression_buffer = new uint8_t [100000];
                auto *decompression_buffer = new double[BLOCK_SIZE];
                ssize_t compression_output_len = machete_compress<lorenzo1, hybrid>(originalData.data(), originalData.size(), &compression_buffer, max_diff);
                ssize_t decompression_output_len = machete_decompress<lorenzo1, hybrid>(compression_buffer, compression_output_len, decompression_buffer);
                EXPECT_EQ(originalData.size(), decompression_output_len);
                for (int i = 0; i < BLOCK_SIZE; ++i) {
                    if (std::abs(originalData[i] - decompression_buffer[i]) > max_diff) {
                        GTEST_LOG_(INFO) << originalData[i] << " " << decompression_buffer[i] << " " << max_diff;
                    }
                    EXPECT_TRUE(std::abs(originalData[i] - decompression_buffer[i]) <= max_diff);
                }
                delete[] compression_buffer;
                delete[] decompression_buffer;
            }

            dataSetInputStream.close();
        }
    }
}