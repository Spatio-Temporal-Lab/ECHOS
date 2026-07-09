#include "elias_delta_codec.h"

int EliasDeltaCodec::Encode(int64_t number, OutputBitStream *output_bit_stream_ptr) {
  int compressed_size_in_bits = 0;
  int n;
  if (number <= 16) {
    n = std::floor(kLog2Table[number]) + 1;
  } else {
    n = std::floor(std::log2(number)) + 1;
  }
  compressed_size_in_bits += EliasGammaCodec::Encode(n, output_bit_stream_ptr);
  if (n > 1) {
    compressed_size_in_bits += output_bit_stream_ptr->WriteLong(number, n - 1);
  }
  return compressed_size_in_bits;
}

int64_t EliasDeltaCodec::Decode(InputBitStream *input_bit_stream_ptr) {
  int n = static_cast<int>(EliasGammaCodec::Decode(input_bit_stream_ptr));
  if (n == 1) {
    return 1;
  }
  return (1LL << (n - 1)) | input_bit_stream_ptr->ReadLong(n - 1);
}
