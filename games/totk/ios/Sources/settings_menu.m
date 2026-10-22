#import "settings_menu.h"
#import "mods_menu.h"
#import "saves_menu.h"
#import "spawn_menu.h"
#import "power.h"
#import <GameController/GameController.h>
#import <Metal/Metal.h>
#include <os/proc.h>
#include "runtime.h"
#include "settings.h"
#include "game_version.h"

NSNotificationName const TOTKSettingsChangedNotification = @"TOTKSettingsChanged";
static BOOL menuOpen;

/* Controller buttons the menu reads, as bits; a held direction repeats after 0.35 s, every 0.09 s. */
enum { PAD_UP = 1, PAD_DOWN = 2, PAD_LEFT = 4, PAD_RIGHT = 8, PAD_OK = 16, PAD_BACK = 32, PAD_PAGE_UP = 64, PAD_PAGE_DOWN = 128 };

@implementation TOTKSettingsMenu {
    NSArray<NSArray<NSNumber *> *> *sections;   /* per section shown: setting indices, or one row for a page of its own */
    NSArray<NSString *> *titles;
    NSArray<NSNumber *> *kinds;                 /* MENU_SETTINGS, MENU_MODS, MENU_SAVES, MENU_SPAWN (settings_schema.h) */
    NSArray<NSArray<NSString *> *> *about;       /* label, value */
    CADisplayLink *link;
    unsigned held; CFTimeInterval repeatAt;
    NSIndexPath *cursor;
}

+ (BOOL)isOpen { return menuOpen; }

+ (void)openFrom:(UIViewController *)host {
    if (menuOpen || host.presentedViewController) return;
    menuOpen = YES; runtime_input_hold(1);
    TOTKSettingsMenu *menu = [[TOTKSettingsMenu alloc] initWithStyle:UITableViewStyleInsetGrouped];
    UINavigationController *nav = [[UINavigationController alloc] initWithRootViewController:menu];
    nav.modalPresentationStyle = UIModalPresentationFormSheet;
    nav.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    nav.preferredContentSize = CGSizeMake(640, 720);
    [host presentViewController:nav animated:YES completion:nil];
}

static unsigned device_mask(void) { return UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad ? SET_IPAD : SET_IPHONE; }

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"Settings";
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:self action:@selector(close)];
    [self loadSections];
    id<MTLDevice> gpu = MTLCreateSystemDefaultDevice();
    NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    (void)docs;
    about = @[@[@"Game version", @TOTK_GAME_VERSION],
              @[@"Graphics", gpu.name ?: @"unknown"],
              @[@"Memory free for the game", [NSString stringWithFormat:@"%.1f GB", os_proc_available_memory() / 1e9]],
              @[@"Display", [NSString stringWithFormat:@"%ld Hz", (long)UIScreen.mainScreen.maximumFramesPerSecond]],
              @[@"Settings file", @"Files › Tears of the Kingdom › settings.txt"]];
    [NSNotificationCenter.defaultCenter addObserver:self.tableView selector:@selector(reloadData) name:TOTKPowerCapChangedNotification object:nil];
    link = [CADisplayLink displayLinkWithTarget:self selector:@selector(poll:)];
    [link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}

/* The Mac's four tabs (Game, Graphics, Content, Tools) as one list: a section per tab section, titled "Tab · Section".
   Cheats are Mac only; Mods, Saves and Spawn are one row each that opens their page. */
