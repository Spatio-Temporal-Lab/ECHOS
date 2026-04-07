#include "elias_gamma_codec.h"

int EliasGammaCodec::Encode(int64_t number, OutputBitStream *output_bit_stream_ptr) {
  int compressed_size_in_bits = 0;
  int n;
  if (number <= 16) {
    n = std::floor(kLog2Table[number]);
  } else {
    n = std::floor(std::log2(number));
  }
  compressed_size_in_bits += output_bit_stream_ptr->WriteInt(0, n);
  compressed_size_in_bits += output_bit_stream_ptr->WriteInt(number, n + 1);
  return compressed_size_in_bits;
}

int64_t EliasGammaCodec::Decode(InputBitStream *input_bit_stream_ptr) {
  int n = 0;
  while (!input_bit_stream_ptr->ReadBit()) n++;
  return n == 0 ? 1 :  (1 << n) | input_bit_stream_ptr->ReadInt(n);
}
