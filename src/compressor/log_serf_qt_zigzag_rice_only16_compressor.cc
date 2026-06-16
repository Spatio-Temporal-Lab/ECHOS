#include "compressor/log_serf_qt_zigzag_rice_only16_compressor.h"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "utils/double.h"

namespace {

bool QuantizeLog(double magnitude, double prediction, double log_max_diff, double inverse_log_step,
                 double lower_log_error_bound, double upper_log_error_bound, int64_t *q,
                 double *target_log, double *recovered_log) {
  if (!std::isfinite(magnitude) || magnitude == 0) return false;
  *target_log = std::log(magnitude);
  const double scaled = (*target_log - prediction) * inverse_log_step;
  if (!std::isfinite(scaled)) return false;

  if (std::abs(scaled) > 0x1p52) {
    const long double step = 2.0L * static_cast<long double>(log_max_diff);
    const long double precise_scaled =
        (static_cast<long double>(*target_log) - static_cast<long double>(prediction)) / step;
    if (!std::isfinite(precise_scaled)) return false;
    const long double precise_rounded = std::round(precise_scaled);
    if (precise_rounded <= static_cast<long double>(std::numeric_limits<int64_t>::min()) ||
        precise_rounded > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
      return false;
    }
    *q = static_cast<int64_t>(precise_rounded);
  } else {
    *q = static_cast<int64_t>(std::round(scaled));
  }
  if (*q == std::numeric_limits<int64_t>::min()) return false;

  *recovered_log = prediction + 2 * log_max_diff * static_cast<double>(*q);
  const double log_error = *recovered_log - *target_log;
  return log_error >= lower_log_error_bound && log_error <= upper_log_error_bound;
}

}  // namespace

LogSerfQtZigZagRiceOnly16Compressor::LogSerfQtZigZagRiceOnly16Compressor(
    int block_size, double relative_error_bound)
    : block_size_(block_size) {
  if (block_size <= 0 || block_size > 65535) throw std::invalid_argument("Invalid block size");
  UpdateErrorConfig(relative_error_bound);
  output_ = std::make_unique<OutputBitStream>(static_cast<uint32_t>(16 * block_size + 32));
}

void LogSerfQtZigZagRiceOnly16Compressor::UpdateErrorConfig(double relative_error_bound) {
  if (!std::isfinite(relative_error_bound) || relative_error_bound <= 0) {
    throw std::invalid_argument("Invalid relative error bound");
  }
  relative_error_bound_ = relative_error_bound;
  upper_log_error_bound_ = std::log1p(relative_error_bound);
  lower_log_error_bound_ =
      relative_error_bound < 1 ? std::log1p(-relative_error_bound)
                               : -std::numeric_limits<double>::infinity();
  log_max_diff_ = upper_log_error_bound_ * 0.999;
  inverse_log_step_ = 1.0 / (2 * log_max_diff_);
}

void LogSerfQtZigZagRiceOnly16Compressor::SetBlockConfig(int block_size,
                                                         double relative_error_bound) {
  if (value_count_ != 0) throw std::runtime_error("Cannot change block config inside a block");
  if (block_size <= 0 || block_size > 65535) throw std::invalid_argument("Invalid block size");
  block_size_ = block_size;
  UpdateErrorConfig(relative_error_bound);
  output_ = std::make_unique<OutputBitStream>(static_cast<uint32_t>(16 * block_size + 32));
}

void LogSerfQtZigZagRiceOnly16Compressor::WriteMetadata() {
  const uint64_t log_max_diff_bits = Double::DoubleToLongBits(log_max_diff_);
  const bool block_size_changed = !metadata_initialized_ || block_size_ != previous_block_size_;
  const bool log_max_diff_changed =
      !metadata_initialized_ || log_max_diff_bits != previous_log_max_diff_bits_;
  compressed_size_in_bits_ += output_->WriteBit(block_size_changed);
  compressed_size_in_bits_ += output_->WriteBit(log_max_diff_changed);
  if (block_size_changed) compressed_size_in_bits_ += output_->WriteInt(block_size_, 16);
  if (log_max_diff_changed) compressed_size_in_bits_ += output_->WriteLong(log_max_diff_bits, 64);
  previous_block_size_ = block_size_;
  previous_log_max_diff_bits_ = log_max_diff_bits;
  metadata_initialized_ = true;
}

