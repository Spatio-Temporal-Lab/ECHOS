#ifndef ECHOS_ABLATION_MODE_H_
#define ECHOS_ABLATION_MODE_H_

#include <cstddef>

enum class EchosAbsMode {
  kAdaptive,
  kBatchOracle,
  kPointwiseOracle,
  kFullHistory,
  kSlidingWindow
};

constexpr std::size_t kDefaultEchosSlidingWindow = 8;

#endif  // ECHOS_ABLATION_MODE_H_
