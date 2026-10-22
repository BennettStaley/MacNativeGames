#import <UIKit/UIKit.h>

/* Tools › Spawn on iPhone / iPad (pushed from TOTKSettingsMenu): the actor list (runtime/spawn_list.h, GameData/
   actor_list.tsv), a category menu (Favorites, Recents, Creatures, NPCs, Items, Weapons, Armor, Zonai, Objects; Show
   unnamed), a search over every category and an amount (1-20). A tap spawns next to the player (runtime/actor_spawn.c,
   1.4.3); the star or a swipe marks a favorite. A toast in the corner says what was spawned or why not. A controller
   works through press: (the settings menu forwards its buttons). The Mac's version is runtime/spawn_panel_mac.h. */
@interface TOTKSpawnViewController : UITableViewController
- (BOOL)press:(unsigned)buttons;   /* settings_menu.m's PAD_* bits, plus SPAWN_PAD_FAVORITE; NO: not handled (B) */
@end
enum { SPAWN_PAD_FAVORITE = 256 };

NSString *TOTKSpawnSummary(void);   /* the settings row's line */
