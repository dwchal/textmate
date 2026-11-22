// Test support for t_tm_file.mm (Objective-C classes can’t live inside the
// namespace gen_test wraps each test file in, so they are declared here).
#import <WebKit/WebKit.h>

static NSString* gTestJobPageHTML;

@interface TestJobProtocol : NSURLProtocol
@end

@implementation TestJobProtocol
+ (BOOL)canInitWithRequest:(NSURLRequest*)request                            { return [request.URL.scheme isEqualToString:@"x-test-job"]; }
+ (NSURLRequest*)canonicalRequestForRequest:(NSURLRequest*)request           { return request; }
+ (BOOL)requestIsCacheEquivalent:(NSURLRequest*)a toRequest:(NSURLRequest*)b { return NO; }
- (void)startLoading
{
	NSData* data = [gTestJobPageHTML dataUsingEncoding:NSUTF8StringEncoding];
	[self.client URLProtocol:self didReceiveResponse:[[NSURLResponse alloc] initWithURL:self.request.URL MIMEType:@"text/html" expectedContentLength:data.length textEncodingName:@"utf-8"] cacheStoragePolicy:NSURLCacheStorageNotAllowed];
	[self.client URLProtocol:self didLoadData:data];
	[self.client URLProtocolDidFinishLoading:self];
}
- (void)stopLoading { }
@end

@interface TestLoadWaiter : NSObject <WebFrameLoadDelegate>
@property (nonatomic) BOOL done;
@end

@implementation TestLoadWaiter
- (void)webView:(WebView*)sender didFinishLoadForFrame:(WebFrame*)frame                                  { if(frame == sender.mainFrame) _done = YES; }
- (void)webView:(WebView*)sender didFailLoadWithError:(NSError*)error forFrame:(WebFrame*)frame          { if(frame == sender.mainFrame) _done = YES; }
- (void)webView:(WebView*)sender didFailProvisionalLoadWithError:(NSError*)error forFrame:(WebFrame*)frame { if(frame == sender.mainFrame) _done = YES; }
@end
