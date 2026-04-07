#include "compressor_32/serf_xor_compressor_32.h"

SerfXORCompressor32::SerfXORCompressor32(int window_size, float max_diff)
    : kWindowSize(window_size), kMaxDiff(max_diff) {
  output_buffer_ = std::make_unique<OutputBitStream>(std::floor(((window_size + 1) * 4 + window_size / 4 + 1) * 1.2));
  compressed_size_this_block_ = output_buffer_->WriteInt(0, 1);
}

void SerfXORCompressor32::AddValue(float v) {
  uint32_t this_val;
  // note we cannot let > maxDiff, because kNan - v > maxDiff is always false
  if (SERF_LIKELY(std::abs(Float::IntBitsToFloat(stored_val_) - v) > kMaxDiff)) {
    // in our implementation, we do not consider special cases and overflow case
    this_val = SerfUtils32::FindAppInt(v - kMaxDiff, v + kMaxDiff, v, stored_val_, kMaxDiff);
  } else {
    // let current value be the last value, making an XORed value of 0.
    this_val = stored_val_;
  }

  compressed_size_this_block_ += CompressValue(this_val);
  stored_val_ = this_val;
  ++number_of_values_this_window_;
}

long SerfXORCompressor32::compressed_size_last_block() const {
  return compressed_size_last_block_;
}

Array<uint8_t> SerfXORCompressor32::compressed_bytes_last_block() {
  return compressed_bytes_last_block_;
}

void SerfXORCompressor32::Close() {
  compressed_size_this_block_ += CompressValue(Float::FloatToIntBits(Float::kNan));
  output_buffer_->Flush();
  compressed_bytes_last_block_ = output_buffer_->GetBuffer(std::ceil((double) compressed_size_this_block_ / 8.0));
  output_buffer_->Refresh();
  compressed_size_last_block_ = compressed_size_this_block_;
  compressed_size_this_block_ = UpdatePositionsIfNeeded();
}

int SerfXORCompressor32::CompressValue(uint32_t value) {
  int this_size = 0;
  uint32_t xor_result = stored_val_ ^ value;

  if (SERF_UNLIKELY(xor_result == 0)) {
    this_size += static_cast<int>(output_buffer_->WriteInt(1, 2));
  } else {
    int leading_count = __builtin_clz(xor_result);
    int trailing_count = __builtin_ctz(xor_result);
    int leading_zeros = leading_round_[leading_count];
    int trailing_zeros = trailing_round_[trailing_count];
    ++lead_distribution_[leading_count];
    ++trail_distribution_[trailing_count];

    if (SERF_UNLIKELY(leading_zeros >= stored_leading_zeros_ && trailing_zeros >= stored_trailing_zeros_ &&
        (leading_zeros - stored_leading_zeros_) + (trailing_zeros - stored_trailing_zeros_)
            < 1 + leading_bits_per_value_ + trailing_bits_per_value_)) {
      // case 1
      int center_bits = 32 - stored_leading_zeros_ - stored_trailing_zeros_;
      int len = 1 + center_bits;
      if (SERF_UNLIKELY(len > 32)) {
        output_buffer_->WriteInt(1, 1);
        output_buffer_->WriteInt(xor_result >> stored_trailing_zeros_, center_bits);
      } else {
        output_buffer_->WriteInt((1 << center_bits) | (xor_result >> stored_trailing_zeros_), 1 + center_bits);
      }
      this_size += len;
    } else {
      stored_leading_zeros_ = leading_zeros;
      stored_trailing_zeros_ = trailing_zeros;
      int center_bits = 32 - stored_leading_zeros_ - stored_trailing_zeros_;

      // case 00
      int len = 2 + leading_bits_per_value_ + trailing_bits_per_value_ + center_bits;
      if (SERF_UNLIKELY(len > 32)) {
        output_buffer_->WriteInt((leading_representation_[stored_leading_zeros_]
                                         << trailing_bits_per_value_)
                                         | trailing_representation_[stored_trailing_zeros_],
                                     2 + leading_bits_per_value_ + trailing_bits_per_value_);
        output_buffer_->WriteInt(xor_result >> stored_trailing_zeros_, center_bits);
      } else {
        output_buffer_->WriteInt((((leading_representation_[stored_leading_zeros_]
            << trailing_bits_per_value_) | trailing_representation_[stored_trailing_zeros_])
            << center_bits) | (xor_result >> stored_trailing_zeros_), len);
      }
      this_size += len;
    }
  }
  return this_size;
}

int SerfXORCompressor32::UpdatePositionsIfNeeded() {
  int len;
  if (SERF_LIKELY(number_of_values_this_window_ < kWindowSize)) {
    // Only Check if update flag
    compressed_size_this_window_ += compressed_size_last_block_;
    len = output_buffer_->WriteInt(0, 1);
  } else {
    double
        compression_ratio_this_window_ = (double) compressed_size_this_window_ / (number_of_values_this_window_ * 32);
    if (SERF_UNLIKELY(compression_ratio_last_window_ < compression_ratio_this_window_)) {
      // update positions
      Array<int> lead_positions = PostOfficeSolver32::InitRoundAndRepresentation(lead_distribution_,
                                                                                 leading_representation_,
                                                                                 leading_round_);
      leading_bits_per_value_ = PostOfficeSolver32::kPositionLength2Bits[lead_positions.length()];
      Array<int> trail_positions = PostOfficeSolver32::InitRoundAndRepresentation(trail_distribution_,
                                                                                  trailing_representation_,
                                                                                  trailing_round_);
      trailing_bits_per_value_ = PostOfficeSolver32::kPositionLength2Bits[trail_positions.length()];
      len = output_buffer_->WriteInt(1, 1)
          + PostOfficeSolver32::WritePositions(lead_positions, output_buffer_.get())
          + PostOfficeSolver32::WritePositions(trail_positions, output_buffer_.get());
    } else {
      len = output_buffer_->WriteInt(0, 1);
    }
    compression_ratio_last_window_ = compression_ratio_this_window_;
    __builtin_memset(lead_distribution_.begin(), 0, 32 * sizeof(int));
    __builtin_memset(trail_distribution_.begin(), 0, 32 * sizeof(int));
    compressed_size_this_window_ = 0;
    number_of_values_this_window_ = 0;
  }
  return len;
}
