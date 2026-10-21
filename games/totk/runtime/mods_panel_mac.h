/* The Mods tab of the Mac settings sidebar (included by runtime/settings_panel_mac.h only): the installed mods (on / off,
   load order, remove, what the loader noticed about each) and a GameBanana browser (search, sort, category, a mod's
   pictures, text, license and files, install with progress) on runtime/mod_browser.m. The sidebar widens while it shows.
   Keyboard: ↑↓ choose, Return or Space switches a mod on or off, ⌘↑ ⌘↓ move it, Delete removes it, ← → Installed /
   Browse, / or ⌘F search; controller: D-pad, A, X / Y move. The search field takes typing while it has the focus; Escape
   or ↓ hands the keys back to the menu. The iPhone / iPad version is ios/Sources/mods_menu.m. Needs TOTKFlippedView
   (settings_panel_mac.h, before its include). */
#pragma once
#import <Cocoa/Cocoa.h>
#include "mods.h"
#import "mod_browser.h"

enum { MODS_PANEL_WIDTH = 760 };
static NSString *const MODS_HINT = @"↑↓ choose   Return on / off   ⌘↑↓ move   ⌫ remove   ←→ Installed / Browse   / search   Esc close\n"
                                   "Controller: D-pad, A on / off, X Y move, B close";
static NSString *const MODS_CONFLICT_TEXT = @"Two mods change the same file: the first in the list wins. Merging is coming later";

@interface TOTKModsClickView : NSView
@property (nonatomic, copy) void (^onClick)(void);
@end
@implementation TOTKModsClickView
- (BOOL)isFlipped { return YES; }
- (void)mouseDown:(NSEvent *)e { (void)e; if (self.onClick) self.onClick(); }
@end

static NSColor *mods_color(double w, double a) { return [NSColor colorWithCalibratedWhite:w alpha:a]; }
static NSTextField *mods_label(NSString *text, CGFloat size, NSFontWeight weight, NSColor *color, CGFloat wrap) {
    NSTextField *l = wrap > 0 ? [NSTextField wrappingLabelWithString:text ?: @""] : [NSTextField labelWithString:text ?: @""];
    l.font = [NSFont systemFontOfSize:size weight:weight]; l.textColor = color;
    if (wrap > 0) { l.preferredMaxLayoutWidth = wrap; [l setContentCompressionResistancePriority:NSLayoutPriorityDefaultHigh forOrientation:NSLayoutConstraintOrientationVertical]; }
    else l.lineBreakMode = NSLineBreakByTruncatingTail;
    l.translatesAutoresizingMaskIntoConstraints = NO;
    return l;
}
static NSButton *mods_button(NSString *title, NSString *symbol, id target, SEL action) {
    NSButton *b = symbol ? [NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:symbol accessibilityDescription:title] target:target action:action]
                         : [NSButton buttonWithTitle:title target:target action:action];
    b.controlSize = NSControlSizeSmall; b.bezelStyle = NSBezelStyleRounded; b.refusesFirstResponder = YES;
    if (symbol) b.toolTip = title;
    b.translatesAutoresizingMaskIntoConstraints = NO;
    return b;
}
static NSStackView *mods_stack(NSArray *views, BOOL vertical, CGFloat spacing) {
    NSStackView *s = [NSStackView stackViewWithViews:views];
    s.orientation = vertical ? NSUserInterfaceLayoutOrientationVertical : NSUserInterfaceLayoutOrientationHorizontal;
    s.alignment = vertical ? NSLayoutAttributeLeading : NSLayoutAttributeCenterY; s.spacing = spacing;
    s.translatesAutoresizingMaskIntoConstraints = NO;
    return s;
}
static NSScrollView *mods_scroll(NSStackView *content) {
    NSScrollView *scroll = [NSScrollView new]; scroll.hasVerticalScroller = YES; scroll.drawsBackground = NO; scroll.autohidesScrollers = YES;
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    NSView *doc = [TOTKFlippedView new]; doc.translatesAutoresizingMaskIntoConstraints = NO;
    [doc addSubview:content]; scroll.documentView = doc;
    [NSLayoutConstraint activateConstraints:@[
        [content.topAnchor constraintEqualToAnchor:doc.topAnchor], [content.leadingAnchor constraintEqualToAnchor:doc.leadingAnchor],
        [content.trailingAnchor constraintEqualToAnchor:doc.trailingAnchor], [content.bottomAnchor constraintEqualToAnchor:doc.bottomAnchor],
        [doc.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor]]];
    return scroll;
}
static void mods_clear(NSStackView *s) { for (NSView *v in s.arrangedSubviews.copy) { [s removeArrangedSubview:v]; [v removeFromSuperview]; } }
static NSString *mods_count_text(NSInteger n) {
    return n >= 1000000 ? [NSString stringWithFormat:@"%.1fM", n / 1e6] : n >= 10000 ? [NSString stringWithFormat:@"%ldk", (long)(n / 1000)] :
           n >= 1000 ? [NSString stringWithFormat:@"%.1fk", n / 1e3] : [NSString stringWithFormat:@"%ld", (long)n];
}
static NSString *mods_when(NSDate *d) {
    if (!d) return @"";
    static NSRelativeDateTimeFormatter *f; if (!f) { f = [NSRelativeDateTimeFormatter new]; f.unitsStyle = NSRelativeDateTimeFormatterUnitsStyleShort; }
    return [f localizedStringForDate:d relativeToDate:NSDate.date];
}

/* The lines the menu shows under an installed mod. Code, TKCL, conflicts and other versions in the words the player was
   promised (later updates); the rest from mods_notice. */
