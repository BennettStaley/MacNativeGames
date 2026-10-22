/* Tools › Spawn in the Mac settings sidebar (included by runtime/settings_panel_mac.h only): a category column (Favorites,
   Recents, Creatures, NPCs, Items, Weapons, Armor, Zonai, Objects), a search field over every category, and the actors
   of the app's actor list (runtime/spawn_list.h; about 10,000 Objects, so the list is a recycling NSTableView). Each row
   shows the display name and, dimmed, the internal name; ★ marks a favorite. Amount 1-20, and Place (In front, Above,
   Far, At Link's feet; P, or ← on the category column, cycles it). A, Return or a click spawns
   (runtime/actor_spawn.c, 1.4.3), Y or F marks a favorite, every spawn goes to Recents; both are kept in spawn-picks.txt
   beside the profile. A toast in the game window's corner says what was spawned or why not (TOTKSpawnToast).
   Levels inside the tab: the category column, then the list; B / Escape goes back one level. The iPhone / iPad version is
   ios/Sources/spawn_menu.m. */
#pragma once
#import <Cocoa/Cocoa.h>
#include "actor_spawn.h"
#include "spawn_list.h"

enum { SPAWN_PANEL_WIDTH = 660, SPAWN_CATEGORY_WIDTH = 120, SPAWN_ROW_HEIGHT = 34 };
static NSWindow *settings_window(void);   /* frontend.m: the game window (the toast's place) */
static NSString *const SPAWN_LIST_PATH = @"build/actor_list.tsv";   /* in the game folder (build_app.py packages it) */

/* ---- the toast: a line in the game window's top-right corner for 2.5 s ---- */

@interface TOTKSpawnToast : NSTextField
+ (void)show:(NSString *)text failed:(BOOL)failed in:(NSView *)host;
@end
@implementation TOTKSpawnToast
+ (void)show:(NSString *)text failed:(BOOL)failed in:(NSView *)host {
    static TOTKSpawnToast *toast; static NSUInteger generation;
    if (!host) return;
    if (!toast) {
        toast = [TOTKSpawnToast labelWithString:@""];
        toast.font = [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold]; toast.alignment = NSTextAlignmentCenter;
        toast.wantsLayer = YES; toast.layer.cornerRadius = 8; toast.drawsBackground = NO;
        toast.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;
    }
    toast.stringValue = [@"  " stringByAppendingString:[text stringByAppendingString:@"  "]];
    toast.textColor = failed ? NSColor.blackColor : NSColor.whiteColor;
    toast.layer.backgroundColor = (failed ? NSColor.systemOrangeColor : [NSColor colorWithCalibratedRed:0.07 green:0.08 blue:0.09 alpha:0.88]).CGColor;
    [toast sizeToFit];
    NSSize size = NSMakeSize(MIN(toast.frame.size.width + 8, host.bounds.size.width - 40), toast.frame.size.height + 10);
    CGFloat y = host.isFlipped ? 16 : host.bounds.size.height - size.height - 16;
    toast.frame = NSMakeRect(host.bounds.size.width - size.width - 16, y, size.width, size.height);
    toast.alphaValue = 1;
    [host addSubview:toast positioned:NSWindowAbove relativeTo:nil];
    NSUInteger mine = ++generation;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(2.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        if (mine != generation) return;
        [NSAnimationContext runAnimationGroup:^(NSAnimationContext *c) { c.duration = 0.4; toast.animator.alphaValue = 0; }
                            completionHandler:^{ if (mine == generation) [toast removeFromSuperview]; }];
    });
    fprintf(stderr, "[spawn] toast: %s\n", text.UTF8String);
}
@end

/* A table that leaves the keys with the menu (it still takes clicks). */
@interface TOTKSpawnTable : NSTableView @end
@implementation TOTKSpawnTable
- (BOOL)acceptsFirstResponder { return NO; }
@end

