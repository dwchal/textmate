#ifndef OAK_COMMAND_REWRITE_FILE_URLS_H
#define OAK_COMMAND_REWRITE_FILE_URLS_H

#include <string>

// HTML output from commands is served from a custom URL scheme, and WebKit no
// longer lets such pages load file:// resources (stylesheets, scripts, images).
// This rewrites file:// URLs that directly follow a quote or ‘(’ — i.e. HTML
// attribute values, CSS url(…) and JavaScript strings — to tm-file://, which
// TextMate serves itself. Streaming: feed chunks to operator(), then finish().
// URLs embedded elsewhere (e.g. txmt://open?url=file://…) are left alone.

namespace oak
{
	struct file_url_rewriter_t
	{
		std::string operator() (char const* bytes, size_t len)
		{
			std::string in = _carry;
			in.append(bytes, len);
			_carry.clear();

			std::string out;
			out.reserve(in.size() + 64);

			static std::string const kNeedle = "file://";
			size_t i = 0;
			while(i < in.size())
			{
				char ch = in[i];
				out += ch;
				++i;

				if(ch != '"' && ch != '\'' && ch != '(')
					continue;

				size_t avail = in.size() - i;
				if(avail < kNeedle.size() && in.compare(i, avail, kNeedle, 0, avail) == 0)
				{
					out.pop_back();            // hold back the quote as well,
					_carry = in.substr(i - 1); // the rest might be the start of “file://”
					return out;
				}

				if(in.compare(i, kNeedle.size(), kNeedle) == 0)
				{
					out += "tm-";
					out += kNeedle;
					i += kNeedle.size();
				}
			}
			return out;
		}

		std::string finish ()
		{
			std::string res;
			res.swap(_carry);
			return res;
		}

	private:
		std::string _carry;
	};

} /* oak */

#endif
