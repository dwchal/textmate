#import <text/types.h>

// Connects open documents to language servers.
//
// Off unless configured. Enable per file type in .tm_properties, e.g.
//
//     [ "source.c, source.c++, source.objc, source.objc++" ]
//     languageServer = "xcrun clangd"
//
// Diagnostics appear as gutter marks (click for the message); completions
// from the server come first in the ⎋ completion list.

@class OakDocument;

extern NSString* const kLSPMarkTypeError;   // "lsp-error"
extern NSString* const kLSPMarkTypeWarning; // "lsp-warning"

@interface LSPManager : NSObject
@property (class, readonly) LSPManager* sharedInstance;

// Safe to call repeatedly; does nothing unless the document has a path and a languageServer setting.
- (void)attachDocument:(OakDocument*)document;
- (BOOL)isDocumentAttached:(OakDocument*)document;

// Blocks for at most `timeout` seconds; returns nil if the server had no answer in time.
// `position` is a TextMate position (line, byte column), where the word being completed ends.
- (NSArray<NSString*>*)completionsForDocument:(OakDocument*)document atPosition:(text::pos_t const&)position prefix:(NSString*)prefix timeout:(NSTimeInterval)timeout;

// For tests: called on the main queue after diagnostics for `document` were applied.
@property (nonatomic, copy) void(^diagnosticsHandler)(OakDocument* document, NSUInteger count);

- (void)shutdownAllServers;
@end
