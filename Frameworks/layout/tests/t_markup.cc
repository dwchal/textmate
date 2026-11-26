#include <layout/layout.h>
#include <buffer/buffer.h>
#include <theme/theme.h>
#include <random>

// Formatted Markdown: markers are hidden and text is styled, but only in the
// layout. These tests check the geometry, that the layout follows edits, and
// above all that an incrementally updated layout equals one built from scratch.

namespace
{
	theme_ptr test_theme ()
	{
		// one per fixture: tests run concurrently and a theme caches styles without locking
		return std::make_shared<theme_t>(bundles::item_ptr());
	}

	struct fixture_t
	{
		fixture_t (std::string const& text, bool softWrap = false) : layout(buffer, test_theme(), "Menlo", 12, softWrap, false, softWrap ? 40 : 0)
		{
			layout.set_viewport_size(CGSizeMake(400, 400));
			buffer.insert(0, text);
			refresh();
		}

		void refresh ()                                { layout.update_metrics(CGRectMake(0, 0, 10000, 1000000)); }
		void format (ng::ranges_t const& selection)    { layout.set_markup(ng::markdown_spans(buffer.substr(0, buffer.size())), selection); refresh(); }
		void select (ng::ranges_t const& selection)    { layout.set_markup_selection(selection); refresh(); }
		CGFloat x (size_t index)                       { return CGRectGetMinX(layout.rect_at_index(index)); }
		CGFloat y (size_t index)                       { return CGRectGetMinY(layout.rect_at_index(index)); }

		std::vector<CGRect> geometry ()
		{
			refresh();
			std::vector<CGRect> res;
			std::string const text = buffer.substr(0, buffer.size());
			for(size_t i = 0; i <= text.size(); ++i)
			{
				if(i == text.size() || (text[i] & 0xC0) != 0x80)
					res.push_back(layout.rect_at_index(i));
			}
			res.push_back(CGRectMake(0, 0, layout.width(), layout.height()));
			return res;
		}

		ng::buffer_t buffer;
		ng::layout_t layout;
	};

	bool same (std::vector<CGRect> const& lhs, std::vector<CGRect> const& rhs)
	{
		if(lhs.size() != rhs.size())
			return false;
		for(size_t i = 0; i < lhs.size(); ++i)
		{
			if(fabs(lhs[i].origin.x - rhs[i].origin.x) > 0.01 || fabs(lhs[i].origin.y - rhs[i].origin.y) > 0.01 || fabs(lhs[i].size.height - rhs[i].size.height) > 0.01)
				return false;
		}
		return true;
	}

	ng::ranges_t caret (size_t index) { return ng::range_t(index); }
}

void test_markers_are_hidden_away_from_the_caret ()
{
	std::string const text = "one\na **b** c\n";
	fixture_t f(text);
	CGFloat const plainEnd = f.x(13);

	f.format(caret(0));
	OAK_ASSERT_EQ(f.layout.concealed().size(), 2);
	OAK_ASSERT_EQ(f.x(6), f.x(8));   // opening ** takes no room
	OAK_ASSERT_EQ(f.x(9), f.x(11));  // closing **
	OAK_ASSERT_LT(f.x(13), plainEnd);
	OAK_ASSERT_EQ(f.buffer.substr(0, f.buffer.size()), text);
}

void test_markers_show_when_the_caret_touches_the_element ()
{
	fixture_t f("one\na **b** c\n");
	f.format(caret(0));

	for(size_t index : { 6, 8, 9, 11 }) // start, inside and end of **b**
	{
		f.select(caret(index));
		OAK_ASSERT_EQ(f.layout.concealed().size(), 0);
		OAK_ASSERT_LT(f.x(6), f.x(8));
	}

	for(size_t index : { 0, 5, 12 })
	{
		f.select(caret(index));
		OAK_ASSERT_EQ(f.layout.concealed().size(), 2);
		OAK_ASSERT_EQ(f.x(6), f.x(8));
	}

	f.select(ng::range_t(0, 7)); // a selection reaching into the element
	OAK_ASSERT_EQ(f.layout.concealed().size(), 0);
}

void test_headings_are_larger_and_lose_their_marker ()
{
	fixture_t f("text\n## Heading\ntext\n");
	CGFloat const plainHeight = CGRectGetHeight(f.layout.rect_at_index(8));
	f.format(caret(0));
	OAK_ASSERT_EQ(f.x(5), f.x(8));
	OAK_ASSERT_LT(plainHeight, CGRectGetHeight(f.layout.rect_at_index(8)));
	OAK_ASSERT_EQ(CGRectGetHeight(f.layout.rect_at_index(0)), CGRectGetHeight(f.layout.rect_at_index(16)));

	f.select(caret(10));
	OAK_ASSERT_LT(f.x(5), f.x(8));
}

void test_turning_formatting_off_restores_the_plain_layout ()
{
	std::string const text = "# Title\n\nSome **bold**, *italic*, `code` and a [link](https://example.com).\n\n- item ~~gone~~\n";
	fixture_t f(text, true);
	auto const plain = f.geometry();

	f.format(caret(0));
	OAK_ASSERT(!same(plain, f.geometry()));
	f.layout.clear_markup();
	OAK_ASSERT(same(plain, f.geometry()));
	OAK_ASSERT_EQ(f.buffer.substr(0, f.buffer.size()), text);
}