- (void)loadSections {
    NSMutableArray *rows = [NSMutableArray array], *names = [NSMutableArray array], *kindList = [NSMutableArray array];
    for (int k = 0; k < SETTINGS_NSECTIONS; k++) {
        const SettingsSection *sec = &SETTINGS_SECTIONS[k];
        NSString *title = [NSString stringWithFormat:@"%s · %s", SETTINGS_TABS[sec->tab], sec->name];
        if (sec->kind == MENU_MODS || sec->kind == MENU_SAVES || sec->kind == MENU_SPAWN) {
            [rows addObject:@[@-1]]; [names addObject:title]; [kindList addObject:@(sec->kind)];
            continue;
        }
        if (sec->kind != MENU_SETTINGS) continue;
        NSMutableArray *page = [NSMutableArray array];
        for (int i = 0; i < SETTINGS_COUNT; i++) {
            const SettingDef *d = &SETTINGS[i];
            if (strcmp(d->page, sec->name) || (d->shown_key && settings_get(d->shown_key) != d->shown_value)) continue;
            if (!(d->platforms & device_mask()) && !strncmp(d->key, "display.", 8)) continue;   /* the Mac's output sizes: iOS shows 720p */
            [page addObject:@(i)];
        }
        if (page.count) { [rows addObject:page]; [names addObject:title]; [kindList addObject:@(MENU_SETTINGS)]; }
    }
    sections = rows; titles = names; kinds = kindList;
}
- (NSInteger)kindOf:(NSInteger)section { return section < (NSInteger)kinds.count ? kinds[section].integerValue : -1; }
- (NSIndexPath *)rowOfKind:(int)kind {
    for (NSUInteger k = 0; k < kinds.count; k++) if (kinds[k].intValue == kind) return [NSIndexPath indexPathForRow:0 inSection:(NSInteger)k];
    return nil;
}

- (void)viewDidDisappear:(BOOL)animated {
    [super viewDidDisappear:animated];
    if (self.navigationController.isBeingDismissed || !self.navigationController.presentingViewController) {
        [link invalidate]; link = nil; menuOpen = NO; runtime_input_hold(0);
    }
}

- (void)close { [self.navigationController dismissViewControllerAnimated:YES completion:nil]; }

#pragma mark - Rows

/* The sections (settings, and the Mods, Saves and Spawn rows that open their pages: mods_menu.m, saves_menu.m,
   spawn_menu.m), then About. */
- (NSIndexPath *)modsRow { return [self rowOfKind:MENU_MODS]; }
- (NSIndexPath *)savesRow { return [self rowOfKind:MENU_SAVES]; }
- (NSIndexPath *)spawnRow { return [self rowOfKind:MENU_SPAWN]; }
- (BOOL)isPageRow:(NSIndexPath *)path { NSInteger k = [self kindOf:path.section]; return k == MENU_MODS || k == MENU_SAVES || k == MENU_SPAWN; }
- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView { (void)tableView; return (NSInteger)sections.count + 1; }
- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    (void)tableView;
    return section < (NSInteger)sections.count ? (NSInteger)sections[section].count : (NSInteger)about.count;
}
- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    (void)tableView;
    return section < (NSInteger)titles.count ? titles[section] : @"About";
}
- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    (void)tableView;
    return section == (NSInteger)sections.count ? @"Open this menu with three fingers, the gear, a controller's Home button or − and + held together, or Esc on a keyboard." : nil;
}

- (const SettingDef *)defAt:(NSIndexPath *)path {
    if (path.section >= (NSInteger)sections.count || [self kindOf:path.section] != MENU_SETTINGS) return NULL;
    return &SETTINGS[sections[path.section][path.row].intValue];
}
- (BOOL)available:(const SettingDef *)d { return (d->platforms & device_mask()) != 0; }

static NSString *value_text(const SettingDef *d, int v) {
    if (d->type == SET_CHOICE) { const SettingChoice *c = settings_choice(d, v); return c ? @(c->label) : @"?"; }
    if (d->type == SET_RANGE) return [NSString stringWithFormat:@"%d%s", v, d->unit ? d->unit : ""];
    return v ? @"On" : @"Off";
}

