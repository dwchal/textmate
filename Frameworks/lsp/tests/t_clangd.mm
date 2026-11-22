#import <lsp/LSPManager.h>
#import <lsp/LSPClient.h>
#import <document/OakDocument.h>
#import <io/io.h>
#import <ns/ns.h>

// End-to-end against the clangd that ships with Xcode: diagnostics become gutter
// marks (at byte columns, past non-ASCII text), completions come back for a
// member access, edits are synced, closing the document removes the marks.

static std::string const kClangd = "/usr/bin/clangd"; // only checked for presence; launched via “xcrun clangd”

static BOOL spin_until (NSTimeInterval timeout, BOOL(^condition)())
{
	NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:timeout];
	while(!condition() && [deadline timeIntervalSinceNow] > 0)
		[NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
	return condition();
}

static std::map<size_t, std::string> marks_of_type (OakDocument* doc, NSString* type, size_t lines)
{
	__block std::map<size_t, std::string> res;
	for(size_t line = 0; line < lines; ++line)
	{
		[doc enumerateBookmarksAtLine:line block:^(text::pos_t const& pos, NSString* markType, NSString* payload){
			if([markType isEqualToString:type])
				res[line] = std::string(pos) + " " + to_s(payload);
		}];
	}
	return res;
}

void test_message_framing ()
{
	NSMutableData* buffer = [NSMutableData dataWithData:[@"X-Junk: 1\r\n\r\nContent-Length: 2\r\n\r\n{}" dataUsingEncoding:NSUTF8StringEncoding]];
	NSArray* messages = [LSPClient messagesFromBuffer:buffer];
	OAK_ASSERT_EQ(messages.count, 1); // a header block without Content-Length is skipped
	OAK_ASSERT_EQ(buffer.length, 0);

	buffer = [NSMutableData dataWithData:[@"Content-Length: 14\r\n\r\n{\"id\":1,\"a\":2}Content-Type: x\r\nContent-Length: 2\r\n\r\n{}Content-Length: 10\r\n\r\n{\"x\"" dataUsingEncoding:NSUTF8StringEncoding]];
	messages = [LSPClient messagesFromBuffer:buffer];
	OAK_ASSERT_EQ(messages.count, 2);
	OAK_ASSERT_EQ(to_s([messages[0][@"a"] stringValue]), "2");
	OAK_ASSERT_EQ(to_s([[NSString alloc] initWithData:buffer encoding:NSUTF8StringEncoding]), "Content-Length: 10\r\n\r\n{\"x\""); // incomplete message is kept
}

void test_clangd_diagnostics_and_completion ()
{
	if(!path::exists(kClangd))
		return OAK_WARN("Skipping: no " + kClangd);

	std::string const dir = path::resolve(path::temp("lsp_test"));
	path::make_dir(dir);
	path::set_content(path::join(dir, "compile_flags.txt"), "-std=c++17\n");
	// The form documented in LSPManager.h: scoped to file types, server found via xcrun.
	path::set_content(path::join(dir, ".tm_properties"), "[ source.c, source.c++, source.objc, source.objc++ ]\nlanguageServer = \"xcrun clangd --log=error\"\n");

	std::string const source =
		"struct Widget { int alpha; int beta; };\n"
		"int main () {\n"
		"\tWidget w;\n"
		"\tw.al;\n"
		"\tchar const* s = \"\xC3\xA9\xF0\x9F\x98\x80\"; undefined_function();\n"
		"\treturn 0;\n"
		"}\n";
	std::string const file = path::join(dir, "main.cc");
	path::set_content(file, source);

	dispatch_sync(dispatch_get_main_queue(), ^{
		OakDocument* doc = [OakDocument documentWithPath:to_ns(file)];
		__block BOOL loaded = NO;
		[doc loadModalForWindow:nil completionHandler:^(OakDocumentIOResult result, NSString* errorMessage, oak::uuid_t const& filterUUID){
			loaded = result == OakDocumentIOResultSuccess;
		}];
		OAK_ASSERT(spin_until(10, ^{ return loaded; }));
		doc.fileType = @"source.c++";

		__block NSUInteger diagnosticsRounds = 0, lastCount = NSNotFound;
		LSPManager.sharedInstance.diagnosticsHandler = ^(OakDocument* document, NSUInteger count){
			if(document == doc)
			{
				++diagnosticsRounds;
				lastCount = count;
			}
		};

		[LSPManager.sharedInstance attachDocument:doc];
		OAK_ASSERT([LSPManager.sharedInstance isDocumentAttached:doc]);
		OAK_ASSERT(spin_until(30, ^{ return lastCount != NSNotFound && lastCount > 0; }));

		auto errors = marks_of_type(doc, kLSPMarkTypeError, 7);
		OAK_ASSERT_EQ(errors.count(3), 1);
		OAK_ASSERT_NE(errors[3].find("member named 'al'"), std::string::npos);
		OAK_ASSERT_EQ(errors.count(4), 1);
		OAK_ASSERT_NE(errors[4].find("undefined_function"), std::string::npos);
		// “é😀” is 6 bytes but 3 UTF-16 units, so the call is at byte column 27 (“5:28”), not UTF-16 column 24.
		OAK_ASSERT_EQ(errors[4].substr(0, 5), "5:28 ");

		NSArray* completions = [LSPManager.sharedInstance completionsForDocument:doc atPosition:text::pos_t(3, 5) prefix:@"al" timeout:10];
		OAK_ASSERT([completions containsObject:@"alpha"]);
		OAK_ASSERT(![completions containsObject:@"beta"]); // doesn’t match the prefix

		// Fix both errors: the edit is synced and the marks go away.
		doc.content = @"struct Widget { int alpha; int beta; };\nint main () {\n\tWidget w;\n\treturn w.alpha;\n}\n";
		NSUInteger const before = diagnosticsRounds;
		OAK_ASSERT(spin_until(30, ^{ return diagnosticsRounds > before && lastCount == 0; }));
		OAK_ASSERT(marks_of_type(doc, kLSPMarkTypeError, 7).empty());

		[doc close];
		OAK_ASSERT(![LSPManager.sharedInstance isDocumentAttached:doc]);
		LSPManager.sharedInstance.diagnosticsHandler = nil;
		[LSPManager.sharedInstance shutdownAllServers];
	});

	path::remove(dir);
}

void test_no_setting_means_no_server ()
{
	std::string const dir = path::resolve(path::temp("lsp_test_off"));
	path::make_dir(dir);
	std::string const file = path::join(dir, "main.cc");
	path::set_content(file, "int main () { return 0; }\n");
	path::set_content(path::join(dir, ".tm_properties"), "[ source.python ]\nlanguageServer = \"xcrun clangd\"\n"); // for another file type only

	dispatch_sync(dispatch_get_main_queue(), ^{
		OakDocument* doc = [OakDocument documentWithPath:to_ns(file)];
		__block BOOL loaded = NO;
		[doc loadModalForWindow:nil completionHandler:^(OakDocumentIOResult result, NSString* errorMessage, oak::uuid_t const& filterUUID){
			loaded = YES;
		}];
		spin_until(10, ^{ return loaded; });
		doc.fileType = @"source.c++";
		[LSPManager.sharedInstance attachDocument:doc];
		OAK_ASSERT(![LSPManager.sharedInstance isDocumentAttached:doc]);
		[doc close];
	});

	path::remove(dir);
}
