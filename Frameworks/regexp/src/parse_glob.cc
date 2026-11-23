#include "parse_glob.h"
#include <rs/tm_rs.h>
#include <oak/oak.h>

// Globs are parsed in Rust: Frameworks/rs/src/glob.rs (which also documents the syntax).

std::string convert_glob_to_regexp (std::string const& str, bool matchDotFiles)
{
	std::string res;
	if(!tm_glob_to_regexp(str.data(), str.size(), matchDotFiles, &tm_rs::append_to_string, &res))
		return NULL_STR;
	return res;
}

std::vector<std::string> expand_braces (std::string const& str)
{
	std::vector<std::string> res;
	tm_glob_expand_braces(str.data(), str.size(), [](void* ctx, char const* bytes, size_t len){
		static_cast<std::vector<std::string>*>(ctx)->emplace_back(bytes, len);
	}, &res);
	return res;
}
