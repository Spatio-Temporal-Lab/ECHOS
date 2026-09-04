#ifndef SERF_TEST_BASELINES_BUFF_BUFF_FFI_H_
#define SERF_TEST_BASELINES_BUFF_BUFF_FFI_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BuffRustInput BuffRustInput;
typedef struct BuffRustCompressed BuffRustCompressed;

BuffRustInput *buff_rust_input_new(const double *data, size_t len, size_t scale);
void buff_rust_input_free(BuffRustInput *input);
BuffRustCompressed *buff_rust_compress(BuffRustInput *input);
size_t buff_rust_compressed_size(const BuffRustCompressed *compressed);
ptrdiff_t buff_rust_decompress(BuffRustCompressed *compressed, double *output, size_t output_capacity);
void buff_rust_compressed_free(BuffRustCompressed *compressed);

#ifdef __cplusplus
}
#endif

#endif