static NSArray<NSString *> *mods_notices(const ModInfo *m) {
    static const unsigned order[] = {MOD_HAS_CODE, MOD_TKCL, MOD_CONFLICT, MOD_OLD_VERSION, MOD_OWN_RSTB, MOD_EMPTY, MOD_HAS_CHEATS};
    NSMutableArray *out = [NSMutableArray array];
    for (size_t i = 0; i < sizeof order / sizeof *order; i++) {
        unsigned f = order[i]; if (!(m->flags & f)) continue;
        const char *s = mods_notice(f); NSString *line = s && *s ? @(s) : @"";
        if (f == MOD_HAS_CODE) line = @"Code patches (exefs) aren't applied yet — coming in a later update";
        if (f == MOD_TKCL) line = @"TKMM .tkcl packages aren't supported yet — coming later";
        if (f == MOD_CONFLICT) line = m->conflicts ? [MODS_CONFLICT_TEXT stringByAppendingFormat:@" (%u file%s)", m->conflicts, m->conflicts == 1 ? "" : "s"] : MODS_CONFLICT_TEXT;
        if (f == MOD_OLD_VERSION) line = [NSString stringWithFormat:@"Made for %s: may not work on 1.4.3", m->game_versions[0] ? m->game_versions : "an older game version"];
        if (line.length) [out addObject:line];
    }
    return out;
}

@interface TOTKModsPane : NSView <NSSearchFieldDelegate>
- (instancetype)initWithMenu:(NSView *)menu;
- (void)refresh;                 /* the tab is shown: scan the folder again, rebuild */
- (BOOL)key:(NSEvent *)e;        /* YES: handled */
- (void)moveFocus:(int)dir;
- (void)step:(int)dir;           /* 0 = activate */
- (void)moveMod:(int)dir;        /* load order, the focused mod */
- (void)showBrowse:(BOOL)browse;
@property (nonatomic, readonly) BOOL browsing;
@end

@implementation TOTKModsPane {
    __weak NSView *menu;
    NSSegmentedControl *sub;
    NSTextField *banner, *footer;
    NSButton *restartNow;   /* on the banner: close the game and open it again (runtime_front_restart) */
    NSView *body;
    /* installed */
    NSScrollView *installedScroll; NSStackView *installedList;
    NSMutableArray<NSView *> *rows; NSMutableArray<NSString *> *rowFolder;
    NSInteger focus; BOOL alertOpen;
    /* browse */
    NSView *browseView;
    NSSearchField *search; NSPopUpButton *sortPopup, *categoryPopup;
    NSScrollView *resultScroll, *detailScroll; NSStackView *resultList, *detailList;
    NSMutableArray<TOTKModSummary *> *results; NSMutableArray<NSView *> *resultRows;
    NSInteger page, total, selected, listGen, detailGen; BOOL loading; NSString *listError;
    NSArray<NSNumber *> *categoryIds;
    TOTKModDetails *details;
    NSImageView *bigImage;
    NSMutableDictionary<NSNumber *, NSDictionary *> *installState;   /* file id -> fraction, stage, done, error, note */
    NSMutableDictionary<NSNumber *, NSView *> *fileBoxes;
}

- (instancetype)initWithMenu:(NSView *)m {
    if (!(self = [super initWithFrame:NSZeroRect])) return nil;
    menu = m; rows = [NSMutableArray array]; rowFolder = [NSMutableArray array];
    results = [NSMutableArray array]; resultRows = [NSMutableArray array]; selected = -1; page = 0;
    installState = [NSMutableDictionary dictionary]; fileBoxes = [NSMutableDictionary dictionary];
    self.translatesAutoresizingMaskIntoConstraints = NO;

    sub = [NSSegmentedControl segmentedControlWithLabels:@[@"Installed", @"Browse GameBanana"] trackingMode:NSSegmentSwitchTrackingSelectOne
                                                  target:self action:@selector(subClicked:)];
    sub.selectedSegment = 0; sub.refusesFirstResponder = YES; sub.controlSize = NSControlSizeSmall; sub.font = [NSFont systemFontOfSize:11];
    sub.translatesAutoresizingMaskIntoConstraints = NO;
    NSButton *folder = mods_button(@"Show the mods folder", @"folder", self, @selector(openFolder));
    banner = mods_label(@"Restart the game to apply mod changes", 12, NSFontWeightSemibold, NSColor.blackColor, 0);
    banner.drawsBackground = YES; banner.backgroundColor = NSColor.systemOrangeColor; banner.alignment = NSTextAlignmentCenter;
    banner.wantsLayer = YES; banner.layer.cornerRadius = 6;
    footer = mods_label(TOTKModBrowserFooter, 10.5, NSFontWeightRegular, NSColor.tertiaryLabelColor, MODS_PANEL_WIDTH - 60);
    body = [NSView new]; body.translatesAutoresizingMaskIntoConstraints = NO;

    installedList = mods_stack(@[], YES, 4); installedList.edgeInsets = NSEdgeInsetsMake(2, 0, 10, 0);
    installedScroll = mods_scroll(installedList);
    [self buildBrowse];
    for (NSView *v in @[installedScroll, browseView]) {
        [body addSubview:v];
        [NSLayoutConstraint activateConstraints:@[[v.topAnchor constraintEqualToAnchor:body.topAnchor], [v.bottomAnchor constraintEqualToAnchor:body.bottomAnchor],
            [v.leadingAnchor constraintEqualToAnchor:body.leadingAnchor], [v.trailingAnchor constraintEqualToAnchor:body.trailingAnchor]]];
    }
    browseView.hidden = YES;
    restartNow = [NSButton buttonWithTitle:@"Restart now" target:self action:@selector(restart)];
    restartNow.controlSize = NSControlSizeSmall; restartNow.font = [NSFont systemFontOfSize:11]; restartNow.refusesFirstResponder = YES;
    restartNow.translatesAutoresizingMaskIntoConstraints = NO; restartNow.hidden = YES;
    for (NSView *v in @[sub, folder, banner, body, footer, restartNow]) [self addSubview:v];
    [NSLayoutConstraint activateConstraints:@[[restartNow.centerYAnchor constraintEqualToAnchor:banner.centerYAnchor],
        [restartNow.trailingAnchor constraintEqualToAnchor:banner.trailingAnchor constant:-4]]];
    [NSLayoutConstraint activateConstraints:@[
        [sub.topAnchor constraintEqualToAnchor:self.topAnchor], [sub.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:8],
        [folder.centerYAnchor constraintEqualToAnchor:sub.centerYAnchor], [folder.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-8],
        [banner.topAnchor constraintEqualToAnchor:sub.bottomAnchor constant:8], [banner.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:8],
        [banner.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-8], [banner.heightAnchor constraintEqualToConstant:22],
        [body.topAnchor constraintEqualToAnchor:banner.bottomAnchor constant:8], [body.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
        [body.trailingAnchor constraintEqualToAnchor:self.trailingAnchor], [body.bottomAnchor constraintEqualToAnchor:footer.topAnchor constant:-6],
        [footer.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:12], [footer.trailingAnchor constraintLessThanOrEqualToAnchor:self.trailingAnchor constant:-12],
        [footer.bottomAnchor constraintEqualToAnchor:self.bottomAnchor]]];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(modsChanged) name:TOTKModsChangedNotification object:nil];
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)browsing { return sub.selectedSegment == 1; }