- (NSString *)detailFor:(const SettingDef *)d {
    if (![self available:d]) return @(d->unavailable ? d->unavailable : "Not on this device.");
    if (!strcmp(d->key, "framerate.cap") && power_cap_reason())
        return [NSString stringWithFormat:@"Held at 30 fps while %s; your limit returns afterwards.", power_cap_reason()];
    if (settings_pending_restart(d->key)) return @"Applies the next time the game starts.";
    return d->help ? @(d->help) : nil;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)path {
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
    cell.detailTextLabel.numberOfLines = 0; cell.detailTextLabel.textColor = UIColor.secondaryLabelColor;
    const SettingDef *d = [self defAt:path];
    if ([path isEqual:[self modsRow]]) {
        UITableViewCell *mods = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:nil];
        mods.textLabel.text = @"Mods"; mods.detailTextLabel.text = TOTKModsSummary();
        mods.imageView.image = [UIImage systemImageNamed:@"shippingbox"];
        mods.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
        return mods;
    }
    if ([path isEqual:[self savesRow]]) {
        UITableViewCell *saves = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
        saves.textLabel.text = @"Send or receive saves"; saves.detailTextLabel.text = TOTKSavesSummary();
        saves.detailTextLabel.textColor = UIColor.secondaryLabelColor;
        saves.imageView.image = [UIImage systemImageNamed:@"square.and.arrow.up.on.square"];
        saves.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
        return saves;
    }
    if ([path isEqual:[self spawnRow]]) {
        UITableViewCell *spawn = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
        spawn.textLabel.text = @"Spawn"; spawn.detailTextLabel.text = TOTKSpawnSummary();
        spawn.detailTextLabel.textColor = UIColor.secondaryLabelColor; spawn.detailTextLabel.numberOfLines = 0;
        spawn.imageView.image = [UIImage systemImageNamed:@"sparkles"];
        spawn.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
        return spawn;
    }
    if (!d) {
        UITableViewCell *info = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:nil];
        info.textLabel.text = about[path.row][0]; info.detailTextLabel.text = about[path.row][1];
        info.selectionStyle = UITableViewCellSelectionStyleNone;
        return info;
    }
    cell.textLabel.text = @(d->label); cell.detailTextLabel.text = [self detailFor:d];
    BOOL on = [self available:d]; int v = settings_get(d->key);
    cell.textLabel.enabled = on; cell.userInteractionEnabled = on;
    if (d->type == SET_BOOL) {
        UISwitch *toggle = [UISwitch new]; toggle.on = v != 0; toggle.enabled = on; toggle.tag = path.section << 16 | path.row;
        [toggle addTarget:self action:@selector(switched:) forControlEvents:UIControlEventValueChanged];
        cell.accessoryView = toggle;
    } else if (d->type == SET_CHOICE) {
        UIButton *pick = [UIButton buttonWithType:UIButtonTypeSystem];
        [pick setTitle:value_text(d, v) forState:UIControlStateNormal]; pick.enabled = on;
        NSMutableArray *actions = [NSMutableArray array]; NSString *key = @(d->key);
        for (int i = 0; i < d->nchoices; i++) {
            int value = d->choices[i].value; __weak TOTKSettingsMenu *weak = self;
            UIAction *a = [UIAction actionWithTitle:@(d->choices[i].label) image:nil identifier:nil handler:^(UIAction *action) {
                (void)action; [weak set:key value:value reload:path]; }];
            a.state = value == v ? UIMenuElementStateOn : UIMenuElementStateOff; [actions addObject:a];
        }
        pick.menu = [UIMenu menuWithChildren:actions]; pick.showsMenuAsPrimaryAction = YES; [pick sizeToFit];
        cell.accessoryView = pick;
    } else {
        UIView *box = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 230, 32)];
        UISlider *slider = [[UISlider alloc] initWithFrame:CGRectMake(0, 0, 170, 32)];
        slider.minimumValue = d->min; slider.maximumValue = d->max; slider.value = v; slider.enabled = on;
        slider.tag = path.section << 16 | path.row;
        [slider addTarget:self action:@selector(slid:) forControlEvents:UIControlEventValueChanged];
        UILabel *label = [[UILabel alloc] initWithFrame:CGRectMake(176, 0, 54, 32)];
        label.text = value_text(d, v); label.textAlignment = NSTextAlignmentRight; label.tag = 1;
        label.font = [UIFont monospacedDigitSystemFontOfSize:15 weight:UIFontWeightRegular];
        [box addSubview:slider]; [box addSubview:label]; cell.accessoryView = box;
    }
    cell.selectionStyle = UITableViewCellSelectionStyleDefault;
    return cell;
}