@interface TOTKSpawnPane : NSView <NSTableViewDataSource, NSTableViewDelegate, NSSearchFieldDelegate>
- (instancetype)initWithMenu:(NSView *)menu;
- (void)refresh;                 /* shown: the list file once, the picks, the rows */
- (void)enter;                   /* from the section bar: the category column */
- (BOOL)back;                    /* YES: went from the list to the categories (else the menu goes to its section bar) */
- (void)moveFocus:(int)dir;
- (void)step:(int)dir;           /* 0 = A / Return */
- (void)page:(int)dir;
- (BOOL)key:(NSEvent *)e;        /* YES: handled */
- (void)button:(char)b;          /* 'X' Show unnamed, 'Y' favorite, 'L' next Place */
- (void)showFocus:(BOOL)active;  /* active: the menu's focus is in this tab (not on the section bar) */
- (NSString *)hint;
@end

@implementation TOTKSpawnPane {
    __weak NSView *menu;
    TOTKSpawnTable *categories, *table;
    NSScrollView *categoryScroll, *tableScroll;
    NSSearchField *search;
    NSStepper *amountStepper; NSTextField *amountLabel, *status, *empty;
    NSButton *unnamedBox;
    NSPopUpButton *placePopup;
    SpawnList list; SpawnPicks picks; BOOL loaded, active;
    int category, level, amount;     /* level 0: the category column, 1: the list */
    int *shown, nshown;
    NSString *query;
}

static TOTKSpawnPane *spawnPane;   /* the observer's way back (one menu) */
static void spawn_observed(const char *name, int requested, int created, const char *problem) {
    NSString *n = @(name), *why = problem ? @(problem) : nil;
    dispatch_async(dispatch_get_main_queue(), ^{ [spawnPane spawned:n requested:requested created:created problem:why]; });
}

