// A Language Server Protocol connection to one server process (JSON-RPC over
// the server’s stdin/stdout). Messages sent before the initialize handshake
// completes are queued and delivered in order once it has.

NS_ASSUME_NONNULL_BEGIN

@interface LSPClient : NSObject
// `shellCommand` runs via /bin/sh -c, so it may contain arguments (e.g. "xcrun clangd --log=error").
- (nullable instancetype)initWithShellCommand:(NSString*)shellCommand rootPath:(NSString*)rootPath environment:(NSDictionary<NSString*, NSString*>*)environment;

@property (nonatomic, readonly) NSString* shellCommand;
@property (nonatomic, readonly) NSString* rootPath;

// After initialization: YES when the server agreed to UTF-8 columns (otherwise columns are UTF-16 code units).
@property (atomic, readonly) BOOL usesUTF8Positions;
@property (atomic, readonly, getter = isRunning) BOOL running;

// Server → client notifications, delivered on the main queue.
@property (nonatomic, copy, nullable) void(^notificationHandler)(NSString* method, id _Nullable params);
// Called on the main queue if the server process exits.
@property (nonatomic, copy, nullable) void(^terminationHandler)(void);

- (void)sendNotification:(NSString*)method params:(nullable id)params;
// Returns the request id. `handler` is called on `queue` with the result, or with an error dictionary.
- (NSInteger)sendRequest:(NSString*)method params:(nullable id)params queue:(dispatch_queue_t)queue handler:(void(^)(id _Nullable result, NSDictionary* _Nullable error))handler;
- (void)cancelRequest:(NSInteger)requestId;

// Polite shutdown (shutdown request + exit), then SIGTERM if the server lingers.
- (void)shutdown;
// Immediate SIGTERM, e.g. when TextMate quits.
- (void)terminate;

// Exposed for tests: parses Content-Length framed messages out of `buffer`, removing what was consumed.
+ (NSArray<NSDictionary*>*)messagesFromBuffer:(NSMutableData*)buffer;
@end

NS_ASSUME_NONNULL_END
