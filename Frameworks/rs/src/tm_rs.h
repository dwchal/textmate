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

bool tm_glob_to_regexp (char const* glob, size_t len, bool matchDotFiles, tm_rs_sink_t sink, void* ctx);
void tm_glob_expand_braces (char const* glob, size_t len, tm_rs_sink_t sink, void* ctx); // sink is called once per expansion

typedef void (*tm_rs_range_sink_t) (void* ctx, size_t from, size_t to);
double tm_text_rank (char const* filter, size_t filterLen, char const* candidate, size_t candidateLen, tm_rs_range_sink_t ranges, void* ctx);

// Markdown structure for the editor’s formatted mode (src/markdown.rs). Kinds:
// 1-6 heading level, 7 bold, 8 italic, 9 strikethrough, 10 code, 11 link text,
// 20 a marker to hide while the caret is outside owner.
enum { kTMMarkdownHeading1 = 1, kTMMarkdownHeading6 = 6, kTMMarkdownBold = 7, kTMMarkdownItalic = 8, kTMMarkdownStrikethrough = 9, kTMMarkdownCode = 10, kTMMarkdownLink = 11, kTMMarkdownConceal = 20 };
typedef void (*tm_markdown_sink_t) (void* ctx, unsigned kind, size_t from, size_t to, size_t ownerFrom, size_t ownerTo);
void tm_markdown_spans (char const* str, size_t len, tm_markdown_sink_t sink, void* ctx);

}

namespace tm_rs
{
	inline void append_to_string (void* ctx, char const* bytes, size_t len)
	{
		static_cast<std::string*>(ctx)->append(bytes, len);
	}
}

#endif /* end of include guard: TM_RS_H_7XQ2M4KD */
