#include "scope.h"
#include "scope_rs.h"

// scope::selector_t is implemented in Rust (Frameworks/scope/rust). This file
// owns the Rust handle and lets the Rust matcher walk scope_t's node chain.

namespace scope
{
	namespace types
	{
		struct selector_t
		{
			explicit selector_t (std::string const& str) : _handle(tm_scope_selector_parse(str.data(), str.size())) { }
			~selector_t () { tm_scope_selector_free(_handle); }

			selector_t (selector_t const&) = delete;
			selector_t& operator= (selector_t const&) = delete;

			bool does_match (scope::scope_t const& lhs, scope::scope_t const& rhs, double* rank) const
			{
				return tm_scope_selector_does_match(_handle, &vtable, lhs.node, rhs.node, rank);
			}

			std::string to_s () const
			{
				std::string res;
				tm_scope_selector_to_s(_handle, [](void* ctx, char const* bytes, size_t len){ static_cast<std::string*>(ctx)->assign(bytes, len); }, &res);
				return res;
			}

		private:
			using node_t = scope::scope_t::node_t;

			static void const* parent (void const* node)
			{
				return static_cast<node_t const*>(node)->parent();
			}

			static char const* atoms (void const* node, size_t* len)
			{
				std::string const& str = static_cast<node_t const*>(node)->_atoms;
				*len = str.size();
				return str.data();
			}

			static constexpr TmScopeNodeVTable vtable = { &parent, &atoms };

			TmScopeSelector* _handle;
		};

	} /* types */

	selector_t::selector_t ()                        { ; }
	selector_t::selector_t (char const* str)         { setup(str); }
	selector_t::selector_t (std::string const& str)  { setup(str); }

	void selector_t::setup (std::string const& str)
	{
		selector = std::make_shared<scope::types::selector_t>(str);
	}

	std::string to_s (selector_t const& s)
	{
		return s.selector ? s.selector->to_s() : "";
	}

	std::optional<double> selector_t::does_match (context_t const& scope) const
	{
		double rank = 1;
		if(selector)
			return scope.left == wildcard || scope.right == wildcard || selector->does_match(scope.left, scope.right, &rank) ? rank : std::optional<double>();
		return 0;
	}
}
