#import "MacSearchBridge_Internal.h"
#import "MacSearchBridge+Semantic.h"
#include "Logger.h"

@implementation MESemanticResult
@end

@implementation MacSearchBridge (Semantic)

- (NSArray<MESemanticResult *> *)semanticSearch:(NSString *)query maxResults:(uint32_t)maxResults {
    @try {
        auto litellm = _serviceEngine->safeLiteLLMClient();
        auto vectorSearch = _serviceEngine->safeVectorSearch();
        auto engine = _serviceEngine->safeEngine();
        if (!litellm || !vectorSearch || !engine) return @[];

        std::string queryStr([query UTF8String]);
        if (queryStr.empty()) return @[];

        // Get embedding for query text
        auto queryVec = litellm->embed("embed", queryStr);
        if (queryVec.empty()) return @[];

        // Search vectors
        auto hits = vectorSearch->search(queryVec, maxResults);
        if (hits.empty()) return @[];

        // Enrich with file metadata
        NSMutableArray<MESemanticResult *> *results = [NSMutableArray arrayWithCapacity:hits.size()];
        for (const auto& hit : hits) {
            auto record = engine->getRecord(hit.id);
            if (record.type == 0) continue;

            std::string fullPath = SearchEngine::makeFullPath(record.path, record.name);

            NSString *nsName = [NSString stringWithUTF8String:record.name.c_str()];
            NSString *nsPath = [NSString stringWithUTF8String:fullPath.c_str()];
            if (!nsName || !nsPath) continue;

            MESemanticResult *r = [[MESemanticResult alloc] init];
            r.name = nsName;
            r.path = nsPath;
            r.type = record.type;
            r.size = record.size;
            r.modTime = record.modTime;
            r.similarity = hit.similarity;
            [results addObject:r];
        }
        return results;
    } @catch (NSException *exception) {
        LOG_ERROR("Bridge", "semanticSearch exception: " << [[exception reason] UTF8String]);
        return @[];
    }
}

- (NSArray<MESemanticResult *> *)similarFiles:(NSString *)filePath maxResults:(uint32_t)maxResults {
    @try {
        auto embeddingIndex = _serviceEngine->safeEmbeddingIndex();
        auto vectorSearch = _serviceEngine->safeVectorSearch();
        auto engine = _serviceEngine->safeEngine();
        if (!embeddingIndex || !vectorSearch || !engine) return @[];

        std::string pathStr([filePath UTF8String]);
        if (pathStr.empty()) return @[];

        // Get embedding for the source file
        std::vector<float> sourceVec;
        if (!embeddingIndex->getEmbedding(pathStr, sourceVec)) return @[];

        // Search for similar vectors
        auto hits = vectorSearch->search(sourceVec, maxResults);
        if (hits.empty()) return @[];

        // Enrich with file metadata
        NSMutableArray<MESemanticResult *> *results = [NSMutableArray arrayWithCapacity:hits.size()];
        for (const auto& hit : hits) {
            auto record = engine->getRecord(hit.id);
            if (record.type == 0) continue;

            std::string fullPath = SearchEngine::makeFullPath(record.path, record.name);

            NSString *nsName = [NSString stringWithUTF8String:record.name.c_str()];
            NSString *nsPath = [NSString stringWithUTF8String:fullPath.c_str()];
            if (!nsName || !nsPath) continue;

            MESemanticResult *r = [[MESemanticResult alloc] init];
            r.name = nsName;
            r.path = nsPath;
            r.type = record.type;
            r.size = record.size;
            r.modTime = record.modTime;
            r.similarity = hit.similarity;
            [results addObject:r];
        }
        return results;
    } @catch (NSException *exception) {
        LOG_ERROR("Bridge", "similarFiles exception: " << [[exception reason] UTF8String]);
        return @[];
    }
}

- (NSDictionary *)translateQuery:(NSString *)naturalLanguage {
    @try {
        auto nlTranslator = _serviceEngine->safeNLTranslator();
        if (!nlTranslator) {
            return @{
                @"original_query": naturalLanguage,
                @"translated_query": @"",
                @"success": @NO,
                @"already_syntax": @NO,
                @"error": @"NLTranslator not available"
            };
        }

        std::string query([naturalLanguage UTF8String]);
        auto result = nlTranslator->translate(query);

        NSString *translatedQuery = [NSString stringWithUTF8String:result.translatedQuery.c_str()];
        NSString *errorStr = [NSString stringWithUTF8String:result.error.c_str()];
        if (!translatedQuery) translatedQuery = @"";
        if (!errorStr) errorStr = @"";

        return @{
            @"original_query": naturalLanguage,
            @"translated_query": translatedQuery,
            @"success": @(result.success),
            @"already_syntax": @(result.alreadySyntax),
            @"error": errorStr
        };
    } @catch (NSException *exception) {
        return @{
            @"original_query": naturalLanguage,
            @"translated_query": @"",
            @"success": @NO,
            @"already_syntax": @NO,
            @"error": [exception reason] ?: @"Unknown exception"
        };
    }
}

- (void)setSemanticExtensions:(NSArray<NSString *> *)extensions {
    auto embeddingIndex = _serviceEngine->safeEmbeddingIndex();
    if (!embeddingIndex) return;
    std::vector<std::string> exts;
    exts.reserve(extensions.count);
    for (NSString *ext in extensions) {
        exts.push_back(std::string([ext UTF8String]));
    }
    embeddingIndex->setExtensions(exts);
}

- (NSArray<NSString *> *)semanticExtensions {
    auto embeddingIndex = _serviceEngine->safeEmbeddingIndex();
    if (!embeddingIndex) return @[];
    auto exts = embeddingIndex->getExtensions();
    NSMutableArray<NSString *> *result = [NSMutableArray arrayWithCapacity:exts.size()];
    for (const auto& ext : exts) {
        NSString *str = [NSString stringWithUTF8String:ext.c_str()];
        if (!str) continue;
        [result addObject:str];
    }
    return result;
}

- (void)setSemanticMaxFileSize:(uint64_t)bytes {
    auto embeddingIndex = _serviceEngine->safeEmbeddingIndex();
    if (embeddingIndex) {
        embeddingIndex->setMaxFileSize(bytes);
    }
}

- (uint64_t)semanticMaxFileSize {
    auto embeddingIndex = _serviceEngine->safeEmbeddingIndex();
    return embeddingIndex ? embeddingIndex->getMaxFileSize() : (1 * 1024 * 1024);
}

- (uint32_t)semanticIndexedCount {
    auto embeddingIndex = _serviceEngine->safeEmbeddingIndex();
    return embeddingIndex ? embeddingIndex->indexedCount() : 0;
}

- (void)rebuildSemanticIndex {
    _serviceEngine->rebuildSemanticIndex();
}

- (BOOL)isLiteLLMAvailable {
    @try {
        auto litellm = _serviceEngine->safeLiteLLMClient();
        return litellm ? litellm->isAvailable() : NO;
    } @catch (NSException *exception) {
        return NO;
    }
}

@end
