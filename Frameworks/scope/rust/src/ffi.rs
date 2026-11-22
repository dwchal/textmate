//! C ABI used by `Frameworks/scope/src/selector.cc`. Declarations live in
//! `Frameworks/scope/src/scope_rs.h`; keep the two in sync.

use crate::{NodeAccess, Selector};
use std::ffi::c_void;
use std::ptr::NonNull;

/// Callbacks for walking a C++ `scope_t::node_t` chain without copying it.
#[repr(C)]
pub struct TmScopeNodeVTable {
    /// Returns the parent node, or null at the root.
    pub parent: unsafe extern "C" fn(node: *const c_void) -> *const c_void,
    /// Returns the node's atoms (e.g. `source.php`) and writes their length.
    pub atoms: unsafe extern "C" fn(node: *const c_void, len: *mut usize) -> *const u8,
}

struct Access<'a>(&'a TmScopeNodeVTable);

impl NodeAccess for Access<'_> {
    type Node = NonNull<c_void>;

    #[inline]
    fn parent(&self, node: Self::Node) -> Option<Self::Node> {
        NonNull::new(unsafe { (self.0.parent)(node.as_ptr()) } as *mut c_void)
    }

    #[inline]
    fn atoms(&self, node: Self::Node) -> &[u8] {
        let mut len = 0usize;
        let ptr = unsafe { (self.0.atoms)(node.as_ptr(), &mut len) };
        if len == 0 {
            &[]
        } else {
            // The node is kept alive by the caller's scope_t for the whole match.
            unsafe { std::slice::from_raw_parts(ptr, len) }
        }
    }
}

/// Opaque handle; owned by C++ and released with `tm_scope_selector_free`.
pub struct TmScopeSelector(Selector);

/// # Safety
/// `str` must point to `len` readable bytes (or be null with `len == 0`).
#[no_mangle]
pub unsafe extern "C" fn tm_scope_selector_parse(str: *const u8, len: usize) -> *mut TmScopeSelector {
    let bytes = if len == 0 { &[][..] } else { std::slice::from_raw_parts(str, len) };
    Box::into_raw(Box::new(TmScopeSelector(Selector::parse(bytes))))
}

/// # Safety
/// `sel` must come from `tm_scope_selector_parse` and not be freed twice.
#[no_mangle]
pub unsafe extern "C" fn tm_scope_selector_free(sel: *mut TmScopeSelector) {
    if !sel.is_null() {
        drop(Box::from_raw(sel));
    }
}

/// Writes the canonical string form by calling `sink(ctx, bytes, len)` once.
///
/// # Safety
/// `sel` must be a live selector handle.
#[no_mangle]
pub unsafe extern "C" fn tm_scope_selector_to_s(
    sel: *const TmScopeSelector,
    sink: unsafe extern "C" fn(ctx: *mut c_void, bytes: *const u8, len: usize),
    ctx: *mut c_void,
) {
    let out = (*sel).0.to_bytes();
    sink(ctx, out.as_ptr(), out.len());
}

/// Matches `lhs`/`rhs` (innermost `node_t*`, or null for an empty scope).
/// When `rank` is non-null it receives the rank on success.
///
/// # Safety
/// `sel` and `vtable` must be valid; the nodes must stay alive during the call.
#[no_mangle]
pub unsafe extern "C" fn tm_scope_selector_does_match(
    sel: *const TmScopeSelector,
    vtable: *const TmScopeNodeVTable,
    lhs: *const c_void,
    rhs: *const c_void,
    rank: *mut f64,
) -> bool {
    let access = Access(&*vtable);
    let lhs = NonNull::new(lhs as *mut c_void);
    let rhs = NonNull::new(rhs as *mut c_void);
    (*sel).0.does_match(&access, lhs, rhs, rank.as_mut())
}
