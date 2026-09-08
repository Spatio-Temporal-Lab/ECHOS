#ifndef SERF_ALL_TEST_PERF_EXPR_CONFIG_HPP_
#define SERF_ALL_TEST_PERF_EXPR_CONFIG_HPP_

#include <string>
#include <unordered_map>
#include <vector>

// File config
const static std::string kExportExprTablePrefix = "test/";
const static std::string kExportExprTableSuffix = "_table.csv";
const static std::string kDataSetDirPrefix = "test/data_set/";
// Default data set config
const static std::string kDataSetList[] = {
    "Air-pressure.csv",
    "Basel-temp.csv",
    "Basel-wind.csv",
    "Chengdu-traj.csv",
    "City-temp.csv",
    "Dew-point-temp.csv",
    "IR-bio-temp.csv",
    "Motor-temp.csv",
    "PM10-dust.csv",
    "Smart-grid.csv",
    "Stocks-USA.csv",
    "T-drive.csv",
    "Wind-Speed.csv"
};
// Overall experiment config
const static std::vector<std::string> kMethodListOverall = {
#ifdef SERF_ENABLE_BASELINE_LZ77
    "LZ77",
#endif
#ifdef SERF_ENABLE_BASELINE_ZSTD
    "Zstd",
#endif
#ifdef SERF_ENABLE_BASELINE_SNAPPY
    "Snappy",
#endif
#ifdef SERF_ENABLE_BASELINE_SZ2
    "SZ2",
#endif
#ifdef SERF_ENABLE_BASELINE_MACHETE
    "Machete",
#endif
#ifdef SERF_ENABLE_BASELINE_SIM_PIECE
    "SimPiece",
#endif
#ifdef SERF_ENABLE_BASELINE_SPRINTZ
    "Sprintz",
#endif
#ifdef SERF_ENABLE_BASELINE_BUFF_RUST
    "Buff",
#endif
#ifdef SERF_ENABLE_BASELINE_DEFLATE
    "Deflate",
#endif
#ifdef SERF_ENABLE_BASELINE_LZ4
    "LZ4",
#endif
#ifdef SERF_ENABLE_BASELINE_FPC
    "FPC",
#endif
#ifdef SERF_ENABLE_BASELINE_GORILLA
    "Gorilla",
#endif
#ifdef SERF_ENABLE_BASELINE_CHIMP128
    "Chimp128",
#endif
#ifdef SERF_ENABLE_BASELINE_ELF
    "Elf",
#endif
#ifdef SERF_ENABLE_BASELINE_ELF_STAR
    "Elf*",
#endif
#ifdef SERF_ENABLE_BASELINE_SERF
    "SerfQt",
#endif
#ifdef SERF_ENABLE_ECHOS
    "ECHOS",
#endif
#ifdef SERF_ENABLE_BASELINE_SERF
    "SerfXOR",
#endif
};
const static double kMaxDiffOverall = 1.0E-3;
const static int kBlockSizeOverall = 50;
// Rel diff experiment config
const static std::string kMethodListRel[] = {
#ifdef SERF_ENABLE_BASELINE_SZ2
    "SZ2_Rel",
#endif
    "SerfXOR_Rel", "ECHOS_Rel"
};
const static double kMaxDiffRel[] = {
    1.0E-4,
    5.0E-4,
    1.0E-3,
    5.0E-3,
    1.0E-2,
    5.0E-2,
    1.0E-1
};
const static int kBlockSizeRel = kBlockSizeOverall;
// Overall experiment under a fixed relative error bound of 1%.
const static double kMaxDiffRelOverall = 1.0E-2;
const static int kBlockSizeRelOverall = kBlockSizeOverall;
// Param experiment (abs max_diff) config
const static std::string kMethodListParamAbsMaxDiff[] = {
    "SZ2", "Machete", "SimPiece", "SerfQt", "ECHOS", "SerfXOR", "Sprintz"
};
const static int kBlockSizeParamAbsMaxDiff = kBlockSizeOverall;
const static double kMaxDiffList[] = {
    1.0E-1, 5.0E-2,
    1.0E-2, 5.0E-3,
    1.0E-3, 5.0E-4,
    1.0E-4, 5.0E-5,
    1.0E-5, 5.0E-6,
    1.0E-6};