- (void)set:(NSString *)key value:(int)value reload:(NSIndexPath *)path {
    settings_set(key.UTF8String, value);
    [NSNotificationCenter.defaultCenter postNotificationName:TOTKSettingsChangedNotification object:nil userInfo:@{@"key": key}];
    if (path) [self.tableView reloadRowsAtIndexPaths:@[path] withRowAnimation:UITableViewRowAnimationNone];
    if (cursor) [self.tableView selectRowAtIndexPath:cursor animated:NO scrollPosition:UITableViewScrollPositionNone];
}
- (NSIndexPath *)pathOf:(UIView *)control { return [NSIndexPath indexPathForRow:control.tag & 0xffff inSection:control.tag >> 16]; }
- (void)switched:(UISwitch *)toggle {
    NSIndexPath *path = [self pathOf:toggle]; [self set:@([self defAt:path]->key) value:toggle.on reload:path];
}
- (void)slid:(UISlider *)slider {
    NSIndexPath *path = [self pathOf:slider]; const SettingDef *d = [self defAt:path];
    int v = d->min + (int)lround((slider.value - d->min) / d->step) * d->step;
    ((UILabel *)[slider.superview viewWithTag:1]).text = value_text(d, v);
    if (v != settings_get(d->key)) [self set:@(d->key) value:v reload:nil];   /* no reload: it would end the drag */
}

- (NSIndexPath *)tableView:(UITableView *)tableView willSelectRowAtIndexPath:(NSIndexPath *)path {
    (void)tableView; const SettingDef *d = [self defAt:path]; return (d && [self available:d]) || [self isPageRow:path] ? path : nil;
}
- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)path {
    (void)tableView; cursor = path; [self activate:path step:0];
}
- (void)viewWillAppear:(BOOL)animated {   /* back from Mods or Saves: their summaries */
    [super viewWillAppear:animated];
    if (sections) [self.tableView reloadRowsAtIndexPaths:@[[self modsRow], [self savesRow], [self spawnRow]] withRowAnimation:UITableViewRowAnimationNone];
}

/* A, Return or a tap: a switch flips, a choice moves to the next one. Left / right (step -1 / +1) move a choice or a slider. */
- (void)activate:(NSIndexPath *)path step:(int)step {
    if ([path isEqual:[self modsRow]]) {
        if (!step) [self.navigationController pushViewController:[TOTKModsViewController new] animated:YES];
        return;
    }
    if ([path isEqual:[self savesRow]]) {
        if (!step) [self.navigationController pushViewController:[TOTKSavesViewController new] animated:YES];
        return;
    }
    if ([path isEqual:[self spawnRow]]) {
        if (!step) [self.navigationController pushViewController:[TOTKSpawnViewController new] animated:YES];
        return;
    }
    const SettingDef *d = [self defAt:path]; if (!d || ![self available:d]) return;
    int v = settings_get(d->key), next = v;
    if (d->type == SET_BOOL) next = step ? step > 0 : !v;
    else if (d->type == SET_RANGE) { if (!step) return; next = v + step * d->step; next = next < d->min ? d->min : next > d->max ? d->max : next; }
    else {
        int at = 0; for (int i = 0; i < d->nchoices; i++) if (d->choices[i].value == v) at = i;
        at = (at + (step < 0 ? d->nchoices - 1 : 1)) % d->nchoices; next = d->choices[at].value;
    }
    if (next != v) [self set:@(d->key) value:next reload:path];
}

#pragma mark - Controller and keyboard

- (void)moveBy:(int)delta {
    NSMutableArray<NSIndexPath *> *all = [NSMutableArray array];
    for (NSUInteger s = 0; s < sections.count; s++)
        for (NSUInteger r = 0; r < sections[s].count; r++) {
            NSIndexPath *p = [NSIndexPath indexPathForRow:(NSInteger)r inSection:(NSInteger)s];
            if ([self isPageRow:p] || [self available:[self defAt:p]]) [all addObject:p];
        }
    if (!all.count) return;
    NSUInteger at = cursor ? [all indexOfObject:cursor] : NSNotFound;
    NSInteger next = at == NSNotFound ? 0 : MAX(0, MIN((NSInteger)all.count - 1, (NSInteger)at + delta));
    cursor = all[(NSUInteger)next];
    [self.tableView selectRowAtIndexPath:cursor animated:YES scrollPosition:UITableViewScrollPositionMiddle];
}

