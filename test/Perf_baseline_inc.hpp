#ifndef SERF_ALL_TEST_PERF_BASELINE_INC_HPP_
#define SERF_ALL_TEST_PERF_BASELINE_INC_HPP_

#define SERF_ENABLE_SERF

#ifdef SERF_ENABLE_SERF
#include "../src/compressor/serf_xor_compressor.h"
#include "../src/decompressor/serf_xor_decompressor.h"
#include "../src/compressor/serf_qt_compressor.h"
#include "../src/decompressor/serf_qt_decompressor.h"

#include "../src/compressor_32/serf_xor_compressor_32.h"
#include "../src/decompressor_32/serf_xor_decompressor_32.h"
#include "../src/compressor_32/serf_qt_compressor_32.h"
#include "../src/decompressor_32/serf_qt_decompressor_32.h"

#include "../src/compressor/serf_xor_compressor_no_opt_appr.h"
#include "../src/compressor/serf_xor_compressor_no_fast_search.h"

#include "../src/compressor/serf_xor_compressor_rel.h"
#endif

// #define SERF_ENABLE_BASELINE_DEFLATE
// #define SERF_ENABLE_BASELINE_LZ4
// #define SERF_ENABLE_BASELINE_FPC
// #define SERF_ENABLE_BASELINE_CHIMP128
// #define SERF_ENABLE_BASELINE_ELF
// #define SERF_ENABLE_BASELINE_GORILLA
// #define SERF_ENABLE_BASELINE_LZ77
// #define SERF_ENABLE_BASELINE_MACHETE
// #define SERF_ENABLE_BASELINE_ZSTD
// #define SERF_ENABLE_BASELINE_SNAPPY
// #define SERF_ENABLE_BASELINE_SIM_PIECE
// #define SERF_ENABLE_BASELINE_SZ2
// #define SERF_ENABLE_BASELINE_SPRINTZ
// #define SERF_ENABLE_BASELINE_ALP

#ifdef SERF_ENABLE_BASELINE_DEFLATE
#include "baselines/deflate/deflate_compressor.h"
#include "baselines/deflate/deflate_decompressor.h"
#endif

#ifdef SERF_ENABLE_BASELINE_LZ4
#include "baselines/lz4/lz4_compressor.h"
#include "baselines/lz4/lz4_decompressor.h"
#endif

#ifdef SERF_ENABLE_BASELINE_FPC
#include "baselines/fpc/fpc_compressor.h"
#include "baselines/fpc/fpc_decompressor.h"
#endif

#ifdef  SERF_ENABLE_BASELINE_CHIMP128
#include "baselines/chimp128/chimp_compressor.h"
#include "baselines/chimp128/chimp_decompressor.h"
#include "baselines/chimp128/chimp_compressor_32.h"
#include "baselines/chimp128/chimp_decompressor_32.h"
#endif

#ifdef SERF_ENABLE_BASELINE_ELF
#include "baselines/elf/elf.h"

#include "baselines/elf_star/elf_star.h"
#endif

#ifdef SERF_ENABLE_BASELINE_GORILLA
#include "baselines/gorilla/gorilla_compressor.h"
#include "baselines/gorilla/gorilla_decompressor.h"
#endif

#ifdef SERF_ENABLE_BASELINE_LZ77
#include "baselines/lz77/fastlz.h"
#endif

#ifdef SERF_ENABLE_BASELINE_MACHETE
#include "baselines/machete/machete.h"
#endif

#ifdef SERF_ENABLE_BASELINE_ZSTD
#include "baselines/zstd/lib/zstd.h"
#endif

#ifdef SERF_ENABLE_BASELINE_SNAPPY
#include "baselines/snappy/snappy.h"
#endif

#ifdef SERF_ENABLE_BASELINE_SIM_PIECE
#include "baselines/sim_piece/sim_piece.h"
#endif

#ifdef SERF_ENABLE_BASELINE_SZ2
#include "baselines/sz2/sz/include/sz.h"
#endif

#ifdef SERF_ENABLE_BASELINE_SPRINTZ
#include "baselines/sprintz/double_sprintz_compressor.h"
#include "baselines/sprintz/double_sprintz_decompressor.h"
#endif

#ifdef SERF_ENABLE_BASELINE_ALP
#include "baselines/alp/include/alp.hpp"
#endif

#endif //SERF_ALL_TEST_PERF_BASELINE_INC_HPP_
