#import "LSPClient.h"
#import <os/log.h>
#import <fcntl.h>
#import <unistd.h>

@interface LSPClient ()
{
	NSTask* _task;
	int _writeFd;
	int _readFd;

	dispatch_queue_t _writeQueue;   // serializes writes to the server
	dispatch_queue_t _pendingQueue; // suspended until the initialize handshake is done
	BOOL _pendingQueueSuspended;

	NSLock* _lock;
	NSInteger _nextRequestId;
	NSMutableDictionary<NSNumber*, void(^)(id, NSDictionary*)>* _handlers;
}
@property (atomic, readwrite) BOOL usesUTF8Positions;
@property (atomic, readwrite, getter = isRunning) BOOL running;
@end

// Delivered through the main run loop rather than the main dispatch queue, so
// that callers spinning the run loop (modal sessions, tests) still receive them.
static void PerformOnMainRunLoop (void(^block)())
{
	CFRunLoopPerformBlock(CFRunLoopGetMain(), kCFRunLoopCommonModes, block);
	CFRunLoopWakeUp(CFRunLoopGetMain());
}

@implementation LSPClient
- (instancetype)initWithShellCommand:(NSString*)shellCommand rootPath:(NSString*)rootPath environment:(NSDictionary<NSString*, NSString*>*)environment
{
	if(!(self = [super init]))
		return nil;

	_shellCommand  = shellCommand;
	_rootPath      = rootPath;
	_lock          = [NSLock new];
	_handlers      = [NSMutableDictionary dictionary];
	_nextRequestId = 1;
	_writeQueue    = dispatch_queue_create("org.textmate.lsp.write", DISPATCH_QUEUE_SERIAL);
	_pendingQueue  = dispatch_queue_create("org.textmate.lsp.pending", DISPATCH_QUEUE_SERIAL);
	dispatch_suspend(_pendingQueue);
	_pendingQueueSuspended = YES;

	NSPipe* input  = [NSPipe pipe];
	NSPipe* output = [NSPipe pipe];

	_task = [NSTask new];
	_task.launchPath           = @"/bin/sh";
	_task.arguments            = @[ @"-c", [@"exec " stringByAppendingString:shellCommand] ];
	_task.currentDirectoryPath = rootPath;
	_task.environment          = environment;
	_task.standardInput        = input;
	_task.standardOutput       = output;
	_task.standardError        = [NSFileHandle fileHandleWithNullDevice];

	__weak LSPClient* weakSelf = self;
	_task.terminationHandler = ^(NSTask* task){
		[weakSelf serverDidTerminate:task.terminationStatus];
	};

	@try {
		[_task launch];
	}
	@catch(NSException* e) {
		os_log_error(OS_LOG_DEFAULT, "LSP: failed to launch ‘%{public}@’: %{public}@", shellCommand, e.reason);
		_writeFd = -1;
		[self resumePendingQueue];
		return nil;
	}
	self.running = YES;

	_writeFd = dup(input.fileHandleForWriting.fileDescriptor);
	_readFd  = dup(output.fileHandleForReading.fileDescriptor);
	fcntl(_writeFd, F_SETNOSIGPIPE, 1);
	[input.fileHandleForWriting closeFile];
	[output.fileHandleForReading closeFile];

	[self startReading];
	[self initializeHandshake];
	return self;
}

- (void)dealloc
{
	[self terminate];
	[self resumePendingQueue];
	if(_writeFd >= 0)
		close(_writeFd);
}

- (void)resumePendingQueue
{
	BOOL resume = NO;
	[_lock lock];
	if(_pendingQueueSuspended)
	{
		_pendingQueueSuspended = NO;
		resume = YES;
	}
	[_lock unlock];
	if(resume)
		dispatch_resume(_pendingQueue);
}

// =============
// = Handshake =
// =============

