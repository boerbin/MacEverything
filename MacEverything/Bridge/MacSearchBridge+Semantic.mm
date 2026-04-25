#import "MacSearchBridge+Semantic.h"

@implementation MESemanticResult
@end

@implementation MacSearchBridge (Semantic)

- (NSArray<MESemanticResult *> *)semanticSearch:(NSString *)query maxResults:(uint32_t)maxResults {
    // Stub: real implementation requires EmbeddingIndex + VectorSearch from feature branch
    return @[];
}

- (NSArray<MESemanticResult *> *)similarFiles:(NSString *)filePath maxResults:(uint32_t)maxResults {
    return @[];
}

- (NSDictionary *)translateQuery:(NSString *)naturalLanguage {
    return @{
        @"original_query": naturalLanguage,
        @"translated_query": @"",
        @"success": @NO,
        @"already_syntax": @NO,
        @"error": @"Semantic search not yet available"
    };
}

- (void)setSemanticExtensions:(NSArray<NSString *> *)extensions {
    // Stub
}

- (NSArray<NSString *> *)semanticExtensions {
    return @[];
}

- (void)setSemanticMaxFileSize:(uint64_t)bytes {
    // Stub
}

- (uint64_t)semanticMaxFileSize {
    return 1 * 1024 * 1024; // 1 MB default
}

- (uint32_t)semanticIndexedCount {
    return 0;
}

- (void)rebuildSemanticIndex {
    // Stub
}

- (BOOL)isLiteLLMAvailable {
    return NO;
}

@end