- (void)refresh { mods_scan(); [self buildInstalled]; [self showBrowse:self.browsing]; }
- (void)modsChanged { [self buildInstalled]; [self rebuildResultsKeepingScroll]; }

- (void)updateBanner {
    BOOL show = mods_changed_since_start() != 0;
    banner.hidden = !show; restartNow.hidden = !show || !runtime_front_can_restart();
    for (NSLayoutConstraint *c in banner.constraints) if (c.firstAttribute == NSLayoutAttributeHeight) c.constant = show ? 22 : 0;
}
- (void)restart { runtime_front_restart(); }

- (void)showBrowse:(BOOL)browse {
    sub.selectedSegment = browse ? 1 : 0;
    installedScroll.hidden = browse; browseView.hidden = !browse;
    if (browse && !results.count && !loading) [self loadPage:1];
    if (browse && !categoryIds) [self loadCategories];
    [self updateBanner];
}
- (void)subClicked:(NSSegmentedControl *)s { [self showBrowse:s.selectedSegment == 1]; [self.window makeFirstResponder:menu]; }

- (void)openFolder { [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:@(mods_dir()) isDirectory:YES]]; }

/* ---- installed ---- */

- (void)buildInstalled {
    mods_clear(installedList); [rows removeAllObjects]; [rowFolder removeAllObjects];
    CGFloat width = MODS_PANEL_WIDTH - 2 * 8 - 14;
    int n = mods_count();
    [sub setLabel:n ? [NSString stringWithFormat:@"Installed (%d)", n] : @"Installed" forSegment:0];
    for (int i = 0; i < n; i++) {
        ModInfo m; if (mods_get(i, &m)) continue;
        NSView *row = [NSView new]; row.wantsLayer = YES; row.layer.cornerRadius = 8; row.translatesAutoresizingMaskIntoConstraints = NO;
        NSSwitch *toggle = [NSSwitch new]; toggle.state = m.enabled ? NSControlStateValueOn : NSControlStateValueOff;
        toggle.target = self; toggle.action = @selector(toggled:); toggle.tag = i; toggle.refusesFirstResponder = YES;
        toggle.translatesAutoresizingMaskIntoConstraints = NO;
        NSTextField *name = mods_label(@(m.name), 13, NSFontWeightMedium, m.enabled ? NSColor.labelColor : NSColor.secondaryLabelColor, 0);
        NSMutableArray *meta = [NSMutableArray arrayWithObject:[NSString stringWithFormat:@"#%d", i + 1]];
        if (m.author[0]) [meta addObject:[@"by " stringByAppendingString:@(m.author)]];
        if (m.version[0]) [meta addObject:[@"v" stringByAppendingString:@(m.version)]];
        if (m.files) [meta addObject:[NSString stringWithFormat:@"%u file%s, %@", m.files, m.files == 1 ? "" : "s",
                                      [NSByteCountFormatter stringFromByteCount:(long long)m.bytes countStyle:NSByteCountFormatterCountStyleFile]]];
        if (m.game_versions[0]) [meta addObject:[@"for " stringByAppendingString:@(m.game_versions)]];
        if (strcmp(m.folder, m.name)) [meta addObject:[NSString stringWithFormat:@"folder “%s”", m.folder]];
        NSStackView *text = mods_stack(@[name, mods_label([meta componentsJoinedByString:@" · "], 11, NSFontWeightRegular, NSColor.secondaryLabelColor, 0)], YES, 2);
        for (NSString *line in mods_notices(&m)) {
            BOOL info = [line containsString:@"ResourceSizeTable"];
            [text addArrangedSubview:mods_label([@"⚠︎ " stringByAppendingString:line], 11, NSFontWeightRegular,
                                                info ? NSColor.secondaryLabelColor : NSColor.systemOrangeColor, width - 200)];
        }
        NSButton *up = mods_button(@"Load earlier (wins over the mods below)", @"chevron.up", self, @selector(moveUp:));
        NSButton *down = mods_button(@"Load later", @"chevron.down", self, @selector(moveDown:));
        NSButton *trash = mods_button(@"Remove…", @"trash", self, @selector(removeClicked:));
        up.tag = down.tag = trash.tag = i; up.enabled = i > 0; down.enabled = i + 1 < n;
        NSStackView *buttons = mods_stack(@[up, down, trash], NO, 4);
        for (NSView *v in @[toggle, text, buttons]) [row addSubview:v];
        [NSLayoutConstraint activateConstraints:@[
            [row.widthAnchor constraintEqualToConstant:width],
            [toggle.leadingAnchor constraintEqualToAnchor:row.leadingAnchor constant:10], [toggle.centerYAnchor constraintEqualToAnchor:row.centerYAnchor],
            [text.leadingAnchor constraintEqualToAnchor:toggle.trailingAnchor constant:10], [text.topAnchor constraintEqualToAnchor:row.topAnchor constant:8],
            [text.bottomAnchor constraintEqualToAnchor:row.bottomAnchor constant:-8],
            [text.trailingAnchor constraintLessThanOrEqualToAnchor:buttons.leadingAnchor constant:-10],
            [buttons.trailingAnchor constraintEqualToAnchor:row.trailingAnchor constant:-10], [buttons.centerYAnchor constraintEqualToAnchor:row.centerYAnchor]]];
        [installedList addArrangedSubview:row]; [rows addObject:row]; [rowFolder addObject:@(m.folder)];
    }
    if (!n) {
        NSTextField *none = mods_label([NSString stringWithFormat:@"No mods yet. Browse GameBanana above, or put a mod's folder (with romfs/ inside) into the mods folder:\n%s",
                                        mods_dir()], 12, NSFontWeightRegular, NSColor.secondaryLabelColor, MODS_PANEL_WIDTH - 60);
        [installedList addArrangedSubview:none];
        [none.leadingAnchor constraintEqualToAnchor:installedList.leadingAnchor constant:12].active = YES;
    }
    NSTextField *order = mods_label(@"Load order: a mod higher in the list wins a file that two mods change. Changes apply at the next start.",
                                    11, NSFontWeightRegular, NSColor.tertiaryLabelColor, MODS_PANEL_WIDTH - 60);
    [installedList addArrangedSubview:order];
    [order.leadingAnchor constraintEqualToAnchor:installedList.leadingAnchor constant:12].active = YES;
    if (focus >= (NSInteger)rows.count) focus = rows.count ? (NSInteger)rows.count - 1 : 0;
    [self showFocus]; [self updateBanner];
}

