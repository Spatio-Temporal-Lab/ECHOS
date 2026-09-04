use crate::compress::split_double::SplitBDDoubleCompress;
use crate::segment::Segment;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;
use std::slice;
use std::time::SystemTime;

pub struct BuffRustInput {
    compressor: SplitBDDoubleCompress,
    segment: Segment<f64>,
}

pub struct BuffRustCompressed {
    compressor: SplitBDDoubleCompress,
    bytes: Option<Vec<u8>>,
}

#[no_mangle]
pub unsafe extern "C" fn buff_rust_input_new(
    data: *const f64,
    len: usize,
    scale: usize,
) -> *mut BuffRustInput {
    if data.is_null() || len == 0 || scale == 0 {
        return ptr::null_mut();
    }

    catch_unwind(AssertUnwindSafe(|| {
        let values = slice::from_raw_parts(data, len).to_vec();
        let input = BuffRustInput {
            compressor: SplitBDDoubleCompress::new(10, 10, scale),
            segment: Segment::new(None, SystemTime::UNIX_EPOCH, 0, values, None, None),
        };
        Box::into_raw(Box::new(input))
    }))
    .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn buff_rust_input_free(input: *mut BuffRustInput) {
    if !input.is_null() {
        drop(Box::from_raw(input));
    }
}

#[no_mangle]
pub unsafe extern "C" fn buff_rust_compress(
    input: *mut BuffRustInput,
) -> *mut BuffRustCompressed {
    if input.is_null() {
        return ptr::null_mut();
    }

    catch_unwind(AssertUnwindSafe(|| {
        let input = &mut *input;
        let bytes = input.compressor.byte_fixed_encode(&mut input.segment);
        Box::into_raw(Box::new(BuffRustCompressed {
            compressor: input.compressor.clone(),
            bytes: Some(bytes),
        }))
    }))
    .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn buff_rust_compressed_size(
    compressed: *const BuffRustCompressed,
) -> usize {
    if compressed.is_null() {
        return 0;
    }
    (*compressed)
        .bytes
        .as_ref()
        .map_or(0, |bytes| bytes.len())
}

#[no_mangle]
pub unsafe extern "C" fn buff_rust_decompress(
    compressed: *mut BuffRustCompressed,
    output: *mut f64,
    output_capacity: usize,
) -> isize {
    if compressed.is_null() || output.is_null() {
        return -1;
    }

    catch_unwind(AssertUnwindSafe(|| {
        let compressed = &mut *compressed;
        let bytes = match compressed.bytes.take() {
            Some(bytes) => bytes,
            None => return -1,
        };
        let values = compressed.compressor.byte_fixed_decode(bytes);
        if values.len() > output_capacity {
            return -1;
        }
        ptr::copy_nonoverlapping(values.as_ptr(), output, values.len());
        values.len() as isize
    }))
    .unwrap_or(-1)
}

#[no_mangle]
pub unsafe extern "C" fn buff_rust_compressed_free(compressed: *mut BuffRustCompressed) {
    if !compressed.is_null() {
        drop(Box::from_raw(compressed));
    }
}
