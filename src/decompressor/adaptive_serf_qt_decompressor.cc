#include "decompressor/adaptive_serf_qt_decompressor.h"

#include <cmath>
#include <limits>

#include "utils/adaptive_qt_codec.h"
#include "utils/double.h"
#include "utils/input_bit_stream.h"

std::vector<double> AdaptiveSerfQtDecompressor::Decompress(const Array<uint8_t> &bytes) {
  constexpr uint64_t kEscape = std::numeric_limits<uint64_t>::max();
  InputBitStream input;
  input.SetBuffer(bytes);
  const int count = input.ReadInt(16);
  const double max_diff = Double::LongBitsToDouble(input.ReadLong(64));

  std::vector<double> result;
  result.reserve(count);

  for (int index = 0; index < count; ++index) {
    const AdaptiveQtCodec::IntegerCodec codec =
        gamma_cost_ < delta_cost_ ? AdaptiveQtCodec::IntegerCodec::kGamma
                                  : AdaptiveQtCodec::IntegerCodec::kDelta;
    const uint64_t mapped = AdaptiveQtCodec::DecodeMapped(codec, 0, &input);

    bool raw = mapped == kEscape;
    double value;
    if (raw) {
      value = Double::LongBitsToDouble(input.ReadLong(64));
    } else {
      const int64_t q = AdaptiveQtCodec::ZigZagDecode(mapped - 1);
      value = previous_ + 2 * max_diff * static_cast<double>(q);
    }
    result.push_back(value);

    if (std::isfinite(value)) {
      previous_ = value;
    } else {
      previous_ = 2;
    }
    if (!raw) {
      gamma_cost_ = AdaptiveQtCodec::SaturatingAdd(gamma_cost_, AdaptiveQtCodec::GammaLength(mapped));
      delta_cost_ = AdaptiveQtCodec::SaturatingAdd(delta_cost_, AdaptiveQtCodec::DeltaLength(mapped));
    }
  }
  return result;
}
