#ifndef SERF_ALL_TEST_PERF_BASELINE_INC_HPP_
#define SERF_ALL_TEST_PERF_BASELINE_INC_HPP_

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

#include "baselines/deflate/deflate_compressor.h"
#include "baselines/deflate/deflate_decompressor.h"

#include "baselines/lz4/lz4_compressor.h"
#include "baselines/lz4/lz4_decompressor.h"

#include "baselines/fpc/fpc_compressor.h"
#include "baselines/fpc/fpc_decompressor.h"

#include "baselines/chimp128/chimp_compressor.h"
#include "baselines/chimp128/chimp_decompressor.h"
#include "baselines/chimp128/chimp_compressor_32.h"
#include "baselines/chimp128/chimp_decompressor_32.h"

#include "baselines/elf/elf.h"

#include "baselines/elf_star/elf_star.h"

#include "baselines/gorilla/gorilla_compressor.h"
#include "baselines/gorilla/gorilla_decompressor.h"

#include "baselines/lz77/fastlz.h"

#include "baselines/machete/machete.h"

#include "baselines/zstd/lib/zstd.h"

#include "baselines/snappy/snappy.h"

#include "baselines/sim_piece/sim_piece.h"

#include "baselines/sz2/sz/include/sz.h"

#include "baselines/sprintz/double_sprintz_compressor.h"
#include "baselines/sprintz/double_sprintz_decompressor.h"

#include "baselines/alp/include/alp.hpp"

#endif //SERF_ALL_TEST_PERF_BASELINE_INC_HPP_