- (instancetype)initWithMenu:(NSView *)m {
    if (!(self = [super initWithFrame:NSZeroRect])) return nil;
    menu = m; spawnPane = self; amount = 1; category = SPAWN_CAT_ITEMS; query = @"";
    self.translatesAutoresizingMaskIntoConstraints = NO;
    runtime_spawn_observe(spawn_observed);

    search = [NSSearchField new]; search.placeholderString = @"Search every category (/ or ⌘F)"; search.delegate = self;
    search.sendsSearchStringImmediately = YES; search.target = self; search.action = @selector(searched:);
    amountStepper = [NSStepper new]; amountStepper.minValue = 1; amountStepper.maxValue = SPAWN_COUNT_MAX; amountStepper.integerValue = 1;
    amountStepper.target = self; amountStepper.action = @selector(amountChanged:); amountStepper.refusesFirstResponder = YES;
    amountLabel = [NSTextField labelWithString:@"× 1"]; amountLabel.font = [NSFont monospacedDigitSystemFontOfSize:13 weight:NSFontWeightSemibold];
    [amountLabel.widthAnchor constraintEqualToConstant:36].active = YES;
    NSTextField *amountName = [NSTextField labelWithString:@"Amount"]; amountName.textColor = NSColor.secondaryLabelColor; amountName.font = [NSFont systemFontOfSize:11];
    placePopup = [NSPopUpButton new]; placePopup.refusesFirstResponder = YES; placePopup.controlSize = NSControlSizeSmall;
    placePopup.font = [NSFont systemFontOfSize:11]; placePopup.target = self; placePopup.action = @selector(placeChanged:);
    for (int i = 0; i < SPAWN_PLACE_COUNT; i++) [placePopup addItemWithTitle:@(SPAWN_PLACE_NAMES[i])];
    placePopup.toolTip = @"Where spawns appear (P, or ← on the categories, changes it)";
    unnamedBox = [NSButton checkboxWithTitle:@"Show unnamed" target:self action:@selector(unnamedClicked:)];
    unnamedBox.font = [NSFont systemFontOfSize:11]; unnamedBox.refusesFirstResponder = YES;
    status = [NSTextField wrappingLabelWithString:@""]; status.font = [NSFont systemFontOfSize:11]; status.textColor = NSColor.secondaryLabelColor;
    empty = [NSTextField wrappingLabelWithString:@""]; empty.font = [NSFont systemFontOfSize:13]; empty.textColor = NSColor.secondaryLabelColor;
    empty.alignment = NSTextAlignmentCenter;

    categoryScroll = [self makeTable:@"category"]; categories = categoryScroll.documentView; categories.rowHeight = 24;
    tableScroll = [self makeTable:@"actor"]; table = tableScroll.documentView; table.rowHeight = SPAWN_ROW_HEIGHT;
    table.usesAlternatingRowBackgroundColors = NO;

    for (NSView *v in @[search, placePopup, amountName, amountStepper, amountLabel, categoryScroll, tableScroll, unnamedBox, status, empty]) {
        v.translatesAutoresizingMaskIntoConstraints = NO; [self addSubview:v];
    }
    [NSLayoutConstraint activateConstraints:@[
        [search.topAnchor constraintEqualToAnchor:self.topAnchor], [search.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
        [amountLabel.trailingAnchor constraintEqualToAnchor:self.trailingAnchor], [amountLabel.centerYAnchor constraintEqualToAnchor:search.centerYAnchor],
        [amountStepper.trailingAnchor constraintEqualToAnchor:amountLabel.leadingAnchor constant:-2], [amountStepper.centerYAnchor constraintEqualToAnchor:search.centerYAnchor],
        [amountName.trailingAnchor constraintEqualToAnchor:amountStepper.leadingAnchor constant:-4], [amountName.centerYAnchor constraintEqualToAnchor:search.centerYAnchor],
        [placePopup.trailingAnchor constraintEqualToAnchor:amountName.leadingAnchor constant:-12], [placePopup.centerYAnchor constraintEqualToAnchor:search.centerYAnchor],
        [search.trailingAnchor constraintEqualToAnchor:placePopup.leadingAnchor constant:-8],
        [categoryScroll.topAnchor constraintEqualToAnchor:search.bottomAnchor constant:8], [categoryScroll.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
        [categoryScroll.widthAnchor constraintEqualToConstant:SPAWN_CATEGORY_WIDTH], [categoryScroll.bottomAnchor constraintEqualToAnchor:unnamedBox.topAnchor constant:-8],
        [tableScroll.topAnchor constraintEqualToAnchor:categoryScroll.topAnchor], [tableScroll.leadingAnchor constraintEqualToAnchor:categoryScroll.trailingAnchor constant:8],
        [tableScroll.trailingAnchor constraintEqualToAnchor:self.trailingAnchor], [tableScroll.bottomAnchor constraintEqualToAnchor:categoryScroll.bottomAnchor],
        [empty.centerYAnchor constraintEqualToAnchor:tableScroll.centerYAnchor], [empty.leadingAnchor constraintEqualToAnchor:tableScroll.leadingAnchor constant:20],
        [empty.trailingAnchor constraintEqualToAnchor:tableScroll.trailingAnchor constant:-20],
        [unnamedBox.leadingAnchor constraintEqualToAnchor:self.leadingAnchor], [unnamedBox.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],
        [status.leadingAnchor constraintEqualToAnchor:unnamedBox.trailingAnchor constant:16], [status.trailingAnchor constraintLessThanOrEqualToAnchor:self.trailingAnchor],
        [status.centerYAnchor constraintEqualToAnchor:unnamedBox.centerYAnchor]]];
    return self;
}
- (BOOL)isFlipped { return YES; }

- (NSScrollView *)makeTable:(NSString *)name {   /* the scroll view, its document the table */
    TOTKSpawnTable *t = [TOTKSpawnTable new];
    NSTableColumn *c = [[NSTableColumn alloc] initWithIdentifier:name]; c.resizingMask = NSTableColumnAutoresizingMask;
    [t addTableColumn:c]; t.headerView = nil; t.dataSource = self; t.delegate = self; t.style = NSTableViewStylePlain;
    t.backgroundColor = NSColor.clearColor; t.intercellSpacing = NSMakeSize(0, 2); t.columnAutoresizingStyle = NSTableViewFirstColumnOnlyAutoresizingStyle;
    t.target = self; t.action = @selector(clicked:);
    t.wantsLayer = YES; t.layer.cornerRadius = 8;
    NSScrollView *s = [NSScrollView new]; s.documentView = t; s.hasVerticalScroller = YES; s.drawsBackground = NO; s.autohidesScrollers = YES;
    s.wantsLayer = YES; s.layer.cornerRadius = 8; s.layer.borderColor = NSColor.controlAccentColor.CGColor;
    return s;
}

/* ---- data ---- */

- (int)availability { return runtime_spawn_available(); }
- (void)refresh {
    if (!loaded) {
        loaded = YES;
        int n = spawn_list_load(&list, SPAWN_LIST_PATH.fileSystemRepresentation);
        fprintf(stderr, "[spawn] actor list: %d actors from %s\n", n, SPAWN_LIST_PATH.UTF8String);
        spawn_picks_load(&picks);
        [self setPlace:picks.place save:NO];
        free(shown); shown = malloc(sizeof *shown * (size_t)(list.count ? list.count : 1));
    }
    [self filter];
    [categories reloadData];
    [categories selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)category] byExtendingSelection:NO];
}
- (void)filter {
    nshown = list.count && shown ? spawn_list_filter(&list, &picks, category, query.UTF8String, unnamedBox.state == NSControlStateValueOn, shown, list.count) : 0;
    [table reloadData];
    int a = self.availability;
    NSString *why = a == SPAWN_ERR_UNSUPPORTED ? @"Spawning needs 1.4.3 for now."
                  : a == SPAWN_ERR_DISABLED ? @"The spawner is off: this game's create call did not match the one it was made for (the log says more)."
                  : !list.count ? @"This app has no actor list. Rebuild it (build_app.py makes build/actor_list.tsv from your game files)."
                  : nshown ? nil
                  : query.length ? @"Nothing matches."
                  : category == SPAWN_CAT_FAVORITES ? @"No favorites yet: Y (or F) on an actor marks it."
                  : category == SPAWN_CAT_RECENTS ? @"Nothing spawned yet."
                  : @"Nothing here.";
    empty.stringValue = why ?: @""; empty.hidden = !why; tableScroll.hidden = why != nil && (a != SPAWN_OK || !list.count);
    if (!status.stringValue.length || ![status.stringValue hasPrefix:@"Spawn"])
        status.stringValue = query.length ? [NSString stringWithFormat:@"%d found in every category", nshown]
                                          : [NSString stringWithFormat:@"%d in %s", nshown, SPAWN_CAT_NAMES[category]];
    [self showFocus:active];
}
- (int)focusedRow { NSInteger r = table.selectedRow; return r >= 0 && r < nshown ? (int)r : -1; }
- (const SpawnActor *)focusedActor { int r = self.focusedRow; return r >= 0 ? &list.actor[shown[r]] : NULL; }
- (NSString *)displayOf:(NSString *)name {
    int i = spawn_list_find(&list, name.UTF8String);
    return i >= 0 ? @(list.actor[i].display) : name;
}

