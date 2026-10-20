#import <UIKit/UIKit.h>

/* The Mods pages of the iPhone / iPad settings menu (pushed from TOTKSettingsMenu): the installed mods (on / off, load
   order and delete in Edit, the loader's notices, a restart banner) and a GameBanana browser (search, sort, results with
   pictures, a mod's page, install with progress) on runtime/mod_browser.m. Zip archives install here; 7z / RAR ones show
   how to install them instead. The Mac version is runtime/mods_panel_mac.h. */
@interface TOTKModsViewController : UITableViewController
@end

@interface TOTKModBrowserViewController : UITableViewController <UISearchBarDelegate>
@end

@interface TOTKModDetailViewController : UITableViewController
- (instancetype)initWithModId:(int64_t)modId name:(NSString *)name;
@end

NSString *TOTKModsSummary(void);   /* "3 installed, 2 on" for the settings row */
