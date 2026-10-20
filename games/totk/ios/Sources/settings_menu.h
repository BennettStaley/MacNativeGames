#import <UIKit/UIKit.h>

/* The in-game settings menu (iPhone and iPad). Its rows come from the shared settings table (runtime/settings_schema.h),
   so a new setting needs no code here. Touch, a game controller (D-pad or stick to move, A to change, left / right to
   adjust, B to close) and a keyboard (arrows, Return, Esc) all work. While it is open the game reads a neutral
   controller (runtime_input_hold). */
extern NSNotificationName const TOTKSettingsChangedNotification;   /* userInfo @"key": the setting that changed */

@interface TOTKSettingsMenu : UITableViewController
+ (BOOL)isOpen;
+ (void)openFrom:(UIViewController *)host;
@end