/* ---- the tables ---- */

- (NSInteger)numberOfRowsInTableView:(NSTableView *)t { return t == categories ? SPAWN_SHOWN_CATS : nshown; }
- (NSView *)tableView:(NSTableView *)t viewForTableColumn:(NSTableColumn *)column row:(NSInteger)row {
    (void)column;
    if (t == categories) {
        NSTextField *cell = [t makeViewWithIdentifier:@"category" owner:self];
        if (!cell) { cell = [NSTextField labelWithString:@""]; cell.identifier = @"category"; cell.font = [NSFont systemFontOfSize:13]; }
        cell.stringValue = @(SPAWN_CAT_NAMES[row]);
        return cell;
    }
    NSTableCellView *cell = [t makeViewWithIdentifier:@"actor" owner:self];
    if (!cell) {   /* made once per visible row, then recycled */
        cell = [NSTableCellView new]; cell.identifier = @"actor";
        NSTextField *name = [NSTextField labelWithString:@""], *internal = [NSTextField labelWithString:@""], *star = [NSTextField labelWithString:@""];
        name.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium]; name.lineBreakMode = NSLineBreakByTruncatingTail;
        internal.font = [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular]; internal.textColor = NSColor.tertiaryLabelColor;
        internal.lineBreakMode = NSLineBreakByTruncatingMiddle; internal.identifier = @"internal";
        star.font = [NSFont systemFontOfSize:14]; star.textColor = NSColor.systemYellowColor; star.identifier = @"star";
        cell.textField = name;
        for (NSTextField *f in @[name, internal, star]) { f.translatesAutoresizingMaskIntoConstraints = NO; [cell addSubview:f]; }
        [name setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
        [internal setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
        [NSLayoutConstraint activateConstraints:@[
            [name.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:8], [name.topAnchor constraintEqualToAnchor:cell.topAnchor constant:2],
            [name.trailingAnchor constraintLessThanOrEqualToAnchor:star.leadingAnchor constant:-6],
            [internal.leadingAnchor constraintEqualToAnchor:name.leadingAnchor], [internal.topAnchor constraintEqualToAnchor:name.bottomAnchor],
            [internal.trailingAnchor constraintLessThanOrEqualToAnchor:star.leadingAnchor constant:-6],
            [star.trailingAnchor constraintEqualToAnchor:cell.trailingAnchor constant:-8], [star.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor]]];
    }
    const SpawnActor *a = &list.actor[shown[row]];
    cell.textField.stringValue = @(a->display);
    for (NSView *v in cell.subviews) {
        if ([v.identifier isEqual:@"internal"])
            ((NSTextField *)v).stringValue = query.length ? [NSString stringWithFormat:@"%s · %s", a->name, SPAWN_CAT_NAMES[a->category]] : @(a->name);
        if ([v.identifier isEqual:@"star"]) ((NSTextField *)v).stringValue = spawn_picks_is_favorite(&picks, a->name) ? @"★" : @"";
    }
    return cell;
}
- (void)clicked:(NSTableView *)t {
    NSInteger row = t.clickedRow;
    [self.window makeFirstResponder:menu];
    if (row < 0) return;
    if (t == categories) { [self chooseCategory:(int)row]; level = 0; }
    else { level = 1; [self spawnRow:(int)row]; }
    [self showFocus:YES];
}

/* ---- choosing ---- */

- (void)chooseCategory:(int)c {
    if (c < 0 || c >= SPAWN_SHOWN_CATS) return;
    category = c;
    if (query.length) { query = @""; search.stringValue = @""; }
    [categories selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)c] byExtendingSelection:NO];
    [categories scrollRowToVisible:c];
    status.stringValue = @"";
    [self filter];
    if (nshown) [table selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
    [table scrollRowToVisible:0];
}
- (void)selectRow:(NSInteger)r {
    if (!nshown) return;
    r = MAX(0, MIN((NSInteger)nshown - 1, r));
    [table selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)r] byExtendingSelection:NO];
    [table scrollRowToVisible:r];
}
- (void)showFocus:(BOOL)on {
    active = on;
    categoryScroll.layer.borderWidth = on && level == 0 ? 2 : 0;
    tableScroll.layer.borderWidth = on && level == 1 ? 2 : 0;
}
- (void)enter { level = 0; if (table.selectedRow < 0 && nshown) [self selectRow:0]; [self showFocus:YES]; }
- (BOOL)back {
    if (level == 1) { level = 0; [self showFocus:YES]; return YES; }
    return NO;
}
- (void)moveFocus:(int)dir {
    if (level == 0) { [self chooseCategory:MAX(0, MIN(SPAWN_SHOWN_CATS - 1, category + dir))]; return; }
    [self selectRow:table.selectedRow < 0 ? 0 : table.selectedRow + dir];
}
- (void)page:(int)dir {
    if (level == 0) { [self chooseCategory:dir < 0 ? 0 : SPAWN_SHOWN_CATS - 1]; return; }
    NSInteger visible = MAX(1, (NSInteger)(tableScroll.contentView.bounds.size.height / (SPAWN_ROW_HEIGHT + 2)) - 1);
    [self selectRow:MAX(0, table.selectedRow) + dir * visible];
}
- (void)setAmount:(int)n {
    amount = MAX(1, MIN(SPAWN_COUNT_MAX, n));
    amountStepper.integerValue = amount; amountLabel.stringValue = [NSString stringWithFormat:@"× %d", amount];
}
- (void)setPlace:(int)p save:(BOOL)save {
    p = ((p % SPAWN_PLACE_COUNT) + SPAWN_PLACE_COUNT) % SPAWN_PLACE_COUNT;
    picks.place = p; runtime_spawn_set_placement(p); [placePopup selectItemAtIndex:p];
    if (!save) return;
    if (spawn_picks_save(&picks)) fprintf(stderr, "[spawn] could not save spawn-picks.txt\n");
    status.stringValue = [NSString stringWithFormat:@"Spawn place: %s", SPAWN_PLACE_NAMES[p]];
}
- (void)placeChanged:(NSPopUpButton *)b { [self setPlace:(int)b.indexOfSelectedItem save:YES]; [self.window makeFirstResponder:menu]; }
- (void)amountChanged:(NSStepper *)s { [self setAmount:(int)s.integerValue]; [self.window makeFirstResponder:menu]; }
- (void)step:(int)dir {
    if (level == 0) {
        if (dir < 0) { [self setPlace:picks.place + 1 save:YES]; return; }   /* ← on the categories: the next Place */
        level = 1; if (table.selectedRow < 0) [self selectRow:0];
        [self showFocus:YES]; return;
    }
    if (dir) { [self setAmount:amount + dir]; return; }   /* ← → the amount */
    if (self.focusedRow >= 0) [self spawnRow:self.focusedRow];
}
- (void)button:(char)b {
    if (b == 'X') { unnamedBox.state = unnamedBox.state == NSControlStateValueOn ? NSControlStateValueOff : NSControlStateValueOn; [self unnamedClicked:unnamedBox]; }
    else if (b == 'Y') [self toggleFavorite];
    else if (b == 'L') [self setPlace:picks.place + 1 save:YES];
}
- (void)unnamedClicked:(NSButton *)b { (void)b; status.stringValue = @""; [self filter]; [self.window makeFirstResponder:menu]; }
- (void)toggleFavorite {
    const SpawnActor *a = level == 1 ? self.focusedActor : NULL;
    if (!a) return;
    NSString *name = @(a->name), *display = @(a->display);
    int on = spawn_picks_toggle_favorite(&picks, a->name);
    if (on < 0) { status.stringValue = @"The favorites are full (200)."; return; }
    if (spawn_picks_save(&picks)) fprintf(stderr, "[spawn] could not save spawn-picks.txt\n");
    status.stringValue = [NSString stringWithFormat:on ? @"Spawn favorite: %@" : @"Spawn favorite removed: %@", display];
    NSInteger row = table.selectedRow;
    if (category == SPAWN_CAT_FAVORITES && !query.length) { [self filter]; [self selectRow:row]; }
    else [table reloadDataForRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row] columnIndexes:[NSIndexSet indexSetWithIndex:0]];
    (void)name;
}

