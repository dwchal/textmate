#ifndef SCOPE_RS_H_4QK2N7VB
#define SCOPE_RS_H_4QK2N7VB

// C ABI exported by the Rust crate in Frameworks/scope/rust (see src/ffi.rs).

#include <stddef.h>

extern "C" {

struct TmScopeSelector;

struct TmScopeNodeVTable
{
	void const* (*parent) (void const* node);
	char const* (*atoms) (void const* node, size_t* len);
};

TmScopeSelector* tm_scope_selector_parse (char const* str, size_t len);
void tm_scope_selector_free (TmScopeSelector* selector);
void tm_scope_selector_to_s (TmScopeSelector const* selector, void (*sink) (void* ctx, char const* bytes, size_t len), void* ctx);
bool tm_scope_selector_does_match (TmScopeSelector const* selector, TmScopeNodeVTable const* vtable, void const* lhs, void const* rhs, double* rank);

}

#endif /* end of include guard: SCOPE_RS_H_4QK2N7VB */
