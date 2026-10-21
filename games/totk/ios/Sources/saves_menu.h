#import <UIKit/UIKit.h>

/* Saves on iPhone / iPad (docs/save-sync.md): the Saves page of the settings menu (pushed from TOTKSettingsMenu) sends
   the saves as one .totksave file (runtime/save_package.h) through the share sheet, AirDrop first; TOTKSaveReceiver takes
   a .totksave the app is asked to open (AirDrop, Files, Messages: application:openURL:options:, main.m), checks it, asks,
   backs up Documents/save/save/<uid> to Documents/save-backups and puts the received saves in its place. The game runs in
   this process and holds the loaded save in memory, so the player is asked to close it and open it again. The Mac's
   version is the Interface tab's Send saves (runtime/settings_panel_mac.h) and app/launcher.m. */
@interface TOTKSavesViewController : UITableViewController
@end

@interface TOTKSaveReceiver : NSObject
+ (BOOL)openURL:(NSURL *)url;   /* YES: a .totksave, taken (the questions follow on screen) */
@end

NSString *TOTKSavesSummary(void);   /* "6 slots, last saved ..." for the settings row */
