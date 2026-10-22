#import "power.h"
#include <os/proc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

/* The entitlements the app was signed with, as the kernel holds them (CS_OPS_ENTITLEMENTS_BLOB: an 8-byte big-endian
   magic / length header, then the XML). signkit signs with the profile's entitlements, so a capability the App ID
   lacks is missing here even though TOTK.entitlements asks for it. */
int csops(pid_t pid, unsigned int ops, void *useraddr, size_t usersize);
static int has_entitlement(const char *blob, const char *key) {
    if (!blob) return -1;
    const char *k = strstr(blob, key); if (!k) return 0;
    const char *v = strstr(k, "</key>"); if (!v) return 0;
    v += 6; while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r') v++;
    return !strncmp(v, "<true/>", 7);
}
void power_memory_report(void) {
    char *blob = NULL; uint32_t head[2] = {0, 0};
    if (csops(getpid(), 7, head, sizeof head) != 0 || head[1]) {   /* ERANGE with the length in the header */
        uint32_t len = __builtin_bswap32(head[1]);
        if (len > 8 && len < (1u << 20) && (blob = calloc(1, len + 1)) && csops(getpid(), 7, blob, len) != 0) { free(blob); blob = NULL; }
    }
    const char *xml = blob ? blob + 8 : NULL;
    int inc = has_entitlement(xml, "com.apple.developer.kernel.increased-memory-limit");
    int ext = has_entitlement(xml, "com.apple.developer.kernel.extended-virtual-addressing");
    static const char *yn[] = {"unknown", "no", "yes"};
    fprintf(stderr, "[memory] available to the app %.2f GB; entitlements: increased-memory-limit %s, extended-virtual-addressing %s\n",
            os_proc_available_memory() / 1e9, yn[inc + 1], yn[ext + 1]);
    free(blob);
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
