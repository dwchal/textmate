# Moving HTML output to WKWebView

HTML output (command output, previews, help) still uses the legacy `WebView`.
It is deprecated but works on current macOS. Since the tm-file fix
(OakCommand/src/rewrite_file_urls.h), migrating no longer fixes anything users
can see today; it is future-proofing. This note records what a migration
involves, so it can be scheduled deliberately.

## What depends on legacy WebView

| Piece | Where | WKWebView replacement |
|---|---|---|
| Streaming command output (`x-txmt-filehandle://job/…` NSURLProtocol) | OakCommand.mm | `WKURLSchemeHandler` (supports streaming `didReceiveData:`) |
| `tm-file://` resources, main-resource redirect to `file://` | HOWebViewDelegateHelper.mm | `WKURLSchemeHandler` + `decidePolicyForNavigationAction` |
| `TextMate` JS object: `system`, `log`, `openFile`, `isBusy`, `progress` | HOJSBridge.mm (WebScriptObject) | `WKUserScript` shim + `WKScriptMessageHandlerWithReply` |
| `TextMate.system(cmd, null)`, **synchronous**, returns `outputString`/`status` | HOJSBridge.mm | No direct equivalent. Workable: route through `window.prompt()` and answer in `WKUIDelegate runJavaScriptTextInputPanelWithPrompt…` (blocks the page until replied) |
| Process objects: `onreadoutput`, `write`, `close`, `cancel` | HOJSBridge.mm | Message handler + callbacks by id |
| Auto-scroll while output streams | HOAutoScroll.mm (scrolls the document view) | JS `scrollTo` driven from the scheme handler |
| Find next/previous, copy to find pasteboard | WebView Additions.mm (DOM ranges) | `WKWebView findString:` (macOS 13+) / JS `window.find` |
| Scroll restore after refresh, `textmate-scroll` opt-out | OakHTMLOutputView.mm | `evaluateJavaScript` before/after load |
| Printing | HOBrowserView | `printOperationWithPrintInfo:` |
| About window, document window HTML views | AboutWindowController.mm, DocumentWindowController.mm | Straight swap |

About 1,350 lines in Frameworks/HTMLOutput plus the call sites above.

## Bundle impact

In the installed bundles, `TextMate.system` is called 36 times across 15 files
(Bundle Support, Git, Mercurial, Subversion, SQL, Ruby and Python), and 17 of
those calls are synchronous. `TextMate.isBusy` appears 39 times. A migration
has to keep both working: shim the synchronous form via `prompt()` and test the
Git, SQL and Subversion commands by hand, because they have no automated
coverage.

## Recommendation

Defer until Apple announces a removal date for `WebView`, or until something
requires WKWebView, such as needing its process isolation for untrusted HTML.
The tests in tests/t_tm_file.mm describe the behaviour a port must keep.
