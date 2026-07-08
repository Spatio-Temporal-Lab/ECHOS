#include "compressor/echos_abs_compressor.h"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "utils/double.h"

namespace {

constexpr uint64_t kEscape = std::numeric_limits<uint64_t>::max();

bool Quantize(double value, double prediction, double max_diff, double quantization_step,
              double inverse_quantization_step, int64_t *q, double *recovered) {
  if (!std::isfinite(value) || !std::isfinite(prediction)) return false;
  const double scaled = (value - prediction) * inverse_quantization_step;
  if (std::isfinite(scaled) && std::abs(scaled) <= 0x1p52) {
    *q = static_cast<int64_t>(std::round(scaled));
    if (AdaptiveQtCodec::ZigZagEncode(*q) < std::numeric_limits<uint64_t>::max() - 1) {
      *recovered = prediction + quantization_step * static_cast<double>(*q);
      if (std::isfinite(*recovered) && std::abs(value - *recovered) <= max_diff) return true;
    }
  }

  const long double step = 2.0L * static_cast<long double>(max_diff);
  const long double precise_scaled =
      (static_cast<long double>(value) - static_cast<long double>(prediction)) / step;
  if (!std::isfinite(precise_scaled)) return false;
  const long double rounded = std::round(precise_scaled);
  if (rounded <= static_cast<long double>(std::numeric_limits<int64_t>::min()) ||
      rounded > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
    return false;
  }
  *q = static_cast<int64_t>(rounded);
  if (AdaptiveQtCodec::ZigZagEncode(*q) >= std::numeric_limits<uint64_t>::max() - 1) return false;
  *recovered = prediction + quantization_step * static_cast<double>(*q);
  return std::isfinite(*recovered) && std::abs(value - *recovered) <= max_diff;
}

}  // namespace

EchosAbsCompressor::EchosAbsCompressor(int block_size, double max_diff)
    : block_size_(block_size),
      max_diff_(max_diff * 0.999),
      quantization_step_(2 * max_diff_),
      inverse_quantization_step_(1.0 / quantization_step_) {
  if (block_size <= 0 || block_size > 65535) throw std::invalid_argument("Invalid block size");
  if (!std::isfinite(max_diff) || max_diff <= 0) throw std::invalid_argument("Invalid error bound");
  output_ = std::make_unique<OutputBitStream>(static_cast<uint32_t>(24 * block_size + 32));
}

void EchosAbsCompressor::SetBlockConfig(int block_size, double max_diff) {
  if (value_count_ != 0) throw std::runtime_error("Cannot change block config inside a block");
  if (block_size <= 0 || block_size > 65535) throw std::invalid_argument("Invalid block size");
  if (!std::isfinite(max_diff) || max_diff <= 0) throw std::invalid_argument("Invalid error bound");
  block_size_ = block_size;
  max_diff_ = max_diff * 0.999;
  quantization_step_ = 2 * max_diff_;
  inverse_quantization_step_ = 1.0 / quantization_step_;
  output_ = std::make_unique<OutputBitStream>(static_cast<uint32_t>(24 * block_size + 32));
}

void EchosAbsCompressor::WriteMetadata() {
  const uint64_t max_diff_bits = Double::DoubleToLongBits(max_diff_);
  const bool block_size_changed = !metadata_initialized_ || block_size_ != previous_block_size_;
  const bool max_diff_changed = !metadata_initialized_ || max_diff_bits != previous_max_diff_bits_;
  compressed_size_in_bits_ += output_->WriteBit(block_size_changed);
  compressed_size_in_bits_ += output_->WriteBit(max_diff_changed);
  if (block_size_changed) compressed_size_in_bits_ += output_->WriteInt(block_size_, 16);
  if (max_diff_changed) compressed_size_in_bits_ += output_->WriteLong(max_diff_bits, 64);
  previous_block_size_ = block_size_;
  previous_max_diff_bits_ = max_diff_bits;
  metadata_initialized_ = true;
}

void EchosAbsCompressor::UpdatePrediction(double recovered) {
  previous_ = std::isfinite(recovered) ? recovered : 2;
}

void EchosAbsCompressor::AddValue(double value) {
  if (value_count_ >= block_size_) {
    throw std::runtime_error("ECHOS absolute block is full");
  }
  if (value_count_ == 0) WriteMetadata();

  const AdaptiveQtCodec::AdaptiveRiceChoice choice =
      AdaptiveQtCodec::SelectAdaptiveDeltaRiceCodec(adaptive_state_);
  int64_t q = 0;
  double recovered = 0;
  if (Quantize(value, previous_, max_diff_, quantization_step_, inverse_quantization_step_, &q,
               &recovered)) {
    const uint64_t mapped = AdaptiveQtCodec::ZigZagEncode(q) + 1;
    const AdaptiveQtCodec::AdaptiveDeltaRiceCodeLengths lengths =
        AdaptiveQtCodec::CalculateCappedDeltaRiceWithRawCodeLengths(
            mapped, choice.rice_parameter, AdaptiveQtCodec::kBoundedRiceQuotientCap);
    if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
      compressed_size_in_bits_ += AdaptiveQtCodec::EncodeCappedRiceWithRaw(
          mapped, choice.rice_parameter, AdaptiveQtCodec::kBoundedRiceQuotientCap, output_.get());
    } else {
      compressed_size_in_bits_ +=
          AdaptiveQtCodec::EncodeMapped(mapped, choice.codec, choice.rice_parameter, output_.get());
    }
    AdaptiveQtCodec::UpdateAdaptiveDeltaRiceState(mapped, lengths, &adaptive_state_);
    UpdatePrediction(recovered);
  } else {
    if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
      compressed_size_in_bits_ +=
          AdaptiveQtCodec::WriteCappedRiceRaw(AdaptiveQtCodec::kBoundedRiceQuotientCap,
                                              output_.get());
    } else {
      compressed_size_in_bits_ +=
          AdaptiveQtCodec::EncodeMapped(kEscape, choice.codec, choice.rice_parameter, output_.get());
    }
    compressed_size_in_bits_ += output_->WriteLong(Double::DoubleToLongBits(value), 64);
    UpdatePrediction(value);
  }
  ++value_count_;
}

void EchosAbsCompressor::Close() {
  if (value_count_ != block_size_) {
    throw std::runtime_error("ECHOS absolute block is incomplete");
  }
  output_->Flush();
  compressed_bytes_ =
      output_->GetBuffer(static_cast<uint32_t>(std::ceil(compressed_size_in_bits_ / 8.0)));
  output_->Refresh();
  stored_compressed_size_in_bits_ = compressed_size_in_bits_;
  compressed_size_in_bits_ = 0;
  value_count_ = 0;
}

Array<uint8_t> EchosAbsCompressor::compressed_bytes() const {
  return compressed_bytes_;
}

long EchosAbsCompressor::get_compressed_size_in_bits() const {
  return stored_compressed_size_in_bits_;
}
