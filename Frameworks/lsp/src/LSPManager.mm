#import "LSPManager.h"
#import "LSPClient.h"
#import "position.h"
#import <document/OakDocument.h>
#import <settings/settings.h>
#import <io/environment.h>
#import <io/path.h>
#import <ns/ns.h>
#import <oak/oak.h>
#import <os/log.h>

NSString* const kLSPMarkTypeError   = @"lsp-error";
NSString* const kLSPMarkTypeWarning = @"lsp-warning";

static NSString* const kSettingsLanguageServerKey = @"languageServer";
static NSTimeInterval const kChangeDebounce       = 0.25;
static NSTimeInterval const kIdleServerShutdown   = 30;

static NSString* LanguageIdForFileType (NSString* fileType)
{
	static NSDictionary* const map = @{
		@"source.c":       @"c",
		@"source.c++":     @"cpp",
		@"source.objc":    @"objective-c",
		@"source.objc++":  @"objective-cpp",
		@"source.swift":   @"swift",
		@"source.python":  @"python",
		@"source.js":      @"javascript",
		@"source.ts":      @"typescript",
		@"source.rust":    @"rust",
		@"source.go":      @"go",
		@"source.ruby":    @"ruby",
		@"source.shell":   @"shellscript",
	};
	NSString* res = map[fileType];
	if(!res && [fileType hasPrefix:@"source."])
		res = [fileType substringFromIndex:7];
	return res ?: fileType;
}

// The nearest ancestor that looks like a project root, else the file’s folder.
static NSString* RootPathForFile (NSString* path)
{
	NSString* const home = NSHomeDirectory();
	NSString* dir = path.stringByDeletingLastPathComponent;
	for(NSString* candidate = dir; candidate.length > 1 && ![candidate isEqualToString:home]; candidate = candidate.stringByDeletingLastPathComponent)
	{
		for(NSString* marker in @[ @"compile_commands.json", @"compile_flags.txt", @".git", @"Package.swift", @"Cargo.toml", @"go.mod", @"pyproject.toml" ])
		{
			if([NSFileManager.defaultManager fileExistsAtPath:[candidate stringByAppendingPathComponent:marker]])
				return candidate;
		}
	}
	return dir;
}

static std::string LineOfString (NSString* content, NSUInteger line)
{
	std::string const str = to_s(content);
	size_t bol = 0;
	for(NSUInteger n = 0; n < line && bol != std::string::npos; ++n)
	{
		bol = str.find('\n', bol);
		if(bol != std::string::npos)
			++bol;
	}
	if(bol == std::string::npos)
		return "";
	size_t eol = str.find('\n', bol);
	return str.substr(bol, eol == std::string::npos ? std::string::npos : eol - bol);
}

// ====================
// = LSPDocumentState =
// ====================

@interface LSPDocumentState : NSObject
@property (nonatomic, weak) OakDocument* document;
@property (nonatomic) LSPClient* client;
@property (nonatomic) NSString* uri;
@property (nonatomic) NSInteger version;
@property (nonatomic) NSTimer* changeTimer;
@end

@implementation LSPDocumentState
@end

// ==============
// = LSPManager =
// ==============

@interface LSPManager ()
@property (nonatomic) NSMutableDictionary<NSUUID*, LSPDocumentState*>* states;   // by document identifier
@property (nonatomic) NSMutableDictionary<NSString*, LSPClient*>* clients;         // by "command\0root"
@property (nonatomic) NSMutableDictionary<NSString*, NSTimer*>* shutdownTimers;
@end

@implementation LSPManager
+ (LSPManager*)sharedInstance
{
	static LSPManager* sharedInstance = [self new];
	return sharedInstance;
}

- (instancetype)init
{
	if(self = [super init])
	{
		_states         = [NSMutableDictionary dictionary];
		_clients        = [NSMutableDictionary dictionary];
		_shutdownTimers = [NSMutableDictionary dictionary];
		[NSNotificationCenter.defaultCenter addObserver:self selector:@selector(applicationWillTerminate:) name:NSApplicationWillTerminateNotification object:nil];
	}
	return self;
}