- (void)showFocus {
    for (NSUInteger k = 0; k < rows.count; k++)
        rows[k].layer.backgroundColor = (NSInteger)k == focus && !self.browsing ? mods_color(1, 0.10).CGColor : mods_color(1, 0.03).CGColor;
    if (!self.browsing && focus < (NSInteger)rows.count) { [installedList layoutSubtreeIfNeeded]; [rows[focus] scrollRectToVisible:NSInsetRect(rows[focus].bounds, 0, -8)]; }
}

- (void)setEnabled:(NSInteger)i on:(BOOL)on {
    if (i < 0 || i >= (NSInteger)rowFolder.count) return;
    mods_set_enabled(rowFolder[i].fileSystemRepresentation, on); mods_save();
    focus = i; [self rebuildInstalledKeepingScroll];
}
- (void)toggled:(NSSwitch *)s { [self setEnabled:s.tag on:s.state == NSControlStateValueOn]; [self.window makeFirstResponder:menu]; }

- (void)rebuildInstalledKeepingScroll {
    CGFloat y = installedScroll.contentView.bounds.origin.y;
    [self buildInstalled];
    [installedScroll.contentView scrollToPoint:NSMakePoint(0, y)]; [installedScroll reflectScrolledClipView:installedScroll.contentView];
    [self showFocus];
}

- (void)move:(NSInteger)i by:(int)dir {
    NSInteger to = i + dir;
    if (i < 0 || i >= (NSInteger)rowFolder.count || to < 0 || to >= (NSInteger)rowFolder.count) return;
    mods_move(rowFolder[i].fileSystemRepresentation, (int)to); mods_save();
    focus = to; [self rebuildInstalledKeepingScroll];
}
- (void)moveUp:(NSButton *)b { [self move:b.tag by:-1]; }
- (void)moveDown:(NSButton *)b { [self move:b.tag by:1]; }
- (void)moveMod:(int)dir { if (!self.browsing) [self move:focus by:dir]; }

- (void)removeClicked:(NSButton *)b { focus = b.tag; [self confirmRemove:b.tag]; }
- (void)confirmRemove:(NSInteger)i {
    if (i < 0 || i >= (NSInteger)rowFolder.count || alertOpen || !self.window) return;
    ModInfo m; if (mods_get((int)i, &m)) return;
    NSString *folder = rowFolder[i];
    NSAlert *a = [NSAlert new];
    a.messageText = [NSString stringWithFormat:@"Remove “%s”?", m.name];
    a.informativeText = [NSString stringWithFormat:@"This deletes its folder “%@” from the mods folder.", folder];
    [a addButtonWithTitle:@"Remove"]; [a addButtonWithTitle:@"Cancel"];
    a.buttons[0].hasDestructiveAction = YES;
    alertOpen = YES;
    [a beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse r) {
        self->alertOpen = NO;
        if (r == NSAlertFirstButtonReturn) { mods_remove(folder.fileSystemRepresentation); mods_save(); mods_scan(); [self rebuildInstalledKeepingScroll]; [self rebuildResultsKeepingScroll]; }
        [self.window makeFirstResponder:self->menu];
    }];
}

/* ---- browse ---- */

- (void)buildBrowse {
    browseView = [NSView new]; browseView.translatesAutoresizingMaskIntoConstraints = NO;
    search = [NSSearchField new]; search.placeholderString = @"Search Tears of the Kingdom mods"; search.delegate = self;
    search.sendsWholeSearchString = YES; search.target = self; search.action = @selector(searched:); search.controlSize = NSControlSizeSmall;
    search.translatesAutoresizingMaskIntoConstraints = NO;
    sortPopup = [NSPopUpButton new]; [sortPopup addItemsWithTitles:TOTKModSortLabels()];
    sortPopup.target = self; sortPopup.action = @selector(sorted:); sortPopup.controlSize = NSControlSizeSmall; sortPopup.refusesFirstResponder = YES;
    categoryPopup = [NSPopUpButton new]; [categoryPopup addItemWithTitle:@"All categories"];
    categoryPopup.target = self; categoryPopup.action = @selector(sorted:); categoryPopup.controlSize = NSControlSizeSmall; categoryPopup.refusesFirstResponder = YES;
    for (NSPopUpButton *p in @[sortPopup, categoryPopup]) { p.font = [NSFont systemFontOfSize:11]; p.translatesAutoresizingMaskIntoConstraints = NO; }
    resultList = mods_stack(@[], YES, 3); resultList.edgeInsets = NSEdgeInsetsMake(2, 0, 8, 0);
    resultScroll = mods_scroll(resultList);
    detailList = mods_stack(@[], YES, 8); detailList.edgeInsets = NSEdgeInsetsMake(4, 4, 12, 4);
    detailScroll = mods_scroll(detailList);
    NSBox *line = [NSBox new]; line.boxType = NSBoxSeparator; line.translatesAutoresizingMaskIntoConstraints = NO;
    [line setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationVertical];   /* its 1 pt height must not squeeze the menu */
    [line setContentCompressionResistancePriority:1 forOrientation:NSLayoutConstraintOrientationVertical];
    for (NSView *v in @[search, sortPopup, categoryPopup, resultScroll, line, detailScroll]) [browseView addSubview:v];
    [NSLayoutConstraint activateConstraints:@[
        [search.topAnchor constraintEqualToAnchor:browseView.topAnchor], [search.leadingAnchor constraintEqualToAnchor:browseView.leadingAnchor constant:8],
        [search.widthAnchor constraintEqualToConstant:290],
        [sortPopup.centerYAnchor constraintEqualToAnchor:search.centerYAnchor], [sortPopup.leadingAnchor constraintEqualToAnchor:search.trailingAnchor constant:10],
        [categoryPopup.centerYAnchor constraintEqualToAnchor:search.centerYAnchor], [categoryPopup.leadingAnchor constraintEqualToAnchor:sortPopup.trailingAnchor constant:8],
        [categoryPopup.trailingAnchor constraintLessThanOrEqualToAnchor:browseView.trailingAnchor constant:-8],
        [resultScroll.topAnchor constraintEqualToAnchor:search.bottomAnchor constant:8], [resultScroll.leadingAnchor constraintEqualToAnchor:browseView.leadingAnchor],
        [resultScroll.widthAnchor constraintEqualToConstant:300], [resultScroll.bottomAnchor constraintEqualToAnchor:browseView.bottomAnchor],
        [line.leadingAnchor constraintEqualToAnchor:resultScroll.trailingAnchor constant:4], [line.widthAnchor constraintEqualToConstant:1],
        [line.topAnchor constraintEqualToAnchor:resultScroll.topAnchor], [line.bottomAnchor constraintEqualToAnchor:resultScroll.bottomAnchor],
        [detailScroll.topAnchor constraintEqualToAnchor:resultScroll.topAnchor], [detailScroll.leadingAnchor constraintEqualToAnchor:line.trailingAnchor constant:4],
        [detailScroll.trailingAnchor constraintEqualToAnchor:browseView.trailingAnchor], [detailScroll.bottomAnchor constraintEqualToAnchor:browseView.bottomAnchor]]];
    [self showDetailsPlaceholder:@"Choose a mod to see its pictures, description and files."];
}

