#include "compressor/log_serf_qt_compressor.h"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "utils/double.h"

namespace {

bool QuantizeLog(double value, double prediction, double log_max_diff, double inverse_log_step,
                 double lower_log_error_bound, double upper_log_error_bound, int64_t *q,
                 double *recovered_log, double *recovered_magnitude) {
  if (!std::isfinite(value) || value == 0) return false;
  const double target_log = std::log(std::abs(value));
  const double scaled = (target_log - prediction) * inverse_log_step;
  if (!std::isfinite(scaled)) return false;

  if (std::abs(scaled) > 0x1p52) {
    const long double step = 2.0L * static_cast<long double>(log_max_diff);
    const long double precise_scaled =
        (static_cast<long double>(target_log) - static_cast<long double>(prediction)) / step;
    if (!std::isfinite(precise_scaled)) return false;
    const long double precise_rounded = std::round(precise_scaled);
    if (precise_rounded <= static_cast<long double>(std::numeric_limits<int64_t>::min()) ||
        precise_rounded > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
      return false;
    }
    *q = static_cast<int64_t>(precise_rounded);
  } else {
    const double rounded = std::round(scaled);
    *q = static_cast<int64_t>(rounded);
  }
  if (*q == std::numeric_limits<int64_t>::min()) return false;

  *recovered_log = prediction + 2 * log_max_diff * static_cast<double>(*q);
  const double log_error = *recovered_log - target_log;
  if (log_error < lower_log_error_bound || log_error > upper_log_error_bound) return false;
  *recovered_magnitude = std::exp(*recovered_log);
  return std::isfinite(*recovered_magnitude);
}

}  // namespace

LogSerfQtCompressor::LogSerfQtCompressor(int block_size, double relative_error_bound)
    : block_size_(block_size) {
  if (block_size <= 0 || block_size > 65535) throw std::invalid_argument("Invalid block size");
  UpdateErrorConfig(relative_error_bound);
  output_ = std::make_unique<OutputBitStream>(static_cast<uint32_t>(16 * block_size + 32));
}

