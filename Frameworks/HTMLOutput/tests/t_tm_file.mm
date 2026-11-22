#import "../src/browser/HOWebViewDelegateHelper.h"
#import <WebKit/WebKit.h>
#import <ns/ns.h>
#import <io/path.h>
#import "support/job_page.h"
#import "../../OakCommand/src/rewrite_file_urls.h"

// Command output is served from a custom scheme registered as local
// (x-txmt-filehandle://job/…), which gives the page an opaque origin. WebKit
// won't load file:// resources for such pages, so command output rewrites them
// to tm-file:// (see OakCommand/src/rewrite_file_urls.h). This checks that
// tm-file resources actually load in that situation.

static NSString* evaluate_in_job_page (NSString* html, NSString* script, NSString* waitFor = nil)
{
	static dispatch_once_t once;
	dispatch_once(&once, ^{
		[NSURLProtocol registerClass:[TestJobProtocol class]];
		[WebView registerURLSchemeAsLocal:@"x-test-job"]; // like x-txmt-filehandle
	});

	__block NSString* res;
	dispatch_sync(dispatch_get_main_queue(), ^{ // tests run concurrently; this serializes them on the main thread
		gTestJobPageHTML = html;
		[NSApplication sharedApplication];
		WebView* webView = [[WebView alloc] initWithFrame:NSMakeRect(0, 0, 400, 400)];
		HOWebViewDelegateHelper* helper = [HOWebViewDelegateHelper new];
		TestLoadWaiter* waiter = [TestLoadWaiter new];
		webView.resourceLoadDelegate = helper;
		webView.frameLoadDelegate    = waiter;
		[webView.mainFrame loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:[NSString stringWithFormat:@"x-test-job://job/%u", arc4random()]]]];

		NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:10];
		while(!waiter.done && [deadline timeIntervalSinceNow] > 0)
			[NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		while(waitFor && ![[webView stringByEvaluatingJavaScriptFromString:waitFor] isEqualToString:@"true"] && [deadline timeIntervalSinceNow] > 0)
			[NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		res = [webView stringByEvaluatingJavaScriptFromString:script];
	});
	return res;
}

static NSString* write_fixture (NSString* name, NSData* data)
{
	NSString* dir = [NSTemporaryDirectory() stringByAppendingPathComponent:@"t_tm_file"];
	[NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
	NSString* path = [dir stringByAppendingPathComponent:name];
	[data writeToFile:path atomically:YES];
	return path;
}

void test_tm_file_resources_load_in_job_page ()
{
	NSString* css = write_fixture(@"style.css", [@"body { color: rgb(1, 2, 3); }" dataUsingEncoding:NSUTF8StringEncoding]);
	NSString* js  = write_fixture(@"script.js", [@"var loadedScript = 'yes';" dataUsingEncoding:NSUTF8StringEncoding]);
	NSData* gif = [[NSData alloc] initWithBase64EncodedString:@"R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7" options:0]; // 1×1 GIF
	NSString* img = write_fixture(@"pixel.gif", gif);

	NSString* html = [NSString stringWithFormat:@"<html><head><link rel='stylesheet' href='tm-file://%@'><script src='tm-file://%@'></script></head><body><img id='i' src='tm-file://%@'></body></html>", css, js, img];
	NSString* result = evaluate_in_job_page(html, @"[getComputedStyle(document.body).color, typeof loadedScript === 'undefined' ? 'no' : loadedScript, document.getElementById('i').naturalWidth].join(' | ')");
	OAK_ASSERT_EQ(to_s(result), "rgb(1, 2, 3) | yes | 1");
}

void test_file_resources_are_blocked_in_job_page ()
{
	// Documents why the rewrite exists; if this starts loading, the rewrite is no longer needed.
	NSString* css = write_fixture(@"style2.css", [@"body { color: rgb(4, 5, 6); }" dataUsingEncoding:NSUTF8StringEncoding]);
	NSString* html = [NSString stringWithFormat:@"<html><head><link rel='stylesheet' href='file://%@'></head><body></body></html>", css];
	OAK_ASSERT_NE(to_s(evaluate_in_job_page(html, @"getComputedStyle(document.body).color")), "rgb(4, 5, 6)");
}

// The Markdown preview (Applications/TextMate/support/MarkdownPreview) as it is
// shown the first time: streamed through the file:// → tm-file:// rewrite into a
// job page. Relative images resolve against the document, Mermaid renders.
void test_markdown_preview_in_job_page ()
{
	std::string const preview = path::join(path::parent(path::parent(path::parent(path::parent(__FILE__)))), "Applications/TextMate/support/MarkdownPreview/preview");
	std::string const multimarkdown = "/opt/homebrew/bin/multimarkdown";
	std::string const mermaidCache = path::join(path::home(), "Library/Caches/com.macromates.TextMate/mermaid-11.4.1.min.js");
	if(!path::exists(multimarkdown) || !path::exists(mermaidCache))
		return OAK_WARN("Skipping: needs " + multimarkdown + " and a cached " + mermaidCache + " (open a preview with a diagram once)");

	NSData* gif = [[NSData alloc] initWithBase64EncodedString:@"R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7" options:0];
	write_fixture(@"pixel.gif", gif);
	NSString* doc = write_fixture(@"doc.md", [@"# One\n\n![p](pixel.gif)\n\n## Two\n\n```mermaid\ngraph TD; A-->B\n```\n" dataUsingEncoding:NSUTF8StringEncoding]);

	std::string const cmd = "TM_MARKDOWN=" + multimarkdown + " TM_LINE_NUMBER=5 TM_FILENAME=doc.md TM_FILEPATH='" + to_s(doc) + "' '" + preview + "' < '" + to_s(doc) + "'";
	std::string output;
	if(FILE* fp = popen(cmd.c_str(), "r"))
	{
		char buf[4096];
		while(size_t len = fread(buf, 1, sizeof(buf), fp))
			output.append(buf, len);
		pclose(fp);
	}
	OAK_ASSERT_NE(output.find("<h1"), std::string::npos);

	oak::file_url_rewriter_t rewrite;
	std::string html = rewrite(output.data(), output.size()) + rewrite.finish();
	OAK_ASSERT_EQ(html.find("'file://"), std::string::npos);

	NSString* result = evaluate_in_job_page(to_ns(html),
		@"[document.querySelector('img').naturalWidth, document.querySelectorAll('div.mermaid svg').length, !!document.getElementById('tm-caret-marker'), getComputedStyle(document.querySelector('h1')).borderBottomStyle].join(' | ')",
		@"document.querySelectorAll('div.mermaid svg').length > 0");
	OAK_ASSERT_EQ(to_s(result), "1 | 1 | true | solid");
}
