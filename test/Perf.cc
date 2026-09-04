#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <stdexcept>

#include "Perf_baseline_inc.hpp"
#include "Perf_expr_config.hpp"
#include "Perf_file_utils.hpp"
#include "Perf_expr_data_struct.hpp"

// Experiment switches
#define RUN_OVERALL_EXPERIMENT
#define RUN_PARAM_ABS_MAX_DIFF_EXPERIMENT
#define RUN_PARAM_BLOCK_SIZE_EXPERIMENT
#define RUN_REL_EXPERIMENT
#define RUN_REL_OVERALL_EXPERIMENT
#define RUN_ECHOS_ABLATION_EXPERIMENT
// #define RUN_SINGLE_PRECISION_EXPERIMENT
// #define RUN_SERF_ABLATION_EXPERIMENT
// #define RUN_LAMBDA_EXPERIMENT
// #define RUN_BETA_EXPERIMENT
// #define RUN_TSBS_EXPERIMENT

void ExportTotalExprTable(ExprTable &expr_table) {
  std::ofstream expr_table_output_stream(kExportExprTablePrefix + "total" + kExportExprTableSuffix);
  if (!expr_table_output_stream.is_open()) {
    std::cerr << "Failed to export performance data." << std::endl;
    exit(-1);
  }

  // Write header
  expr_table_output_stream
      << "Method,DataSet,BlockSize,MaxDiff,CompressionRatio,CompressionTime(AvgPerBlock),DecompressionTime(AvgPerBlock)"
      << std::endl;
  // Write record
  for (const auto &conf_record : expr_table) {
    auto conf = conf_record.first;
    auto record = conf_record.second;
    expr_table_output_stream << conf.method() << "," << conf.data_set() << "," << conf.block_size() << ","
                             << conf.max_diff() << "," << record.CalCompressionRatio(conf) << ","
                             << record.AvgCompressionTimePerBlock() << ","
                             << record.AvgDecompressionTimePerBlock() << std::endl;
  }

  expr_table_output_stream.flush();
  expr_table_output_stream.close();
}

// Auto-Gen for the Overall Experiment

void GenOverallTableCR(ExprTable &expr_table) {
  std::ofstream expr_table_output_stream(kExportExprTablePrefix + "overall_cr" + kExportExprTableSuffix);
  if (!expr_table_output_stream.is_open()) {
    std::cerr << "Failed to export performance data." << std::endl;
    exit(-1);
  }

  expr_table_output_stream << std::setiosflags(std::ios::fixed) << std::setprecision(6);

  for (const auto &method : kMethodListOverall) {
    expr_table_output_stream << method << ",";
    for (const auto &data_set : kDataSetList) {
      ExprConf this_conf = ExprConf(method, data_set, kBlockSizeOverall, kMaxDiffOverall);
      expr_table_output_stream << expr_table.find(this_conf)->second.CalCompressionRatio(this_conf) << ",";
    }
    expr_table_output_stream << std::endl;
  }

  expr_table_output_stream.flush();
  expr_table_output_stream.close();
}

void GenOverallTableCT(ExprTable &expr_table) {
  std::ofstream expr_table_output_stream(kExportExprTablePrefix + "overall_ct" + kExportExprTableSuffix);
  if (!expr_table_output_stream.is_open()) {
    std::cerr << "Failed to export performance data." << std::endl;
    exit(-1);
  }

  expr_table_output_stream << std::setiosflags(std::ios::fixed) << std::setprecision(6);

  for (const auto &method : kMethodListOverall) {
    expr_table_output_stream << method << ",";
    for (const auto &data_set : kDataSetList) {
      ExprConf this_conf = ExprConf(method, data_set, kBlockSizeOverall, kMaxDiffOverall);
      expr_table_output_stream << expr_table.find(this_conf)->second.AvgCompressionTimePerBlock() << ",";
    }
    expr_table_output_stream << std::endl;
  }

  expr_table_output_stream.flush();
  expr_table_output_stream.close();
}

void GenOverallTableDT(ExprTable &expr_table) {
  std::ofstream expr_table_output_stream(kExportExprTablePrefix + "overall_dt" + kExportExprTableSuffix);
  if (!expr_table_output_stream.is_open()) {
    std::cerr << "Failed to export performance data." << std::endl;
    exit(-1);
  }

  expr_table_output_stream << std::setiosflags(std::ios::fixed) << std::setprecision(6);

  for (const auto &method : kMethodListOverall) {
    expr_table_output_stream << method << ",";
    for (const auto &data_set : kDataSetList) {
      ExprConf this_conf = ExprConf(method, data_set, kBlockSizeOverall, kMaxDiffOverall);
      expr_table_output_stream << expr_table.find(this_conf)->second.AvgDecompressionTimePerBlock() << ",";
    }
    expr_table_output_stream << std::endl;
  }

  expr_table_output_stream.flush();
  expr_table_output_stream.close();
}

void GenOverallMethodAverageTable(ExprTable &expr_table) {
  std::cout << std::setiosflags(std::ios::fixed) << std::setprecision(6);
  std::cout << "Method,AvgCompressionRatio,AvgCompressionTimePerBlock,AvgDecompressionTimePerBlock"
            << std::endl;

  for (const auto &method : kMethodListOverall) {
    double sum_cr = 0;
    double sum_ct = 0;
    double sum_dt = 0;
    int cnt = 0;

    for (const auto &data_set : kDataSetList) {
      ExprConf this_conf = ExprConf(method, data_set, kBlockSizeOverall, kMaxDiffOverall);
      auto it = expr_table.find(this_conf);
      if (it == expr_table.end()) {
        continue;
      }

      sum_cr += it->second.CalCompressionRatio(this_conf);
      sum_ct += it->second.AvgCompressionTimePerBlock();
      sum_dt += it->second.AvgDecompressionTimePerBlock();
      ++cnt;
    }

    if (cnt > 0) {
      std::cout << method << ","
                << (sum_cr / cnt) << ","
                << (sum_ct / cnt) << ","
                << (sum_dt / cnt) << std::endl;
    }
  }
}

// Auto-Gen for the Overall Relative-Error Experiment

void GenRelOverallTables(ExprTable &expr_table) {
  std::ofstream cr_output(kExportExprTablePrefix + "overall_rel_cr" + kExportExprTableSuffix);
  std::ofstream ct_output(kExportExprTablePrefix + "overall_rel_ct" + kExportExprTableSuffix);
  std::ofstream dt_output(kExportExprTablePrefix + "overall_rel_dt" + kExportExprTableSuffix);
  if (!cr_output.is_open() || !ct_output.is_open() || !dt_output.is_open()) {
    std::cerr << "Failed to export relative-error performance data." << std::endl;
    exit(-1);
  }

  cr_output << std::setiosflags(std::ios::fixed) << std::setprecision(6);
  ct_output << std::setiosflags(std::ios::fixed) << std::setprecision(6);
  dt_output << std::setiosflags(std::ios::fixed) << std::setprecision(6);

  for (const auto &method : kMethodListRel) {
    cr_output << method << ",";
    ct_output << method << ",";
    dt_output << method << ",";
    for (const auto &data_set : kDataSetList) {
      ExprConf this_conf =
          ExprConf(method, data_set, kBlockSizeRelOverall, kMaxDiffRelOverall);
      const auto result = expr_table.find(this_conf);
      if (result == expr_table.end()) {
        std::cerr << "Missing relative-error result for " << method << " on " << data_set
                  << std::endl;
        exit(-1);
      }
      cr_output << result->second.CalCompressionRatio(this_conf) << ",";
      ct_output << result->second.AvgCompressionTimePerBlock() << ",";
      dt_output << result->second.AvgDecompressionTimePerBlock() << ",";
    }
    cr_output << std::endl;
    ct_output << std::endl;
    dt_output << std::endl;
  }
}

void PrintRelOverallMethodAverageTable(ExprTable &expr_table) {
  std::cout << std::setiosflags(std::ios::fixed) << std::setprecision(6);
  std::cout << "Method,AvgCompressionRatio,AvgCompressionTimePerBlock,"
               "AvgDecompressionTimePerBlock"
            << std::endl;

  for (const auto &method : kMethodListRel) {
    double sum_cr = 0;
    double sum_ct = 0;
    double sum_dt = 0;
    int count = 0;
    for (const auto &data_set : kDataSetList) {
      ExprConf this_conf =
          ExprConf(method, data_set, kBlockSizeRelOverall, kMaxDiffRelOverall);
      const auto result = expr_table.find(this_conf);
      if (result == expr_table.end()) continue;
      sum_cr += result->second.CalCompressionRatio(this_conf);
      sum_ct += result->second.AvgCompressionTimePerBlock();
      sum_dt += result->second.AvgDecompressionTimePerBlock();
      ++count;
    }
    if (count > 0) {
      std::cout << method << "," << sum_cr / count << "," << sum_ct / count << ","
                << sum_dt / count << std::endl;
    }
  }
}

void ExportEchosAblationResults(
    ExprTable &expr_table, const std::vector<std::string> &methods,
    int block_size, double error_bound, const std::string &file_name) {
  std::ofstream output(kExportExprTablePrefix + file_name +
                       kExportExprTableSuffix);
  if (!output.is_open()) {
    throw std::runtime_error("Failed to export ECHOS ablation data");
  }
  output << "Method,DataSet,CompressionRatio,CompressionTime(us/block),"
            "DecompressionTime(us/block),DecisionMetadataRatio,"
            "DecisionMetadataBitsPerValue"
         << std::endl;
  output << std::setiosflags(std::ios::fixed) << std::setprecision(8);
  for (const auto &method : methods) {
    for (const auto &data_set : kDataSetList) {
      ExprConf conf(method, data_set, block_size, error_bound);
      const auto result = expr_table.find(conf);
      if (result == expr_table.end()) {
        throw std::runtime_error("Missing ECHOS ablation result");
      }
      auto &record = result->second;
      const double metadata_bits_per_value =
          static_cast<double>(record.decision_metadata_size_in_bits()) /
          static_cast<double>(record.block_count() * block_size);
      output << method << "," << data_set << ","
             << record.CalCompressionRatio(conf) << ","
             << record.AvgCompressionTimePerBlock() << ","
             << record.AvgDecompressionTimePerBlock() << ","
             << record.DecisionMetadataRatio() << ","
             << metadata_bits_per_value << std::endl;
    }
  }
}

// Auto-Gen for the SinglePrecision Experiment

void GenSinglePrecisionTableCR(ExprTable &expr_table) {
  std::ofstream expr_table_output_stream(kExportExprTablePrefix + "single_precision_cr" + kExportExprTableSuffix);
  if (!expr_table_output_stream.is_open()) {
    std::cerr << "Failed to export performance data." << std::endl;
    exit(-1);
  }

  expr_table_output_stream << std::setiosflags(std::ios::fixed) << std::setprecision(6);

  for (const auto &method : kMethodList32) {
    expr_table_output_stream << method << ",";
    for (const auto &data_set : kDataSetList32) {
      ExprConf this_conf = ExprConf(method, data_set, kBlockSize32, kMaxDiff32, true);
      expr_table_output_stream << expr_table.find(this_conf)->second.CalCompressionRatio(this_conf) << ",";
    }
    expr_table_output_stream << std::endl;
  }

  expr_table_output_stream.flush();
  expr_table_output_stream.close();
}

