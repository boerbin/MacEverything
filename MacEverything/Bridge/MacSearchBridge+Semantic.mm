#import "MacSearchBridge_Internal.h"
#import "MacSearchBridge+Semantic.h"
#include "Logger.h"

@implementation MacSearchBridge (AI)

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

- (BOOL)isAIAvailable {
    @try {
        auto nlTranslator = _serviceEngine->safeNLTranslator();
        return nlTranslator != nullptr;
    } @catch (NSException *exception) {
        return NO;
    }
}

@end
