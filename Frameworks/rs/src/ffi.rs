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

/// Receives one covered range of the candidate, as byte offsets.
pub type RangeSink = unsafe extern "C" fn(ctx: *mut c_void, from: usize, to: usize);

/// Fuzzy match score of `filter` against `candidate` (0 = no match); see `rank.rs`.
///
/// # Safety
/// Both strings must point to as many readable bytes as their lengths say.
/// `ranges` may be null; otherwise it is called once per covered range.
#[no_mangle]
pub unsafe extern "C" fn tm_text_rank(filter: *const u8, filter_len: usize, candidate: *const u8, candidate_len: usize, ranges: Option<RangeSink>, ctx: *mut c_void) -> f64 {
    let (filter, candidate) = (bytes(filter, filter_len), bytes(candidate, candidate_len));
    match ranges {
        None => crate::rank::rank(filter, candidate, None),
        Some(sink) => {
            let mut out = Vec::new();
            let score = crate::rank::rank(filter, candidate, Some(&mut out));
            for (from, to) in out {
                sink(ctx, from, to);
            }
            score
        }
    }
}

/// Writes the regular expression for `glob` to `sink` and returns true, or
/// returns false (writing nothing) if the glob is empty; see `glob.rs`.
///
/// # Safety
/// `glob` must point to `len` readable bytes; `sink` must be a valid callback.
#[no_mangle]
pub unsafe extern "C" fn tm_glob_to_regexp(glob: *const u8, len: usize, match_dot_files: bool, sink: Sink, ctx: *mut c_void) -> bool {
    match crate::glob::glob_to_regexp(bytes(glob, len), match_dot_files) {
        Some(regexp) => {
            emit(sink, ctx, &regexp);
            true
        }
        None => false,
    }
}

/// Calls `sink` once per brace expansion of `glob`, in order (also for empty strings).
///
/// # Safety
/// `glob` must point to `len` readable bytes; `sink` must be a valid callback.
#[no_mangle]
pub unsafe extern "C" fn tm_glob_expand_braces(glob: *const u8, len: usize, sink: Sink, ctx: *mut c_void) {
    for expansion in crate::glob::expand_braces(bytes(glob, len)) {
        sink(ctx, expansion.as_ptr(), expansion.len());
    }
}

/// Receives one Markdown span: `kind` is a `markdown::Kind` value, `from..to`
/// the bytes it applies to and `owner_from..owner_to` the element it is part of.
pub type MarkdownSink = unsafe extern "C" fn(ctx: *mut c_void, kind: u32, from: usize, to: usize, owner_from: usize, owner_to: usize);

/// Reports the styled and hideable ranges of a Markdown document, ordered by
/// start offset; see `markdown.rs`.
///
/// # Safety
/// `str` must point to `len` readable bytes; `sink` must be a valid callback.
#[no_mangle]
pub unsafe extern "C" fn tm_markdown_spans(str: *const u8, len: usize, sink: MarkdownSink, ctx: *mut c_void) {
    for span in crate::markdown::spans(bytes(str, len)) {
        sink(ctx, span.kind as u32, span.from, span.to, span.owner.0, span.owner.1);
    }
}
