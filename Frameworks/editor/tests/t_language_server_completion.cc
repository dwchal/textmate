#include <editor/editor.h>

// ⎋ (next completion) asks the editor’s delegate for language server
// completions; they rank ahead of words from the buffer.

namespace
{
	struct stub_delegate_t : ng::editor_delegate_t
	{
		std::map<std::string, std::string> variables_for_bundle_item (bundles::item_ptr item) { return { }; }

		std::vector<std::string> language_server_completions (text::pos_t const& pos, std::string const& prefix)
		{
			requests.emplace_back(std::string(pos) + " " + prefix);
			std::vector<std::string> res;
			for(std::string const& word : { "alpha", "alphabet", "beta" })
			{
				if(word.find(prefix) == 0)
					res.push_back(word);
			}
			return res;
		}

		std::vector<std::string> requests;
	};
}

void test_server_completions_come_first ()
{
	ng::buffer_t buf;
	ng::editor_t editor(buf);
	stub_delegate_t delegate;
	editor.set_delegate(&delegate);

	editor.insert("alpine\nw.al");
	editor.perform(ng::kNextCompletion);
	OAK_ASSERT_EQ(editor.as_string(), "alpine\nw.alpha"); // not “alpine” from the buffer
	OAK_ASSERT_EQ(delegate.requests.size(), 1);
	OAK_ASSERT_EQ(delegate.requests.front(), "2:5 al"); // position where the word being completed ends
}

void test_server_completions_after_member_access ()
{
	ng::buffer_t buf;
	ng::editor_t editor(buf);
	stub_delegate_t delegate;
	editor.set_delegate(&delegate);

	editor.insert("w.");
	editor.perform(ng::kNextCompletion);
	OAK_ASSERT_EQ(editor.as_string(), "w.alpha");
	OAK_ASSERT_EQ(delegate.requests.front(), "1:3 "); // asked with an empty prefix, after the “.”

	editor.insert("\np->");
	editor.perform(ng::kNextCompletion);
	OAK_ASSERT_EQ(editor.as_string(), "w.alpha\np->alpha");
}

void test_no_server_request_without_prefix ()
{
	ng::buffer_t buf;
	ng::editor_t editor(buf);
	stub_delegate_t delegate;
	editor.set_delegate(&delegate);

	editor.insert("alpha ");
	editor.perform(ng::kNextCompletion);
	OAK_ASSERT_EQ(editor.as_string(), "alpha ");
	OAK_ASSERT(delegate.requests.empty());
}
