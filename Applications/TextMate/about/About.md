Title: About TextMate
CSS: css/about.css

<header class="hero">
<img class="icon" src="images/icon.png" width="112" height="112" alt="">
<div>
<h1>TextMate</h1>
<p class="version">Version <span id="version"></span> <span class="badge">Opportunistic Labs build</span></p>
<p class="build secondary"><span id="build-info"></span> <button id="copy-build-info" type="button">Copy</button></p>
</div>
</header>
<script>
(function () {
	const t = TextMate;
	document.getElementById("version").textContent = t.version;
	const info = [ "Build " + t.build, t.buildDate, t.architecture, "macOS " + t.osVersion ].filter(s => s && !/undefined|null/.test(s));
	document.getElementById("build-info").textContent = info.join(" · ");
	const button = document.getElementById("copy-build-info");
	button.addEventListener("click", () => {
		t.copyText("TextMate " + t.version + " (Opportunistic Labs build)\n" + info.join("\n"));
		button.textContent = "Copied";
		setTimeout(() => button.textContent = "Copy", 1500);
	});
})();
</script>

## What’s new in this build

This copy of TextMate is built by [Opportunistic Labs](https://opportunisticlabs.com) from [github.com/dwchal/textmate](https://github.com/dwchal/textmate), a fork of the open source [TextMate](https://github.com/textmate/textmate) with these additions:

### Formatted Markdown

* **Edit the formatted text.** View → Show Formatted Markdown (<kbd class="keyEquivalent">⌥⌘E</kbd>), or the Source/Formatted button in the status bar, switches a Markdown document between its source and a formatted view in the same editor.
* **Same text, both ways.** Headings, bold, italic, strikethrough, inline code and links are styled and their markers hidden until the caret is in them. Switching views never changes the file.
* Tables, images and diagrams stay as source here; the preview below renders them.

### Markdown preview

* **Built in, no Ruby required.** Bundles → Markdown → Show Preview (⌃⌥⌘P) renders with MultiMarkdown when installed, including tables.
* **Opens beside the document** and keeps the section you are editing in view as you type.
* **Diagrams:** fenced ` ```mermaid ` blocks render as Mermaid diagrams.
* **Follows Dark Mode.**

### Language servers

* Errors and warnings from a [language server](https://microsoft.github.io/language-server-protocol/) appear in the gutter; click one to read it.
* The server’s suggestions come first in completion (⎋), including after `.`, `->`, and `::`.
* Off until enabled per file type in `.tm_properties`, for example:

		[ "source.c, source.c++, source.objc, source.objc++" ]
		languageServer = "xcrun clangd"

### Faster

* **Find in Folder** searches files in parallel: about 3× faster.
* **Syntax highlighting** parses 12–19% faster, and the app is compiled for speed rather than size.
* **Launching** loads the bundle index in the background, and the file browser no longer computes icons it never shows.
* **Choosers** (Go to File, symbols, bundle items) rank matches about twice as fast.
* **Lower energy use:** timers let macOS coalesce wake-ups.

### Other changes

* HTML output (previews, command output) is styled correctly again on current macOS.
* Software update checks are removed: update by rebuilding from the repository.
* **One app for Intel and Apple silicon,** built for macOS 10.13 and later (so far only run on macOS 27).
* **Rust:** scope selectors, fuzzy matching, glob parsing and ASCII transliteration are implemented in Rust, each checked against the code it replaced on millions of inputs.
* **Fixes:** line numbers, bookmarks and fold arrows are visible again on macOS 14 and later; text parsed right after opening is spell-checked; names such as `.gitignore` no longer upset sorting in Go to File.
* A new app icon: the gear, with a text cursor for an axle.

See the **Changes** tab for dates and details.

## Help and source

* [FAQ](https://github.com/textmate/textmate/wiki/FAQ) and [hidden settings](https://github.com/textmate/textmate/wiki/Hidden-Settings) on the TextMate wiki
* [Opportunistic Labs](https://opportunisticlabs.com), which maintains this build
* [Source code for this build](https://github.com/dwchal/textmate) and its [history](https://github.com/dwchal/textmate/commits/master)
* [Upstream TextMate](https://github.com/textmate/textmate)

<footer class="secondary">
TextMate is a trademark of Allan Odgaard, and the program is <span id="copyright"></span>.
Released under the GNU General Public License, version 3.
</footer>
<script>document.getElementById("copyright").textContent = TextMate.copyright;</script>
