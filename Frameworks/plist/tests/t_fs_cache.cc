#include <plist/fs_cache.h>
#include <io/io.h>

// A folder that did not exist when the cache was saved (e.g. the user’s
// ~/Library/Application Support/TextMate/Bundles, created while TextMate was
// not running) stays “missing” in a loaded cache until it is reloaded.
void test_reload_finds_folder_created_after_save ()
{
	std::string const root  = path::resolve(path::temp("fs_cache_test"));
	std::string const dir   = path::join(root, "Bundles");
	std::string const index = path::join(root, "index.binary");
	path::make_dir(root);

	plist::cache_t before;
	OAK_ASSERT(before.entries(dir).empty());
	before.save_capnp(index);

	path::make_dir(dir);
	path::make_dir(path::join(dir, "Markdown.tmbundle"));

	plist::cache_t after;
	after.load_capnp(index);
	OAK_ASSERT(after.entries(dir).empty()); // stale: what TextMate saw at launch

	OAK_ASSERT(after.reload(dir));
	OAK_ASSERT_EQ(after.entries(dir), std::vector<std::string>{ path::join(dir, "Markdown.tmbundle") });
	OAK_ASSERT(!after.reload(dir)); // nothing changed since

	path::remove(root);
}
