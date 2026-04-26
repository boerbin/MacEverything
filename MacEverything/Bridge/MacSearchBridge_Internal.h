#pragma once
#import "MacSearchBridge.h"
#include "ServiceEngine.h"
#include <memory>

/// Class extension: Bridge holds a ServiceEngine and forwards all operations.
@interface MacSearchBridge () {
@public
    std::shared_ptr<ServiceEngine> _serviceEngine;
    BOOL _engineStarted;
    BOOL _startupFinished;
    uint32_t _startupFinishedCount;
    BOOL _startupDidFullScan;
    void (^_Nullable _startupCompletion)(uint32_t, BOOL);
}

@end
