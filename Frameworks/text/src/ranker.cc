#include "ranker.h"
#include "case.h"
#include <rs/tm_rs.h>

namespace oak
{
	std::string normalize_filter (std::string const& filter)
	{
		std::string res = "";
		for(auto const& ch : text::lowercase(filter))
		{
			if(ch != ' ')
				res += ch;
		}
		return res;
	}

	// The matching itself is implemented in Rust: Frameworks/rs/src/rank.rs
	double rank (std::string const& filter, std::string const& candidate, std::vector< std::pair<size_t, size_t> >* out)
	{
		return tm_text_rank(filter.data(), filter.size(), candidate.data(), candidate.size(), out ? [](void* ctx, size_t from, size_t to){
			static_cast<std::vector< std::pair<size_t, size_t> >*>(ctx)->emplace_back(from, to);
		} : (tm_rs_range_sink_t)nullptr, out);
	}

} /* oak */
