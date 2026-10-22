#import "power.h"
#include <stdio.h>
#include <string.h>
#include "settings.h"

NSNotificationName const TOTKPowerCapChangedNotification = @"TOTKPowerCapChanged";
extern void nvn_fps_cap_set(int hz);
static const char *g_reason;   /* main queue only */

const char *power_cap_reason(void) { return g_reason; }

static const char *thermal_name(NSProcessInfoThermalState t) {
    return t == NSProcessInfoThermalStateCritical ? "critical" : t == NSProcessInfoThermalStateSerious ? "serious"
         : t == NSProcessInfoThermalStateFair ? "fair" : "nominal";
}

/* On the main queue, where the settings menu changes the limit too. Heat is named before Low Power Mode, as the one
   the player can do less about. A limit the player picks meanwhile is stored (settings.c) and applied at the end. */
static void power_update(void) {
    NSProcessInfo *p = NSProcessInfo.processInfo;
    NSProcessInfoThermalState t = p.thermalState; BOOL low = p.lowPowerModeEnabled;
    const char *why = t == NSProcessInfoThermalStateCritical ? "the device is very hot"
                    : t == NSProcessInfoThermalStateSerious ? "the device is hot"
                    : low ? "Low Power Mode is on" : NULL;
    fprintf(stderr, "[power] thermal state %s, Low Power Mode %s\n", thermal_name(t), low ? "on" : "off");
    if (why == g_reason) return;
    int player = settings_get("framerate.cap");
    if (why && !g_reason) {
        settings_fps_held = 1; nvn_fps_cap_set(30);
        fprintf(stderr, "[power] frame rate held at 30 fps: %s (the player's limit, %d, returns afterwards)\n", why, player);
    } else if (!why) {
        settings_fps_held = 0; nvn_fps_cap_set(player);
        fprintf(stderr, "[power] frame rate hold ended; the player's limit (%d) again\n", player);
    } else fprintf(stderr, "[power] frame rate still held at 30 fps: %s\n", why);
    g_reason = why;
    [NSNotificationCenter.defaultCenter postNotificationName:TOTKPowerCapChangedNotification object:nil];
}

void power_observe(void) {
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSNotificationCenter *nc = NSNotificationCenter.defaultCenter; NSOperationQueue *main = NSOperationQueue.mainQueue;
        [nc addObserverForName:NSProcessInfoThermalStateDidChangeNotification object:nil queue:main usingBlock:^(NSNotification *n) { (void)n; power_update(); }];
        [nc addObserverForName:NSProcessInfoPowerStateDidChangeNotification object:nil queue:main usingBlock:^(NSNotification *n) { (void)n; power_update(); }];
        if (NSThread.isMainThread) power_update(); else dispatch_async(dispatch_get_main_queue(), ^{ power_update(); });
    });
}
