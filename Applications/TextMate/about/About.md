Title: About TextMate

## TextMate version <script>document.write(TextMate.version)</script>

The manual is a work in progress and can be found at <https://macromates.com/textmate/manual/>. The MacroMates Blog has a [TextMate 2 category](https://blog.macromates.com/categories/textmate-2/).

There is a [FAQ](https://github.com/textmate/textmate/wiki/FAQ) and [hidden settings](https://github.com/textmate/textmate/wiki/Hidden-Settings) page.

For comments, questions, and general feedback see <https://macromates.com/support>

_TextMate is a trademark of Allan Odgaard and the program is <script>document.write(TextMate.copyright)</script>._

## This build: DC Productions

This is a fork of TextMate maintained by DC Productions, built from <https://github.com/dwchal/textmate>. It tracks upstream TextMate and layers in the following changes:

* **New app icon** — a gear/cog design, replacing the placeholder icon shipped in the public `document-icons` repo (which can't redistribute the official trademarked artwork).
* **Faster app launch** — the bundles index cache is now loaded on a background queue starting at process launch, instead of synchronously on the main thread during `applicationWillFinishLaunching:`, so it overlaps with AppKit start-up rather than adding to it.
* **Faster file browser** — directory listings no longer prefetch `NSURLEffectiveIconKey` for every entry. That value is the most expensive one to compute (it invokes LaunchServices/QuickLook per file) and was never actually used for rendering file browser icons, so dropping it speeds up opening and restoring projects with large folder trees.
* **Lower energy use** — timers throughout the app (caret blink, autosave, SCM status polling, and others) now declare a tolerance so macOS can coalesce their wake-ups instead of waking the CPU for each one individually, per Apple's Energy Efficiency Guide.

See the Changes tab for upstream TextMate release notes.