- (void)loadCategories {
    categoryIds = @[];
    [TOTKModBrowser.shared categories:^(NSArray<NSDictionary *> *cats, NSError *error) {
        (void)error; NSMutableArray *ids = [NSMutableArray arrayWithObject:@0];
        for (NSDictionary *c in cats) { [self->categoryPopup addItemWithTitle:[NSString stringWithFormat:@"%@ (%@)", c[@"name"], c[@"count"]]]; [ids addObject:c[@"id"]]; }
        self->categoryIds = ids;
    }];
}

- (void)searched:(NSSearchField *)f { (void)f; [self loadPage:1]; }
- (void)sorted:(NSPopUpButton *)p { (void)p; [self loadPage:1]; [self.window makeFirstResponder:menu]; }

- (void)loadPage:(NSInteger)n {
    NSInteger gen = ++listGen; loading = YES; listError = nil;
    if (n == 1) { [results removeAllObjects]; selected = -1; page = 0; total = 0; }
    [self buildResults];
    NSInteger cat = categoryPopup.indexOfSelectedItem;
    int64_t category = cat > 0 && cat < (NSInteger)categoryIds.count ? categoryIds[cat].longLongValue : 0;
    [TOTKModBrowser.shared listPage:n sort:(TOTKModSort)sortPopup.indexOfSelectedItem search:search.stringValue category:category
                         completion:^(NSArray<TOTKModSummary *> *mods, NSInteger count, NSError *error) {
        if (gen != self->listGen) return;
        self->loading = NO; self->listError = error.localizedDescription;
        if (!error) { [self->results addObjectsFromArray:mods]; self->page = n; self->total = count; }
        [self rebuildResultsKeepingScroll];
        if (n == 1 && self->results.count && self->selected < 0) [self select:0 scroll:NO];
    }];
}

- (void)rebuildResultsKeepingScroll {
    CGFloat y = resultScroll.contentView.bounds.origin.y;
    [self buildResults];
    [resultScroll.contentView scrollToPoint:NSMakePoint(0, y)]; [resultScroll reflectScrolledClipView:resultScroll.contentView];
}

- (void)buildResults {
    mods_clear(resultList); [resultRows removeAllObjects];
    for (NSUInteger i = 0; i < results.count; i++) {
        TOTKModSummary *m = results[i];
        TOTKModsClickView *row = [TOTKModsClickView new]; row.wantsLayer = YES; row.layer.cornerRadius = 7; row.translatesAutoresizingMaskIntoConstraints = NO;
        __weak TOTKModsPane *weak = self; NSInteger at = (NSInteger)i;
        row.onClick = ^{ TOTKModsPane *me = weak; if (!me) return; [me select:at scroll:NO]; [me.window makeFirstResponder:me->menu]; };
        NSImageView *thumb = [NSImageView new]; thumb.imageScaling = NSImageScaleProportionallyUpOrDown; thumb.wantsLayer = YES;
        thumb.layer.cornerRadius = 4; thumb.layer.backgroundColor = mods_color(1, 0.06).CGColor; thumb.layer.masksToBounds = YES;
        thumb.translatesAutoresizingMaskIntoConstraints = NO;
        [self loadImage:m.thumbURL into:thumb];
        NSTextField *name = mods_label(m.name, 12, NSFontWeightMedium, NSColor.labelColor, 0);
        name.maximumNumberOfLines = 2; name.lineBreakMode = NSLineBreakByWordWrapping; name.cell.truncatesLastVisibleLine = YES;
        name.preferredMaxLayoutWidth = 170;
        BOOL have = [TOTKModBrowser.shared installedFolderFor:m.modId] != nil;
        NSString *meta = [NSString stringWithFormat:@"%@\n♥ %@ · %@ views · %@", m.author, mods_count_text(m.likes), mods_count_text(m.views), mods_when(m.updated)];
        NSStackView *text = mods_stack(@[name, mods_label(meta, 10.5, NSFontWeightRegular, NSColor.secondaryLabelColor, 170)], YES, 1);
        if (have) [text addArrangedSubview:mods_label(@"✓ Installed", 10.5, NSFontWeightSemibold, NSColor.systemGreenColor, 0)];
        for (NSView *v in @[thumb, text]) [row addSubview:v];
        [NSLayoutConstraint activateConstraints:@[
            [row.widthAnchor constraintEqualToConstant:286],
            [thumb.leadingAnchor constraintEqualToAnchor:row.leadingAnchor constant:6], [thumb.topAnchor constraintEqualToAnchor:row.topAnchor constant:6],
            [thumb.widthAnchor constraintEqualToConstant:96], [thumb.heightAnchor constraintEqualToConstant:54],
            [text.leadingAnchor constraintEqualToAnchor:thumb.trailingAnchor constant:8], [text.topAnchor constraintEqualToAnchor:row.topAnchor constant:5],
            [text.trailingAnchor constraintLessThanOrEqualToAnchor:row.trailingAnchor constant:-6],
            [row.bottomAnchor constraintGreaterThanOrEqualToAnchor:thumb.bottomAnchor constant:6], [row.bottomAnchor constraintGreaterThanOrEqualToAnchor:text.bottomAnchor constant:5]]];
        [resultList addArrangedSubview:row]; [resultRows addObject:row];
    }
    NSString *status = loading ? @"Loading from GameBanana…" : listError ? [@"Couldn't reach GameBanana: " stringByAppendingString:listError] :
                       !results.count ? @"No mods found." : nil;
    if (status) [resultList addArrangedSubview:mods_label(status, 11, NSFontWeightRegular, listError ? NSColor.systemRedColor : NSColor.secondaryLabelColor, 270)];
    if (!loading && !listError && (NSInteger)results.count < total) {
        NSButton *more = mods_button([NSString stringWithFormat:@"More (%ld of %ld)", (long)results.count, (long)total], nil, self, @selector(more));
        [resultList addArrangedSubview:more];
    }
    [self showSelection];
}

