#import <document/OakDocument.h>
#import <document/OakDocumentController.h>
#import <io/path.h>

// Find in Folder: serial (as before) vs OakDocumentMatchesForStringInDocuments.
// Set TM_BENCH_CORPUS to search a different tree than this repository.

static NSString* const kRoot = [NSString stringWithUTF8String:(getenv("TM_BENCH_CORPUS") ?: path::parent(path::parent(path::parent(path::parent(__FILE__)))).c_str())];

static NSArray<OakDocument*>* documents_in_root ()
{
	NSMutableArray* res = [NSMutableArray array];
	NSDictionary* options = @{
		kSearchDepthFirstSearchKey:      @YES,
		kSearchDirectoryGlobsKey:        @[ @"*" ],
		kSearchFileGlobsKey:             @[ @"*" ],
		kSearchExcludeDirectoryGlobsKey: @[ @".git" ],
	};
	[OakDocumentController.sharedInstance enumerateDocumentsAtPaths:@[ kRoot ] options:options usingBlock:^(OakDocument* document, BOOL* stop){
		[res addObject:document];
	}];
	return res;
}

static std::string serialize (NSArray* matchesPerDocument)
{
	std::string res;
	for(id matches in matchesPerDocument)
	{
		if(matches == NSNull.null)
		{
			res += "-\n";
			continue;
		}
		for(OakDocumentMatch* m in matches)
			res += std::to_string(m.first) + "-" + std::to_string(m.last) + " ";
		res += "\n";
	}
	return res;
}

static NSArray* serial_search (NSArray<OakDocument*>* documents, NSString* str, find::options_t options)
{
	NSMutableArray* res = [NSMutableArray array];
	for(OakDocument* document in documents)
		[res addObject:[document matchesForString:str options:options] ?: NSNull.null];
	return res;
}

void test_parallel_find_matches_serial ()
{
	NSArray* documents = documents_in_root();
	OAK_ASSERT_GT(documents.count, 100);
	for(NSString* str in @[ @"include", @"\\bstd::(map|set)<" ])
	{
		find::options_t options = [str hasPrefix:@"\\"] ? find::regular_expression : find::none;
		NSArray* serial   = serial_search(documents, str, options);
		NSArray* parallel = OakDocumentMatchesForStringInDocuments(documents, str, options, nullptr);
		OAK_ASSERT_EQ(serialize(serial), serialize(parallel));
	}
}

void benchmark_find_plain_serial ()   { serial_search(documents_in_root(), @"include", find::none); }
void benchmark_find_plain_parallel () { OakDocumentMatchesForStringInDocuments(documents_in_root(), @"include", find::none, nullptr); }
void benchmark_find_regex_serial ()   { serial_search(documents_in_root(), @"\\bstd::(map|set)<", find::regular_expression); }
void benchmark_find_regex_parallel () { OakDocumentMatchesForStringInDocuments(documents_in_root(), @"\\bstd::(map|set)<", find::regular_expression, nullptr); }