void LogSerfQtCompressor::UpdateErrorConfig(double relative_error_bound) {
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

void LogSerfQtCompressor::SetBlockConfig(int block_size, double relative_error_bound) {
  if (value_count_ != 0) throw std::runtime_error("Cannot change block config inside a block");
  if (block_size <= 0 || block_size > 65535) throw std::invalid_argument("Invalid block size");
  block_size_ = block_size;
  UpdateErrorConfig(relative_error_bound);
  output_ = std::make_unique<OutputBitStream>(static_cast<uint32_t>(16 * block_size + 32));
}

void LogSerfQtCompressor::WriteMetadata() {
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

LogSerfQtCompressor::Choice LogSerfQtCompressor::Choose(double value) const {
  Choice best;
  best.sign = std::signbit(value);

  if (value == 0) {
    best.mode = Mode::kZero;
    best.bits = 4;
    return best;
  }

  if (std::isfinite(value) && best.sign == previous_sign_ &&
      std::abs(previous_value_ - value) <= relative_error_bound_ * std::abs(value)) {
    best.mode = Mode::kRepeat;
    best.bits = 1;
    return best;
  }
  if (std::isfinite(value) && best.sign != previous_sign_ &&
      std::abs(std::abs(previous_value_) - std::abs(value)) <=
          relative_error_bound_ * std::abs(value)) {
    best.mode = Mode::kChangedSignZeroResidual;
    best.recovered_log = previous_log_;
    best.recovered_value = best.sign ? -std::abs(previous_value_) : std::abs(previous_value_);
    best.bits = 4;
    return best;
  }

  best.integer_choice = AdaptiveQtCodec::SelectAdaptiveRiceCodec(adaptive_state_);
  int64_t q = 0;
  double recovered_log = 0;
  double recovered_magnitude = 0;
  if (!QuantizeLog(value, previous_log_, log_max_diff_, inverse_log_step_, lower_log_error_bound_,
                   upper_log_error_bound_, &q, &recovered_log, &recovered_magnitude)) {
    return best;
  }

  const bool changed_sign = best.sign != previous_sign_;
  if (q == 0) return best;

  const uint64_t mapped =
      q < 0 ? static_cast<uint64_t>(-q) : static_cast<uint64_t>(q);
  best.mapped = mapped;
  best.recovered_log = recovered_log;
  best.recovered_value = best.sign ? -recovered_magnitude : recovered_magnitude;

  uint64_t residual_bits;
  if (best.integer_choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
    residual_bits = AdaptiveQtCodec::AdaptiveRiceLength(mapped, best.integer_choice.rice_parameter);
  } else {
    residual_bits = AdaptiveQtCodec::EncodedLength(
        mapped, best.integer_choice.codec, best.integer_choice.rice_parameter);
  }
  const uint64_t prefix_bits = changed_sign ? 5 : 3;
  if (prefix_bits + residual_bits < best.bits) {
    if (changed_sign) {
      best.mode =
          q > 0 ? Mode::kChangedSignPositiveResidual : Mode::kChangedSignNegativeResidual;
    } else {
      best.mode = q > 0 ? Mode::kSameSignPositiveResidual : Mode::kSameSignNegativeResidual;
    }
    best.bits = prefix_bits + residual_bits;
  }
  return best;
}

void LogSerfQtCompressor::WriteResidual(const Choice &choice) {
  if (choice.integer_choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
    if (AdaptiveQtCodec::ShouldUseRiceDeltaFallback(
            choice.mapped, choice.integer_choice.rice_parameter)) {
      compressed_size_in_bits_ += output_->WriteInt(2, 2);
      compressed_size_in_bits_ += AdaptiveQtCodec::EncodeMapped(
          choice.mapped, AdaptiveQtCodec::IntegerCodec::kDelta, 0, output_.get());
    } else {
      compressed_size_in_bits_ += output_->WriteBit(false);
      compressed_size_in_bits_ += AdaptiveQtCodec::EncodeMapped(
          choice.mapped, choice.integer_choice.codec, choice.integer_choice.rice_parameter,
          output_.get());
    }
    return;
  }
  compressed_size_in_bits_ += AdaptiveQtCodec::EncodeMapped(
      choice.mapped, choice.integer_choice.codec, choice.integer_choice.rice_parameter, output_.get());
}

void LogSerfQtCompressor::WriteChoice(const Choice &choice, double original) {
  switch (choice.mode) {
    case Mode::kRepeat:
      compressed_size_in_bits_ += output_->WriteBit(false);
      return;
    case Mode::kSameSignPositiveResidual:
      compressed_size_in_bits_ += output_->WriteInt(4, 3);
      WriteResidual(choice);
      return;
    case Mode::kSameSignNegativeResidual:
      compressed_size_in_bits_ += output_->WriteInt(5, 3);
      WriteResidual(choice);
      return;
    case Mode::kChangedSignZeroResidual:
      compressed_size_in_bits_ += output_->WriteInt(12, 4);
      return;
    case Mode::kChangedSignPositiveResidual:
      compressed_size_in_bits_ += output_->WriteInt(26, 5);
      WriteResidual(choice);
      return;
    case Mode::kChangedSignNegativeResidual:
      compressed_size_in_bits_ += output_->WriteInt(27, 5);
      WriteResidual(choice);
      return;
    case Mode::kZero:
      compressed_size_in_bits_ += output_->WriteInt(14, 4);
      return;
    case Mode::kRaw:
      compressed_size_in_bits_ += output_->WriteInt(15, 4);
      compressed_size_in_bits_ += output_->WriteLong(Double::DoubleToLongBits(original), 64);
      return;
  }
}

void LogSerfQtCompressor::UpdateState(const Choice &choice, double original) {
  if (choice.mode == Mode::kRaw) {
    if (std::isfinite(original) && original != 0) {
      previous_log_ = std::log(std::abs(original));
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
    AdaptiveQtCodec::UpdateAdaptiveRiceState(
        choice.mapped, choice.integer_choice.rice_parameter, &adaptive_state_);
  }
}

void LogSerfQtCompressor::AddValue(double value) {
  if (value_count_ >= block_size_) throw std::runtime_error("Log Serf-QT block is full");
  if (value_count_ == 0) WriteMetadata();
  const Choice choice = Choose(value);
  WriteChoice(choice, value);
  UpdateState(choice, value);
  ++value_count_;
}

void LogSerfQtCompressor::Close() {
  if (value_count_ != block_size_) throw std::runtime_error("Log Serf-QT block is incomplete");
  output_->Flush();
  compressed_bytes_ = output_->GetBuffer(static_cast<uint32_t>(std::ceil(compressed_size_in_bits_ / 8.0)));
  output_->Refresh();
  stored_compressed_size_in_bits_ = compressed_size_in_bits_;
  compressed_size_in_bits_ = 0;
  value_count_ = 0;
}

Array<uint8_t> LogSerfQtCompressor::compressed_bytes() const {
  return compressed_bytes_;
}

long LogSerfQtCompressor::get_compressed_size_in_bits() const {
  return stored_compressed_size_in_bits_;
}