/* ---- spawning ---- */

- (void)spawnRow:(int)row {
    if (row < 0 || row >= nshown) return;
    [self selectRow:row];
    const SpawnActor *a = &list.actor[shown[row]];
    NSString *display = @(a->display);
    int r = runtime_spawn_actor(a->name, amount);
    if (r != SPAWN_OK) {
        NSString *why = r == SPAWN_ERR_UNSUPPORTED ? @"spawning needs 1.4.3 for now" : @(runtime_spawn_error(r));
        status.stringValue = [NSString stringWithFormat:@"Spawn failed: %@", why];
        [TOTKSpawnToast show:[NSString stringWithFormat:@"Could not spawn %@: %@", display, why] failed:YES in:settings_window().contentView];
        return;
    }
    fprintf(stderr, "[spawn] menu: %s x%d\n", a->name, amount);
    spawn_picks_add_recent(&picks, a->name);
    if (spawn_picks_save(&picks)) fprintf(stderr, "[spawn] could not save spawn-picks.txt\n");
    status.stringValue = [NSString stringWithFormat:@"Spawning %@ ×%d…", display, amount];
    if (category == SPAWN_CAT_RECENTS && !query.length) { [self filter]; [self selectRow:0]; }
}
/* The frame-end thread's answer (actor_spawn.c), on the main thread. */
- (void)spawned:(NSString *)name requested:(int)requested created:(int)created problem:(NSString *)problem {
    NSString *display = [self displayOf:name], *text;
    if (created == requested) text = [NSString stringWithFormat:@"Spawned %@ ×%d", display, created];
    else if (created) text = [NSString stringWithFormat:@"Spawned %@ ×%d of %d: %@", display, created, requested, problem ?: @"?"];
    else text = [NSString stringWithFormat:@"Could not spawn %@: %@", display, problem ?: @"?"];
    status.stringValue = text;
    [TOTKSpawnToast show:text failed:created < requested in:settings_window().contentView];
}

