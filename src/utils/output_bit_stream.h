#ifndef SERF_OUTPUT_BIT_STREAM_H
#define SERF_OUTPUT_BIT_STREAM_H

#include <cstdint>

#include "array.h"
#include "endian_compat.h"

class OutputBitStream {
 public:
  explicit OutputBitStream(uint32_t buffer_size);

  uint32_t Write(uint64_t content, uint32_t len);

  uint32_t WriteLong(uint64_t content, uint64_t len);

  uint32_t WriteInt(uint32_t content, uint32_t len);

  uint32_t WriteBit(bool bit) {
    buffer_ |= static_cast<uint64_t>(bit) << (63 - bit_in_buffer_);
    if (++bit_in_buffer_ >= 32) {
      data_[cursor_++] = static_cast<uint32_t>(buffer_ >> 32);
      buffer_ <<= 32;
      bit_in_buffer_ -= 32;
    }
    return 1;
  }

  void Flush();

  Array<uint8_t> GetBuffer(uint32_t len);

  void Refresh();

 private:
  Array<uint32_t> data_;
  uint32_t cursor_;
  uint32_t bit_in_buffer_;
  uint64_t buffer_;
};

#endif  // SERF_OUTPUT_BIT_STREAM_H
