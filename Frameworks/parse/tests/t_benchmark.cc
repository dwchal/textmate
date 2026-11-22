#include "support.h"
#include <test/bundle_index.h>
#include <plist/plist.h>
#include <io/path.h>
#include <io/entries.h>
#include <text/format.h>

// Grammar parsing throughput with the real grammars installed by TextMate
// (~/Library/Application Support/TextMate/Managed/Bundles), on inputs built
// from this repository’s own sources. Run with: ninja parse/bench

static std::string const kRepoRoot = path::parent(path::parent(path::parent(path::parent(__FILE__))));

static std::map<std::string, bundles::item_ptr>& grammars ()
{
	static std::map<std::string, bundles::item_ptr> res = []{
		std::map<std::string, bundles::item_ptr> map;
		test::bundle_index_t index;
		std::string const bundlesDir = path::join(path::home(), "Library/Application Support/TextMate/Managed/Bundles");
		for(auto const& bundle : path::entries(bundlesDir))
		{
			std::string const syntaxes = path::join(path::join(bundlesDir, bundle->d_name), "Syntaxes");
			if(!path::is_directory(syntaxes))
				continue;
			for(auto const& file : path::entries(syntaxes))
			{
				plist::dictionary_t plist = plist::load(path::join(syntaxes, file->d_name));
				std::string scopeName;
				if(plist::get_key_path(plist, "scopeName", scopeName))
					map[scopeName] = index.add(bundles::kItemTypeGrammar, plist);
			}
		}
		index.commit();
		return map;
	}();
	return res;
}

// Concatenate repository files matching `ext` until we have `bytes` of input.
static std::string corpus (std::vector<std::string> const& dirs, std::string const& ext, size_t bytes)
{
	std::vector<std::string> files;
	std::function<void(std::string const&)> walk = [&](std::string const& dir){
		for(auto const& entry : path::entries(dir))
		{
			std::string const p = path::join(dir, entry->d_name);
			if(entry->d_type == DT_DIR)
				walk(p);
			else if(path::extension(p) == ext)
				files.push_back(p);
		}
	};
	for(auto const& dir : dirs)
		walk(path::join(kRepoRoot, dir));
	std::sort(files.begin(), files.end());

	std::string res;
	while(!files.empty() && res.size() < bytes)
	{
		for(auto const& file : files)
		{
			res += path::content(file);
			if(res.size() >= bytes)
				break;
		}
	}
	return res;
}

static void parse_all (std::string const& scopeName, std::string const& buf)
{
	auto it = grammars().find(scopeName);
	if(it == grammars().end())
	{
		OAK_WARN("Skipping: no grammar for " + scopeName + " (TextMate bundles not installed?)");
		return;
	}

	auto grammar = parse::parse_grammar(it->second);
	parse::stack_ptr state = grammar->seed();
	size_t lines = 0, scopeChanges = 0;
	for(std::string::size_type i = 0; i != buf.size(); ++lines)
	{
		auto eol = buf.find('\n', i);
		eol = eol != std::string::npos ? eol + 1 : buf.size();
		std::map<size_t, scope::scope_t> scopes;
		state = parse::parse(buf.data() + i, buf.data() + eol, state, scopes, i == 0);
		scopeChanges += scopes.size();
		i = eol;
	}
	OAK_ASSERT_GT(scopeChanges, lines / 4); // sanity: the grammar actually did something
	fprintf(stderr, "%s: %zu bytes, %zu lines, %zu scope changes\n", scopeName.c_str(), buf.size(), lines, scopeChanges);
}

void benchmark_parse_cxx_4mb ()
{
	static std::string const buf = corpus({ "Frameworks" }, ".cc", 4 << 20);
	parse_all("source.c++", buf);
}

void benchmark_parse_objcxx_2mb ()
{
	static std::string const buf = corpus({ "Frameworks", "Applications" }, ".mm", 2 << 20);
	parse_all("source.objc++", buf);
}

void benchmark_parse_markdown_2mb ()
{
	static std::string const buf = corpus({ "Applications", "Frameworks", "." }, ".md", 2 << 20);
	parse_all("text.html.markdown", buf);
}

void benchmark_parse_ruby_1mb ()
{
	static std::string const buf = corpus({ "bin", "vendor" }, ".rb", 1 << 20) + path::content(path::join(kRepoRoot, "bin/rave"));
	parse_all("source.ruby", buf);
}

void benchmark_parse_python_1mb ()
{
	static std::string const buf = corpus({ "vendor", "Frameworks" }, ".py", 1 << 20);
	parse_all("source.python", buf);
}

// One long minified line, the classic editor killer.
void benchmark_parse_long_line_json_1mb ()
{
	std::string buf = "[";
	while(buf.size() < (1 << 20))
		buf += text::format("{\"id\":%zu,\"name\":\"item %zu\",\"tags\":[\"a\",\"b\"],\"ok\":true},", buf.size(), buf.size());
	buf.back() = ']';
	parse_all("source.json", buf);
}