void test_caret_movement_skips_hidden_markers ()
{
	fixture_t f("one\na **b** c\n");
	f.format(caret(0));
	OAK_ASSERT_EQ(f.layout.index_right_of(6).index, 8);
	OAK_ASSERT_EQ(f.layout.index_left_of(8).index, 6);
	OAK_ASSERT_EQ(f.layout.index_right_of(9).index, 11);
}

void test_clicking_lands_on_shown_text ()
{
	fixture_t f("one\na **b** c\n");
	f.format(caret(0));
	for(size_t index : { 4, 5, 8, 11, 12 })
	{
		CGRect r = f.layout.rect_at_index(index);
		size_t const clicked = f.layout.index_at_point(CGPointMake(CGRectGetMinX(r) + 1, CGRectGetMidY(r))).index;
		OAK_ASSERT(clicked == index || f.x(clicked) == f.x(index));
	}
}

void test_hidden_ranges_follow_edits ()
{
	fixture_t f("one\na **b** c\n");
	f.format(caret(0));

	f.buffer.insert(0, "xx");
	OAK_ASSERT_EQ(f.layout.concealed().size(), 2);
	OAK_ASSERT_EQ(f.layout.concealed().begin()->first, 8);
	f.refresh();
	OAK_ASSERT_EQ(f.x(8), f.x(10));

	f.buffer.erase(0, 2);
	OAK_ASSERT_EQ(f.layout.concealed().begin()->first, 6);

	f.buffer.insert(7, "!"); // inside the opening marker: it must show again
	OAK_ASSERT_EQ(f.layout.concealed().count(6), 0);
	f.refresh();
	OAK_ASSERT_LT(f.x(6), f.x(9));
}

void test_soft_wrap_counts_only_shown_text ()
{
	// 38 shown columns plus 8 hidden marker bytes: fits a 40 column line when formatted
	std::string const text = "one\n**aaaaaaaa** **bbbbbbbb** cccccccc dddddddd ee\n";
	fixture_t f(text, true);
	OAK_ASSERT_LT(f.y(4), f.y(text.size() - 2));
	f.format(caret(0));
	OAK_ASSERT_EQ(f.y(4), f.y(text.size() - 2));
}

void test_incremental_updates_match_a_fresh_layout ()
{
	static std::string const pieces[] = {
		"# ", "## ", "**", "*", "_", "`", "~~", "[", "](u)", "<", ">", "\n", "\n\n", " ", "word", "été", "- ", "> ", "```\n", "| a | b |\n", "\\", "1. ", "x", "日本"
	};

	std::mt19937 random(20261126);
	auto pick = [&](size_t n){ return n ? random() % n : 0; };

	for(size_t round = 0; round < 40; ++round)
	{
		bool const softWrap = round % 2;
		fixture_t f("# Start\n\nSome **bold** and *italic* text with `code` and [a link](https://example.com).\n", softWrap);
		f.format(caret(0));

		for(size_t step = 0; step < 60; ++step)
		{
			// positions on character boundaries
			std::vector<size_t> bounds;
			std::string const before = f.buffer.substr(0, f.buffer.size());
			for(size_t i = 0; i < before.size(); ++i)
			{
				if((before[i] & 0xC0) != 0x80)
					bounds.push_back(i);
			}
			bounds.push_back(before.size());

			size_t const a = bounds[pick(bounds.size())], b = bounds[pick(bounds.size())];
			size_t const from = std::min(a, b), to = std::min(std::max(a, b), from + 12);
			size_t const safeTo = *std::lower_bound(bounds.begin(), bounds.end(), to);

			switch(pick(4))
			{
				case 0: f.buffer.insert(from, pieces[pick(sizeof(pieces) / sizeof(pieces[0]))]); break;
				case 1: f.buffer.erase(from, safeTo); break;
				case 2: f.buffer.replace(from, safeTo, pieces[pick(sizeof(pieces) / sizeof(pieces[0]))]); break;
				case 3: break; // only move the selection
			}

			std::string const after = f.buffer.substr(0, f.buffer.size());
			size_t const size = after.size();
			size_t s1 = std::min(from, size), s2 = pick(3) == 0 ? std::min(safeTo, size) : s1;
			while(s1 && s1 < size && (after[s1] & 0xC0) == 0x80) --s1;
			while(s2 && s2 < size && (after[s2] & 0xC0) == 0x80) --s2;
			ng::ranges_t const selection = ng::range_t(s1, s2);

			if(pick(3) == 0)
					f.select(selection);  // analysis has not caught up with the edit yet
			else	f.format(selection);

			if(pick(4) == 0)
			{
				f.format(selection);
				fixture_t fresh(f.buffer.substr(0, f.buffer.size()), softWrap);
				fresh.format(selection);
				OAK_ASSERT(f.layout.concealed() == fresh.layout.concealed());
				OAK_ASSERT(same(f.geometry(), fresh.geometry()));
			}
		}
	}
}
