#ifndef TM_RS_H_7XQ2M4KD
#define TM_RS_H_7XQ2M4KD

// C ABI of TextMate’s Rust library (Frameworks/rs, see src/ffi.rs).
// Text is passed as pointer + length and may contain any bytes; results are
// delivered to a sink callback, which tm_rs::to_string() wraps for C++.

#include <stddef.h>
#include <string>

extern "C" {

typedef void (*tm_rs_sink_t) (void* ctx, char const* bytes, size_t len);

void tm_text_asciify (char const* str, size_t len, tm_rs_sink_t sink, void* ctx);

}

namespace tm_rs
{
	inline void append_to_string (void* ctx, char const* bytes, size_t len)
	{
		static_cast<std::string*>(ctx)->append(bytes, len);
	}
}

#endif /* end of include guard: TM_RS_H_7XQ2M4KD */
