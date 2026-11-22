#include <bundles/bundles.h>
#include <bundles/load.h>
#include <test/jail.h>
#include <io/path.h>

// Applications/TextMate/support/MarkdownPreview/install-override writes a delta
// bundle to ~/Library/Application Support/TextMate/Bundles. Check that loading
// it alongside the (managed) Markdown bundle swaps in the new command while
// keeping the rest of the original item.

static std::string const kRepoRoot = path::parent(path::parent(path::parent(path::parent(__FILE__))));

void test_markdown_preview_override ()
{
	test::jail_t jail;

	jail.set_content("Managed/Bundles/Markdown.tmbundle/info.plist",
		"{ name = 'Markdown'; uuid = 'D99E8C0C-792F-11D9-A212-000D93B3A10E'; }");
	jail.set_content("Managed/Bundles/Markdown.tmbundle/Commands/Markdown preview.plist",
		"{	name = 'Show Preview';"
		"	uuid = 'B52DEA16-8480-11D9-BE59-000D93B3A10E';"
		"	command = '#!/usr/bin/env ruby18 -wKU\nputs 1\n';"
		"	scope = 'text.html.markdown';"
		"	keyEquivalent = '^~@p';"
		"	outputFormat = 'html';"
		"}");

	std::string const script = path::join(kRepoRoot, "Applications/TextMate/support/MarkdownPreview/install-override");
	std::string const cmd = "HOME='" + jail.path("home") + "' '" + script + "' >/dev/null";
	OAK_ASSERT_EQ(system(cmd.c_str()), 0);

	std::string const userBundles = jail.path("home/Library/Application Support/TextMate/Bundles");
	OAK_ASSERT(path::exists(path::join(userBundles, "Markdown.tmbundle/Commands/Markdown preview.plist")));

	plist::cache_t cache;
	auto const index = create_bundle_index({ userBundles, jail.path("Managed/Bundles") }, cache);

	bundles::item_ptr preview;
	for(auto const& item : index.first)
	{
		if(item->uuid() == "B52DEA16-8480-11D9-BE59-000D93B3A10E")
			preview = item;
	}

	OAK_ASSERT(preview);
	OAK_ASSERT_EQ(preview->name(), "Show Preview");
	OAK_ASSERT_EQ(preview->value_for_field(bundles::kFieldKeyEquivalent), "^~@p");

	std::string command;
	OAK_ASSERT(plist::get_key_path(preview->plist(), "command", command));
	OAK_ASSERT(command.find("SharedSupport/MarkdownPreview/preview") != std::string::npos);
	OAK_ASSERT(command.find("ruby18") == std::string::npos);

	// Running it again must not fail (idempotent).
	OAK_ASSERT_EQ(system(cmd.c_str()), 0);
}
