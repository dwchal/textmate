#import <OakTextView/OakDocumentView.h>
#import <OakTextView/OakTextView.h>
#import <document/OakDocument.h>

// Formatted Markdown in the real text view: toggling must not change the
// document, typing must edit the same text as in source view, and the two
// views must actually look different. Set TM_TEST_RENDER_DIR to keep the
// rendered images for inspection.

static NSString* const kSample =
	@"# Release checklist\n"
	@"\n"
	@"A short **Markdown** document with *emphasis*, `inline code`, ~~old text~~\n"
	@"and a [link to the site](https://opportunisticlabs.com).\n"
	@"\n"
	@"## Steps\n"
	@"\n"
	@"- Build the **universal** app\n"
	@"- Run the tests on *Intel*\n"
	@"\n"
	@"### Notes\n"
	@"\n"
	@"Plain text with 2 * 3 * 4 and snake_case_names stays as it is.\n";

static std::string Text (NSString* str) { return str.UTF8String ?: ""; }

static NSBitmapImageRep* Render (NSView* view, NSString* name)
{
	[view.window layoutIfNeeded];
	[view layoutSubtreeIfNeeded];
	[view displayIfNeeded];
	NSBitmapImageRep* rep = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
	[view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
	if(char const* dir = getenv("TM_TEST_RENDER_DIR"))
		[[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{ }] writeToFile:[[NSString stringWithUTF8String:dir] stringByAppendingPathComponent:name] atomically:YES];
	return rep;
}

static NSUInteger Difference (NSBitmapImageRep* lhs, NSBitmapImageRep* rhs)
{
	NSData* a = [lhs representationUsingType:NSBitmapImageFileTypeBMP properties:@{ }];
	NSData* b = [rhs representationUsingType:NSBitmapImageFileTypeBMP properties:@{ }];
	if(a.length != b.length)
		return NSUIntegerMax;

	NSUInteger res = 0;
	uint8_t const* p = (uint8_t const*)a.bytes;
	uint8_t const* q = (uint8_t const*)b.bytes;
	for(NSUInteger i = 0; i < a.length; ++i)
		res += p[i] != q[i];
	return res;
}

static void WithDocumentView (void(^block)(OakDocumentView* documentView, OakDocument* doc))
{
	dispatch_sync(dispatch_get_main_queue(), ^{
		BOOL const saved = [NSUserDefaults.standardUserDefaults boolForKey:kUserDefaultsFormattedMarkdownKey];
		[NSUserDefaults.standardUserDefaults setBool:NO forKey:kUserDefaultsFormattedMarkdownKey];

		OakDocument* doc = [OakDocument documentWithString:kSample fileType:@"text.html.markdown" customName:@"formatted test"];
		NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 720, 460) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
		OakDocumentView* documentView = [[OakDocumentView alloc] initWithFrame:NSMakeRect(0, 0, 720, 460)];
		window.contentView = documentView;
		documentView.document = doc;
		documentView.textView.softWrap = YES;
		[window makeFirstResponder:documentView.textView];

		// The first pass settles the view’s size and the gutter; rows are only measured once visible
		Render(documentView, @"markdown-source.png");
		[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];

		block(documentView, doc);

		[doc close];
		[NSUserDefaults.standardUserDefaults setBool:saved forKey:kUserDefaultsFormattedMarkdownKey];
	});
}

void test_toggling_formatted_markdown_keeps_the_text ()
{
	WithDocumentView(^(OakDocumentView* documentView, OakDocument* doc){
		OakTextView* textView = documentView.textView;
		OAK_ASSERT(textView.isMarkdownDocument);
		OAK_ASSERT(!textView.isFormattedMarkdown);

		NSBitmapImageRep* source = Render(documentView, @"markdown-source.png");

		[textView toggleFormattedMarkdown:nil];
		OAK_ASSERT(textView.isFormattedMarkdown);
		OAK_ASSERT_EQ(Text(doc.content), Text(kSample));
		OAK_ASSERT(!doc.isDocumentEdited);

		NSBitmapImageRep* formatted = Render(documentView, @"markdown-formatted.png");
		OAK_ASSERT_GT(Difference(source, formatted), 2000);

		[textView toggleFormattedMarkdown:nil];
		OAK_ASSERT(!textView.isFormattedMarkdown);
		OAK_ASSERT_EQ(Text(doc.content), Text(kSample));
		OAK_ASSERT_EQ(Difference(source, Render(documentView, @"markdown-source-again.png")), 0);
	});
}

void test_typing_in_formatted_markdown_edits_the_source ()
{
	WithDocumentView(^(OakDocumentView* documentView, OakDocument* doc){
		OakTextView* textView = documentView.textView;
		[textView toggleFormattedMarkdown:nil];

		// caret at the very start, i.e. on the heading: its marker is shown and typing goes before it
		[(id <NSTextInputClient>)textView insertText:@"X" replacementRange:NSMakeRange(NSNotFound, 0)];
		OAK_ASSERT_EQ(Text(doc.content), "X" + Text(kSample));
		[textView deleteBackward:nil];
		OAK_ASSERT_EQ(Text(doc.content), Text(kSample));

		// move into the paragraph and type Markdown: it is stored as typed
		[textView moveDown:nil];
		[textView moveDown:nil];
		[textView moveToEndOfLine:nil];
		[(id <NSTextInputClient>)textView insertText:@" and **new**" replacementRange:NSMakeRange(NSNotFound, 0)];
		OAK_ASSERT(Text(doc.content).find("~~old text~~ and **new**\n") != std::string::npos);
		Render(documentView, @"markdown-formatted-editing.png");

		// the same edits give the same text in source view
		std::string const formattedResult = Text(doc.content);
		[textView performSelector:@selector(undo:) withObject:nil];
		while(Text(doc.content) != Text(kSample) && doc.isDocumentEdited)
			[textView performSelector:@selector(undo:) withObject:nil];
		OAK_ASSERT_EQ(Text(doc.content), Text(kSample));

		[textView toggleFormattedMarkdown:nil];
		[textView moveToBeginningOfDocument:nil];
		[textView moveDown:nil];
		[textView moveDown:nil];
		[textView moveToEndOfLine:nil];
		[(id <NSTextInputClient>)textView insertText:@" and **new**" replacementRange:NSMakeRange(NSNotFound, 0)];
		OAK_ASSERT_EQ(Text(doc.content), formattedResult);
	});
}

void test_formatted_markdown_is_only_for_markdown ()
{
	dispatch_sync(dispatch_get_main_queue(), ^{
		OakDocument* doc = [OakDocument documentWithString:@"int **p = &q;\n" fileType:@"source.c" customName:@"not markdown"];
		NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 400, 200) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
		OakDocumentView* documentView = [[OakDocumentView alloc] initWithFrame:NSMakeRect(0, 0, 400, 200)];
		window.contentView = documentView;
		documentView.document = doc;

		OAK_ASSERT(!documentView.textView.isMarkdownDocument);
		[documentView.textView toggleFormattedMarkdown:nil];
		OAK_ASSERT(!documentView.textView.isFormattedMarkdown);
		[doc close];
	});
}
