#ifndef SERF_INPUT_BIT_STREAM_H
#define SERF_INPUT_BIT_STREAM_H

#include <cstdlib>
#include <cstring>
#include <vector>
#include <cmath>
#include <memory>
#include <cstdint>

#include "array.h"
#include "endian_compat.h"

class InputBitStream {
 public:
  InputBitStream() = default;

  InputBitStream(uint8_t *raw_data, size_t size);

  uint64_t ReadLong(size_t len);

  uint32_t ReadInt(size_t len);

  uint32_t ReadBit() {
    const uint32_t result = static_cast<uint32_t>(buffer_ >> 63);
    --bit_in_buffer_;
    buffer_ <<= 1;
    if (bit_in_buffer_ < 32 && cursor_ < data_.length()) {
      buffer_ |= static_cast<uint64_t>(data_[cursor_++]) << (32 - bit_in_buffer_);
      bit_in_buffer_ += 32;
    }
    return result;
  }

  uint64_t ReadUnaryZeros(uint64_t max_zeros);

  void SetBuffer(const Array<uint8_t> &new_buffer);

  void SetBuffer(const std::vector<uint8_t> &new_buffer);

 private:
  void Forward(size_t len);
  uint64_t Peek(size_t len);

  Array<uint32_t> data_;
  uint64_t buffer_ = 0;
  uint64_t cursor_ = 0;
  uint64_t bit_in_buffer_ = 0;
};

#endif  // SERF_INPUT_BIT_STREAM_H
