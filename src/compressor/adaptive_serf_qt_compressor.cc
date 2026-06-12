#include "compressor/adaptive_serf_qt_compressor.h"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "utils/double.h"

namespace {

constexpr uint64_t kEscape = std::numeric_limits<uint64_t>::max();

bool Quantize(double value, double prediction, double max_diff, int64_t *q, double *recovered) {
  if (!std::isfinite(value) || !std::isfinite(prediction)) return false;
  const long double step = 2.0L * static_cast<long double>(max_diff);
  const long double scaled =
      (static_cast<long double>(value) - static_cast<long double>(prediction)) / step;
  if (!std::isfinite(scaled)) return false;

  const long double rounded = std::round(scaled);
  if (rounded <= static_cast<long double>(std::numeric_limits<int64_t>::min()) ||
      rounded > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
    return false;
  }
  *q = static_cast<int64_t>(rounded);
  if (AdaptiveQtCodec::ZigZagEncode(*q) >= std::numeric_limits<uint64_t>::max() - 1) return false;

  *recovered = prediction + 2 * max_diff * static_cast<double>(*q);
  return std::isfinite(*recovered) && std::abs(value - *recovered) <= max_diff;
}

}  // namespace

AdaptiveSerfQtCompressor::AdaptiveSerfQtCompressor(int block_size, double max_diff)
    : kBlockSize(block_size), kMaxDiff(max_diff * 0.999) {
  if (block_size <= 0 || block_size > 65535) throw std::invalid_argument("Invalid block size");
  if (!std::isfinite(max_diff) || max_diff <= 0) throw std::invalid_argument("Invalid error bound");
  output_ = std::make_unique<OutputBitStream>(static_cast<uint32_t>(24 * block_size + 32));
}

AdaptiveQtCodec::IntegerCodec AdaptiveSerfQtCompressor::CurrentCodec() const {
  return gamma_cost_ < delta_cost_ ? AdaptiveQtCodec::IntegerCodec::kGamma
                                   : AdaptiveQtCodec::IntegerCodec::kDelta;
}

void AdaptiveSerfQtCompressor::UpdateState(double recovered, uint64_t mapped, bool raw) {
  if (std::isfinite(recovered)) {
    previous_ = recovered;
  } else {
    previous_ = 2;
  }
  if (!raw) {
    gamma_cost_ = AdaptiveQtCodec::SaturatingAdd(gamma_cost_, AdaptiveQtCodec::GammaLength(mapped));
    delta_cost_ = AdaptiveQtCodec::SaturatingAdd(delta_cost_, AdaptiveQtCodec::DeltaLength(mapped));
  }
}

void AdaptiveSerfQtCompressor::AddValue(double value) {
  if (value_count_ >= kBlockSize) throw std::runtime_error("Adaptive Serf-QT block is full");
  if (value_count_ == 0) {
    compressed_size_in_bits_ += output_->WriteInt(kBlockSize, 16);
    compressed_size_in_bits_ += output_->WriteLong(Double::DoubleToLongBits(kMaxDiff), 64);
  }

  const AdaptiveQtCodec::IntegerCodec codec = CurrentCodec();
  int64_t q = 0;
  double recovered = 0;
  if (Quantize(value, previous_, kMaxDiff, &q, &recovered)) {
    const uint64_t mapped = AdaptiveQtCodec::ZigZagEncode(q) + 1;
    compressed_size_in_bits_ += AdaptiveQtCodec::EncodeMapped(mapped, codec, 0, output_.get());
    UpdateState(recovered, mapped, false);
  } else {
    compressed_size_in_bits_ += AdaptiveQtCodec::EncodeMapped(kEscape, codec, 0, output_.get());
    compressed_size_in_bits_ += output_->WriteLong(Double::DoubleToLongBits(value), 64);
    UpdateState(value, 1, true);
  }
  ++value_count_;
}

void AdaptiveSerfQtCompressor::Close() {
  if (value_count_ != kBlockSize) throw std::runtime_error("Adaptive Serf-QT block is incomplete");
  output_->Flush();
  compressed_bytes_ = output_->GetBuffer(static_cast<uint32_t>(std::ceil(compressed_size_in_bits_ / 8.0)));
  output_->Refresh();
  stored_compressed_size_in_bits_ = compressed_size_in_bits_;
  compressed_size_in_bits_ = 0;
  value_count_ = 0;
}

Array<uint8_t> AdaptiveSerfQtCompressor::compressed_bytes() const {
  return compressed_bytes_;
}

long AdaptiveSerfQtCompressor::get_compressed_size_in_bits() const {
  return stored_compressed_size_in_bits_;
}