- (void)applicationWillTerminate:(NSNotification*)aNotification
{
	for(LSPClient* client in _clients.allValues)
		[client terminate];
}

- (void)shutdownAllServers
{
	for(LSPDocumentState* state in _states.allValues)
		[state.changeTimer invalidate];
	[_states removeAllObjects];
	for(NSTimer* timer in _shutdownTimers.allValues)
		[timer invalidate];
	[_shutdownTimers removeAllObjects];
	for(LSPClient* client in _clients.allValues)
		[client shutdown];
	[_clients removeAllObjects];
}

- (BOOL)isDocumentAttached:(OakDocument*)document
{
	return document.identifier && _states[document.identifier] != nil;
}

// ==================
// = Server Clients =
// ==================

- (LSPClient*)clientForCommand:(NSString*)command root:(NSString*)root environment:(NSDictionary*)environment
{
	NSString* key = [NSString stringWithFormat:@"%@%C%@", command, (unichar)0, root];
	[_shutdownTimers[key] invalidate];
	[_shutdownTimers removeObjectForKey:key];

	LSPClient* client = _clients[key];
	if(client && client.isRunning)
		return client;

	if(!(client = [[LSPClient alloc] initWithShellCommand:command rootPath:root environment:environment]))
		return nil;

	__weak LSPManager* weakSelf = self;
	client.notificationHandler = ^(NSString* method, id params){
		if([method isEqualToString:@"textDocument/publishDiagnostics"])
			[weakSelf applyDiagnostics:params];
	};
	client.terminationHandler = ^{
		[weakSelf clientDidTerminate:key];
	};

	_clients[key] = client;
	os_log_info(OS_LOG_DEFAULT, "LSP: started ‘%{public}@’ for %{public}@", command, root);
	return client;
}

- (void)clientDidTerminate:(NSString*)key
{
	LSPClient* client = _clients[key];
	if(!client || client.isRunning)
		return;
	[_clients removeObjectForKey:key];

	for(NSUUID* identifier in _states.allKeys)
	{
		LSPDocumentState* state = _states[identifier];
		if(state.client == client)
		{
			[state.changeTimer invalidate];
			[self removeMarksFromDocument:state.document];
			[_states removeObjectForKey:identifier];
		}
	}
}

- (void)scheduleShutdownIfUnused:(LSPClient*)client
{
	for(LSPDocumentState* state in _states.allValues)
	{
		if(state.client == client)
			return;
	}

	NSString* key = [_clients allKeysForObject:client].firstObject;
	if(!key)
		return;

	__weak LSPManager* weakSelf = self;
	_shutdownTimers[key] = [NSTimer scheduledTimerWithTimeInterval:kIdleServerShutdown repeats:NO block:^(NSTimer* timer){
		LSPManager* manager = weakSelf;
		[manager.shutdownTimers removeObjectForKey:key];
		if(LSPClient* idle = manager.clients[key])
		{
			[manager.clients removeObjectForKey:key];
			[idle shutdown];
		}
	}];
}

// =============
// = Documents =
// =============

- (void)attachDocument:(OakDocument*)document
{
	if(!document.path || !document.identifier || !document.isLoaded || _states[document.identifier])
		return;

	std::string const path     = to_s(document.path);
	std::string const fileType = to_s(document.fileType ?: @"");
	std::string const command  = settings_for_path(path, fileType, path::parent(path)).get(to_s(kSettingsLanguageServerKey), NULL_STR);
	if(command == NULL_STR || command.empty())
		return;

	std::map<std::string, std::string> const variables = variables_for_path(oak::basic_environment(), path, fileType);
	NSMutableDictionary* environment = [NSMutableDictionary dictionary];
	for(auto const& pair : variables)
		environment[to_ns(pair.first)] = to_ns(pair.second);

	LSPClient* client = [self clientForCommand:to_ns(command) root:RootPathForFile(document.path) environment:environment];
	if(!client)
		return;

	LSPDocumentState* state = [LSPDocumentState new];
	state.document = document;
	state.client   = client;
	state.uri      = [NSURL fileURLWithPath:document.path].absoluteString;
	state.version  = 1;
	_states[document.identifier] = state;

	[client sendNotification:@"textDocument/didOpen" params:@{
		@"textDocument": @{
			@"uri":        state.uri,
			@"languageId": LanguageIdForFileType(document.fileType),
			@"version":    @(state.version),
			@"text":       document.content ?: @"",
		}
	}];

	[NSNotificationCenter.defaultCenter addObserver:self selector:@selector(documentContentDidChange:) name:OakDocumentContentDidChangeNotification object:document];
	[NSNotificationCenter.defaultCenter addObserver:self selector:@selector(documentDidSave:) name:OakDocumentDidSaveNotification object:document];
	[NSNotificationCenter.defaultCenter addObserver:self selector:@selector(documentWillClose:) name:OakDocumentWillCloseNotification object:document];
}

