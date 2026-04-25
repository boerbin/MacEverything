#import "MacSearchBridge.h"

NS_ASSUME_NONNULL_BEGIN

/// Lightweight wrapper exposing a semantic search result to Swift.
@interface MESemanticResult : NSObject
@property (nonatomic, strong) NSString *name;
@property (nonatomic, strong) NSString *path;
@property (nonatomic) uint8_t type;
@property (nonatomic) uint64_t size;
@property (nonatomic) time_t modTime;
@property (nonatomic) float similarity;
@end

/// Category exposing semantic-search methods on MacSearchBridge.
@interface MacSearchBridge (Semantic)

// Search
- (NSArray<MESemanticResult *> *)semanticSearch:(NSString *)query maxResults:(uint32_t)maxResults;
- (NSArray<MESemanticResult *> *)similarFiles:(NSString *)filePath maxResults:(uint32_t)maxResults;

// Translation
- (NSDictionary *)translateQuery:(NSString *)naturalLanguage;

// Config
- (uint32_t)semanticIndexedCount;
- (void)rebuildSemanticIndex;

// Status
- (BOOL)isLiteLLMAvailable;

@end

NS_ASSUME_NONNULL_END
