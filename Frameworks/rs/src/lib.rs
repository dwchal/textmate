//! TextMate’s Rust code.
//!
//! An executable can only link one Rust static library (each one carries the
//! standard library), so everything written in Rust is built into this crate
//! and exposed to C++ through the C ABI declared in `tm_rs.h`.
//!
//! Rules for code in here:
//! * Text is `&[u8]`, never `&str`: buffers and paths may hold invalid UTF-8.
//! * No panics on any input: the library is built with `panic = "abort"`.
//! * Every port is tested against the C++ it replaces before that is deleted.

pub mod asciify;
pub mod ffi;

// Keep the scope selector C ABI (tm_scope_selector_*) in the static library.
pub use scope_rs;
