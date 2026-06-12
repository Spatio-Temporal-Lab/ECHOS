#ifndef SERF_ENDIAN_COMPAT_H_
#define SERF_ENDIAN_COMPAT_H_

#include <cstdint>

#if defined(_WIN32) || defined(WIN32) || defined(WIN64)
inline uint32_t SerfByteSwap32(uint32_t value) {
  return ((value & 0x000000FFU) << 24) |
      ((value & 0x0000FF00U) << 8) |
      ((value & 0x00FF0000U) >> 8) |
      ((value & 0xFF000000U) >> 24);
}
#define htobe32(x) SerfByteSwap32(static_cast<uint32_t>(x))
#define be32toh(x) SerfByteSwap32(static_cast<uint32_t>(x))
#elif defined(__APPLE__)
#include <machine/endian.h>
#define htobe32(x) htonl(x)
#define be32toh(x) ntohl(x)
#else
#include <endian.h>
#endif

#endif  // SERF_ENDIAN_COMPAT_H_
