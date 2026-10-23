#import <Foundation/Foundation.h>

/* Heat and Low Power Mode (power.m). While the device is seriously or critically hot, or Low Power Mode is on, the
   frame rate is held at 30 (nvn_fps_cap_set); the player's own limit comes back when it ends. settings.txt is never
   written: the hold lives only in memory (settings_fps_held). */
extern NSNotificationName const TOTKPowerCapChangedNotification;   /* posted on the main queue when the hold starts or ends */
void power_observe(void);              /* once, after settings_init */
const char *power_cap_reason(void);    /* NULL, or why the frame rate is held at 30 ("the device is hot", ...) */
void power_memory_report(void);        /* logs the memory left to the app and the memory entitlements it was signed with */
