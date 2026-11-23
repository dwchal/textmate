Title: About TextMate
CSS: css/about.css

<header class="hero">
<img class="icon" src="images/icon.png" width="112" height="112" alt="">
<div>
<h1>TextMate</h1>
<p class="version">Version <span id="version"></span> <span class="badge">DC Productions build</span></p>
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
		t.copyText("TextMate " + t.version + " (DC Productions build)\n" + info.join("\n"));
		button.textContent = "Copied";
		setTimeout(() => button.textContent = "Copy", 1500);
	});
})();
</script>

## What’s new in this build

This copy of TextMate is built from [github.com/dwchal/textmate](https://github.com/dwchal/textmate), a fork of the open source [TextMate](https://github.com/textmate/textmate) with these additions:

### Markdown preview

* **Built in, no Ruby required.** Bundles → Markdown → Show Preview (⌃⌥⌘P) renders with MultiMarkdown when installed, including tables.
* **Opens beside the document** and keeps the section you are editing in view as you type.
* **Diagrams:** fenced ` ```mermaid ` blocks render as Mermaid diagrams.
* **Follows Dark Mode.**

### Language servers

* Errors and warnings from a [language server](https://microsoft.github.io/language-server-protocol/) appear in the gutter; click one to read it.
* The server’s suggestions come first in completion (⎋), including after `.`, `->`, and `::`.
* Off until enabled per file type in `.tm_properties`, for example:

		[ source.c, source.c++, source.objc, source.objc++ ]
		languageServer = "xcrun clangd"

### Faster

* **Find in Folder** searches files in parallel: about 3× faster.
* **Syntax highlighting** parses 12–19% faster, and the app is compiled for speed rather than size.
* **Launching** loads the bundle index in the background, and the file browser no longer computes icons it never shows.
* **Lower energy use:** timers let macOS coalesce wake-ups.

### Other changes

* HTML output (previews, command output) is styled correctly again on current macOS.
* Software update checks are removed: update by rebuilding from the repository.
* Scope selectors, used by themes, bundles, and settings, are implemented in Rust.
* An original gear app icon.

See the **Changes** tab for dates and details.

## Help and source

* [FAQ](https://github.com/textmate/textmate/wiki/FAQ) and [hidden settings](https://github.com/textmate/textmate/wiki/Hidden-Settings) on the TextMate wiki
* [Source code for this build](https://github.com/dwchal/textmate) and its [history](https://github.com/dwchal/textmate/commits/perf-and-features)
* [Upstream TextMate](https://github.com/textmate/textmate)

<footer class="secondary">
TextMate is a trademark of Allan Odgaard, and the program is <span id="copyright"></span>.
Released under the GNU General Public License, version 3.
</footer>
<script>document.getElementById("copyright").textContent = TextMate.copyright;</script>