- (void)initializeHandshake
{
	NSDictionary* params = @{
		@"processId":  @(getpid()),
		@"clientInfo": @{ @"name": @"TextMate" },
		@"rootPath":   _rootPath,
		@"rootUri":    [NSURL fileURLWithPath:_rootPath isDirectory:YES].absoluteString,
		@"capabilities": @{
			@"general": @{ @"positionEncodings": @[ @"utf-8", @"utf-16" ] },
			@"textDocument": @{
				@"synchronization":    @{ @"didSave": @YES },
				@"completion":         @{ @"completionItem": @{ @"snippetSupport": @NO } },
				@"publishDiagnostics": @{ @"relatedInformation": @NO },
			},
		},
		@"offsetEncoding": @[ @"utf-8", @"utf-16" ], // clangd’s pre-3.17 spelling of positionEncodings
	};

	__weak LSPClient* weakSelf = self;
	[self sendRequest:@"initialize" params:params queue:dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0) handler:^(id result, NSDictionary* error){
		LSPClient* client = weakSelf;
		if(!client)
			return;

		if(error)
			os_log_error(OS_LOG_DEFAULT, "LSP: ‘%{public}@’ failed to initialize: %{public}@", client.shellCommand, error);

		if([result isKindOfClass:[NSDictionary class]])
		{
			NSString* encoding = result[@"capabilities"][@"positionEncoding"];
			if(![encoding isKindOfClass:[NSString class]])
				encoding = result[@"offsetEncoding"];
			client.usesUTF8Positions = [encoding isKindOfClass:[NSString class]] && [encoding isEqualToString:@"utf-8"];
		}

		[client writeMessage:@{ @"jsonrpc": @"2.0", @"method": @"initialized", @"params": @{ } }];
		[client resumePendingQueue];
	} direct:YES];
}

// =============
// = Messaging =
// =============

- (void)writeMessage:(NSDictionary*)message
{
	NSData* body = [NSJSONSerialization dataWithJSONObject:message options:0 error:nil];
	if(!body)
		return;

	NSMutableData* data = [[[NSString stringWithFormat:@"Content-Length: %lu\r\n\r\n", body.length] dataUsingEncoding:NSUTF8StringEncoding] mutableCopy];
	[data appendData:body];

	int fd = _writeFd;
	dispatch_async(_writeQueue, ^{
		char const* bytes = (char const*)data.bytes;
		size_t remaining = data.length;
		while(remaining)
		{
			ssize_t len = write(fd, bytes, remaining);
			if(len == -1 && errno == EINTR)
				continue;
			if(len <= 0)
				break;
			bytes += len;
			remaining -= len;
		}
	});
}

- (void)enqueueMessage:(NSDictionary*)message direct:(BOOL)direct
{
	if(direct)
		return [self writeMessage:message];

	__weak LSPClient* weakSelf = self;
	dispatch_async(_pendingQueue, ^{
		[weakSelf writeMessage:message];
	});
}

- (void)sendNotification:(NSString*)method params:(id)params
{
	NSMutableDictionary* message = [@{ @"jsonrpc": @"2.0", @"method": method } mutableCopy];
	if(params)
		message[@"params"] = params;
	[self enqueueMessage:message direct:NO];
}

- (NSInteger)sendRequest:(NSString*)method params:(id)params queue:(dispatch_queue_t)queue handler:(void(^)(id, NSDictionary*))handler
{
	return [self sendRequest:method params:params queue:queue handler:handler direct:NO];
}

- (NSInteger)sendRequest:(NSString*)method params:(id)params queue:(dispatch_queue_t)queue handler:(void(^)(id, NSDictionary*))handler direct:(BOOL)direct
{
	[_lock lock];
	NSInteger requestId = _nextRequestId++;
	_handlers[@(requestId)] = ^(id result, NSDictionary* error){
		dispatch_async(queue, ^{ handler(result, error); });
	};
	[_lock unlock];

	if(!self.running)
	{
		[self completeRequest:@(requestId) result:nil error:@{ @"code": @(-32099), @"message": @"Server is not running" }];
		return requestId;
	}

	NSMutableDictionary* message = [@{ @"jsonrpc": @"2.0", @"id": @(requestId), @"method": method } mutableCopy];
	if(params)
		message[@"params"] = params;
	[self enqueueMessage:message direct:direct];
	return requestId;
}

- (void)cancelRequest:(NSInteger)requestId
{
	[self completeRequest:@(requestId) result:nil error:@{ @"code": @(-32800), @"message": @"Cancelled" }];
	[self sendNotification:@"$/cancelRequest" params:@{ @"id": @(requestId) }];
}

- (void)completeRequest:(NSNumber*)requestId result:(id)result error:(NSDictionary*)error
{
	[_lock lock];
	void(^handler)(id, NSDictionary*) = _handlers[requestId];
	[_handlers removeObjectForKey:requestId];
	[_lock unlock];

	if(handler)
		handler(result == NSNull.null ? nil : result, error);
}

// ===========
// = Reading =
// ===========