void GenSinglePrecisionTableCT(ExprTable &expr_table) {
  std::ofstream expr_table_output_stream(kExportExprTablePrefix + "single_precision_ct" + kExportExprTableSuffix);
  if (!expr_table_output_stream.is_open()) {
    std::cerr << "Failed to export performance data." << std::endl;
    exit(-1);
  }

  expr_table_output_stream << std::setiosflags(std::ios::fixed) << std::setprecision(6);

  for (const auto &method : kMethodList32) {
    expr_table_output_stream << method << ",";
    for (const auto &data_set : kDataSetList32) {
      ExprConf this_conf = ExprConf(method, data_set, kBlockSize32, kMaxDiff32, true);
      expr_table_output_stream << expr_table.find(this_conf)->second.AvgCompressionTimePerBlock() << ",";
    }
    expr_table_output_stream << std::endl;
  }

  expr_table_output_stream.flush();
  expr_table_output_stream.close();
}

void GenSinglePrecisionTableDT(ExprTable &expr_table) {
  std::ofstream expr_table_output_stream(kExportExprTablePrefix + "single_precision_dt" + kExportExprTableSuffix);
  if (!expr_table_output_stream.is_open()) {
    std::cerr << "Failed to export performance data." << std::endl;
    exit(-1);
  }

  expr_table_output_stream << std::setiosflags(std::ios::fixed) << std::setprecision(6);

  for (const auto &method : kMethodList32) {
    expr_table_output_stream << method << ",";
    for (const auto &data_set : kDataSetList32) {
      ExprConf this_conf = ExprConf(method, data_set, kBlockSize32, kMaxDiff32, true);
      expr_table_output_stream << expr_table.find(this_conf)->second.AvgDecompressionTimePerBlock() << ",";
    }
    expr_table_output_stream << std::endl;
  }

  expr_table_output_stream.flush();
  expr_table_output_stream.close();
}

// Auto-Gen for the TSBS Experiment

void GenTSBSTableCR(ExprTable &expr_table) {
  std::ofstream expr_table_output_stream(kExportExprTablePrefix + "tsbs_cr" + kExportExprTableSuffix);
  if (!expr_table_output_stream.is_open()) {
    std::cerr << "Failed to export performance data." << std::endl;
    exit(-1);
  }

  expr_table_output_stream << std::setiosflags(std::ios::fixed) << std::setprecision(6);

  for (const auto &method : kMethodListTSBS) {
    expr_table_output_stream << method << ",";
    for (const auto &data_set : kDataSetListTSBS) {
      ExprConf this_conf = ExprConf(method, data_set, kBlockSizeTSBS, kMaxDiffTSBS);
      expr_table_output_stream << expr_table.find(this_conf)->second.CalCompressionRatio(this_conf) << ",";
    }
    expr_table_output_stream << std::endl;
  }

  expr_table_output_stream.flush();
  expr_table_output_stream.close();
}

void GenTSBSTableCT(ExprTable &expr_table) {
  std::ofstream expr_table_output_stream(kExportExprTablePrefix + "tsbs_ct" + kExportExprTableSuffix);
  if (!expr_table_output_stream.is_open()) {
    std::cerr << "Failed to export performance data." << std::endl;
    exit(-1);
  }

  expr_table_output_stream << std::setiosflags(std::ios::fixed) << std::setprecision(6);

  for (const auto &method : kMethodListTSBS) {
    expr_table_output_stream << method << ",";
    for (const auto &data_set : kDataSetListTSBS) {
      ExprConf this_conf = ExprConf(method, data_set, kBlockSizeTSBS, kMaxDiffTSBS);
      expr_table_output_stream << expr_table.find(this_conf)->second.AvgCompressionTimePerBlock() << ",";
    }
    expr_table_output_stream << std::endl;
  }

  expr_table_output_stream.flush();
  expr_table_output_stream.close();
}

void GenTSBSTableDT(ExprTable &expr_table) {
  std::ofstream expr_table_output_stream(kExportExprTablePrefix + "tsbs_dt" + kExportExprTableSuffix);
  if (!expr_table_output_stream.is_open()) {
    std::cerr << "Failed to export performance data." << std::endl;
    exit(-1);
  }

  expr_table_output_stream << std::setiosflags(std::ios::fixed) << std::setprecision(6);

  for (const auto &method : kMethodListTSBS) {
    expr_table_output_stream << method << ",";
    for (const auto &data_set : kDataSetListTSBS) {
      ExprConf this_conf = ExprConf(method, data_set, kBlockSizeTSBS, kMaxDiffTSBS);
      expr_table_output_stream << expr_table.find(this_conf)->second.AvgDecompressionTimePerBlock() << ",";
    }
    expr_table_output_stream << std::endl;
  }

  expr_table_output_stream.flush();
  expr_table_output_stream.close();
}

// Auto-Gen for the Param(Abs MaxDiff) Experiment

void GenParamAbsDiffTable(ExprTable &expr_table) {
  std::ofstream expr_table_output_stream(kExportExprTablePrefix + "param_abs_diff_results" + kExportExprTableSuffix);
  if (!expr_table_output_stream.is_open()) {
    std::cerr << "Failed to export performance data." << std::endl;
    exit(-1);
  }

  expr_table_output_stream << std::setiosflags(std::ios::fixed) << std::setprecision(6);

  for (const auto &max_diff : kMaxDiffList) {
    expr_table_output_stream << max_diff << std::endl;
    expr_table_output_stream << "Compression Ratio" << std::endl;
    for (const auto &method : kMethodListParamAbsMaxDiff) {
      expr_table_output_stream << method << ",";
      for (const auto &data_set : kDataSetList) {
        ExprConf this_conf = ExprConf(method, data_set, kBlockSizeParamAbsMaxDiff, max_diff);
        auto result = expr_table.find(this_conf);
        if (result != expr_table.end()) {
          expr_table_output_stream << result->second.CalCompressionRatio(this_conf) << ",";
        }
      }
      expr_table_output_stream << std::endl;
    }
    expr_table_output_stream << "Compression Time" << std::endl;
    for (const auto &method : kMethodListParamAbsMaxDiff) {
      expr_table_output_stream << method << ",";
      for (const auto &data_set : kDataSetList) {
        ExprConf this_conf = ExprConf(method, data_set, kBlockSizeParamAbsMaxDiff, max_diff);
        auto result = expr_table.find(this_conf);
        if (result != expr_table.end()) {
          expr_table_output_stream << result->second.AvgCompressionTimePerBlock() << ",";
        }
      }
      expr_table_output_stream << std::endl;
    }
    expr_table_output_stream << "Decompression Time" << std::endl;
    for (const auto &method : kMethodListParamAbsMaxDiff) {
      expr_table_output_stream << method << ",";
      for (const auto &data_set : kDataSetList) {
        ExprConf this_conf = ExprConf(method, data_set, kBlockSizeParamAbsMaxDiff, max_diff);
        auto result = expr_table.find(this_conf);
        if (result != expr_table.end()) {
          expr_table_output_stream << result->second.AvgDecompressionTimePerBlock() << ",";
        }
      }
      expr_table_output_stream << std::endl;
    }
  }

  expr_table_output_stream.flush();
  expr_table_output_stream.close();
}

// Auto-Gen for the Param(Block Size) Experiment

void GenParamBlockSizeTable(ExprTable &expr_table) {
  std::ofstream expr_table_output_stream(kExportExprTablePrefix + "param_block_size_results" + kExportExprTableSuffix);
  if (!expr_table_output_stream.is_open()) {
    std::cerr << "Failed to export performance data." << std::endl;
    exit(-1);
  }

  expr_table_output_stream << std::setiosflags(std::ios::fixed) << std::setprecision(6);

  for (const auto &block_size : kBlockSizeList) {
    expr_table_output_stream << block_size << std::endl;
    expr_table_output_stream << "Compression Ratio" << std::endl;
    for (const auto &method : kMethodListParamBlockSize) {
      expr_table_output_stream << method << ",";
      for (const auto &data_set : kDataSetList) {
        ExprConf this_conf = ExprConf(method, data_set, block_size, kAbsMaxDiffParamBlockSize);
        auto result = expr_table.find(this_conf);
        if (result != expr_table.end()) {
          expr_table_output_stream << result->second.CalCompressionRatio(this_conf) << ",";
        }
      }
      expr_table_output_stream << std::endl;
    }
    expr_table_output_stream << "Compression Time" << std::endl;
    for (const auto &method : kMethodListParamBlockSize) {
      expr_table_output_stream << method << ",";
      for (const auto &data_set : kDataSetList) {
        ExprConf this_conf = ExprConf(method, data_set, block_size, kAbsMaxDiffParamBlockSize);
        auto result = expr_table.find(this_conf);
        if (result != expr_table.end()) {
          expr_table_output_stream << result->second.AvgCompressionTimePerBlock() << ",";
        }
      }
      expr_table_output_stream << std::endl;
    }
    expr_table_output_stream << "Decompression Time" << std::endl;
    for (const auto &method : kMethodListParamBlockSize) {
      expr_table_output_stream << method << ",";
      for (const auto &data_set : kDataSetList) {
        ExprConf this_conf = ExprConf(method, data_set, block_size, kAbsMaxDiffParamBlockSize);
        auto result = expr_table.find(this_conf);
        if (result != expr_table.end()) {
          expr_table_output_stream << result->second.AvgDecompressionTimePerBlock() << ",";
        }
      }
      expr_table_output_stream << std::endl;
    }
  }

  expr_table_output_stream.flush();
  expr_table_output_stream.close();
}

// Auto-Gen for the Param(Rel MaxDiff) Experiment

void GenParamRelDiffTableCR(ExprTable &expr_table) {
  std::ofstream expr_table_output_stream(kExportExprTablePrefix + "param_rel_diff_cr" + kExportExprTableSuffix);
  if (!expr_table_output_stream.is_open()) {
    std::cerr << "Failed to export performance data." << std::endl;
    exit(-1);
  }

  expr_table_output_stream << std::setiosflags(std::ios::fixed) << std::setprecision(6);

  for (const auto &max_diff : kMaxDiffRel) {
    for (const auto &method : kMethodListRel) {
      expr_table_output_stream << method << ",";
      for (const auto &data_set : kDataSetList) {
        ExprConf this_conf = ExprConf(method, data_set, kBlockSizeRel, max_diff);
        expr_table_output_stream << expr_table.find(this_conf)->second.CalCompressionRatio(this_conf) << ",";
      }
      expr_table_output_stream << std::endl;
    }
  }

  expr_table_output_stream.flush();
  expr_table_output_stream.close();
}

void GenParamRelDiffTableCT(ExprTable &expr_table) {
  std::ofstream expr_table_output_stream(kExportExprTablePrefix + "param_rel_diff_ct" + kExportExprTableSuffix);
  if (!expr_table_output_stream.is_open()) {
    std::cerr << "Failed to export performance data." << std::endl;
    exit(-1);
  }

  expr_table_output_stream << std::setiosflags(std::ios::fixed) << std::setprecision(6);

  for (const auto &max_diff : kMaxDiffRel) {
    for (const auto &method : kMethodListRel) {
      expr_table_output_stream << method << ",";
      for (const auto &data_set : kDataSetList) {
        ExprConf this_conf = ExprConf(method, data_set, kBlockSizeRel, max_diff);
        expr_table_output_stream << expr_table.find(this_conf)->second.AvgCompressionTimePerBlock() << ",";
      }
      expr_table_output_stream << std::endl;
    }
  }

  expr_table_output_stream.flush();
  expr_table_output_stream.close();
}