// Param experiment (block size) config
const static std::string kMethodListParamBlockSize[] = {
    "SZ2", "Machete", "SimPiece", "SerfQt", "ECHOS", "SerfXOR", "ALP", "SZ-ADT", "Sprintz"
};
const static double kAbsMaxDiffParamBlockSize = kMaxDiffOverall;
const static int kBlockSizeList[] = {50, 100, 200, 400, 600, 800, 1000};
const static int kBlockSizeList_ADT[] = {200, 400, 600, 800, 1000};
// Single precision experiment config
const static std::string kDataSetList32[] = {
    "Air-pressure.csv",
    "Basel-temp.csv",
    "Basel-wind.csv",
    "Chengdu-traj.csv",
    "City-temp.csv",
    "Dew-point-temp.csv",
    "IR-bio-temp.csv",
    "Motor-temp.csv",
    "PM10-dust.csv",
    "Smart-grid.csv",
    "Stocks-USA.csv",
    "T-drive.csv",
    "Wind-Speed.csv"
};
const static std::vector<std::string> kMethodList32 = {
#ifdef SERF_ENABLE_BASELINE_LZ77
    "LZ77",
#endif
#ifdef SERF_ENABLE_BASELINE_ZSTD
    "Zstd",
#endif
#ifdef SERF_ENABLE_BASELINE_SNAPPY
    "Snappy",
#endif
#ifdef SERF_ENABLE_BASELINE_SZ2
    "SZ2",
#endif
#ifdef SERF_ENABLE_BASELINE_DEFLATE
    "Deflate",
#endif
#ifdef SERF_ENABLE_BASELINE_LZ4
    "LZ4",
#endif
#ifdef SERF_ENABLE_BASELINE_CHIMP128
    "Chimp128",
#endif
#ifdef SERF_ENABLE_BASELINE_ELF
    "Elf",
#endif
#ifdef SERF_ENABLE_BASELINE_ELF_STAR
    "Elf*",
#endif
#ifdef SERF_ENABLE_BASELINE_SERF
    "SerfQt",
#endif
#ifdef SERF_ENABLE_ECHOS
    "ECHOS",
#endif
#ifdef SERF_ENABLE_BASELINE_SERF
    "SerfXOR",
#endif
};
const static int kBlockSize32 = kBlockSizeOverall;
const static float kMaxDiff32 = kMaxDiffOverall;
// TSBS experiment config
const static std::string kDataSetListTSBS[] = {
    "Tsbs-iot-latitude.csv",
    "Tsbs-iot-longitude.csv"
};
const static std::string kMethodListTSBS[] = {
    "LZ77", "Zstd", "Snappy", "SZ2", "Machete", "SimPiece", "Sprintz", "Deflate", "LZ4", "FPC", "Gorilla", "Chimp128",
    "Elf", "SerfQt", "ECHOS", "SerfXOR"
};
const static int kBlockSizeTSBS = kBlockSizeOverall;
const static double kMaxDiffTSBS = kMaxDiffOverall;
// Ablation experiment config
const static std::string kMethodListAblation[] = {
    "SerfXOR", "SerfXOR_w/o_Shifter", "SerfXOR_w/o_OptAppr", "SerfXOR_w/o_FastSearch"
};
const static int kBlockSizeAblation = kBlockSizeOverall;
const static double kMaxDiffAblation = kMaxDiffOverall;

// ECHOS ablation experiment config
const static std::vector<std::string> kMethodListEchosAbsAblation = {
    "ECHOS", "Batch-Oracle", "Pointwise-Oracle+Meta",
    "Full-History", "Sliding-Window"};
const static std::vector<std::string> kMethodListEchosRelAblation = {
    "ECHOS", "Explicit Flags"};
const static int kBlockSizeEchosAblation = kBlockSizeOverall;
const static double kAbsMaxDiffEchosAblation = kMaxDiffOverall;
const static double kRelMaxDiffEchosAblation = kMaxDiffRelOverall;
const static std::size_t kSlidingWindowEchosAblation = 8;

// Lock-Up Table for SerfXOR
const static std::unordered_map<std::string, int> kFileNameToAdjustDigit{
    {"Air-pressure.csv", 0},
    {"Basel-temp.csv", 80},
    {"Basel-wind.csv", 126},
    {"Chengdu-traj.csv", 0},
    {"City-temp.csv", 355},
    {"Dew-point-temp.csv", 109},
    {"IR-bio-temp.csv", 39},
    {"Motor-temp.csv", 109},
    {"PM10-dust.csv", 256},
    {"Smart-grid.csv", 4},
    {"Stocks-USA.csv", 245},
    {"T-drive.csv", 0},
    {"Wind-Speed.csv", 8},
    {"Tsbs-iot-latitude.csv", 0},
    {"Tsbs-iot-longitude.csv", 0}
};
// Lambda config
const static double kLambdaFactorList[] = {0.2, 0.4, 0.6, 0.8, 1, 1.2, 1.4, 1.6, 1.8};

#endif //SERF_ALL_TEST_PERF_EXPR_CONFIG_HPP_