- (void)more { [self loadPage:page + 1]; [self.window makeFirstResponder:menu]; }

- (void)showSelection {
    for (NSUInteger k = 0; k < resultRows.count; k++)
        resultRows[k].layer.backgroundColor = (NSInteger)k == selected ? mods_color(1, 0.13).CGColor : mods_color(1, 0.03).CGColor;
}

- (void)select:(NSInteger)i scroll:(BOOL)scroll {
    if (i < 0 || i >= (NSInteger)results.count) return;
    if (i == selected && details) return;
    selected = i; [self showSelection];
    if (scroll) { [resultList layoutSubtreeIfNeeded]; [resultRows[i] scrollRectToVisible:NSInsetRect(resultRows[i].bounds, 0, -6)]; }
    TOTKModSummary *m = results[i]; NSInteger gen = ++detailGen;
    details = nil;
    [self showDetailsPlaceholder:[NSString stringWithFormat:@"Loading “%@”…", m.name]];
    [TOTKModBrowser.shared details:m.modId completion:^(TOTKModDetails *d, NSError *error) {
        if (gen != self->detailGen) return;
        if (!d) { [self showDetailsPlaceholder:[@"Couldn't load the mod's page: " stringByAppendingString:error.localizedDescription ?: @"?"]]; return; }
        self->details = d; [self buildDetails];
    }];
}

- (void)loadImage:(NSString *)url into:(NSImageView *)view {
    view.identifier = url;
    if (!url) return;
    [TOTKModBrowser.shared image:url completion:^(NSData *data) {
        if (data && [view.identifier isEqual:url]) view.image = [[NSImage alloc] initWithData:data];
    }];
}

- (void)showDetailsPlaceholder:(NSString *)text {
    mods_clear(detailList); [fileBoxes removeAllObjects];
    [detailList addArrangedSubview:mods_label(text, 12, NSFontWeightRegular, NSColor.secondaryLabelColor, 400)];
}

- (CGFloat)detailWidth { return MODS_PANEL_WIDTH - 16 - 300 - 10 - 8 - 16; }

- (void)buildDetails {
    TOTKModDetails *d = details; if (!d) return;
    mods_clear(detailList); [fileBoxes removeAllObjects];
    CGFloat w = [self detailWidth];
    if (d.imageURLs.count) {
        bigImage = [NSImageView new]; bigImage.imageScaling = NSImageScaleProportionallyUpOrDown; bigImage.wantsLayer = YES;
        bigImage.layer.cornerRadius = 6; bigImage.layer.masksToBounds = YES; bigImage.layer.backgroundColor = mods_color(1, 0.05).CGColor;
        bigImage.translatesAutoresizingMaskIntoConstraints = NO;
        [bigImage.widthAnchor constraintEqualToConstant:w].active = YES; [bigImage.heightAnchor constraintEqualToConstant:round(w * 298 / 530)].active = YES;
        [self loadImage:d.imageURLs[0] into:bigImage];
        [detailList addArrangedSubview:bigImage];
        if (d.thumbURLs.count > 1) {
            NSStackView *strip = mods_stack(@[], NO, 4);
            for (NSUInteger i = 0; i < MIN(6u, d.thumbURLs.count); i++) {
                NSButton *b = [NSButton new]; b.bordered = NO; b.imageScaling = NSImageScaleProportionallyUpOrDown; b.tag = (NSInteger)i;
                b.target = self; b.action = @selector(pickImage:); b.refusesFirstResponder = YES; b.translatesAutoresizingMaskIntoConstraints = NO;
                [b.widthAnchor constraintEqualToConstant:(w - 20) / 6].active = YES; [b.heightAnchor constraintEqualToConstant:round((w - 20) / 6 * 0.5625)].active = YES;
                NSString *u = d.thumbURLs[i];
                [TOTKModBrowser.shared image:u completion:^(NSData *data) { if (data) b.image = [[NSImage alloc] initWithData:data]; }];
                [strip addArrangedSubview:b];
            }
            [detailList addArrangedSubview:strip];
        }
    }
    [detailList addArrangedSubview:mods_label(d.name, 16, NSFontWeightBold, NSColor.labelColor, w)];
    NSMutableArray *meta = [NSMutableArray array];
    if (d.author.length) [meta addObject:[@"by " stringByAppendingString:d.author]];
    if (d.version.length) [meta addObject:[@"version " stringByAppendingString:d.version]];
    if (d.category.length) [meta addObject:d.category];
    [detailList addArrangedSubview:mods_label([meta componentsJoinedByString:@" · "], 11.5, NSFontWeightRegular, NSColor.secondaryLabelColor, w)];
    NSString *stats = [NSString stringWithFormat:@"%@ downloads · ♥ %@ · %@ views · updated %@", mods_count_text(d.downloads), mods_count_text(d.likes),
                       mods_count_text(d.views), d.updated ? [NSDateFormatter localizedStringFromDate:d.updated dateStyle:NSDateFormatterMediumStyle timeStyle:NSDateFormatterNoStyle] : @"?"];
    NSButton *page_ = mods_button(@"Open on GameBanana", nil, self, @selector(openPage));
    [detailList addArrangedSubview:mods_stack(@[mods_label(stats, 11, NSFontWeightRegular, NSColor.secondaryLabelColor, w - 150), page_], NO, 8)];
    NSString *installed = [TOTKModBrowser.shared installedFolderFor:d.modId];
    if (installed) [detailList addArrangedSubview:mods_label([NSString stringWithFormat:@"✓ Installed as “%@”", installed], 11.5, NSFontWeightSemibold, NSColor.systemGreenColor, w)];
    if (d.summary.length) [detailList addArrangedSubview:mods_label(d.summary, 12, NSFontWeightMedium, NSColor.labelColor, w)];
    NSString *text = d.text;
    if (text.length > 1200) text = [[text substringToIndex:[text rangeOfComposedCharacterSequenceAtIndex:1200].location] stringByAppendingString:@"…"];
    if (text.length) [detailList addArrangedSubview:mods_label(text, 11.5, NSFontWeightRegular, mods_color(0.85, 1), w)];
    [detailList addArrangedSubview:mods_label(@"License", 12, NSFontWeightSemibold, NSColor.labelColor, 0)];
    [detailList addArrangedSubview:mods_label(d.license, 11, NSFontWeightRegular, NSColor.secondaryLabelColor, w)];
    [detailList addArrangedSubview:mods_label(d.files.count ? @"Files" : @"No files to download.", 12, NSFontWeightSemibold, NSColor.labelColor, 0)];
    for (TOTKModFile *f in d.files) {
        NSView *box = [self fileBox:f width:w];
        [detailList addArrangedSubview:box]; fileBoxes[@(f.fileId)] = box;
    }
    [detailScroll.contentView scrollToPoint:NSZeroPoint]; [detailScroll reflectScrolledClipView:detailScroll.contentView];
}

