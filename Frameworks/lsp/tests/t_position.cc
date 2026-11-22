#include <lsp/position.h>

// "aé😀b": a (1 byte, 1 unit), é (2 bytes, 1 unit), 😀 (4 bytes, 2 units), b
static std::string const kLine = "a\xC3\xA9\xF0\x9F\x98\x80" "b";

void test_utf16_to_utf8 ()
{
	OAK_ASSERT_EQ(lsp::utf16_to_utf8_column(kLine, 0), 0);
	OAK_ASSERT_EQ(lsp::utf16_to_utf8_column(kLine, 1), 1);
	OAK_ASSERT_EQ(lsp::utf16_to_utf8_column(kLine, 2), 3);
	OAK_ASSERT_EQ(lsp::utf16_to_utf8_column(kLine, 4), 7);
	OAK_ASSERT_EQ(lsp::utf16_to_utf8_column(kLine, 5), 8);
	OAK_ASSERT_EQ(lsp::utf16_to_utf8_column(kLine, 99), 8);
	OAK_ASSERT_EQ(lsp::utf16_to_utf8_column("", 3), 0);
}

void test_utf8_to_utf16 ()
{
	OAK_ASSERT_EQ(lsp::utf8_to_utf16_column(kLine, 0), 0);
	OAK_ASSERT_EQ(lsp::utf8_to_utf16_column(kLine, 1), 1);
	OAK_ASSERT_EQ(lsp::utf8_to_utf16_column(kLine, 3), 2);
	OAK_ASSERT_EQ(lsp::utf8_to_utf16_column(kLine, 7), 4);
	OAK_ASSERT_EQ(lsp::utf8_to_utf16_column(kLine, 8), 5);
}

void test_round_trip ()
{
	for(size_t units : { 0, 1, 2, 4, 5 })
		OAK_ASSERT_EQ(lsp::utf8_to_utf16_column(kLine, lsp::utf16_to_utf8_column(kLine, units)), units);
}
