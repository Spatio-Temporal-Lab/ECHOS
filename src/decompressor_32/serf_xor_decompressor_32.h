#ifndef SERF_XOR_DECOMPRESSOR_32_H
#define SERF_XOR_DECOMPRESSOR_32_H

/*
 * Give hints to the compiler for branch prediction optimization.
 */
#if defined(__clang__) || (defined(__GNUC__) && (__GNUC__ > 2))
#define SERF_LIKELY(c) (__builtin_expect(!!(c), 1))
#define SERF_UNLIKELY(c) (__builtin_expect(!!(c), 0))
#else
#define SERF_LIKELY(c) (c)
#define SERF_UNLIKELY(c) (c)
#endif

#include <limits>
#include <memory>
#include <vector>

#include "utils/serf_utils_32.h"
#include "utils/post_office_solver_32.h"
#include "utils/float.h"
#include "utils/input_bit_stream.h"
#include "utils/serf_utils_32.h"

class SerfXORDecompressor32 {
 public:
  SerfXORDecompressor32() = default;

  std::vector<float> Decompress(const Array<uint8_t> &bs);

 private:
  uint32_t stored_val_ = Float::FloatToIntBits(2);
  int stored_leading_zeros_ = std::numeric_limits<int>::max();
  int stored_trailing_zeros_ = std::numeric_limits<int>::max();
  std::unique_ptr<InputBitStream> input_bit_stream_ = std::make_unique<InputBitStream>();
  Array<int> leading_representation_ = {0, 8, 12, 16};
  Array<int> trailing_representation_ = {0, 16};
  int leading_bits_per_value_ = 2;
  int trailing_bits_per_value_ = 1;
  int look_up_table[16] = {16, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

  uint32_t ReadValue();
  void UpdatePositionsIfNeeded();
  void UpdateLeadingRepresentation();
  void UpdateTrailingRepresentation();
};

#endif  // SERF_XOR_DECOMPRESSOR_32_H