- (void)pickImage:(NSButton *)b { if (details && b.tag < (NSInteger)details.imageURLs.count) [self loadImage:details.imageURLs[b.tag] into:bigImage]; }
- (void)openPage { if (details.profileURL) [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:details.profileURL]]; }

- (NSView *)fileBox:(TOTKModFile *)f width:(CGFloat)w {
    NSView *box = [NSView new]; box.wantsLayer = YES; box.layer.cornerRadius = 8; box.translatesAutoresizingMaskIntoConstraints = NO;
    box.layer.backgroundColor = mods_color(1, f.recommended ? 0.09 : 0.04).CGColor;
    if (f.recommended) { box.layer.borderWidth = 1; box.layer.borderColor = [NSColor.systemGreenColor colorWithAlphaComponent:0.6].CGColor; }
    NSTextField *name = mods_label(f.name, 12, NSFontWeightMedium, NSColor.labelColor, 0);
    NSStackView *title = mods_stack(@[name], NO, 6);
    if (f.recommended) {
        NSTextField *badge = mods_label(@" Recommended ", 10, NSFontWeightBold, NSColor.blackColor, 0);
        badge.drawsBackground = YES; badge.backgroundColor = NSColor.systemGreenColor; badge.wantsLayer = YES; badge.layer.cornerRadius = 4;
        [title addArrangedSubview:badge];
    }
    NSString *meta = [NSString stringWithFormat:@"%@ · %@ · %@ downloads%@", f.sizeText, f.versionLabel, mods_count_text(f.downloads),
                      f.archiveKind.length ? [@" · " stringByAppendingString:f.archiveKind] : @""];
    NSStackView *text = mods_stack(@[title, mods_label(meta, 11, NSFontWeightRegular, NSColor.secondaryLabelColor, 0)], YES, 2);
    if (f.fileDescription.length) [text addArrangedSubview:mods_label(f.fileDescription, 11, NSFontWeightRegular, mods_color(0.8, 1), w - 120)];
    if (f.notice) [text addArrangedSubview:mods_label([@"⚠︎ " stringByAppendingString:[f.notice stringByReplacingOccurrencesOfString:@"\n" withString:@"\n⚠︎ "]],
                                                     11, NSFontWeightRegular, NSColor.systemOrangeColor, w - 120)];
    NSDictionary *st = installState[@(f.fileId)];
    BOOL busy = [TOTKModBrowser.shared isInstalling:f.fileId];
    if (busy || st) {
        NSProgressIndicator *bar = [NSProgressIndicator new]; bar.style = NSProgressIndicatorStyleBar; bar.controlSize = NSControlSizeSmall;
        bar.minValue = 0; bar.maxValue = 1; bar.identifier = @"bar"; bar.translatesAutoresizingMaskIntoConstraints = NO;
        [bar.widthAnchor constraintEqualToConstant:w - 130].active = YES;
        double fr = [st[@"fraction"] doubleValue];
        bar.indeterminate = fr < 0; bar.doubleValue = MAX(0, fr); if (fr < 0) [bar startAnimation:nil];
        bar.hidden = !busy;
        NSTextField *status = mods_label(st[@"stage"], 11, NSFontWeightRegular, st[@"error"] ? NSColor.systemRedColor : st[@"done"] ? NSColor.systemGreenColor : NSColor.secondaryLabelColor, w - 120);
        status.identifier = @"status";
        [text addArrangedSubview:bar]; [text addArrangedSubview:status];
    }
    NSButton *act = busy ? mods_button(@"Cancel", nil, self, @selector(cancelInstall:)) : mods_button(st[@"done"] ? @"Install again" : @"Install", nil, self, @selector(installFile:));
    act.tag = (NSInteger)f.fileId; act.enabled = busy || (f.canInstallHere && f.archiveKind.length);
    for (NSView *v in @[text, act]) [box addSubview:v];
    [NSLayoutConstraint activateConstraints:@[
        [box.widthAnchor constraintEqualToConstant:w],
        [text.leadingAnchor constraintEqualToAnchor:box.leadingAnchor constant:10], [text.topAnchor constraintEqualToAnchor:box.topAnchor constant:8],
        [text.bottomAnchor constraintEqualToAnchor:box.bottomAnchor constant:-8], [text.trailingAnchor constraintLessThanOrEqualToAnchor:act.leadingAnchor constant:-8],
        [act.trailingAnchor constraintEqualToAnchor:box.trailingAnchor constant:-10], [act.topAnchor constraintEqualToAnchor:box.topAnchor constant:8]]];
    return box;
}