- (void)press:(unsigned)button {
    UIViewController *top = self.navigationController.topViewController;
    if (top != self) {   /* a Mods, Saves or Spawn page is shown: B goes back one level; Spawn takes the rest */
        if ([top isKindOfClass:TOTKSpawnViewController.class] && !(button & PAD_BACK) && [(TOTKSpawnViewController *)top press:button]) return;
        if (button & PAD_BACK) [self.navigationController popViewControllerAnimated:YES];
        return;
    }
    if (button & PAD_BACK) { [self close]; return; }
    if (button & (PAD_UP | PAD_DOWN)) { [self moveBy:button & PAD_UP ? -1 : 1]; return; }   /* no wrapping round */
    if (button & (PAD_PAGE_UP | PAD_PAGE_DOWN)) { [self moveBy:button & PAD_PAGE_UP ? -8 : 8]; return; }   /* LT / RT */
    if (!cursor) { [self moveBy:0]; return; }
    if (button & PAD_OK) [self activate:cursor step:0];
    else if (button & (PAD_LEFT | PAD_RIGHT)) [self activate:cursor step:button & PAD_LEFT ? -1 : 1];
}

- (void)poll:(CADisplayLink *)sender {
    unsigned now = 0;
    for (GCController *controller in GCController.controllers) {
        GCExtendedGamepad *pad = controller.extendedGamepad; if (!pad) continue;
        float y = pad.leftThumbstick.yAxis.value, x = pad.leftThumbstick.xAxis.value;
        if (pad.dpad.up.pressed || y > .6f) now |= PAD_UP;
        if (pad.dpad.down.pressed || y < -.6f) now |= PAD_DOWN;
        if (pad.dpad.left.pressed || x < -.6f) now |= PAD_LEFT;
        if (pad.dpad.right.pressed || x > .6f) now |= PAD_RIGHT;
        if (pad.buttonA.pressed) now |= PAD_OK;
        if (pad.buttonB.pressed || pad.buttonMenu.pressed) now |= PAD_BACK;
        if (pad.buttonY.pressed) now |= SPAWN_PAD_FAVORITE;   /* the Spawn page's favorite */
        if (pad.leftTrigger.value > .5f) now |= PAD_PAGE_UP;
        if (pad.rightTrigger.value > .5f) now |= PAD_PAGE_DOWN;
    }
    unsigned fresh = now & ~held, repeat = 0; CFTimeInterval t = sender.timestamp;
    if (fresh) repeatAt = t + .35;
    else if ((now & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT | PAD_PAGE_UP | PAD_PAGE_DOWN)) && t >= repeatAt) {
        repeat = now & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT | PAD_PAGE_UP | PAD_PAGE_DOWN); repeatAt = t + .09;
    }
    held = now;
    if (fresh | repeat) [self press:fresh | repeat];
}

- (BOOL)canBecomeFirstResponder { return YES; }
- (void)viewDidAppear:(BOOL)animated { [super viewDidAppear:animated]; [self becomeFirstResponder]; }
- (NSArray<UIKeyCommand *> *)keyCommands {
    NSArray *keys = @[UIKeyInputUpArrow, UIKeyInputDownArrow, UIKeyInputLeftArrow, UIKeyInputRightArrow, @"\r", UIKeyInputEscape];
    NSMutableArray *commands = [NSMutableArray array];
    for (NSString *k in keys) {
        UIKeyCommand *c = [UIKeyCommand keyCommandWithInput:k modifierFlags:0 action:@selector(key:)];
        c.wantsPriorityOverSystemBehavior = YES; [commands addObject:c];
    }
    return commands;
}
- (void)key:(UIKeyCommand *)command {
    NSString *k = command.input;
    [self press:[k isEqualToString:UIKeyInputUpArrow] ? PAD_UP : [k isEqualToString:UIKeyInputDownArrow] ? PAD_DOWN :
                [k isEqualToString:UIKeyInputLeftArrow] ? PAD_LEFT : [k isEqualToString:UIKeyInputRightArrow] ? PAD_RIGHT :
                [k isEqualToString:UIKeyInputEscape] ? PAD_BACK : PAD_OK];
}
@end
