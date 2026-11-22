#import "../src/browser/HOWebViewDelegateHelper.h"
#import <WebKit/WebKit.h>
#import <ns/ns.h>
#import "support/job_page.h"

// Command output is served from a custom scheme registered as local
// (x-txmt-filehandle://job/…), which gives the page an opaque origin. WebKit
// won't load file:// resources for such pages, so command output rewrites them
// to tm-file:// (see OakCommand/src/rewrite_file_urls.h). This checks that
// tm-file resources actually load in that situation.

static NSString* evaluate_in_job_page (NSString* html, NSString* script)
{
	static dispatch_once_t once;
	dispatch_once(&once, ^{
		[NSURLProtocol registerClass:[TestJobProtocol class]];
		[WebView registerURLSchemeAsLocal:@"x-test-job"]; // like x-txmt-filehandle
	});

	gTestJobPageHTML = html;
	__block NSString* res;
	dispatch_sync(dispatch_get_main_queue(), ^{
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