- (TOTKModFile *)fileWithId:(int64_t)fileId { for (TOTKModFile *f in details.files) if (f.fileId == fileId) return f; return nil; }

- (void)replaceFileBox:(int64_t)fileId {
    NSView *old = fileBoxes[@(fileId)]; TOTKModFile *f = [self fileWithId:fileId];
    if (!old || !f) return;
    NSUInteger at = [detailList.arrangedSubviews indexOfObject:old]; if (at == NSNotFound) return;
    NSView *box = [self fileBox:f width:[self detailWidth]];
    [detailList removeArrangedSubview:old]; [old removeFromSuperview];
    [detailList insertArrangedSubview:box atIndex:at]; fileBoxes[@(fileId)] = box;
}

- (void)installFile:(NSButton *)b {
    TOTKModFile *f = [self fileWithId:b.tag]; TOTKModDetails *d = details;
    if (!f || !d) return;
    int64_t fid = f.fileId; NSNumber *key = @(fid);
    installState[key] = @{@"fraction": @0, @"stage": @"Starting…"};
    __weak TOTKModsPane *weak = self;
    [TOTKModBrowser.shared install:f of:d progress:^(double fraction, NSString *stage) {
        TOTKModsPane *s = weak; if (!s) return;
        NSString *line = fraction >= 0 ? [NSString stringWithFormat:@"%@ %d%%", stage, (int)lround(fraction * 100)] : stage;
        s->installState[key] = @{@"fraction": @(fraction), @"stage": line};
        NSView *box = s->fileBoxes[key];
        for (NSView *v in [box.subviews.firstObject isKindOfClass:NSStackView.class] ? ((NSStackView *)box.subviews.firstObject).arrangedSubviews : @[]) {
            if ([v.identifier isEqual:@"bar"]) { NSProgressIndicator *bar = (NSProgressIndicator *)v; bar.indeterminate = fraction < 0; bar.doubleValue = MAX(0, fraction); if (fraction < 0) [bar startAnimation:nil]; }
            if ([v.identifier isEqual:@"status"]) ((NSTextField *)v).stringValue = line;
        }
    } completion:^(NSString *folder, NSString *note, NSError *error) {
        TOTKModsPane *s = weak; if (!s) return;
        NSString *line = folder ? [NSString stringWithFormat:@"Installed as “%@” and switched on.%@%@", folder, note ? @"\n" : @"", note ?: @""] : error.localizedDescription;
        s->installState[key] = folder ? @{@"fraction": @1, @"stage": line, @"done": @YES} : @{@"fraction": @0, @"stage": line, @"error": @YES};
        if (s->details.modId == d.modId) {   /* the "Installed as" line and the file's status, at the same place */
            CGFloat y = s->detailScroll.contentView.bounds.origin.y;
            [s buildDetails];
            [s->detailList layoutSubtreeIfNeeded];
            [s->detailScroll.contentView scrollToPoint:NSMakePoint(0, y)]; [s->detailScroll reflectScrolledClipView:s->detailScroll.contentView];
        }
        [s buildInstalled]; [s rebuildResultsKeepingScroll];
    }];
    [self replaceFileBox:fid];
    [self.window makeFirstResponder:menu];
}

- (void)cancelInstall:(NSButton *)b { [TOTKModBrowser.shared cancelInstall:b.tag]; [self.window makeFirstResponder:menu]; }

/* ---- keys and the controller ---- */

- (void)focusSearch { [self showBrowse:YES]; [self.window makeFirstResponder:search]; }

- (BOOL)control:(NSControl *)c textView:(NSTextView *)tv doCommandBySelector:(SEL)cmd {
    (void)c; (void)tv;
    if (cmd == @selector(cancelOperation:) || cmd == @selector(moveDown:)) {   /* Escape or ↓: back to the list */
        [self.window makeFirstResponder:menu];
        if (cmd == @selector(moveDown:) && results.count) [self select:MAX(0, selected) scroll:YES];
        return YES;
    }
    return NO;
}

- (void)moveFocus:(int)dir {
    if (alertOpen) return;
    if (self.browsing) { [self select:selected < 0 ? 0 : MAX(0, MIN((NSInteger)results.count - 1, selected + dir)) scroll:YES]; return; }
    if (!rows.count) return;
    focus = MAX(0, MIN((NSInteger)rows.count - 1, focus + dir)); [self showFocus];
}

- (void)step:(int)dir {
    if (alertOpen) return;
    if (dir) { [self showBrowse:dir > 0]; [self showFocus]; return; }
    if (self.browsing) return;
    if (focus < (NSInteger)rows.count) {
        ModInfo m; if (!mods_get((int)focus, &m)) [self setEnabled:focus on:!m.enabled];
    }
}

- (BOOL)key:(NSEvent *)e {
    if (alertOpen) return YES;
    BOOL cmd = (e.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagOption)) != 0;
    switch (e.keyCode) {
    case 126: if (cmd) [self moveMod:-1]; else [self moveFocus:-1]; return YES;   /* up */
    case 125: if (cmd) [self moveMod:1]; else [self moveFocus:1]; return YES;     /* down */
    case 123: [self step:-1]; return YES;                                         /* left: Installed */
    case 124: [self step:1]; return YES;                                          /* right: Browse */
    case 36: case 76: case 49: if (!e.isARepeat) [self step:0]; return YES;      /* Return, Enter, Space */
    case 51: case 117: if (!self.browsing && !e.isARepeat) [self confirmRemove:focus]; return YES;   /* Delete */
    case 44: if (!e.isARepeat) [self focusSearch]; return YES;                    /* / */
    case 3: if (cmd) { [self focusSearch]; return YES; } return NO;               /* ⌘F */
    default: return NO;
    }
}
@end
