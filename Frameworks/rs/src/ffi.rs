//! C ABI. Declarations live in `tm_rs.h`; keep the two in sync.

use std::ffi::c_void;

/// Receives output bytes; may be called more than once.
pub type Sink = unsafe extern "C" fn(ctx: *mut c_void, bytes: *const u8, len: usize);

/// # Safety
/// `ptr` must point to `len` readable bytes (or `len` must be 0).
pub(crate) unsafe fn bytes<'a>(ptr: *const u8, len: usize) -> &'a [u8] {
    if len == 0 || ptr.is_null() {
        &[]
    } else {
        std::slice::from_raw_parts(ptr, len)
    }
}

pub(crate) unsafe fn emit(sink: Sink, ctx: *mut c_void, out: &[u8]) {
    if !out.is_empty() {
        sink(ctx, out.as_ptr(), out.len());
    }
}

/// # Safety
/// `str` must point to `len` readable bytes; `sink` must be a valid callback.
#[no_mangle]
pub unsafe extern "C" fn tm_text_asciify(str: *const u8, len: usize, sink: Sink, ctx: *mut c_void) {
    emit(sink, ctx, &crate::asciify::asciify(bytes(str, len)));
}