void GenParamRelDiffTableDT(ExprTable &expr_table) {
  std::ofstream expr_table_output_stream(kExportExprTablePrefix + "param_rel_diff_dt" + kExportExprTableSuffix);
  if (!expr_table_output_stream.is_open()) {
    std::cerr << "Failed to export performance data." << std::endl;
    exit(-1);
  }

  expr_table_output_stream << std::setiosflags(std::ios::fixed) << std::setprecision(6);

  for (const auto &max_diff : kMaxDiffRel) {
    for (const auto &method : kMethodListRel) {
      expr_table_output_stream << method << ",";
      for (const auto &data_set : kDataSetList) {
        ExprConf this_conf = ExprConf(method, data_set, kBlockSizeRel, max_diff);
        expr_table_output_stream << expr_table.find(this_conf)->second.AvgDecompressionTimePerBlock() << ",";
      }
      expr_table_output_stream << std::endl;
    }
  }

  expr_table_output_stream.flush();
  expr_table_output_stream.close();
}

void PrintParamRelDiffSummary(ExprTable &expr_table) {
  std::cout << "RelativeError,Method,AvgCompressionRatio,AvgCompressionTimePerBlock,"
               "AvgDecompressionTimePerBlock"
            << std::endl;
  for (const auto &max_diff : kMaxDiffRel) {
    for (const auto &method : kMethodListRel) {
      double sum_cr = 0;
      double sum_ct = 0;
      double sum_dt = 0;
      int count = 0;
      for (const auto &data_set : kDataSetList) {
        ExprConf this_conf = ExprConf(method, data_set, kBlockSizeRel, max_diff);
        const auto result = expr_table.find(this_conf);
        if (result == expr_table.end()) continue;
        sum_cr += result->second.CalCompressionRatio(this_conf);
        sum_ct += result->second.AvgCompressionTimePerBlock();
        sum_dt += result->second.AvgDecompressionTimePerBlock();
        ++count;
      }
      if (count > 0) {
        std::cout << max_diff << "," << method << "," << sum_cr / count << "," << sum_ct / count
                  << "," << sum_dt / count << std::endl;
      }
    }
  }
}

// Auto-Gen for the Ablation Experiment

void GenAblationTableCR(ExprTable &expr_table) {
  std::ofstream expr_table_output_stream(kExportExprTablePrefix + "ablation_cr" + kExportExprTableSuffix);
  if (!expr_table_output_stream.is_open()) {
    std::cerr << "Failed to export performance data." << std::endl;
    exit(-1);
  }

  expr_table_output_stream << std::setiosflags(std::ios::fixed) << std::setprecision(6);

  for (const auto &method : kMethodListAblation) {
    expr_table_output_stream << method << ",";
    for (const auto &data_set : kDataSetList) {
      ExprConf this_conf = ExprConf(method, data_set, kBlockSizeAblation, kMaxDiffAblation);
      expr_table_output_stream << expr_table.find(this_conf)->second.CalCompressionRatio(this_conf) << ",";
    }
    expr_table_output_stream << std::endl;
  }

  expr_table_output_stream.flush();
  expr_table_output_stream.close();
}

void GenAblationTableCT(ExprTable &expr_table) {
  std::ofstream expr_table_output_stream(kExportExprTablePrefix + "ablation_ct" + kExportExprTableSuffix);
  if (!expr_table_output_stream.is_open()) {
    std::cerr << "Failed to export performance data." << std::endl;
    exit(-1);
  }

  expr_table_output_stream << std::setiosflags(std::ios::fixed) << std::setprecision(6);

  for (const auto &method : kMethodListAblation) {
    expr_table_output_stream << method << ",";
    for (const auto &data_set : kDataSetList) {
      ExprConf this_conf = ExprConf(method, data_set, kBlockSizeAblation, kMaxDiffAblation);
      expr_table_output_stream << expr_table.find(this_conf)->second.AvgCompressionTimePerBlock() << ",";
    }
    expr_table_output_stream << std::endl;
  }

  expr_table_output_stream.flush();
  expr_table_output_stream.close();
}

void GenAblationTableDT(ExprTable &expr_table) {
  std::ofstream expr_table_output_stream(kExportExprTablePrefix + "ablation_dt" + kExportExprTableSuffix);
  if (!expr_table_output_stream.is_open()) {
    std::cerr << "Failed to export performance data." << std::endl;
    exit(-1);
  }

  expr_table_output_stream << std::setiosflags(std::ios::fixed) << std::setprecision(6);

  for (const auto &method : kMethodListAblation) {
    expr_table_output_stream << method << ",";
    for (const auto &data_set : kDataSetList) {
      ExprConf this_conf = ExprConf(method, data_set, kBlockSizeAblation, kMaxDiffAblation);
      expr_table_output_stream << expr_table.find(this_conf)->second.AvgDecompressionTimePerBlock() << ",";
    }
    expr_table_output_stream << std::endl;
  }

  expr_table_output_stream.flush();
  expr_table_output_stream.close();
}

