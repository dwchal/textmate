#import <OakTextView/OakDocumentView.h>
#import <OakTextView/GutterView.h>
#import <document/OakDocument.h>

// Renders a document view offscreen and checks that the gutter’s line numbers
// are visible. Since macOS 14 views do not clip to their bounds by default, and
// the 1 pt divider next to the gutter used to fill its whole dirty rect, which
// painted over the entire gutter (no line numbers, bookmarks, or marks).

static GutterView* FindGutter (NSView* view)
{
	if([view isKindOfClass:[GutterView class]])
		return (GutterView*)view;
	for(NSView* subview in view.subviews)
	{
		if(GutterView* res = FindGutter(subview))
			return res;
	}
	return nil;
}

void test_gutter_line_numbers_are_visible ()
{
	dispatch_sync(dispatch_get_main_queue(), ^{
		OakDocument* doc = [OakDocument documentWithString:@"one\ntwo\nthree\nfour\nfive\n" fileType:@"text.plain" customName:@"gutter test"];
		NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 400, 300) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
		OakDocumentView* documentView = [[OakDocumentView alloc] initWithFrame:NSMakeRect(0, 0, 400, 300)];
		window.contentView = documentView;
		documentView.document = doc;
		[window layoutIfNeeded];
		[documentView layoutSubtreeIfNeeded];

		GutterView* gutter = FindGutter(documentView);
		OAK_ASSERT(gutter != nil);
		NSRect gutterRect = [gutter convertRect:gutter.visibleRect toView:documentView];
		OAK_ASSERT_GT(NSWidth(gutterRect), 10);

		NSBitmapImageRep* rep = [documentView bitmapImageRepForCachingDisplayInRect:documentView.bounds];
		[documentView cacheDisplayInRect:documentView.bounds toBitmapImageRep:rep];

		CGFloat const scale  = rep.pixelsWide / NSWidth(documentView.bounds);
		NSInteger const maxX = (NSInteger)(scale * NSWidth(gutterRect)) - 1;
		NSInteger const maxY = (NSInteger)(scale * 80); // the first few lines, measured from the top

		CGFloat const background = [[rep colorAtX:1 y:maxY + 40] colorUsingColorSpace:NSColorSpace.sRGBColorSpace].brightnessComponent;
		NSUInteger contrasting = 0;
		for(NSInteger x = 0; x < maxX; ++x)
		{
			for(NSInteger y = 0; y < maxY; ++y)
			{
				if(std::abs([[rep colorAtX:x y:y] colorUsingColorSpace:NSColorSpace.sRGBColorSpace].brightnessComponent - background) > 0.2)
					++contrasting;
			}
		}
		OAK_ASSERT_GT(contrasting, 20);

		[doc close];
	});
}