LogSerfQtZigZagRiceOnly16Compressor::Choice
LogSerfQtZigZagRiceOnly16Compressor::Choose(double value) const {
  Choice best;
  best.mode = Mode::kRaw;
  best.sign = std::signbit(value);
  best.has_original_log = false;
  best.bits = 69;

  if (value == 0) {
    best.mode = Mode::kZero;
    best.bits = 5;
    return best;
  }

  const double magnitude = std::abs(value);
  if (std::isfinite(value) && best.sign == previous_sign_ &&
      std::abs(previous_value_ - value) <= relative_error_bound_ * magnitude) {
    best.mode = Mode::kRepeat;
    best.bits = 1;
    return best;
  }
  if (std::isfinite(value) && best.sign != previous_sign_ &&
      std::abs(std::abs(previous_value_) - magnitude) <= relative_error_bound_ * magnitude) {
    best.mode = Mode::kChangedSignZeroResidual;
    best.recovered_log = previous_log_;
    best.recovered_value = best.sign ? -std::abs(previous_value_) : std::abs(previous_value_);
    best.bits = 4;
    return best;
  }

  best.rice_parameter = AdaptiveQtCodec::EstimateRiceParameter(adaptive_state_);
  int64_t q = 0;
  double target_log = 0;
  double recovered_log = 0;
  if (!QuantizeLog(magnitude, previous_log_, log_max_diff_, inverse_log_step_,
                   lower_log_error_bound_, upper_log_error_bound_, &q, &target_log,
                   &recovered_log)) {
    return best;
  }
  best.original_log = target_log;
  best.has_original_log = true;
  if (q == 0) return best;

  const bool changed_sign = best.sign != previous_sign_;
  best.mapped = AdaptiveQtCodec::ZigZagEncode(q);
  best.recovered_log = recovered_log;
  best.rice_bits = AdaptiveQtCodec::CappedRiceLength(
      best.mapped, best.rice_parameter, AdaptiveQtCodec::kBoundedRiceQuotientCap);
  const uint64_t prefix_bits = changed_sign ? 3 : 2;
  if (prefix_bits + best.rice_bits < best.bits) {
    const double recovered_magnitude = std::exp(recovered_log);
    if (!std::isfinite(recovered_magnitude)) return best;
    best.mode = changed_sign ? Mode::kChangedSignResidual : Mode::kSameSignResidual;
    best.recovered_value = best.sign ? -recovered_magnitude : recovered_magnitude;
    best.bits = prefix_bits + best.rice_bits;
  }
  return best;
}

void LogSerfQtZigZagRiceOnly16Compressor::WriteResidual(const Choice &choice) {
  compressed_size_in_bits_ += AdaptiveQtCodec::EncodeCappedRice(
      choice.mapped, choice.rice_parameter, AdaptiveQtCodec::kBoundedRiceQuotientCap,
      output_.get());
}

void LogSerfQtZigZagRiceOnly16Compressor::WriteChoice(const Choice &choice, double original) {
  switch (choice.mode) {
    case Mode::kRepeat:
      compressed_size_in_bits_ += output_->WriteBit(false);
      return;
    case Mode::kSameSignResidual:
      compressed_size_in_bits_ += output_->WriteInt(2, 2);
      WriteResidual(choice);
      return;
    case Mode::kChangedSignResidual:
      compressed_size_in_bits_ += output_->WriteInt(6, 3);
      WriteResidual(choice);
      return;
    case Mode::kChangedSignZeroResidual:
      compressed_size_in_bits_ += output_->WriteInt(14, 4);
      return;
    case Mode::kZero:
      compressed_size_in_bits_ += output_->WriteInt(30, 5);
      return;
    case Mode::kRaw:
      compressed_size_in_bits_ += output_->WriteInt(31, 5);
      compressed_size_in_bits_ += output_->WriteLong(Double::DoubleToLongBits(original), 64);
      return;
  }
}

void LogSerfQtZigZagRiceOnly16Compressor::UpdateState(const Choice &choice, double original) {
  if (choice.mode == Mode::kRaw) {
    if (std::isfinite(original) && original != 0) {
      previous_log_ = choice.has_original_log ? choice.original_log : std::log(std::abs(original));
      previous_value_ = original;
      previous_sign_ = std::signbit(original);
    }
    return;
  }
  if (choice.mode == Mode::kZero || choice.mode == Mode::kRepeat) return;
  previous_log_ = choice.recovered_log;
  previous_value_ = choice.recovered_value;
  previous_sign_ = choice.sign;
  if (choice.mode != Mode::kChangedSignZeroResidual) {
    AdaptiveQtCodec::UpdateAdaptiveRiceParameterState(choice.mapped, &adaptive_state_);
  }
}

void LogSerfQtZigZagRiceOnly16Compressor::AddValue(double value) {
  if (value_count_ >= block_size_) {
    throw std::runtime_error("ZigZag RiceOnly16 Log Serf-QT block is full");
  }
  if (value_count_ == 0) WriteMetadata();
  const Choice choice = Choose(value);
  WriteChoice(choice, value);
  UpdateState(choice, value);
  ++value_count_;
}

void LogSerfQtZigZagRiceOnly16Compressor::Close() {
  if (value_count_ != block_size_) {
    throw std::runtime_error("ZigZag RiceOnly16 Log Serf-QT block is incomplete");
  }
  output_->Flush();
  compressed_bytes_ =
      output_->GetBuffer(static_cast<uint32_t>(std::ceil(compressed_size_in_bits_ / 8.0)));
  output_->Refresh();
  stored_compressed_size_in_bits_ = compressed_size_in_bits_;
  compressed_size_in_bits_ = 0;
  value_count_ = 0;
}

Array<uint8_t> LogSerfQtZigZagRiceOnly16Compressor::compressed_bytes() const {
  return compressed_bytes_;
}

long LogSerfQtZigZagRiceOnly16Compressor::get_compressed_size_in_bits() const {
  return stored_compressed_size_in_bits_;
}
