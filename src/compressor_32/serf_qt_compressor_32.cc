#include "compressor_32/serf_qt_compressor_32.h"

SerfQtCompressor32::SerfQtCompressor32(int block_size, float max_diff): kBlockSize(block_size),
kMaxDiff(max_diff * 0.99f) {
  output_bit_stream_ = std::make_unique<OutputBitStream>(2 * kBlockSize * 8);
}

void SerfQtCompressor32::AddValue(float v) {
  if (first_) {
    first_ = false;
    compressed_size_in_bits_ += output_bit_stream_->WriteInt(kBlockSize, 16);
    compressed_size_in_bits_ += output_bit_stream_->WriteInt(Float::FloatToIntBits(kMaxDiff), 32);
  }
  long q = static_cast<long>(std::round((v - pre_value_) / (2 * kMaxDiff)));
  float recover_value = pre_value_ + 2 * kMaxDiff * static_cast<float>(q);
  compressed_size_in_bits_ += EliasGammaCodec::Encode(ZigZagCodec::Encode(static_cast<int64_t>(q)) + 1,
                                                      output_bit_stream_.get());
  pre_value_ = recover_value;
}

Array<uint8_t> SerfQtCompressor32::compressed_bytes() {
  return compressed_bytes_;
}

void SerfQtCompressor32::Close() {
  output_bit_stream_->Flush();
  compressed_bytes_ = output_bit_stream_->GetBuffer(std::ceil(compressed_size_in_bits_ / 8.0));
  output_bit_stream_->Refresh();
  first_ = true;
  pre_value_ = 2;
  stored_compressed_size_in_bits_ = compressed_size_in_bits_;
  compressed_size_in_bits_ = 0;
}

long SerfQtCompressor32::stored_compressed_size_in_bits() const {
    return stored_compressed_size_in_bits_;
}