- (LSPDocumentState*)stateForDocument:(OakDocument*)document
{
	return document.identifier ? _states[document.identifier] : nil;
}

- (void)documentContentDidChange:(NSNotification*)aNotification
{
	LSPDocumentState* state = [self stateForDocument:aNotification.object];
	if(!state || state.changeTimer)
		return;

	__weak LSPManager* weakSelf = self;
	__weak LSPDocumentState* weakState = state;
	state.changeTimer = [NSTimer scheduledTimerWithTimeInterval:kChangeDebounce repeats:NO block:^(NSTimer* timer){
		[weakSelf flushChangesForState:weakState];
	}];
}

- (void)flushChangesForState:(LSPDocumentState*)state
{
	if(!state.changeTimer)
		return;
	[state.changeTimer invalidate];
	state.changeTimer = nil;

	OakDocument* document = state.document;
	if(!document)
		return;

	state.version += 1;
	[state.client sendNotification:@"textDocument/didChange" params:@{
		@"textDocument":   @{ @"uri": state.uri, @"version": @(state.version) },
		@"contentChanges": @[ @{ @"text": document.content ?: @"" } ],
	}];
}

- (void)documentDidSave:(NSNotification*)aNotification
{
	if(LSPDocumentState* state = [self stateForDocument:aNotification.object])
	{
		[self flushChangesForState:state];
		[state.client sendNotification:@"textDocument/didSave" params:@{ @"textDocument": @{ @"uri": state.uri } }];
	}
}

- (void)documentWillClose:(NSNotification*)aNotification
{
	OakDocument* document = aNotification.object;
	LSPDocumentState* state = [self stateForDocument:document];
	if(!state)
		return;

	[NSNotificationCenter.defaultCenter removeObserver:self name:nil object:document];
	[state.changeTimer invalidate];
	[state.client sendNotification:@"textDocument/didClose" params:@{ @"textDocument": @{ @"uri": state.uri } }];
	[self removeMarksFromDocument:document];
	[_states removeObjectForKey:document.identifier];
	[self scheduleShutdownIfUnused:state.client];
}

// ===============
// = Diagnostics =
// ===============

- (void)removeMarksFromDocument:(OakDocument*)document
{
	[document removeAllMarksOfType:kLSPMarkTypeError];
	[document removeAllMarksOfType:kLSPMarkTypeWarning];
}