#ifdef SERF_ENABLE_BASELINE_SERF
void PerfSerfXOR(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
                 const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  SerfXORCompressor serf_xor_compressor(1000, max_diff, kFileNameToAdjustDigit.find(data_set)->second);
  SerfXORDecompressor serf_xor_decompressor(kFileNameToAdjustDigit.find(data_set)->second);

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;

    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) serf_xor_compressor.AddValue(value);
    serf_xor_compressor.Close();
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(serf_xor_compressor.compressed_size_last_block());
    Array<uint8_t> compression_output = serf_xor_compressor.compressed_bytes_last_block();

    auto decompression_start_time = std::chrono::steady_clock::now();
    std::vector<double> decompressed_data = serf_xor_decompressor.Decompress(compression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("SerfXOR", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}

void PerfSerfQt(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
                const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  SerfQtCompressor serf_qt_compressor(block_size, max_diff);
  SerfQtDecompressor serf_qt_decompressor;

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;

    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) serf_qt_compressor.AddValue(value);
    serf_qt_compressor.Close();
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(serf_qt_compressor.get_compressed_size_in_bits());
    Array<uint8_t> compression_output = serf_qt_compressor.compressed_bytes();

    auto decompression_start_time = std::chrono::steady_clock::now();
    std::vector<double> decompressed_data = serf_qt_decompressor.Decompress(compression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("SerfQt", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_ECHOS
void PerfEchosAbs(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
                  const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  EchosAbsCompressor compressor(block_size, max_diff);
  EchosAbsDecompressor decompressor;

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;

    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) compressor.AddValue(value);
    compressor.Close();
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(compressor.get_compressed_size_in_bits());
    Array<uint8_t> compression_output = compressor.compressed_bytes();

    auto decompression_start_time = std::chrono::steady_clock::now();
    std::vector<double> decompressed_data = decompressor.Decompress(compression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("ECHOS", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}

void PerfEchosRel(std::ifstream &data_set_input_stream_ref, double rel_diff, int block_size,
                  const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  EchosRelCompressor compressor(block_size, rel_diff);
  EchosRelDecompressor decompressor;

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;

    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) compressor.AddValue(value);
    compressor.Close();
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(compressor.get_compressed_size_in_bits());
    Array<uint8_t> compression_output = compressor.compressed_bytes();

    auto decompression_start_time = std::chrono::steady_clock::now();
    std::vector<double> decompressed_data = decompressor.Decompress(compression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("ECHOS_Rel", data_set, block_size, rel_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}

void VerifyAbsoluteBlock(const std::vector<double> &original,
                         const std::vector<double> &reconstructed,
                         double max_diff) {
  if (original.size() != reconstructed.size()) {
    throw std::runtime_error("Invalid ECHOS absolute output length");
  }
  for (std::size_t index = 0; index < original.size(); ++index) {
    if (std::isfinite(original[index])) {
      if (!std::isfinite(reconstructed[index]) ||
          std::abs(original[index] - reconstructed[index]) > max_diff) {
        throw std::runtime_error("ECHOS absolute error bound violated");
      }
    }
  }
}

void VerifyRelativeBlock(const std::vector<double> &original,
                         const std::vector<double> &reconstructed,
                         double relative_error_bound) {
  if (original.size() != reconstructed.size()) {
    throw std::runtime_error("Invalid ECHOS relative output length");
  }
  for (std::size_t index = 0; index < original.size(); ++index) {
    if (!std::isfinite(original[index])) continue;
    if (original[index] == 0) {
      if (reconstructed[index] != 0) {
        throw std::runtime_error("ECHOS relative zero reconstruction failed");
      }
      continue;
    }
    const double error = std::abs(original[index] - reconstructed[index]);
    const double bound = relative_error_bound * std::abs(original[index]);
    if (!std::isfinite(reconstructed[index]) ||
        error > bound + 1e-12 * std::abs(original[index])) {
      throw std::runtime_error("ECHOS relative error bound violated");
    }
  }
}

void PerfEchosAbsAblation(std::ifstream &input, double max_diff,
                          int block_size, const std::string &data_set,
                          const std::string &method, EchosAbsMode mode,
                          ExprTable &table) {
  PerfRecord record;
  EchosAbsCompressor compressor(block_size, max_diff, mode,
                                kSlidingWindowEchosAblation);
  EchosAbsDecompressor decompressor(mode, kSlidingWindowEchosAblation);
  int block_count = 0;
  std::vector<double> original;
  while ((original = ReadBlock(input, block_size)).size() == block_size) {
    ++block_count;
    const auto compression_start = std::chrono::steady_clock::now();
    for (double value : original) compressor.AddValue(value);
    compressor.Close();
    const auto compression_end = std::chrono::steady_clock::now();
    record.AddCompressedSize(compressor.get_compressed_size_in_bits());
    record.AddDecisionMetadataSize(
        compressor.get_decision_metadata_size_in_bits());
    const Array<uint8_t> compressed = compressor.compressed_bytes();
    const auto decompression_start = std::chrono::steady_clock::now();
    const std::vector<double> reconstructed =
        decompressor.Decompress(compressed);
    const auto decompression_end = std::chrono::steady_clock::now();
    auto compression_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end - compression_start);
    auto decompression_time =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            decompression_end - decompression_start);
    record.IncreaseCompressionTime(compression_time);
    record.IncreaseDecompressionTime(decompression_time);
    VerifyAbsoluteBlock(original, reconstructed, max_diff);
  }
  record.set_block_count(block_count);
  table.insert(std::make_pair(
      ExprConf(method, data_set, block_size, max_diff), record));
  ResetFileStream(input);
}

void PerfEchosRelAblation(std::ifstream &input, double relative_error_bound,
                          int block_size, const std::string &data_set,
                          const std::string &method, bool explicit_flags,
                          ExprTable &table) {
  PerfRecord record;
  EchosRelCompressor compressor(block_size, relative_error_bound,
                                explicit_flags);
  EchosRelDecompressor decompressor(explicit_flags);
  int block_count = 0;
  std::vector<double> original;
  while ((original = ReadBlock(input, block_size)).size() == block_size) {
    ++block_count;
    const auto compression_start = std::chrono::steady_clock::now();
    for (double value : original) compressor.AddValue(value);
    compressor.Close();
    const auto compression_end = std::chrono::steady_clock::now();
    record.AddCompressedSize(compressor.get_compressed_size_in_bits());
    const Array<uint8_t> compressed = compressor.compressed_bytes();
    const auto decompression_start = std::chrono::steady_clock::now();
    const std::vector<double> reconstructed =
        decompressor.Decompress(compressed);
    const auto decompression_end = std::chrono::steady_clock::now();
    auto compression_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end - compression_start);
    auto decompression_time =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            decompression_end - decompression_start);
    record.IncreaseCompressionTime(compression_time);
    record.IncreaseDecompressionTime(decompression_time);
    VerifyRelativeBlock(original, reconstructed, relative_error_bound);
  }
  record.set_block_count(block_count);
  table.insert(std::make_pair(
      ExprConf(method, data_set, block_size, relative_error_bound), record));
  ResetFileStream(input);
}
#endif

#ifdef SERF_ENABLE_BASELINE_DEFLATE
void PerfDeflate(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
                 const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;

    DeflateCompressor deflate_compressor(block_size);
    DeflateDecompressor deflate_decompressor;

    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) deflate_compressor.addValue(value);
    deflate_compressor.close();
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(deflate_compressor.getCompressedSizeInBits());
    Array<uint8_t> compression_output = deflate_compressor.getBytes();

    auto decompression_start_time = std::chrono::steady_clock::now();
    std::vector<double> decompressed_data = deflate_decompressor.decompress(compression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("Deflate", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_LZ4
void PerfLZ4(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
             const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;

    LZ4Compressor lz_4_compressor(block_size);
    LZ4Decompressor lz_4_decompressor;

    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) lz_4_compressor.addValue(value);
    lz_4_compressor.close();
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(lz_4_compressor.getCompressedSizeInBits());
    Array<char> compression_output = lz_4_compressor.getBytes();

    auto decompression_start_time = std::chrono::steady_clock::now();
    std::vector<double> decompressed_data = lz_4_decompressor.decompress(compression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("LZ4", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_FPC
void PerfFPC(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
             const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;
    FpcCompressor fpc_compressor(5, block_size);
    FpcDecompressor fpc_decompressor(5, block_size);

    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) fpc_compressor.addValue(value);
    fpc_compressor.close();
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(fpc_compressor.getCompressedSizeInBits());
    std::vector<char> compression_output = fpc_compressor.getBytes();
    fpc_decompressor.setBytes(compression_output.data(), compression_output.size());

    auto decompression_start_time = std::chrono::steady_clock::now();
    std::vector<double> decompressed_data = fpc_decompressor.decompress();
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("FPC", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_ZSTD
void PerfZstd(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
              const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;
    char compression_output[block_size * 10];
    double decompression_output[block_size];

    auto compression_start_time = std::chrono::steady_clock::now();
    size_t compression_output_len = ZSTD_compress(compression_output, block_size * 10, original_data.data(),
                                                  original_data.size() * sizeof(double), ZSTD_defaultCLevel());
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(compression_output_len * 8);

    auto decompression_start_time = std::chrono::steady_clock::now();
    ZSTD_decompress(decompression_output, block_size * sizeof(double), compression_output, compression_output_len);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);

    for (int i = 0; i < block_size; ++i) {
      EXPECT_FLOAT_EQ(original_data[i], decompression_output[i]);
    }
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("Zstd", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_SNAPPY
void PerfSnappy(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
                const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;
    std::string compression_output;
    std::string decompression_output;
    auto compression_start_time = std::chrono::steady_clock::now();
    size_t compression_output_len = snappy::Compress(reinterpret_cast<const char *>(original_data.data()),
                                                     original_data.size() * sizeof(double), &compression_output);
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(compression_output_len * 8);

    auto decompression_start_time = std::chrono::steady_clock::now();
    snappy::Uncompress(compression_output.data(), compression_output.size(), &decompression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("Snappy", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_ELF
void PerfElf(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
             const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;

    uint8_t *compression_output_buffer;
    double *decompression_output = new double[block_size];
    ssize_t compression_output_len_in_bytes;
    ssize_t decompression_len;

    auto compression_start_time = std::chrono::steady_clock::now();
    compression_output_len_in_bytes = elf_encode(original_data.data(), original_data.size(),
                                                 &compression_output_buffer, 0);
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(compression_output_len_in_bytes * 8);

    auto decompression_start_time = std::chrono::steady_clock::now();
    decompression_len = elf_decode(compression_output_buffer, compression_output_len_in_bytes, decompression_output,
                                   0);
    delete[] decompression_output;
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("Elf", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_ELF_STAR
void PerfElfStar(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
                 const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;

    uint8_t *compression_output_buffer = nullptr;
    std::vector<double> decompression_output(block_size);

    auto compression_start_time = std::chrono::steady_clock::now();
    const ssize_t compression_output_len_in_bytes =
        elf_star_encode(original_data.data(), original_data.size(), &compression_output_buffer);
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(compression_output_len_in_bytes * 8);

    auto decompression_start_time = std::chrono::steady_clock::now();
    const ssize_t decompression_len =
        elf_star_decode(compression_output_buffer, compression_output_len_in_bytes,
                        decompression_output.data());
    auto decompression_end_time = std::chrono::steady_clock::now();

    std::free(compression_output_buffer);

    if (decompression_len != block_size) {
      throw std::runtime_error("Elf* decompressed an unexpected number of values");
    }

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(
      std::make_pair(ExprConf("Elf*", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_CHIMP128
void PerfChimp128(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
                  const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;
    ChimpCompressor chimp_compressor(128);
    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) {
      chimp_compressor.addValue(value);
    }
    chimp_compressor.close();
    auto compression_end_time = std::chrono::steady_clock::now();
    perf_record.AddCompressedSize(chimp_compressor.get_size());
    Array<uint8_t> compression_output = chimp_compressor.get_compress_pack();
    auto decompression_start_time = std::chrono::steady_clock::now();
    ChimpDecompressor chimp_decompressor(compression_output, 128);
    std::vector<double> decompression_output = chimp_decompressor.decompress();
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("Chimp128", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_GORILLA
void PerfGorilla(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
                 const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<double> original_data;
  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    GorillaCompressor gorilla_compressor(block_size);
    GorillaDecompressor gorilla_decompressor;
    ++block_count;
    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) {
      gorilla_compressor.addValue(value);
    }
    gorilla_compressor.close();
    auto compression_end_time = std::chrono::steady_clock::now();
    perf_record.AddCompressedSize(gorilla_compressor.get_compress_size_in_bits());
    Array<uint8_t> compression_output = gorilla_compressor.get_compress_pack();
    auto decompression_start_time = std::chrono::steady_clock::now();
    std::vector<double> decompression_output = gorilla_decompressor.decompress(compression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("Gorilla", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_LZ77
void PerfLZ77(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
              const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<double> original_data;
  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;
    auto *compression_output = new uint8_t[10000];
    auto *decompression_output = new double[1000];

    auto compression_start_time = std::chrono::steady_clock::now();
    int compression_output_len = fastlz_compress_level(2, original_data.data(), block_size * sizeof(double),
                                                       compression_output);
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(compression_output_len * 8);

    auto decompression_start_time = std::chrono::steady_clock::now();
    fastlz_decompress(compression_output, compression_output_len, decompression_output,
                      block_size * sizeof(double));
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);

    delete[] compression_output;
    delete[] decompression_output;
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("LZ77", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_MACHETE
void PerfMachete(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
                 const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;
    auto *compression_buffer = new uint8_t[100000];
    auto *decompression_buffer = new double[block_size];

    auto compression_start_time = std::chrono::steady_clock::now();
    ssize_t compression_output_len = machete_compress<lorenzo1, hybrid>(original_data.data(), original_data.size(),
                                                                        &compression_buffer, max_diff);
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(compression_output_len * 8);

    auto decompression_start_time = std::chrono::steady_clock::now();
    ssize_t decompression_output_len = machete_decompress<lorenzo1, hybrid>(compression_buffer,
                                                                            compression_output_len,
                                                                            decompression_buffer);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);

    delete[] compression_buffer;
    delete[] decompression_buffer;
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("Machete", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_SZ2
void PerfSZ2(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
             const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;
    size_t compression_output_len;
    auto decompression_output = new double[block_size];

    auto compression_start_time = std::chrono::steady_clock::now();
    auto compression_output = SZ_compress_args(SZ_DOUBLE, original_data.data(), &compression_output_len,
                                               ABS, max_diff * 0.99, 0, 0, 0, 0, 0, 0, original_data.size());
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(compression_output_len * 8);

    auto decompression_start_time = std::chrono::steady_clock::now();
    size_t decompression_output_len = SZ_decompress_args(SZ_DOUBLE, compression_output,
                                                         compression_output_len, decompression_output, 0, 0,
                                                         0, 0, block_size);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);

    delete[] decompression_output;
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("SZ2", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_SIM_PIECE
void PerfSimPiece(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
                  const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;

    std::vector<Point> input_points;
    for (int i = 0; i < original_data.size(); ++i) {
      input_points.emplace_back(i, original_data[i]);
    }

    char *compression_output = new char[original_data.size() * 8];
    int compression_output_len = 0;
    int timestamp_store_size;
    auto compression_start_time = std::chrono::steady_clock::now();
    SimPiece sim_piece_compress(input_points, max_diff);
    compression_output_len = sim_piece_compress.toByteArray(compression_output, true, &timestamp_store_size);
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize((compression_output_len - timestamp_store_size) * 8);

    auto decompression_start_time = std::chrono::steady_clock::now();
    SimPiece sim_piece_decompress(compression_output, compression_output_len, true);
    sim_piece_decompress.decompress();
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("SimPiece", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_SPRINTZ
void PerfSprintz(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
             const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;
    DoubleSprintzCompressor sprintz_compressor(max_diff);
    auto *compression_output = new int16_t [original_data.size() * 8];
    DoubleSprintzDecompressor sprintz_decompressor;

    auto compression_start_time = std::chrono::steady_clock::now();
    int compression_size = sprintz_compressor.compress(original_data, compression_output);
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(compression_size * 8);

    auto decompression_start_time = std::chrono::steady_clock::now();
    sprintz_decompressor.decompress(compression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);

    delete[] compression_output;
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("Sprintz", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_BUFF_RUST
std::size_t BuffScaleForAbsoluteError(double max_diff) {
  if (!std::isfinite(max_diff) || max_diff <= 0.0) {
    throw std::invalid_argument("BUFF requires a finite positive absolute error bound");
  }

  std::size_t scale = 1;
  while (0.49 / static_cast<double>(scale) > max_diff) {
    if (scale >= 1000000000000ULL) {
      throw std::invalid_argument("BUFF supports at most 12 decimal places");
    }
    scale *= 10;
  }
  return scale;
}

void PerfBuff(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
              const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;
  const std::size_t scale = BuffScaleForAbsoluteError(max_diff);

  int block_count = 0;
  std::vector<double> original_data;
  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;
    BuffRustInput *input =
        buff_rust_input_new(original_data.data(), original_data.size(), scale);
    if (input == nullptr) {
      throw std::runtime_error("Failed to prepare BUFF input for " + data_set);
    }

    const auto compression_start_time = std::chrono::steady_clock::now();
    BuffRustCompressed *compressed = buff_rust_compress(input);
    const auto compression_end_time = std::chrono::steady_clock::now();
    buff_rust_input_free(input);
    if (compressed == nullptr) {
      throw std::runtime_error("Native BUFF compression failed for " + data_set);
    }

    perf_record.AddCompressedSize(
        static_cast<long>(buff_rust_compressed_size(compressed) * 8));

    std::vector<double> decompression_output(original_data.size());
    const auto decompression_start_time = std::chrono::steady_clock::now();
    const ptrdiff_t decompression_output_len =
        buff_rust_decompress(compressed, decompression_output.data(),
                             decompression_output.size());
    const auto decompression_end_time = std::chrono::steady_clock::now();
    buff_rust_compressed_free(compressed);

    if (decompression_output_len != static_cast<ptrdiff_t>(original_data.size())) {
      throw std::runtime_error("Native BUFF decompression failed for " + data_set);
    }
    for (std::size_t i = 0; i < original_data.size(); ++i) {
      const double error = std::abs(original_data[i] - decompression_output[i]);
      if (!std::isfinite(decompression_output[i]) || error > max_diff) {
        throw std::runtime_error(
            "Native BUFF exceeded the error bound for " + data_set +
            " at block " + std::to_string(block_count) + ", offset " +
            std::to_string(i) + ": error=" + std::to_string(error));
      }
    }

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);
    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(
      std::make_pair(ExprConf("Buff", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_ALP
void PerfALP(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
             const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;
    uint8_t compress_output_buffer[(block_size * sizeof(double)) + 8096];
    auto decompress_buffer_size = alp::AlpApiUtils::align_value<size_t, alp::config::VECTOR_SIZE>(block_size);
    double decompress_output_buffer[decompress_buffer_size];
    alp::AlpCompressor compressor;
    alp::AlpDecompressor decompressor;
    auto compression_start_time = std::chrono::steady_clock::now();
    compressor.compress(original_data.data(), original_data.size(), compress_output_buffer);
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(compressor.get_size() * 8);

    auto decompression_start_time = std::chrono::steady_clock::now();
    decompressor.decompress(compress_output_buffer, block_size, decompress_output_buffer);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("ALP", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

// Single Precision

#ifdef SERF_ENABLE_ECHOS
void PerfEchosAbs_32(std::ifstream &data_set_input_stream_ref, float max_diff, int block_size,
                     const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  EchosAbsCompressor32 compressor(block_size, max_diff);
  EchosAbsDecompressor32 decompressor;

  int block_count = 0;
  std::vector<float> original_data;

  while ((original_data = ReadBlock32(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;

    auto compression_start_time = std::chrono::steady_clock::now();
    for (float value : original_data) compressor.AddValue(value);
    compressor.Close();
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(compressor.get_compressed_size_in_bits());
    Array<uint8_t> compression_output = compressor.compressed_bytes();

    auto decompression_start_time = std::chrono::steady_clock::now();
    std::vector<float> decompressed_data = decompressor.Decompress(compression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("ECHOS", data_set, block_size, max_diff, true), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_SERF
void PerfSerfXOR_32(std::ifstream &data_set_input_stream_ref, float max_diff, int block_size,
                    const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  SerfXORCompressor32 serf_xor_compressor(1000, max_diff);
  SerfXORDecompressor32 serf_xor_decompressor;

  int block_count = 0;
  std::vector<float> original_data;

  while ((original_data = ReadBlock32(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;

    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) serf_xor_compressor.AddValue(value);
    serf_xor_compressor.Close();
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(serf_xor_compressor.compressed_size_last_block());
    Array<uint8_t> compression_output = serf_xor_compressor.compressed_bytes_last_block();

    auto decompression_start_time = std::chrono::steady_clock::now();
    std::vector<float> decompressed_data = serf_xor_decompressor.Decompress(compression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("SerfXOR", data_set, block_size, max_diff, true), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}

void PerfSerfQt_32(std::ifstream &data_set_input_stream_ref, float max_diff, int block_size,
                   const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  SerfQtCompressor32 serf_qt_compressor(block_size, max_diff * 0.97f);
  SerfQtDecompressor32 serf_qt_decompressor;

  int block_count = 0;
  std::vector<float> original_data;

  while ((original_data = ReadBlock32(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;

    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) serf_qt_compressor.AddValue(value);
    serf_qt_compressor.Close();
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(serf_qt_compressor.stored_compressed_size_in_bits());
    Array<uint8_t> compression_output = serf_qt_compressor.compressed_bytes();

    auto decompression_start_time = std::chrono::steady_clock::now();
    std::vector<float> decompressed_data = serf_qt_decompressor.Decompress(compression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("SerfQt", data_set, block_size, max_diff, true), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_DEFLATE
void PerfDeflate_32(std::ifstream &data_set_input_stream_ref, float max_diff, int block_size,
                    const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<float> original_data;

  while ((original_data = ReadBlock32(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;

    DeflateCompressor deflate_compressor(block_size);
    DeflateDecompressor deflate_decompressor;

    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) deflate_compressor.addValue32(value);
    deflate_compressor.close();
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(deflate_compressor.getCompressedSizeInBits());
    Array<uint8_t> compression_output = deflate_compressor.getBytes();

    auto decompression_start_time = std::chrono::steady_clock::now();
    std::vector<float> decompressed_data = deflate_decompressor.decompress32(compression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("Deflate", data_set, block_size, max_diff, true), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_LZ4
void PerfLZ4_32(std::ifstream &data_set_input_stream_ref, float max_diff, int block_size,
                const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<float> original_data;

  while ((original_data = ReadBlock32(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;

    LZ4Compressor lz_4_compressor(block_size);
    LZ4Decompressor lz_4_decompressor;

    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) lz_4_compressor.addValue32(value);
    lz_4_compressor.close();
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(lz_4_compressor.getCompressedSizeInBits());
    Array<char> compression_output = lz_4_compressor.getBytes();

    auto decompression_start_time = std::chrono::steady_clock::now();
    std::vector<float> decompressed_data = lz_4_decompressor.decompress32(compression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("LZ4", data_set, block_size, max_diff, true), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_LZ77
void PerfLZ77_32(std::ifstream &data_set_input_stream_ref, float max_diff, int block_size,
                 const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<float> original_data;
  while ((original_data = ReadBlock32(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;
    auto *compression_output = new uint8_t[10000];
    auto *decompression_output = new float[1000];

    auto compression_start_time = std::chrono::steady_clock::now();
    int compression_output_len = fastlz_compress_level(2, original_data.data(), block_size * sizeof(float),
                                                       compression_output);
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(compression_output_len * 8);

    auto decompression_start_time = std::chrono::steady_clock::now();
    fastlz_decompress(compression_output, compression_output_len, decompression_output,
                      block_size * sizeof(float));
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);

    delete[] compression_output;
    delete[] decompression_output;
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("LZ77", data_set, block_size, max_diff, true), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_SNAPPY
void PerfSnappy_32(std::ifstream &data_set_input_stream_ref, float max_diff, int block_size,
                   const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<float> original_data;

  while ((original_data = ReadBlock32(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;
    std::string compression_output;
    std::string decompression_output;
    auto compression_start_time = std::chrono::steady_clock::now();
    size_t compression_output_len = snappy::Compress(reinterpret_cast<const char *>(original_data.data()),
                                                     original_data.size() * sizeof(float), &compression_output);
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(compression_output_len * 8);

    auto decompression_start_time = std::chrono::steady_clock::now();
    snappy::Uncompress(compression_output.data(), compression_output.size(), &decompression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("Snappy", data_set, block_size, max_diff, true), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_ZSTD
void PerfZstd_32(std::ifstream &data_set_input_stream_ref, float max_diff, int block_size,
                 const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<float> original_data;

  while ((original_data = ReadBlock32(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;
    char compression_output[block_size * 10];
    float decompression_output[block_size];

    auto compression_start_time = std::chrono::steady_clock::now();
    size_t compression_output_len = ZSTD_compress(compression_output, block_size * 10, original_data.data(),
                                                  original_data.size() * sizeof(float), 3);
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(compression_output_len * 8);

    auto decompression_start_time = std::chrono::steady_clock::now();
    ZSTD_decompress(decompression_output, block_size * sizeof(float), compression_output, compression_output_len);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("Zstd", data_set, block_size, max_diff, true), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_SZ2
void PerfSZ2_32(std::ifstream &data_set_input_stream_ref, float max_diff, int block_size,
                const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<float> original_data;

  while ((original_data = ReadBlock32(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;
    size_t compression_output_len;
    auto decompression_output = new float[block_size];

    auto compression_start_time = std::chrono::steady_clock::now();
    auto compression_output = SZ_compress_args(SZ_FLOAT, original_data.data(), &compression_output_len,
                                               ABS, max_diff * 0.97, 0, 0, 0, 0, 0, 0, original_data.size());
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(compression_output_len * 8);

    auto decompression_start_time = std::chrono::steady_clock::now();
    size_t decompression_output_len = SZ_decompress_args(SZ_FLOAT, compression_output,
                                                         compression_output_len, decompression_output, 0, 0,
                                                         0, 0, block_size);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);

    delete[] decompression_output;
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("SZ2", data_set, block_size, max_diff, true), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_ELF
void PerfElf_32(std::ifstream &data_set_input_stream_ref, float max_diff, int block_size,
                const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<float> original_data;

  while ((original_data = ReadBlock32(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;

    uint8_t *compression_output_buffer;
    auto *decompression_output = new float[block_size];
    ssize_t compression_output_len_in_bytes;
    ssize_t decompression_len;

    auto compression_start_time = std::chrono::steady_clock::now();
    compression_output_len_in_bytes = elf_encode_32(original_data.data(), original_data.size(),
                                                    &compression_output_buffer, 0);
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(compression_output_len_in_bytes * 8);

    auto decompression_start_time = std::chrono::steady_clock::now();
    decompression_len = elf_decode_32(compression_output_buffer, compression_output_len_in_bytes,
                                      decompression_output, 0);
    delete[] decompression_output;
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("Elf", data_set, block_size, max_diff, true), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_CHIMP128
void PerfChimp128_32(std::ifstream &data_set_input_stream_ref, float max_diff, int block_size,
                     const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<float> original_data;

  while ((original_data = ReadBlock32(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;
    ChimpCompressor32 chimp_compressor(128);

    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) {
      chimp_compressor.addValue(value);
    }
    chimp_compressor.close();
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(chimp_compressor.get_size());

    Array<uint8_t> compression_output = chimp_compressor.get_compress_pack();

    auto decompression_start_time = std::chrono::steady_clock::now();
    ChimpDecompressor32 chimp_decompressor(compression_output, 128);
    std::vector<float> decompression_output = chimp_decompressor.decompress();
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("Chimp128", data_set, block_size, max_diff, true), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

// Ablation

#ifdef SERF_ENABLE_BASELINE_SERF
void PerfSerfXOR_Without_Shifter(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
                                 const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  SerfXORCompressor serf_xor_compressor(1000, max_diff, 0);
  SerfXORDecompressor serf_xor_decompressor(0);

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;

    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) serf_xor_compressor.AddValue(value);
    serf_xor_compressor.Close();
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(serf_xor_compressor.compressed_size_last_block());
    Array<uint8_t> compression_output = serf_xor_compressor.compressed_bytes_last_block();

    auto decompression_start_time = std::chrono::steady_clock::now();
    std::vector<double> decompressed_data = serf_xor_decompressor.Decompress(compression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("SerfXOR_w/o_Shifter", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}

void PerfSerfXOR_Without_OptAppr(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
                                 const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  SerfXORCompressorNoAppr serf_xor_compressor(1000, max_diff, kFileNameToAdjustDigit.find(data_set)->second);
  SerfXORDecompressor serf_xor_decompressor(kFileNameToAdjustDigit.find(data_set)->second);

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;

    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) serf_xor_compressor.AddValue(value);
    serf_xor_compressor.Close();
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(serf_xor_compressor.compressed_size_last_block());
    Array<uint8_t> compression_output = serf_xor_compressor.compressed_bytes_last_block();

    auto decompression_start_time = std::chrono::steady_clock::now();
    std::vector<double> decompressed_data = serf_xor_decompressor.Decompress(compression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("SerfXOR_w/o_OptAppr", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}

void PerfSerfXOR_Without_FastSearch(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
                                    const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  SerfXORCompressorNoFastSearch serf_xor_compressor(1000, max_diff, kFileNameToAdjustDigit.find(data_set)->second);
  SerfXORDecompressor serf_xor_decompressor(kFileNameToAdjustDigit.find(data_set)->second);

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;

    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) serf_xor_compressor.AddValue(value);
    serf_xor_compressor.Close();
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(serf_xor_compressor.compressed_size_last_block());
    Array<uint8_t> compression_output = serf_xor_compressor.compressed_bytes_last_block();

    auto decompression_start_time = std::chrono::steady_clock::now();
    std::vector<double> decompressed_data = serf_xor_decompressor.Decompress(compression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("SerfXOR_w/o_FastSearch", data_set, block_size, max_diff),
                                        perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

// Relational error-bound

#ifdef SERF_ENABLE_BASELINE_SZ2
void PerfSZ2Rel(std::ifstream &data_set_input_stream_ref, double rel_diff, int block_size,
                const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;
    size_t compression_output_len;
    auto decompression_output = new double[block_size];

    auto compression_start_time = std::chrono::steady_clock::now();
    auto compression_output = SZ_compress_args(SZ_DOUBLE, original_data.data(), &compression_output_len,
                                               REL, 0, rel_diff, 0, 0, 0, 0, 0, original_data.size());
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(compression_output_len * 8);

    auto decompression_start_time = std::chrono::steady_clock::now();
    size_t decompression_output_len = SZ_decompress_args(SZ_DOUBLE, compression_output,
                                                         compression_output_len, decompression_output, 0, 0,
                                                         0, 0, block_size);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);

    delete[] decompression_output;
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("SZ2_Rel", data_set, block_size, rel_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_SERF
void PerfSerfXORRel(std::ifstream &data_set_input_stream_ref, double rel_diff, int block_size,
                    const std::string &data_set, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  SerfXORCompressorRel serf_xor_compressor_rel(1000, rel_diff, kFileNameToAdjustDigit.find(data_set)->second);
  SerfXORDecompressor serf_xor_decompressor(kFileNameToAdjustDigit.find(data_set)->second);

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;

    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) serf_xor_compressor_rel.AddValue(value);
    serf_xor_compressor_rel.Close();
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(serf_xor_compressor_rel.compressed_size_last_block());
    Array<uint8_t> compression_output = serf_xor_compressor_rel.compressed_bytes_last_block();

    auto decompression_start_time = std::chrono::steady_clock::now();
    std::vector<double> decompressed_data = serf_xor_decompressor.Decompress(compression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("SerfXOR_Rel", data_set, block_size, rel_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

// Lambda Expr

#ifdef SERF_ENABLE_BASELINE_SERF
void PerfSerfXORLambda(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
                       const std::string &data_set, int lambda, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  SerfXORCompressor serf_xor_compressor(1000, max_diff, lambda);
  SerfXORDecompressor serf_xor_decompressor(lambda);

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;

    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) serf_xor_compressor.AddValue(value);
    serf_xor_compressor.Close();
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(serf_xor_compressor.compressed_size_last_block());
    Array<uint8_t> compression_output = serf_xor_compressor.compressed_bytes_last_block();

    auto decompression_start_time = std::chrono::steady_clock::now();
    std::vector<double> decompressed_data = serf_xor_decompressor.Decompress(compression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("SerfXOR", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}

void PerfSerfXORLambdaRel(std::ifstream &data_set_input_stream_ref, double max_diff, int block_size,
                       const std::string &data_set, int lambda, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  SerfXORCompressorRel serf_xor_compressor(1000, max_diff, lambda);
  SerfXORDecompressor serf_xor_decompressor(lambda);

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlock(data_set_input_stream_ref, block_size)).size() == block_size) {
    ++block_count;

    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) serf_xor_compressor.AddValue(value);
    serf_xor_compressor.Close();
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(serf_xor_compressor.compressed_size_last_block());
    Array<uint8_t> compression_output = serf_xor_compressor.compressed_bytes_last_block();

    auto decompression_start_time = std::chrono::steady_clock::now();
    std::vector<double> decompressed_data = serf_xor_decompressor.Decompress(compression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("SerfXOR_Rel", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

// Beta experiment
#ifdef SERF_ENABLE_BASELINE_SERF
void PerfSerfXORBeta(std::ifstream &data_set_input_stream_ref, const std::string &data_set, double max_diff,
                     int block_size, int beta, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  SerfXORCompressor serf_xor_compressor(1000, max_diff, kFileNameToAdjustDigit.find(data_set)->second);
  SerfXORDecompressor serf_xor_decompressor(kFileNameToAdjustDigit.find(data_set)->second);

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlockUsingBeta(data_set_input_stream_ref, block_size, beta)).size() == block_size) {
    ++block_count;

    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) serf_xor_compressor.AddValue(value);
    serf_xor_compressor.Close();
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(serf_xor_compressor.compressed_size_last_block());
    Array<uint8_t> compression_output = serf_xor_compressor.compressed_bytes_last_block();

    auto decompression_start_time = std::chrono::steady_clock::now();
    std::vector<double> decompressed_data = serf_xor_decompressor.Decompress(compression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("SerfXOR", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}

void PerfSerfQtBeta(std::ifstream &data_set_input_stream_ref, const std::string &data_set, double max_diff,
                    int block_size, int beta, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  SerfQtCompressor serf_qt_compressor(block_size, max_diff);
  SerfQtDecompressor serf_qt_decompressor;

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlockUsingBeta(data_set_input_stream_ref, block_size, beta)).size() == block_size) {
    ++block_count;

    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) serf_qt_compressor.AddValue(value);
    serf_qt_compressor.Close();
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(serf_qt_compressor.get_compressed_size_in_bits());
    Array<uint8_t> compression_output = serf_qt_compressor.compressed_bytes();

    auto decompression_start_time = std::chrono::steady_clock::now();
    std::vector<double> decompressed_data = serf_qt_decompressor.Decompress(compression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("SerfQt", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_ECHOS
void PerfEchosAbsBeta(std::ifstream &data_set_input_stream_ref, const std::string &data_set,
                      double max_diff, int block_size, int beta, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  EchosAbsCompressor compressor(block_size, max_diff);
  EchosAbsDecompressor decompressor;

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlockUsingBeta(data_set_input_stream_ref, block_size, beta)).size() == block_size) {
    ++block_count;

    auto compression_start_time = std::chrono::steady_clock::now();
    for (const auto &value : original_data) compressor.AddValue(value);
    compressor.Close();
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(compressor.get_compressed_size_in_bits());
    Array<uint8_t> compression_output = compressor.compressed_bytes();

    auto decompression_start_time = std::chrono::steady_clock::now();
    std::vector<double> decompressed_data = decompressor.Decompress(compression_output);
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("ECHOS", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef SERF_ENABLE_BASELINE_ELF
void PerfElfBeta(std::ifstream &data_set_input_stream_ref, const std::string &data_set, double max_diff,
                 int block_size, int beta, ExprTable &table_to_insert) {
  PerfRecord perf_record;

  int block_count = 0;
  std::vector<double> original_data;

  while ((original_data = ReadBlockUsingBeta(data_set_input_stream_ref, block_size, beta)).size() == block_size) {
    ++block_count;

    uint8_t *compression_output_buffer;
    double *decompression_output = new double[block_size];
    ssize_t compression_output_len_in_bytes;
    ssize_t decompression_len;

    auto compression_start_time = std::chrono::steady_clock::now();
    compression_output_len_in_bytes = elf_encode(original_data.data(), original_data.size(),
                                                 &compression_output_buffer, 0);
    auto compression_end_time = std::chrono::steady_clock::now();

    perf_record.AddCompressedSize(compression_output_len_in_bytes * 8);

    auto decompression_start_time = std::chrono::steady_clock::now();
    decompression_len = elf_decode(compression_output_buffer, compression_output_len_in_bytes, decompression_output,
                                   0);
    delete[] decompression_output;
    auto decompression_end_time = std::chrono::steady_clock::now();

    auto compression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        compression_end_time - compression_start_time);
    auto decompression_time_in_a_block = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decompression_end_time - decompression_start_time);

    perf_record.IncreaseCompressionTime(compression_time_in_a_block);
    perf_record.IncreaseDecompressionTime(decompression_time_in_a_block);
  }

  perf_record.set_block_count(block_count);
  table_to_insert.insert(std::make_pair(ExprConf("Elf", data_set, block_size, max_diff), perf_record));
  ResetFileStream(data_set_input_stream_ref);
}
#endif

#ifdef RUN_OVERALL_EXPERIMENT
TEST(Perf, Overall) {
  ExprTable expr_table_overall;

  for (const auto &data_set : kDataSetList) {
    std::ifstream data_input_stream(kDataSetDirPrefix + data_set);
    if (!data_input_stream.is_open()) {
      std::cerr << "Failed to open the file [" << data_set << "]" << std::endl;
    }

    // Lossy Compression
#ifdef SERF_ENABLE_BASELINE_SERF
    PerfSerfXOR(data_input_stream, kMaxDiffOverall, kBlockSizeOverall, data_set, expr_table_overall);
    PerfSerfQt(data_input_stream, kMaxDiffOverall, kBlockSizeOverall, data_set, expr_table_overall);
#endif
#ifdef SERF_ENABLE_ECHOS
    PerfEchosAbs(data_input_stream, kMaxDiffOverall, kBlockSizeOverall, data_set, expr_table_overall);
#endif
#ifdef SERF_ENABLE_BASELINE_MACHETE
    PerfMachete(data_input_stream, kMaxDiffOverall, kBlockSizeOverall, data_set, expr_table_overall);
#endif
#ifdef SERF_ENABLE_BASELINE_SZ2
    PerfSZ2(data_input_stream, kMaxDiffOverall, kBlockSizeOverall, data_set, expr_table_overall);
#endif
#ifdef SERF_ENABLE_BASELINE_SIM_PIECE
    PerfSimPiece(data_input_stream, kMaxDiffOverall, kBlockSizeOverall, data_set, expr_table_overall);
#endif
#ifdef SERF_ENABLE_BASELINE_SPRINTZ
    PerfSprintz(data_input_stream, kMaxDiffOverall, kBlockSizeOverall, data_set, expr_table_overall);
#endif
#ifdef SERF_ENABLE_BASELINE_BUFF_RUST
    PerfBuff(data_input_stream, kMaxDiffOverall, kBlockSizeOverall, data_set, expr_table_overall);
#endif

    // Lossless Compression
#ifdef SERF_ENABLE_BASELINE_GORILLA
    PerfGorilla(data_input_stream, kMaxDiffOverall, kBlockSizeOverall, data_set, expr_table_overall);
#endif
#ifdef SERF_ENABLE_BASELINE_CHIMP128
    PerfChimp128(data_input_stream, kMaxDiffOverall, kBlockSizeOverall, data_set, expr_table_overall);
#endif
#ifdef SERF_ENABLE_BASELINE_DEFLATE
    PerfDeflate(data_input_stream, kMaxDiffOverall, kBlockSizeOverall, data_set, expr_table_overall);
#endif
#ifdef SERF_ENABLE_BASELINE_ELF
    PerfElf(data_input_stream, kMaxDiffOverall, kBlockSizeOverall, data_set, expr_table_overall);
#endif
#ifdef SERF_ENABLE_BASELINE_ELF_STAR
    PerfElfStar(data_input_stream, kMaxDiffOverall, kBlockSizeOverall, data_set, expr_table_overall);
#endif
#ifdef SERF_ENABLE_BASELINE_FPC
    PerfFPC(data_input_stream, kMaxDiffOverall, kBlockSizeOverall, data_set, expr_table_overall);
#endif
#ifdef SERF_ENABLE_BASELINE_LZ4
    PerfLZ4(data_input_stream, kMaxDiffOverall, kBlockSizeOverall, data_set, expr_table_overall);
#endif
#ifdef SERF_ENABLE_BASELINE_SERF
    PerfSerfXOR(data_input_stream, kMaxDiffOverall, kBlockSizeOverall, data_set, expr_table_overall);
#endif
#ifdef SERF_ENABLE_BASELINE_LZ77
    PerfLZ77(data_input_stream, kMaxDiffOverall, kBlockSizeOverall, data_set, expr_table_overall);
#endif
#ifdef SERF_ENABLE_BASELINE_ZSTD
    PerfZstd(data_input_stream, kMaxDiffOverall, kBlockSizeOverall, data_set, expr_table_overall);
#endif
#ifdef SERF_ENABLE_BASELINE_SNAPPY
    PerfSnappy(data_input_stream, kMaxDiffOverall, kBlockSizeOverall, data_set, expr_table_overall);
#endif

    data_input_stream.close();
  }

  GenOverallTableCR(expr_table_overall);
  GenOverallTableCT(expr_table_overall);
  GenOverallTableDT(expr_table_overall);
  GenOverallMethodAverageTable(expr_table_overall);
}
#endif

#ifdef RUN_PARAM_ABS_MAX_DIFF_EXPERIMENT
TEST(Perf, ParamAbsMaxDiff) {
  ExprTable expr_table_abs_diff;

  for (const auto &data_set : kDataSetList) {
    std::ifstream data_input_stream(kDataSetDirPrefix + data_set);
    if (!data_input_stream.is_open()) {
      std::cerr << "Failed to open the file [" << data_set << "]" << std::endl;
    }

    for (const auto &max_diff : kMaxDiffList) {
      PerfSerfXOR(data_input_stream, max_diff, kBlockSizeParamAbsMaxDiff, data_set, expr_table_abs_diff);
      PerfSerfQt(data_input_stream, max_diff, kBlockSizeParamAbsMaxDiff, data_set, expr_table_abs_diff);
      PerfEchosAbs(data_input_stream, max_diff, kBlockSizeParamAbsMaxDiff, data_set, expr_table_abs_diff);
      PerfSimPiece(data_input_stream, max_diff, kBlockSizeParamAbsMaxDiff, data_set, expr_table_abs_diff);
      PerfSZ2(data_input_stream, max_diff, kBlockSizeParamAbsMaxDiff, data_set, expr_table_abs_diff);
      PerfMachete(data_input_stream, max_diff, kBlockSizeParamAbsMaxDiff, data_set, expr_table_abs_diff);
      PerfSprintz(data_input_stream, max_diff, kBlockSizeParamAbsMaxDiff, data_set, expr_table_abs_diff);
    }
  }

  GenParamAbsDiffTable(expr_table_abs_diff);
}
#endif

#ifdef RUN_PARAM_BLOCK_SIZE_EXPERIMENT
TEST(Perf, ParamBlockSize) {
  ExprTable expr_table_block_size;

  for (const auto &data_set : kDataSetList) {
    std::ifstream data_input_stream(kDataSetDirPrefix + data_set);
    if (!data_input_stream.is_open()) {
      std::cerr << "Failed to open the file [" << data_set << "]" << std::endl;
    }

    for (const auto & block_size : kBlockSizeList) {
      PerfSerfXOR(data_input_stream, kAbsMaxDiffParamBlockSize, block_size, data_set, expr_table_block_size);
      PerfSerfQt(data_input_stream, kAbsMaxDiffParamBlockSize, block_size, data_set, expr_table_block_size);
      PerfEchosAbs(data_input_stream,
                   kAbsMaxDiffParamBlockSize,
                   block_size,
                   data_set,
                   expr_table_block_size);
      PerfSimPiece(data_input_stream, kAbsMaxDiffParamBlockSize, block_size, data_set, expr_table_block_size);
      PerfSZ2(data_input_stream, kAbsMaxDiffParamBlockSize, block_size, data_set, expr_table_block_size);
      PerfMachete(data_input_stream, kAbsMaxDiffParamBlockSize, block_size, data_set, expr_table_block_size);
      PerfSprintz(data_input_stream, kAbsMaxDiffParamBlockSize, block_size, data_set, expr_table_block_size);
      if (block_size >= 600) {
        PerfALP(data_input_stream, kAbsMaxDiffParamBlockSize, block_size, data_set, expr_table_block_size);
      }
    }
  }

  GenParamBlockSizeTable(expr_table_block_size);
}
#endif

#ifdef RUN_REL_OVERALL_EXPERIMENT
TEST(Perf, RelOverall) {
  ExprTable expr_table_rel_overall;

  for (const auto &data_set : kDataSetList) {
    std::ifstream data_input_stream(kDataSetDirPrefix + data_set);
    if (!data_input_stream.is_open()) {
      std::cerr << "Failed to open the file [" << data_set << "]" << std::endl;
      continue;
    }

#ifdef SERF_ENABLE_BASELINE_SERF
    PerfSerfXORRel(data_input_stream,
                   kMaxDiffRelOverall,
                   kBlockSizeRelOverall,
                   data_set,
                   expr_table_rel_overall);
#endif
#ifdef SERF_ENABLE_BASELINE_SZ2
    PerfSZ2Rel(data_input_stream,
               kMaxDiffRelOverall,
               kBlockSizeRelOverall,
               data_set,
               expr_table_rel_overall);
#endif
#ifdef SERF_ENABLE_ECHOS
    PerfEchosRel(data_input_stream,
                 kMaxDiffRelOverall,
                 kBlockSizeRelOverall,
                 data_set,
                 expr_table_rel_overall);
#endif

    data_input_stream.close();
  }

  GenRelOverallTables(expr_table_rel_overall);
  PrintRelOverallMethodAverageTable(expr_table_rel_overall);
}
#endif
#ifdef RUN_REL_EXPERIMENT
TEST(Perf, Rel) {
  ExprTable expr_table_rel;

  for (const auto &data_set : kDataSetList) {
    std::ifstream data_input_stream(kDataSetDirPrefix + data_set);
    if (!data_input_stream.is_open()) {
      std::cerr << "Failed to open the file [" << data_set << "]" << std::endl;
    }

    for (const auto &rel_diff : kMaxDiffRel) {
#ifdef SERF_ENABLE_BASELINE_SERF
      PerfSerfXORRel(data_input_stream, rel_diff, kBlockSizeOverall, data_set, expr_table_rel);
#endif
#ifdef SERF_ENABLE_BASELINE_SZ2
      PerfSZ2Rel(data_input_stream, rel_diff, kBlockSizeOverall, data_set, expr_table_rel);
#endif
#ifdef SERF_ENABLE_ECHOS
      PerfEchosRel(data_input_stream, rel_diff, kBlockSizeRel, data_set, expr_table_rel);
#endif
    }

    data_input_stream.close();
  }

  GenParamRelDiffTableCR(expr_table_rel);
  GenParamRelDiffTableCT(expr_table_rel);
  GenParamRelDiffTableDT(expr_table_rel);
  PrintParamRelDiffSummary(expr_table_rel);
}
#endif

#ifdef RUN_ECHOS_ABLATION_EXPERIMENT
TEST(Perf, EchosAblation) {
  ExprTable abs_results;
  ExprTable rel_results;
  for (const auto &data_set : kDataSetList) {
    std::ifstream input(kDataSetDirPrefix + data_set);
    if (!input.is_open()) {
      throw std::runtime_error("Failed to open " + data_set);
    }
    PerfEchosAbsAblation(
        input, kAbsMaxDiffEchosAblation, kBlockSizeEchosAblation, data_set,
        "ECHOS", EchosAbsMode::kAdaptive, abs_results);
    PerfEchosAbsAblation(
        input, kAbsMaxDiffEchosAblation, kBlockSizeEchosAblation, data_set,
        "Batch-Oracle", EchosAbsMode::kBatchOracle, abs_results);
    PerfEchosAbsAblation(
        input, kAbsMaxDiffEchosAblation, kBlockSizeEchosAblation, data_set,
        "Pointwise-Oracle+Meta", EchosAbsMode::kPointwiseOracle, abs_results);
    PerfEchosAbsAblation(
        input, kAbsMaxDiffEchosAblation, kBlockSizeEchosAblation, data_set,
        "Full-History", EchosAbsMode::kFullHistory, abs_results);
    PerfEchosAbsAblation(
        input, kAbsMaxDiffEchosAblation, kBlockSizeEchosAblation, data_set,
        "Sliding-Window", EchosAbsMode::kSlidingWindow, abs_results);

    PerfEchosRelAblation(
        input, kRelMaxDiffEchosAblation, kBlockSizeEchosAblation, data_set,
        "ECHOS", false, rel_results);
    PerfEchosRelAblation(
        input, kRelMaxDiffEchosAblation, kBlockSizeEchosAblation, data_set,
        "Explicit Flags", true, rel_results);
  }
  ExportEchosAblationResults(
      abs_results, kMethodListEchosAbsAblation, kBlockSizeEchosAblation,
      kAbsMaxDiffEchosAblation, "echos_ablation_abs");
  ExportEchosAblationResults(
      rel_results, kMethodListEchosRelAblation, kBlockSizeEchosAblation,
      kRelMaxDiffEchosAblation, "echos_ablation_rel");
}
#endif

#ifdef RUN_SINGLE_PRECISION_EXPERIMENT
TEST(Perf, SinglePrecision) {
  ExprTable expr_table_32;

  for (const auto &data_set : kDataSetList32) {
    std::ifstream data_set_input_stream(kDataSetDirPrefix + data_set);
    if (!data_set_input_stream.is_open()) {
      std::cerr << "Failed to open the file [" << data_set << "]" << std::endl;
    }

    // Lossy
#ifdef SERF_ENABLE_BASELINE_SERF
    PerfSerfXOR_32(data_set_input_stream, kMaxDiff32, kBlockSize32, data_set, expr_table_32);
    PerfSerfQt_32(data_set_input_stream, kMaxDiff32, kBlockSize32, data_set, expr_table_32);
#endif
#ifdef SERF_ENABLE_ECHOS
    PerfEchosAbs_32(data_set_input_stream, kMaxDiff32, kBlockSize32, data_set, expr_table_32);
#endif
#ifdef SERF_ENABLE_BASELINE_SZ2
    PerfSZ2_32(data_set_input_stream, kMaxDiff32, kBlockSize32, data_set, expr_table_32);
#endif

    // Lossless
#ifdef SERF_ENABLE_BASELINE_CHIMP128
    PerfChimp128_32(data_set_input_stream, kMaxDiff32, kBlockSize32, data_set, expr_table_32);
#endif
#ifdef SERF_ENABLE_BASELINE_DEFLATE
    PerfDeflate_32(data_set_input_stream, kMaxDiff32, kBlockSize32, data_set, expr_table_32);
#endif
#ifdef SERF_ENABLE_BASELINE_ELF
    PerfElf_32(data_set_input_stream, kMaxDiff32, kBlockSize32, data_set, expr_table_32);
#endif
#ifdef SERF_ENABLE_BASELINE_LZ4
    PerfLZ4_32(data_set_input_stream, kMaxDiff32, kBlockSize32, data_set, expr_table_32);
#endif
#ifdef SERF_ENABLE_BASELINE_LZ77
    PerfLZ77_32(data_set_input_stream, kMaxDiff32, kBlockSize32, data_set, expr_table_32);
#endif
#ifdef SERF_ENABLE_BASELINE_ZSTD
    PerfZstd_32(data_set_input_stream, kMaxDiff32, kBlockSize32, data_set, expr_table_32);
#endif
#ifdef SERF_ENABLE_BASELINE_SNAPPY
    PerfSnappy_32(data_set_input_stream, kMaxDiff32, kBlockSize32, data_set, expr_table_32);
#endif

    data_set_input_stream.close();
  }

  GenSinglePrecisionTableCR(expr_table_32);
  GenSinglePrecisionTableCT(expr_table_32);
  GenSinglePrecisionTableDT(expr_table_32);
}
#endif

#ifdef RUN_SERF_ABLATION_EXPERIMENT
TEST(Perf, Serf_Ablation) {
  ExprTable expr_table_ablation;

  for (const auto &data_set : kDataSetList) {
    std::ifstream data_set_input_stream(kDataSetDirPrefix + data_set);
    if (!data_set_input_stream.is_open()) {
      std::cerr << "Failed to open the file [" << data_set << "]" << std::endl;
    }

    PerfSerfXOR(data_set_input_stream, kMaxDiffAblation, kBlockSizeAblation, data_set, expr_table_ablation);
    PerfSerfXOR_Without_Shifter(data_set_input_stream,
                                kMaxDiffAblation,
                                kBlockSizeAblation,
                                data_set,
                                expr_table_ablation);
    PerfSerfXOR_Without_OptAppr(data_set_input_stream,
                                kMaxDiffAblation,
                                kBlockSizeAblation,
                                data_set,
                                expr_table_ablation);
    PerfSerfXOR_Without_FastSearch(data_set_input_stream,
                                   kMaxDiffAblation,
                                   kBlockSizeAblation,
                                   data_set,
                                   expr_table_ablation);

    data_set_input_stream.close();
  }

  GenAblationTableCR(expr_table_ablation);
  GenAblationTableCT(expr_table_ablation);
  GenAblationTableDT(expr_table_ablation);
}
#endif

#ifdef RUN_LAMBDA_EXPERIMENT
TEST(Perf, Lambda) {
  std::ofstream result_output(kExportExprTablePrefix + "lambda_ct" + kExportExprTableSuffix);
  if (!result_output.is_open()) std::cout << "Failed to creat perf result file." << std::endl;

  for (const auto &factor : kLambdaFactorList) {
    result_output << factor << ",";
    for (const auto &data_set : kDataSetList) {
      std::ifstream data_set_input_stream(kDataSetDirPrefix + data_set);
      if (!data_set_input_stream.is_open()) {
        std::cerr << "Failed to open the file [" << data_set << "]" << std::endl;
      }
      int lambda_for_this_data_set = kFileNameToAdjustDigit.find(data_set)->second;
      ExprTable expr_table_lambda;
      ExprConf this_conf = ExprConf("SerfXOR", data_set, kBlockSizeOverall, kMaxDiffOverall);
      int test_lambda = static_cast<int>((factor * lambda_for_this_data_set));
      PerfSerfXORLambda(data_set_input_stream, kMaxDiffOverall, kBlockSizeOverall, data_set, test_lambda,
                           expr_table_lambda);
      result_output << expr_table_lambda.find(this_conf)->second.AvgCompressionTimePerBlock() << ",";
      data_set_input_stream.close();
    }
    result_output << std::endl;
  }
}
#endif

#ifdef RUN_BETA_EXPERIMENT
TEST(Perf, Beta) {
  const static std::string chosen_data_set = "Motor-temp.csv";
  const static int min_beta = 1;
  const static int max_beta = 15;

  std::ofstream result_output(kExportExprTablePrefix + "beta_cr" + kExportExprTableSuffix);
  if (!result_output.is_open()) std::cout << "Failed to creat perf result file." << std::endl;

  std::ifstream data_set_input_stream(kDataSetDirPrefix + chosen_data_set);
  if (!data_set_input_stream.is_open()) {
    std::cerr << "Failed to open the file [" << chosen_data_set << "]" << std::endl;
  }

  for (int beta = min_beta; beta <= max_beta; beta++) {
    ExprTable expr_table_beta;
#ifdef SERF_ENABLE_BASELINE_SERF
    PerfSerfXORBeta(data_set_input_stream, chosen_data_set, kMaxDiffOverall, kBlockSizeOverall, beta, expr_table_beta);
    PerfSerfQtBeta(data_set_input_stream, chosen_data_set, kMaxDiffOverall, kBlockSizeOverall, beta, expr_table_beta);
#endif
#ifdef SERF_ENABLE_ECHOS
    PerfEchosAbsBeta(data_set_input_stream,
                     chosen_data_set,
                     kMaxDiffOverall,
                     kBlockSizeOverall,
                     beta,
                     expr_table_beta);
#endif
#ifdef SERF_ENABLE_BASELINE_ELF
    PerfElfBeta(data_set_input_stream, chosen_data_set, kMaxDiffOverall, kBlockSizeOverall, beta, expr_table_beta);
#endif
    ExprConf serf_xor_conf = ExprConf("SerfXOR", chosen_data_set, kBlockSizeOverall, kMaxDiffOverall);
    ExprConf serf_qt_conf = ExprConf("SerfQt", chosen_data_set, kBlockSizeOverall, kMaxDiffOverall);
    ExprConf echos_conf = ExprConf("ECHOS", chosen_data_set, kBlockSizeOverall, kMaxDiffOverall);
#ifdef SERF_ENABLE_BASELINE_ELF
    ExprConf elf_conf = ExprConf("Elf", chosen_data_set, kBlockSizeOverall, kMaxDiffOverall);
#endif
    result_output << beta << "," << "SerfXOR,"
                  << expr_table_beta.find(serf_xor_conf)->second.CalCompressionRatio(serf_xor_conf)
                  << std::endl;
    result_output << beta << "," << "SerfQt,"
                  << expr_table_beta.find(serf_qt_conf)->second.CalCompressionRatio(serf_qt_conf)
                  << std::endl;
    result_output << beta << "," << "ECHOS,"
                  << expr_table_beta.find(echos_conf)->second.CalCompressionRatio(echos_conf)
                  << std::endl;
#ifdef SERF_ENABLE_BASELINE_ELF
    result_output << beta << "," << "Elf,"
                  << expr_table_beta.find(elf_conf)->second.CalCompressionRatio(elf_conf)
                  << std::endl;
#endif
  }
}
#endif

#ifdef RUN_TSBS_EXPERIMENT
TEST(Perf, TSBS) {
  ExprTable expr_table_tsbs;

  for (const auto &data_set : kDataSetListTSBS) {
    std::ifstream data_input_stream(kDataSetDirPrefix + data_set);
    if (!data_input_stream.is_open()) {
      std::cerr << "Failed to open the file [" << data_set << "]" << std::endl;
    }

    // Lossy Compression
    PerfSerfXOR(data_input_stream, kMaxDiffTSBS, kBlockSizeTSBS, data_set, expr_table_tsbs);
    PerfSerfQt(data_input_stream, kMaxDiffTSBS, kBlockSizeTSBS, data_set, expr_table_tsbs);
    PerfEchosAbs(data_input_stream, kMaxDiffTSBS, kBlockSizeTSBS, data_set, expr_table_tsbs);
    PerfMachete(data_input_stream, kMaxDiffTSBS, kBlockSizeTSBS, data_set, expr_table_tsbs);
    PerfSZ2(data_input_stream, kMaxDiffTSBS, kBlockSizeTSBS, data_set, expr_table_tsbs);
    PerfSimPiece(data_input_stream, kMaxDiffTSBS, kBlockSizeTSBS, data_set, expr_table_tsbs);
    PerfSprintz(data_input_stream, kMaxDiffTSBS, kBlockSizeTSBS, data_set, expr_table_tsbs);

    // Lossless Compression
    PerfGorilla(data_input_stream, kMaxDiffTSBS, kBlockSizeTSBS, data_set, expr_table_tsbs);
    PerfChimp128(data_input_stream, kMaxDiffTSBS, kBlockSizeTSBS, data_set, expr_table_tsbs);
    PerfDeflate(data_input_stream, kMaxDiffTSBS, kBlockSizeTSBS, data_set, expr_table_tsbs);
    PerfElf(data_input_stream, kMaxDiffTSBS, kBlockSizeTSBS, data_set, expr_table_tsbs);
    PerfFPC(data_input_stream, kMaxDiffTSBS, kBlockSizeTSBS, data_set, expr_table_tsbs);
    PerfLZ4(data_input_stream, kMaxDiffTSBS, kBlockSizeTSBS, data_set, expr_table_tsbs);
    PerfSerfXOR(data_input_stream, kMaxDiffTSBS, kBlockSizeTSBS, data_set, expr_table_tsbs);
    PerfLZ77(data_input_stream, kMaxDiffTSBS, kBlockSizeTSBS, data_set, expr_table_tsbs);
    PerfZstd(data_input_stream, kMaxDiffTSBS, kBlockSizeTSBS, data_set, expr_table_tsbs);
    PerfSnappy(data_input_stream, kMaxDiffTSBS, kBlockSizeTSBS, data_set, expr_table_tsbs);

    data_input_stream.close();
  }

  GenTSBSTableCR(expr_table_tsbs);
  GenTSBSTableCT(expr_table_tsbs);
  GenTSBSTableDT(expr_table_tsbs);
}
#endif

