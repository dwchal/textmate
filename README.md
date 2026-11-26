<p align="center">
  <img src="docs/images/logo.png" width="160" height="160" alt="TextMate icon: a gear with a text cursor for an axle">
</p>

<h1 align="center">TextMate</h1>

<p align="center">
  <strong>The Mac text editor, tuned up by <a href="https://opportunisticlabs.com">Opportunistic Labs</a>.</strong><br>
  Faster, partly rewritten in Rust, and built for Macs from 2017’s High Sierra to today, Intel or Apple silicon.
</p>

<p align="center">
  <a href="#whats-different">What’s different</a> ·
  <a href="#building">Building</a> ·
  <a href="#older-macos-and-intel">Older macOS and Intel</a> ·
  <a href="PERFORMANCE.md">Performance</a>
</p>

<p align="center">
  <img src="docs/images/editor.png" width="820" alt="The TextMate editor showing C++ source with syntax highlighting and line numbers">
</p>

This is a fork of [TextMate](https://github.com/textmate/textmate), the open source editor by Allan Odgaard and [many contributors](https://github.com/textmate/textmate/graphs/contributors). It keeps everything that makes TextMate what it is (bundles, snippets, scopes, `.tm_properties`) and adds the changes below. It is maintained independently of MacroMates.

## What’s different

| | |
|---|---|
| **Markdown preview, built in** | <kbd>⌃⌥⌘P</kbd> opens a live preview beside the document: tables, Mermaid diagrams, Dark Mode, and it follows the section you are editing. No Ruby required. |
| **Language servers** | Errors and warnings from a language server such as `clangd` in the gutter, and its suggestions first in <kbd>⎋</kbd> completion. Off until you enable it per file type. |
| **Faster** | Find in Folder searches files in parallel (about 3×), syntax parsing is 12–19% faster, fuzzy matching in the choosers is about 2× faster, and launch no longer waits for the bundle index. |
| **Rust inside** | Scope selectors, fuzzy matching, glob parsing and ASCII transliteration are written in Rust. Each port was compared with the C++ it replaced on millions of inputs, on both architectures, before the C++ was deleted. |
| **Old Macs welcome** | One app for Intel and Apple silicon, built for macOS 10.13 and later. It has so far only been run on macOS 27; see [below](#older-macos-and-intel). |
| **Bugs fixed** | Line numbers and bookmarks were invisible on macOS 14 and later; text parsed right after opening was never spell-checked; names like `.gitignore` broke sorting in the file chooser; and [more](Applications/TextMate/about/Changes.md). All 27 test suites pass on both architectures. |

<p align="center">
  <img src="docs/images/markdown-preview.png" width="400" alt="Markdown preview with a table and a Mermaid flow chart">
  &nbsp;&nbsp;
  <img src="docs/images/about.png" width="400" alt="The About window, showing version and build details">
</p>
<p align="center"><sub>The Markdown preview and the About window. Images are rendered from the app’s own views.</sub></p>

### Enabling a language server

Add this to a `.tm_properties` file in your project or home folder:

```
[ "source.c, source.c++, source.objc, source.objc++" ]
languageServer = "xcrun clangd"
```

## Getting it

There are no prebuilt downloads yet: build it from source as described below. Upstream TextMate is available from [macromates.com](https://macromates.com/download).

Questions about this fork go to [Opportunistic Labs](https://opportunisticlabs.com), not to MacroMates.

# Building

## Setup

To build TextMate, you need the following:

 * [boost][]            — portable C++ source libraries
 * [Cap’n Proto][capnp] — serialization library
 * [multimarkdown][]    — marked-up plain text compiler
 * [ninja][]            — build system similar to `make`
 * [ragel][]            — state machine compiler
 * [sparsehash][]       — a cache friendly `hash_map`
 * [Rust][]             — for the parts written in Rust (`cargo` must be in your `PATH` or `~/.cargo/bin`)

All this can be installed using either [Homebrew][] or [MacPorts][]:

```sh
# Homebrew
brew install boost capnp google-sparsehash multimarkdown ninja ragel

# MacPorts
sudo port install boost capnproto multimarkdown ninja ragel sparsehash
```

After installing dependencies, make sure you have a full checkout (including submodules) and then run `./configure` followed by `ninja`, for example:

```sh
git clone --recursive https://github.com/dwchal/textmate.git
cd textmate
./configure && ninja TextMate/run
```

The `./configure` script simply checks that all dependencies can be found, and then calls `bin/rave` to bootstrap a `build.ninja` file with default config set to `release` and default target set to `TextMate`.

## Older macOS and Intel

By default the app is built for the Mac you build on and requires macOS 12 or later (`APP_MIN_OS` in `default.rave`), which is the oldest version the current Xcode SDK supports.

To build one app that runs on both Intel and Apple silicon Macs, and on macOS back to 10.13 (High Sierra):

 1. Install Rust’s Intel target: `rustup target add x86_64-apple-darwin`.
 2. Find an SDK that still supports the old deployment target. `MinimumDeploymentTarget` in the SDK’s `SDKSettings.json` says how far back it goes: the macOS 26 SDK (for example `/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk`) supports 10.13, the macOS 27 SDK only 12.0.
 3. Build Cap’n Proto for both architectures and that target (Homebrew’s library is built for one architecture and the current macOS only):

    ```sh
    bin/build_capnp_universal 10.13 /Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk
    ```

 4. Put this in `local.rave`, with the paths the script printed, then run `./configure && ninja TextMate`:

    ```
    # default.rave has already composed its flags by the time local.rave is read,
    # so the deployment target is repeated here: the last one wins.
    set APP_MIN_OS  "10.13"
    set APP_SDK     "/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk"
    add FLAGS       "-mmacosx-version-min=${APP_MIN_OS} -isysroot ${APP_SDK}"
    add LN_FLAGS    "-mmacosx-version-min=${APP_MIN_OS} -isysroot ${APP_SDK}"
    add AC_FLAGS    "--minimum-deployment-target=${APP_MIN_OS}"
    add PLIST_FLAGS "-dAPP_MIN_OS='${APP_MIN_OS}'"

    add FLAGS     "-I${HOME}/build/textmate-deps/capnp-1.5.0-macos10.13/include"
    add FLAGS     "-I/opt/homebrew/include"
    set CAPNP_LIB "${HOME}/build/textmate-deps/capnp-1.5.0-macos10.13/lib/libcapnp.a"
    set KJ_LIB    "${HOME}/build/textmate-deps/capnp-1.5.0-macos10.13/lib/libkj.a"

    config release {
    	arch "arm64" "x86_64" {
    		add FLAGS    "-arch ${arch}"
    		add LN_FLAGS "-arch ${arch}"
    	}
    }
    ```

**Not yet tested on older systems.** The build has only been run on macOS 27 (Apple silicon natively, the Intel code under Rosetta). Support for older versions rests on the compiler and linker checks described here, so treat it as untested until it has been tried on the system you care about.

The Intel half is built for macOS 10.13 and the Apple silicon half for macOS 11, the first version that ran on Apple silicon. Using an API newer than `APP_MIN_OS` without an `@available` check is a compile error, and the linker warns about any object built for a newer system; both must stay at zero. In a universal build every test suite can also be run as Intel code under Rosetta, e.g. `ninja plist/test-x86_64`.

## Building from within TextMate

You should install the [Ninja][NinjaBundle] bundle which can be installed via _Preferences_ → _Bundles_.

After this you can press ⌘B to build from within TextMate. In case you haven't already you also need to set up the `PATH` variable either in _Preferences_ → _Variables_ or `~/.tm_properties` so it can find `ninja` and related tools; an example could be `$PATH:/usr/local/bin`.

The default target (set in `.tm_properties`) is `TextMate/run`. This will relaunch TextMate but when called from within TextMate, a dialog will appear before the current instance is killed. As there is full session restore, it is safe to relaunch even with unsaved changes.

If the current file is a test file then the target to build is changed to build the library to which the test belongs (this is done by setting `TM_NINJA_TARGET` in the `.tm_properties` file found in the root of the source tree).

Similarly, if the current file belongs to an application target (other than `TextMate.app`) then `TM_NINJA_TARGET` is set to build and run this application.

## Build Targets

For the `TextMate.app` application there are two symbolic build targets:

```sh
ninja TextMate      # Build and sign TextMate
ninja TextMate/run  # Build, sign, and (re)launch TextMate
```

To clean everything run:

```sh
ninja -t clean
```

Or simply delete `~/build/TextMate`.

# Legal

The source for TextMate is released under the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

TextMate is a trademark of Allan Odgaard. This fork is maintained by [Opportunistic Labs](https://opportunisticlabs.com) and is not affiliated with MacroMates.

[boost]:         http://www.boost.org/
[ninja]:         https://ninja-build.org/
[multimarkdown]: http://fletcherpenney.net/multimarkdown/
[ragel]:         http://www.complang.org/ragel/
[capnp]:         https://github.com/capnproto/capnproto.git
[MacPorts]:      http://www.macports.org/
[Homebrew]:      http://brew.sh/
[NinjaBundle]:   https://github.com/textmate/ninja.tmbundle
[sparsehash]:    https://code.google.com/p/sparsehash/
[Rust]:          https://rustup.rs/