- (void)applyDiagnostics:(NSDictionary*)params
{
	if(![params isKindOfClass:[NSDictionary class]])
		return;

	LSPDocumentState* state;
	for(LSPDocumentState* candidate in _states.allValues)
	{
		if([candidate.uri isEqualToString:params[@"uri"]])
			state = candidate;
	}

	OakDocument* document = state.document;
	if(!document)
		return;

	NSArray* diagnostics = [params[@"diagnostics"] isKindOfClass:[NSArray class]] ? params[@"diagnostics"] : @[ ];
	BOOL const utf8 = state.client.usesUTF8Positions;
	NSString* content = utf8 ? nil : document.content;

	// Several diagnostics can start at the same place; a mark holds one message, so join them.
	std::map<std::pair<std::string, text::pos_t>, std::vector<std::string>> marks;
	for(NSDictionary* diagnostic in diagnostics)
	{
		if(![diagnostic isKindOfClass:[NSDictionary class]])
			continue;

		NSInteger severity = [diagnostic[@"severity"] integerValue] ?: 1;
		if(severity > 2) // information and hints: not shown
			continue;

		NSDictionary* start = diagnostic[@"range"][@"start"];
		NSUInteger line   = [start[@"line"] unsignedIntegerValue];
		NSUInteger column = [start[@"character"] unsignedIntegerValue];
		if(!utf8)
			column = lsp::utf16_to_utf8_column(LineOfString(content, line), column);

		NSString* message = diagnostic[@"message"];
		NSString* source  = diagnostic[@"source"];
		std::string text = to_s([message isKindOfClass:[NSString class]] ? message : @"");
		if([source isKindOfClass:[NSString class]] && source.length)
			text += " (" + to_s(source) + ")";

		std::string const type = to_s(severity == 1 ? kLSPMarkTypeError : kLSPMarkTypeWarning);
		marks[{ type, text::pos_t(line, column) }].push_back(text);
	}

	[self removeMarksFromDocument:document];
	for(auto const& pair : marks)
	{
		std::string joined;
		for(auto const& text : pair.second)
			joined += (joined.empty() ? "" : "\n") + text;
		[document setMarkOfType:to_ns(pair.first.first) atPosition:pair.first.second content:to_ns(joined)];
	}

	if(_diagnosticsHandler)
		_diagnosticsHandler(document, diagnostics.count);
}

// ===============
// = Completions =
// ===============

- (NSArray<NSString*>*)completionsForDocument:(OakDocument*)document atPosition:(text::pos_t const&)position prefix:(NSString*)prefix timeout:(NSTimeInterval)timeout
{
	LSPDocumentState* state = [self stateForDocument:document];
	if(!state || !state.client.isRunning)
		return nil;

	[self flushChangesForState:state];

	NSUInteger column = position.column;
	if(!state.client.usesUTF8Positions)
		column = lsp::utf8_to_utf16_column(LineOfString(document.content, position.line), position.column);

	dispatch_semaphore_t done = dispatch_semaphore_create(0);
	__block id completionResult;
	NSInteger requestId = [state.client sendRequest:@"textDocument/completion" params:@{
		@"textDocument": @{ @"uri": state.uri },
		@"position":     @{ @"line": @(position.line), @"character": @(column) },
	} queue:dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0) handler:^(id result, NSDictionary* error){
		completionResult = result;
		dispatch_semaphore_signal(done);
	}];

	if(dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeout * NSEC_PER_SEC))) != 0)
	{
		[state.client cancelRequest:requestId];
		dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER); // cancelRequest completes the handler
		return nil;
	}

	NSArray* items = [completionResult isKindOfClass:[NSDictionary class]] ? completionResult[@"items"] : completionResult;
	if(![items isKindOfClass:[NSArray class]])
		return @[ ];

	NSMutableArray<NSDictionary*>* candidates = [NSMutableArray array];
	for(NSDictionary* item in items)
	{
		if(![item isKindOfClass:[NSDictionary class]] || [item[@"insertTextFormat"] integerValue] == 2) // snippets: we asked for none
			continue;

		NSString* text = item[@"textEdit"][@"newText"];
		if(![text isKindOfClass:[NSString class]])
			text = item[@"insertText"];
		if(![text isKindOfClass:[NSString class]])
			text = [item[@"label"] isKindOfClass:[NSString class]] ? [item[@"label"] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet] : nil;
		if(!text || (prefix.length && ![text hasPrefix:prefix]) || [text isEqualToString:prefix] || [text rangeOfCharacterFromSet:NSCharacterSet.newlineCharacterSet].location != NSNotFound)
			continue;

		NSString* sortText = [item[@"sortText"] isKindOfClass:[NSString class]] ? item[@"sortText"] : text;
		[candidates addObject:@{ @"text": text, @"sort": sortText }];
	}

	[candidates sortWithOptions:NSSortStable usingComparator:^NSComparisonResult(NSDictionary* lhs, NSDictionary* rhs){
		return [lhs[@"sort"] compare:rhs[@"sort"]];
	}];

	NSMutableOrderedSet* res = [NSMutableOrderedSet orderedSet];
	for(NSDictionary* candidate in candidates)
		[res addObject:candidate[@"text"]];
	return res.array;
}
@end