/* ---- keys ---- */

- (void)focusSearch { [self.window makeFirstResponder:search]; }
- (void)searched:(NSSearchField *)f {
    query = f.stringValue ?: @"";
    status.stringValue = @"";
    [self filter];
    if (nshown) [self selectRow:0];
}
- (BOOL)control:(NSControl *)c textView:(NSTextView *)tv doCommandBySelector:(SEL)cmd {
    (void)c; (void)tv;
    if (cmd == @selector(cancelOperation:) || cmd == @selector(moveDown:) || cmd == @selector(insertNewline:)) {   /* back to the list */
        [self.window makeFirstResponder:menu];
        level = nshown ? 1 : 0; if (nshown && table.selectedRow < 0) [self selectRow:0];
        [self showFocus:YES];
        return YES;
    }
    return NO;
}
- (BOOL)key:(NSEvent *)e {
    BOOL cmd = (e.modifierFlags & NSEventModifierFlagCommand) != 0;
    switch (e.keyCode) {
    case 44: [self focusSearch]; return YES;                       /* / */
    case 3: if (cmd) { [self focusSearch]; return YES; }           /* ⌘F; F alone: favorite */
            if (!e.isARepeat) [self toggleFavorite]; return YES;
    case 16: if (!e.isARepeat) [self toggleFavorite]; return YES;  /* Y */
    case 32: if (!e.isARepeat) [self button:'X']; return YES;      /* U: Show unnamed */
    case 35: if (!e.isARepeat) [self button:'L']; return YES;      /* P: Place */
    case 27: case 78: [self setAmount:amount - 1]; return YES;     /* - */
    case 24: case 69: [self setAmount:amount + 1]; return YES;     /* = + */
    default: return NO;
    }
}
- (NSString *)hint {
    return @"↑↓ choose   → list   ←→ amount   Return spawn   F favorite   P place   U unnamed   / search   Esc back\n"
           "Controller: D-pad, A spawn, ←→ amount, Y favorite, ← on categories place, X unnamed, LT RT page, B back";
}
@end
