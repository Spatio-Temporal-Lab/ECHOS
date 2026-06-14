#include "decompressor/adaptive_serf_qt_rice_bounded16_decompressor.h"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "utils/double.h"
#include "utils/input_bit_stream.h"

std::vector<double> AdaptiveSerfQtRiceBounded16Decompressor::Decompress(
    const Array<uint8_t> &bytes) {
  constexpr uint32_t kRiceQuotientCap = 16;
  constexpr uint64_t kEscape = std::numeric_limits<uint64_t>::max();
  InputBitStream input;
  input.SetBuffer(bytes);
  const bool block_size_changed = input.ReadBit();
  const bool max_diff_changed = input.ReadBit();
  if (!metadata_initialized_ && (!block_size_changed || !max_diff_changed)) {
    throw std::runtime_error("First Adaptive Serf-QT-Rice-Bounded16 block must contain full metadata");
  }
  if (block_size_changed) block_size_ = input.ReadInt(16);
  if (max_diff_changed) {
    max_diff_ = Double::LongBitsToDouble(input.ReadLong(64));
    quantization_step_ = 2 * max_diff_;
  }
  if (block_size_ <= 0 || block_size_ > 65535 || !std::isfinite(max_diff_) || max_diff_ <= 0) {
    throw std::runtime_error("Invalid Adaptive Serf-QT-Rice-Bounded16 metadata");
  }
  metadata_initialized_ = true;

  std::vector<double> result;
  result.reserve(block_size_);
  for (int index = 0; index < block_size_; ++index) {
    const AdaptiveQtCodec::AdaptiveRiceChoice choice =
        AdaptiveQtCodec::SelectAdaptiveBoundedRiceCodec(adaptive_state_);
    bool raw = false;
    uint64_t mapped = 1;
    if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
      mapped = AdaptiveQtCodec::DecodeCappedRiceWithRaw(choice.rice_parameter, kRiceQuotientCap,
                                                        &raw, &input);
    } else {
      mapped = AdaptiveQtCodec::DecodeMapped(choice.codec, choice.rice_parameter, &input);
      raw = mapped == kEscape;
    }

    double value;
    if (raw) {
      value = Double::LongBitsToDouble(input.ReadLong(64));
    } else {
      const int64_t q = AdaptiveQtCodec::ZigZagDecode(mapped - 1);
      value = previous_ + quantization_step_ * static_cast<double>(q);
      const AdaptiveQtCodec::AdaptiveCodeLengths lengths =
          AdaptiveQtCodec::CalculateCappedRiceWithRawCodeLengths(mapped, choice.rice_parameter,
                                                                 kRiceQuotientCap);
      AdaptiveQtCodec::UpdateAdaptiveBoundedRiceState(mapped, lengths, &adaptive_state_);
    }
    result.push_back(value);
    previous_ = std::isfinite(value) ? value : 2;
  }
  return result;
}
