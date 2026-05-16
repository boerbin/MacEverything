#import "MacSearchBridge.h"

NS_ASSUME_NONNULL_BEGIN

/// Category exposing AI methods on MacSearchBridge.
@interface MacSearchBridge (AI)

// Translation
- (NSDictionary *)translateQuery:(NSString *)naturalLanguage;

// Status
- (BOOL)isAIAvailable;

@end

NS_ASSUME_NONNULL_END