+ (NSArray<NSDictionary*>*)messagesFromBuffer:(NSMutableData*)buffer
{
	NSMutableArray* res = [NSMutableArray array];
	NSData* const separator = [NSData dataWithBytes:"\r\n\r\n" length:4];
	while(true)
	{
		NSRange headerEnd = [buffer rangeOfData:separator options:0 range:NSMakeRange(0, buffer.length)];
		if(headerEnd.location == NSNotFound)
			break;

		NSString* header = [[NSString alloc] initWithBytes:buffer.bytes length:headerEnd.location encoding:NSUTF8StringEncoding];
		NSInteger contentLength = -1;
		for(NSString* line in [header componentsSeparatedByString:@"\r\n"])
		{
			if([line.lowercaseString hasPrefix:@"content-length:"])
				contentLength = [[line substringFromIndex:15] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet].integerValue;
		}

		NSUInteger const bodyStart = NSMaxRange(headerEnd);
		if(contentLength < 0) // garbage: skip this header
		{
			[buffer replaceBytesInRange:NSMakeRange(0, bodyStart) withBytes:NULL length:0];
			continue;
		}
		if(buffer.length < bodyStart + contentLength)
			break;

		NSData* body = [buffer subdataWithRange:NSMakeRange(bodyStart, contentLength)];
		[buffer replaceBytesInRange:NSMakeRange(0, bodyStart + contentLength) withBytes:NULL length:0];

		id message = [NSJSONSerialization JSONObjectWithData:body options:0 error:nil];
		if([message isKindOfClass:[NSDictionary class]])
			[res addObject:message];
	}
	return res;
}

- (void)startReading
{
	int fd = _readFd;
	__weak LSPClient* weakSelf = self;
	dispatch_async(dispatch_queue_create("org.textmate.lsp.read", DISPATCH_QUEUE_SERIAL), ^{
		NSMutableData* buffer = [NSMutableData data];
		char chunk[65536];
		while(true)
		{
			ssize_t len = read(fd, chunk, sizeof(chunk));
			if(len == -1 && errno == EINTR)
				continue;
			if(len <= 0)
				break;

			[buffer appendBytes:chunk length:len];
			for(NSDictionary* message in [LSPClient messagesFromBuffer:buffer])
			{
				LSPClient* client = weakSelf;
				if(!client)
					break;
				[client handleMessage:message];
			}
		}
		close(fd);
	});
}

- (void)handleMessage:(NSDictionary*)message
{
	id requestId = message[@"id"];
	NSString* method = message[@"method"];

	if(method && requestId) // server → client request: we implement none, so answer each with an empty result
	{
		[self writeMessage:@{ @"jsonrpc": @"2.0", @"id": requestId, @"result": NSNull.null }];
	}
	else if(method)
	{
		id params = message[@"params"];
		__weak LSPClient* weakSelf = self;
		PerformOnMainRunLoop(^{
			if(LSPClient* client = weakSelf)
			{
				if(client.notificationHandler)
					client.notificationHandler(method, params == NSNull.null ? nil : params);
			}
		});
	}
	else if([requestId isKindOfClass:[NSNumber class]])
	{
		NSDictionary* error = [message[@"error"] isKindOfClass:[NSDictionary class]] ? message[@"error"] : nil;
		[self completeRequest:requestId result:message[@"result"] error:error];
	}
}

// =============
// = Lifecycle =
// =============

- (void)serverDidTerminate:(int)status
{
	self.running = NO;
	[self resumePendingQueue];

	[_lock lock];
	NSArray* requestIds = _handlers.allKeys;
	[_lock unlock];
	for(NSNumber* requestId in requestIds)
		[self completeRequest:requestId result:nil error:@{ @"code": @(-32099), @"message": @"Server exited" }];

	__weak LSPClient* weakSelf = self;
	PerformOnMainRunLoop(^{
		if(LSPClient* client = weakSelf)
		{
			if(client.terminationHandler)
				client.terminationHandler();
		}
	});
}

- (void)shutdown
{
	if(!self.running)
		return;

	NSTask* task = _task;
	__weak LSPClient* weakSelf = self;
	[self sendRequest:@"shutdown" params:nil queue:dispatch_get_global_queue(QOS_CLASS_UTILITY, 0) handler:^(id result, NSDictionary* error){
		[weakSelf sendNotification:@"exit" params:nil];
	}];

	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC), dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
		if(task.isRunning)
			[task terminate];
	});
}

- (void)terminate
{
	if(_task.isRunning)
		[_task terminate];
}
@end
