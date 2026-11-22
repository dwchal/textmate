#include "../src/rewrite_file_urls.h"

static std::string rewrite (std::string const& str, size_t chunkSize = std::string::npos)
{
	oak::file_url_rewriter_t rewriter;
	std::string res;
	for(size_t i = 0; i < str.size(); i += chunkSize)
		res += rewriter(str.data() + i, std::min(chunkSize, str.size() - i));
	return res + rewriter.finish();
}

void test_rewrite_attributes ()
{
	OAK_ASSERT_EQ(rewrite("<link href=\"file:///a/style.css\">"), "<link href=\"tm-file:///a/style.css\">");
	OAK_ASSERT_EQ(rewrite("<img src='file:///a/b.png'>"),        "<img src='tm-file:///a/b.png'>");
	OAK_ASSERT_EQ(rewrite("background: url(file:///a/c.png);"),  "background: url(tm-file:///a/c.png);");
	OAK_ASSERT_EQ(rewrite("var image_path = \"file:///img/\";"), "var image_path = \"tm-file:///img/\";");
}

void test_leaves_other_urls_alone ()
{
	OAK_ASSERT_EQ(rewrite("<a href=\"txmt://open?url=file:///x\">"), "<a href=\"txmt://open?url=file:///x\">");
	OAK_ASSERT_EQ(rewrite("see file:///etc/hosts"),                   "see file:///etc/hosts");
	OAK_ASSERT_EQ(rewrite("\"files://x\" \"file:/x\""),               "\"files://x\" \"file:/x\"");
	OAK_ASSERT_EQ(rewrite("\"tm-file:///x\""),                        "\"tm-file:///x\"");
}

void test_chunk_boundaries ()
{
	std::string const html = "<link href=\"file:///a.css\"><script src='file:///b.js'></script> url(file:///c) \"file:/";
	std::string const expected = rewrite(html);
	OAK_ASSERT_EQ(expected, "<link href=\"tm-file:///a.css\"><script src='tm-file:///b.js'></script> url(tm-file:///c) \"file:/");
	for(size_t n = 1; n < html.size(); ++n)
		OAK_ASSERT_EQ(rewrite(html, n), expected);
}
