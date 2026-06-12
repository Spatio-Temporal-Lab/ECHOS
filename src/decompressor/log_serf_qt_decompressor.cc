#include "decompressor/log_serf_qt_decompressor.h"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "utils/adaptive_qt_codec.h"
#include "utils/double.h"
#include "utils/input_bit_stream.h"

namespace {

constexpr uint32_t kRiceQuotientCap = 16;

enum class Mode {
  kRepeat,
  kSameSignPositiveResidual,
  kSameSignNegativeResidual,
  kChangedSignZeroResidual,
  kChangedSignPositiveResidual,
  kChangedSignNegativeResidual,
  kZero,
  kRaw
};

Mode ReadMode(InputBitStream *input) {
  if (input->ReadBit() == 0) return Mode::kRepeat;
  if (input->ReadBit() == 0) return Mode::kSameSignNegativeResidual;
  if (input->ReadBit() == 0) return Mode::kSameSignPositiveResidual;
  if (input->ReadBit() == 0) return Mode::kChangedSignNegativeResidual;
  if (input->ReadBit() == 0) return Mode::kChangedSignPositiveResidual;
  if (input->ReadBit() == 0) return Mode::kChangedSignZeroResidual;
  return input->ReadBit() == 0 ? Mode::kZero : Mode::kRaw;
}

}  // namespace

std::vector<double> LogSerfQtDecompressor::Decompress(const Array<uint8_t> &bytes) {
  InputBitStream input;
  input.SetBuffer(bytes);
  const bool block_size_changed = input.ReadBit();
  const bool log_max_diff_changed = input.ReadBit();
  if (!metadata_initialized_ && (!block_size_changed || !log_max_diff_changed)) {
    throw std::runtime_error("First Log Serf-QT block must contain full metadata");
  }
  if (block_size_changed) block_size_ = input.ReadInt(16);
  if (log_max_diff_changed) log_max_diff_ = Double::LongBitsToDouble(input.ReadLong(64));
  if (block_size_ <= 0 || block_size_ > 65535 || !std::isfinite(log_max_diff_) || log_max_diff_ <= 0) {
    throw std::runtime_error("Invalid Log Serf-QT metadata");
  }
  metadata_initialized_ = true;

  std::vector<double> result;
  result.reserve(block_size_);

  for (int index = 0; index < block_size_; ++index) {
    const Mode mode = ReadMode(&input);
    if (mode == Mode::kRaw) {
      const double value = Double::LongBitsToDouble(input.ReadLong(64));
      result.push_back(value);
      if (std::isfinite(value) && value != 0) {
        previous_log_ = std::log(std::abs(value));
        previous_value_ = value;
        previous_sign_ = std::signbit(value);
      }
      continue;
    }
    if (mode == Mode::kZero) {
      result.push_back(0.0);
      continue;
    }
    if (mode == Mode::kRepeat) {
      result.push_back(previous_value_);
      continue;
    }
    if (mode == Mode::kChangedSignZeroResidual) {
      previous_sign_ = !previous_sign_;
      previous_value_ = previous_sign_ ? -std::abs(previous_value_) : std::abs(previous_value_);
      result.push_back(previous_value_);
      continue;
    }

    const AdaptiveQtCodec::AdaptiveRiceChoice choice =
        AdaptiveQtCodec::SelectAdaptiveRiceCodec(adaptive_state_);
    const bool changed_sign = mode == Mode::kChangedSignPositiveResidual ||
                              mode == Mode::kChangedSignNegativeResidual;
    const bool negative_residual = mode == Mode::kSameSignNegativeResidual ||
                                   mode == Mode::kChangedSignNegativeResidual;
    uint64_t mapped;
    if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
      mapped = AdaptiveQtCodec::DecodeCappedRice(choice.rice_parameter, kRiceQuotientCap, &input);
    } else {
      mapped = AdaptiveQtCodec::DecodeMapped(choice.codec, choice.rice_parameter, &input);
    }

    if (changed_sign) previous_sign_ = !previous_sign_;
    if (mapped > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      throw std::runtime_error("Invalid Log Serf-QT residual magnitude");
    }
    const int64_t magnitude_q = static_cast<int64_t>(mapped);
    const int64_t q = negative_residual ? -magnitude_q : magnitude_q;
    previous_log_ += 2 * log_max_diff_ * static_cast<double>(q);
    const double magnitude = std::exp(previous_log_);
    previous_value_ = previous_sign_ ? -magnitude : magnitude;
    result.push_back(previous_value_);
    const AdaptiveQtCodec::AdaptiveCodeLengths lengths =
        AdaptiveQtCodec::CalculateCappedRiceCodeLengths(mapped, choice.rice_parameter,
                                                        kRiceQuotientCap);
    AdaptiveQtCodec::UpdateAdaptiveRiceState(mapped, lengths, &adaptive_state_);
  }
  return result;
}
