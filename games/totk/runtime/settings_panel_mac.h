/* The settings menu on the Mac (included by runtime/frontend.m only): a sidebar drawn inside the game window, so it works
   in full screen and leaves the picture beside it in view while a change takes effect. Built from the shared settings
   table (runtime/settings_schema.h), one tab per page. Opened with Command-comma, F1, the app menu's Settings item, or a
   controller's Home button (or Options and Menu together). Mouse, keyboard (arrows choose and change, Return toggles,
   Q / E or Tab switch tabs, Escape closes) and controller (D-pad or left stick, A, B, shoulder buttons) all work. While it
   is open the game reads a neutral controller (runtime_input_hold). After the settings pages, Mods is runtime/mods_panel_mac.h
   (the installed mods and the GameBanana browser; the sidebar widens while it shows), and Cheats lists the community
   cheat list the game downloads (cheat_db_fetch.h; shown by group through cheat_catalog.h, variants as one choice, the ones
   this port cannot run folded away) and then the cheat files of the player's own (runtime/cheats.c, cheat_files.h), with
   the folder, Turn off all and Restart now; it is built here like the settings pages, and Reset all leaves cheats alone. The Interface tab ends with Send saves:
   the profile's saves as one .totksave file (runtime/save_package.h) for AirDrop, Messages, Mail or Finder; the other
   device opens it to bring them in (docs/save-sync.md). The iPhone / iPad menu is ios/Sources/settings_menu.m. */
#pragma once
#import <Cocoa/Cocoa.h>
#import <GameController/GameController.h>
#include <sys/sysctl.h>
#include "runtime.h"
#include "settings.h"
#import "save_package.h"
#include "cheat_catalog.h"
#include "cheat_db_fetch.h"

@interface TOTKFlippedView : NSView @end   /* the scrolling rows start at the top */
@implementation TOTKFlippedView
- (BOOL)isFlipped { return YES; }
@end
#include "mods_panel_mac.h"

@interface TOTKSettingsPanel : NSView
+ (void)toggle;
+ (BOOL)snapshotTo:(NSString *)path;   /* the menu as a PNG (test runs, TOTK_INPUT_FILE "settings-shot") */
+ (void)key:(NSEvent *)event;          /* a key for the open menu (test runs, TOTK_INPUT_FILE "menukey") */
+ (void)showCheats;                    /* opens the menu on the Cheats tab (the app menu's Cheats… item) */
@end

static NSWindow *settings_window(void);   /* frontend.m: the game window */

@interface TOTKSettingsPanel () <NSSharingServicePickerDelegate> @end
@implementation TOTKSettingsPanel {
    NSSegmentedControl *tabs;
    NSScrollView *scroll;
    NSStackView *list;
    NSTextField *status, *hint;
    NSButton *reset, *restartNow;   /* restartNow: shown while a setting or the mods wait for a restart */
    TOTKModsPane *mods;                   /* the Mods tab (page MODS_PAGE) */
    NSMutableArray<NSView *> *rows;       /* the rows of the page shown, in order */
    NSMutableArray<NSNumber *> *rowSetting;   /* the setting of each row; SAVES_ROW: Send saves (Interface tab); CHEAT_ROW...: the Cheats tab */
    NSButton *sendButton; NSTextField *savesNote;
    NSInteger page, focus;
    NSTimer *pollTimer;
    uint64_t padPrev; double padRepeatAt; int padDir;
    BOOL cheatsUnavailableShown, cheatDbAsked;   /* the Cheats tab: the folded list is open; the list was checked this run */
}
static TOTKSettingsPanel *settingsPanel;
enum { PANEL_WIDTH = 480, PANEL_MARGIN = 16, MODS_PAGE = SETTINGS_NPAGES, CHEATS_PAGE = SETTINGS_NPAGES + 1, NPAGES = SETTINGS_NPAGES + 2,
       SAVES_ROW = -1, CHEATS_FOLDER_ROW = -2, CHEATS_OFF_ROW = -3, CHEATS_RESTART_ROW = -4, CHEATS_DB_ROW = -5, CHEATS_UNAVAILABLE_ROW = -6,
       CHEAT_ROW = -1000 /* - k: cheat k, or the choice whose first variant is k */ };
static NSString *const SETTINGS_HINT = @"↑↓ choose   ←→ change   Q E tabs   Esc close\nController: D-pad, A, B, shoulder buttons";

static BOOL settings_row_shown(const SettingDef *d) {
    if (!(d->platforms & SET_MAC)) return NO;
    return !d->shown_key || settings_get(d->shown_key) == d->shown_value;
}
static NSString *settings_value_text(const SettingDef *d, int v) {
    if (d->type == SET_CHOICE) { const SettingChoice *c = settings_choice(d, v); return c ? @(c->label) : @"?"; }
    if (d->type == SET_RANGE) return [NSString stringWithFormat:@"%d%s", v, d->unit ? d->unit : ""];
    return v ? @"On" : @"Off";
}
static NSColor *panel_color(double w, double a) { return [NSColor colorWithCalibratedWhite:w alpha:a]; }

+ (TOTKSettingsPanel *)shared {
    if (!settingsPanel) settingsPanel = [[TOTKSettingsPanel alloc] initWithFrame:NSMakeRect(0, 0, PANEL_WIDTH, 700)];
    return settingsPanel;
}

+ (void)key:(NSEvent *)event { if (settingsPanel.superview) [settingsPanel keyDown:event]; }

+ (void)toggle {
    TOTKSettingsPanel *p = [self shared];
    if (p.superview) [p hide]; else [p show];
}

- (instancetype)initWithFrame:(NSRect)frame {
    if (!(self = [super initWithFrame:frame])) return nil;
    self.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    self.wantsLayer = YES;
    self.layer.backgroundColor = [NSColor colorWithCalibratedRed:0.07 green:0.08 blue:0.09 alpha:0.94].CGColor;
    self.layer.cornerRadius = 14; self.layer.borderWidth = 1; self.layer.borderColor = panel_color(1, 0.08).CGColor;
    rows = [NSMutableArray array]; rowSetting = [NSMutableArray array];

    NSTextField *title = [NSTextField labelWithString:@"Settings"];
    title.font = [NSFont systemFontOfSize:22 weight:NSFontWeightBold]; title.textColor = NSColor.whiteColor;
    status = [NSTextField labelWithString:@""];
    status.font = [NSFont monospacedDigitSystemFontOfSize:11 weight:NSFontWeightRegular]; status.textColor = NSColor.secondaryLabelColor;

    NSMutableArray *labels = [NSMutableArray array];
    for (int p = 0; p < SETTINGS_NPAGES; p++) [labels addObject:@(SETTINGS_PAGES[p])];
    [labels addObject:@"Mods"]; [labels addObject:@"Cheats"];
    tabs = [NSSegmentedControl segmentedControlWithLabels:labels trackingMode:NSSegmentSwitchTrackingSelectOne target:self action:@selector(tabClicked:)];
    tabs.segmentStyle = NSSegmentStyleAutomatic; tabs.selectedSegment = 0; tabs.refusesFirstResponder = YES;
    tabs.segmentDistribution = NSSegmentDistributionFit; tabs.controlSize = NSControlSizeMini;   /* nine tabs, widths after their names */
    tabs.font = [NSFont systemFontOfSize:10];

    list = [NSStackView new]; list.orientation = NSUserInterfaceLayoutOrientationVertical;
    list.alignment = NSLayoutAttributeLeading; list.spacing = 4; list.translatesAutoresizingMaskIntoConstraints = NO;
    list.edgeInsets = NSEdgeInsetsMake(4, 0, 12, 0);
    scroll = [NSScrollView new]; scroll.hasVerticalScroller = YES; scroll.drawsBackground = NO; scroll.autohidesScrollers = YES;
    NSView *doc = [TOTKFlippedView new]; doc.translatesAutoresizingMaskIntoConstraints = NO;
    [doc addSubview:list]; scroll.documentView = doc;

    hint = [NSTextField wrappingLabelWithString:SETTINGS_HINT];
    hint.font = [NSFont systemFontOfSize:10.5]; hint.textColor = NSColor.tertiaryLabelColor;
    reset = [NSButton buttonWithTitle:@"Reset all" target:self action:@selector(reset)];
    restartNow = [NSButton buttonWithTitle:@"Restart now" target:self action:@selector(restartGame)];
    restartNow.controlSize = NSControlSizeSmall; restartNow.refusesFirstResponder = YES; restartNow.hidden = YES;
    restartNow.bezelColor = NSColor.systemOrangeColor; restartNow.translatesAutoresizingMaskIntoConstraints = NO;
    reset.controlSize = NSControlSizeSmall; reset.refusesFirstResponder = YES; reset.bezelStyle = NSBezelStyleRounded;

    mods = [[TOTKModsPane alloc] initWithMenu:self]; mods.hidden = YES;
    [NSNotificationCenter.defaultCenter addObserverForName:TOTKCheatDbChanged object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n) {
        (void)n; if (self.superview && self->page == CHEATS_PAGE) [self rebuildKeepingPlace];
    }];
    for (NSView *v in @[title, status, tabs, scroll, mods, hint, reset, restartNow]) { v.translatesAutoresizingMaskIntoConstraints = NO; [self addSubview:v]; }
    [NSLayoutConstraint activateConstraints:@[
        [title.topAnchor constraintEqualToAnchor:self.topAnchor constant:18], [title.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:20],
        [status.firstBaselineAnchor constraintEqualToAnchor:title.firstBaselineAnchor], [status.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-20],
        [status.leadingAnchor constraintGreaterThanOrEqualToAnchor:title.trailingAnchor constant:12],
        [tabs.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:14],
        [tabs.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:16], [tabs.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-16],
        [scroll.topAnchor constraintEqualToAnchor:tabs.bottomAnchor constant:10],
        [scroll.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:8], [scroll.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-8],
        [scroll.bottomAnchor constraintEqualToAnchor:hint.topAnchor constant:-10],
        [mods.topAnchor constraintEqualToAnchor:scroll.topAnchor], [mods.bottomAnchor constraintEqualToAnchor:scroll.bottomAnchor],
        [mods.leadingAnchor constraintEqualToAnchor:scroll.leadingAnchor], [mods.trailingAnchor constraintEqualToAnchor:scroll.trailingAnchor],
        [hint.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:20], [hint.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-14],
        [hint.trailingAnchor constraintLessThanOrEqualToAnchor:restartNow.leadingAnchor constant:-10],
        [reset.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-16], [reset.centerYAnchor constraintEqualToAnchor:hint.centerYAnchor],
        [restartNow.trailingAnchor constraintEqualToAnchor:reset.leadingAnchor constant:-6], [restartNow.centerYAnchor constraintEqualToAnchor:hint.centerYAnchor],
        [list.topAnchor constraintEqualToAnchor:doc.topAnchor], [list.leadingAnchor constraintEqualToAnchor:doc.leadingAnchor],
        [list.trailingAnchor constraintEqualToAnchor:doc.trailingAnchor], [list.bottomAnchor constraintEqualToAnchor:doc.bottomAnchor],
        [doc.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor]]];
    return self;
}

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isFlipped { return YES; }
- (void)mouseDown:(NSEvent *)e { (void)e; [self.window makeFirstResponder:self]; }   /* clicks on the panel stay with the menu */

/* ---- rows ---- */

- (NSView *)controlFor:(const SettingDef *)d index:(int)i {
    int v = settings_get(d->key); NSControl *c;
    if (d->type == SET_BOOL) { NSSwitch *s = [NSSwitch new]; s.state = v ? NSControlStateValueOn : NSControlStateValueOff; c = s; }
    else if (d->type == SET_CHOICE) {
        NSPopUpButton *p = [NSPopUpButton new];
        for (int k = 0; k < d->nchoices; k++) { [p addItemWithTitle:@(d->choices[k].label)]; if (d->choices[k].value == v) [p selectItemAtIndex:k]; }
        [p.widthAnchor constraintEqualToConstant:190].active = YES; c = p;
    } else {
        NSSlider *s = [NSSlider sliderWithValue:v minValue:d->min maxValue:d->max target:nil action:nil];
        s.continuous = YES; [s.widthAnchor constraintEqualToConstant:118].active = YES; c = s;
    }
    c.tag = i; c.target = self; c.action = @selector(changed:); c.refusesFirstResponder = YES;
    if (d->type != SET_RANGE) return c;
    NSTextField *value = [NSTextField labelWithString:settings_value_text(d, v)];
    value.font = [NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightRegular]; value.alignment = NSTextAlignmentRight;
    value.identifier = @"value"; [value.widthAnchor constraintEqualToConstant:52].active = YES;
    NSStackView *row = [NSStackView stackViewWithViews:@[c, value]]; row.spacing = 6;
    return row;
}

- (NSView *)rowFor:(const SettingDef *)d index:(int)i {
    NSView *row = [NSView new]; row.wantsLayer = YES; row.layer.cornerRadius = 8; row.translatesAutoresizingMaskIntoConstraints = NO;
    NSString *name = settings_pending_restart(d->key) ? [NSString stringWithFormat:@"%s (after restart)", d->label] : @(d->label);
    NSTextField *label = [NSTextField labelWithString:name];
    label.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium]; label.textColor = NSColor.labelColor;
    NSStackView *text = [NSStackView stackViewWithViews:@[label]]; text.orientation = NSUserInterfaceLayoutOrientationVertical;
    text.alignment = NSLayoutAttributeLeading; text.spacing = 2;
    if (d->help) {
        NSTextField *help = [NSTextField wrappingLabelWithString:@(d->help)];
        help.font = [NSFont systemFontOfSize:11]; help.textColor = NSColor.secondaryLabelColor; help.preferredMaxLayoutWidth = 176;
        [text addArrangedSubview:help];
    }
    NSView *control = [self controlFor:d index:i];
    for (NSView *v in @[text, control]) { v.translatesAutoresizingMaskIntoConstraints = NO; [row addSubview:v]; }
    [NSLayoutConstraint activateConstraints:@[
        [row.widthAnchor constraintEqualToConstant:PANEL_WIDTH - 2 * 8 - 14],
        [text.leadingAnchor constraintEqualToAnchor:row.leadingAnchor constant:12], [text.topAnchor constraintEqualToAnchor:row.topAnchor constant:9],
        [text.bottomAnchor constraintLessThanOrEqualToAnchor:row.bottomAnchor constant:-9], [text.widthAnchor constraintLessThanOrEqualToConstant:180],
        [control.trailingAnchor constraintEqualToAnchor:row.trailingAnchor constant:-10], [control.centerYAnchor constraintEqualToAnchor:row.centerYAnchor],
        [row.heightAnchor constraintGreaterThanOrEqualToAnchor:control.heightAnchor constant:18],
        [control.leadingAnchor constraintGreaterThanOrEqualToAnchor:text.trailingAnchor constant:8]]];
    [row setContentHuggingPriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationVertical];
    return row;
}

/* Send saves, at the end of the Interface tab: what is on disk now, sent as one file (runtime/save_package.h). */
static NSString *saves_profile(void) { const char *d = getenv("TOTK_PROFILE_DIR"); return d && *d ? @(d) : @"user-data"; }
- (NSView *)savesRow {
    NSView *row = [NSView new]; row.wantsLayer = YES; row.layer.cornerRadius = 8; row.translatesAutoresizingMaskIntoConstraints = NO;
    NSTextField *label = [NSTextField labelWithString:@"Send saves"];
    label.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium]; label.textColor = NSColor.labelColor;
    NSTextField *help = [NSTextField wrappingLabelWithString:[NSString stringWithFormat:@"%@. To an iPhone, iPad or another Mac with "
        "AirDrop: save in the game first, the newest saves on disk are what is sent. Opening the file there brings them in "
        "(the saves it replaces are kept).", TOTKSaveSummary(saves_profile())]];
    help.font = [NSFont systemFontOfSize:11]; help.textColor = NSColor.secondaryLabelColor; help.preferredMaxLayoutWidth = 250;
    savesNote = [NSTextField wrappingLabelWithString:@""]; savesNote.hidden = YES;
    savesNote.font = [NSFont systemFontOfSize:11 weight:NSFontWeightMedium]; savesNote.textColor = NSColor.systemOrangeColor; savesNote.preferredMaxLayoutWidth = 250;
    NSStackView *text = [NSStackView stackViewWithViews:@[label, help, savesNote]]; text.orientation = NSUserInterfaceLayoutOrientationVertical;
    text.alignment = NSLayoutAttributeLeading; text.spacing = 2;
    sendButton = [NSButton buttonWithTitle:@"Send…" target:self action:@selector(sendSaves)];
    sendButton.image = [NSImage imageWithSystemSymbolName:@"square.and.arrow.up" accessibilityDescription:nil]; sendButton.imagePosition = NSImageLeading;
    sendButton.controlSize = NSControlSizeSmall; sendButton.bezelStyle = NSBezelStyleRounded; sendButton.refusesFirstResponder = YES;
    for (NSView *v in @[text, sendButton]) { v.translatesAutoresizingMaskIntoConstraints = NO; [row addSubview:v]; }
    [NSLayoutConstraint activateConstraints:@[
        [row.widthAnchor constraintEqualToConstant:PANEL_WIDTH - 2 * 8 - 14],
        [text.leadingAnchor constraintEqualToAnchor:row.leadingAnchor constant:12], [text.topAnchor constraintEqualToAnchor:row.topAnchor constant:9],
        [text.bottomAnchor constraintEqualToAnchor:row.bottomAnchor constant:-9], [text.widthAnchor constraintLessThanOrEqualToConstant:256],
        [sendButton.trailingAnchor constraintEqualToAnchor:row.trailingAnchor constant:-10], [sendButton.topAnchor constraintEqualToAnchor:row.topAnchor constant:9],
        [sendButton.leadingAnchor constraintGreaterThanOrEqualToAnchor:text.trailingAnchor constant:8]]];
    return row;
}
- (void)savesNote:(NSString *)text {
    savesNote.stringValue = text ?: @""; savesNote.hidden = !text.length;
    if (text.length) fprintf(stderr, "[saves] %s\n", text.UTF8String);
}
- (void)sendSaves {
    NSString *profile = saves_profile();
    if (runtime_fs_save_busy() || TOTKSaveBusy(profile)) { [self savesNote:@"The game is saving right now. Try again in a moment."]; return; }
    char model[64] = ""; size_t n = sizeof model - 1; sysctlbyname("hw.model", model, &n, NULL, 0);
    NSError *error = nil;
    NSString *path = TOTKSaveExport(profile, [NSTemporaryDirectory() stringByAppendingPathComponent:@"totk-saves-out"], @TOTK_GAME_VERSION,
                                    @"mac", NSHost.currentHost.localizedName, @(model), &error);
    if (!path) { [self savesNote:error.localizedDescription]; return; }
    [self savesNote:nil];
    fprintf(stderr, "[saves] packed %s\n", path.fileSystemRepresentation);
    NSSharingServicePicker *picker = [[NSSharingServicePicker alloc] initWithItems:@[[NSURL fileURLWithPath:path]]];
    picker.delegate = self;
    [picker showRelativeToRect:sendButton.bounds ofView:sendButton preferredEdge:NSRectEdgeMinY];
}
/* AirDrop first; Finder's Reveal stays available through the picker's own services. */
- (NSArray<NSSharingService *> *)sharingServicePicker:(NSSharingServicePicker *)picker sharingServicesForItems:(NSArray *)items
                              proposedSharingServices:(NSArray<NSSharingService *> *)proposed {
    (void)picker; (void)items;
    NSSharingService *airdrop = [NSSharingService sharingServiceNamed:NSSharingServiceNameSendViaAirDrop];
    NSMutableArray *out = [NSMutableArray array];
    if (airdrop) [out addObject:airdrop];
    for (NSSharingService *service in proposed) if (!airdrop || ![service.title isEqual:airdrop.title]) [out addObject:service];
    return out;
}

/* ---- the Cheats tab (runtime/cheats.c) ---- */

/* Code cheats switched since this launch: they apply (or turn off) only when the game starts again. */
static int cheats_pending(void) {
    int n = 0;
    for (int i = 0; i < runtime_cheats_count(); i++) {
        int st = runtime_cheats_state(i);
        if ((st & CHEAT_AT_LAUNCH) && !(st & CHEAT_REFUSED) && !!(st & CHEAT_ON) != !!(st & CHEAT_APPLIED)) n++;
    }
    return n;
}
static int cheats_any_on(void) { for (int i = 0; i < runtime_cheats_count(); i++) if (runtime_cheats_state(i) & CHEAT_ON) return 1; return 0; }
/* The line under a cheat: how it applies, or why it cannot. */
static NSString *cheat_detail(const RuntimeCheatInfo *c, NSColor **color) {
    int st = c->state; *color = NSColor.secondaryLabelColor;
    if (st & CHEAT_REFUSED) { *color = NSColor.systemOrangeColor; return [NSString stringWithFormat:@"Can't be used here: %s", c->problem]; }
    if (c->problem[0]) { *color = NSColor.systemOrangeColor; return @(c->problem); }   /* a conflict, a refused write */
    if (st & CHEAT_AT_LAUNCH) {
        if ((st & CHEAT_ON) && !(st & CHEAT_APPLIED)) { *color = NSColor.systemOrangeColor; return @"Applies after restart"; }
        if (!(st & CHEAT_ON) && (st & CHEAT_APPLIED)) { *color = NSColor.systemOrangeColor; return @"Turns off after restart"; }
        return st & CHEAT_ON ? @"Code patch: applied at launch" : @"Code patch: applies at the next start";
    }
    if (st & CHEAT_ON) { *color = NSColor.systemGreenColor; return @"Live: running now"; }
    return @"Live: takes effect at once";
}
/* A row of the Cheats tab: a title, a line about it (or none), a line under it, and a control on the right (or none). */
- (NSView *)cheatsRow:(NSString *)title detail:(NSString *)detail color:(NSColor *)color control:(NSView *)control {
    return [self cheatsRow:title about:nil detail:detail color:color control:control];
}
- (NSView *)cheatsRow:(NSString *)title about:(NSString *)about detail:(NSString *)detail color:(NSColor *)color control:(NSView *)control {
    NSView *row = [NSView new]; row.wantsLayer = YES; row.layer.cornerRadius = 8; row.translatesAutoresizingMaskIntoConstraints = NO;
    NSTextField *label = [NSTextField wrappingLabelWithString:title];
    label.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium]; label.textColor = NSColor.labelColor;
    CGFloat textWidth = control ? 300 : PANEL_WIDTH - 2 * 8 - 14 - 24;
    label.preferredMaxLayoutWidth = textWidth;
    NSStackView *text = [NSStackView stackViewWithViews:@[label]]; text.orientation = NSUserInterfaceLayoutOrientationVertical;
    text.alignment = NSLayoutAttributeLeading; text.spacing = 2;
    if (about.length) {
        NSTextField *a = [NSTextField wrappingLabelWithString:about];
        a.font = [NSFont systemFontOfSize:11]; a.textColor = NSColor.secondaryLabelColor; a.preferredMaxLayoutWidth = textWidth;
        a.selectable = NO; [text addArrangedSubview:a];
    }
    if (detail.length) {
        NSTextField *help = [NSTextField wrappingLabelWithString:detail];
        help.font = [NSFont systemFontOfSize:11]; help.textColor = color ?: NSColor.secondaryLabelColor; help.preferredMaxLayoutWidth = textWidth;
        help.selectable = NO; [text addArrangedSubview:help];
    }
    text.translatesAutoresizingMaskIntoConstraints = NO; [row addSubview:text];
    NSMutableArray *c = [NSMutableArray arrayWithArray:@[
        [row.widthAnchor constraintEqualToConstant:PANEL_WIDTH - 2 * 8 - 14],
        [text.leadingAnchor constraintEqualToAnchor:row.leadingAnchor constant:12], [text.topAnchor constraintEqualToAnchor:row.topAnchor constant:8],
        [text.bottomAnchor constraintEqualToAnchor:row.bottomAnchor constant:-8], [text.widthAnchor constraintLessThanOrEqualToConstant:textWidth]]];
    if (control) {
        control.translatesAutoresizingMaskIntoConstraints = NO; [row addSubview:control];
        [c addObjectsFromArray:@[[control.trailingAnchor constraintEqualToAnchor:row.trailingAnchor constant:-10],
            [control.centerYAnchor constraintEqualToAnchor:row.centerYAnchor],
            [control.leadingAnchor constraintGreaterThanOrEqualToAnchor:text.trailingAnchor constant:8]]];
    }
    [NSLayoutConstraint activateConstraints:c];
    return row;
}
- (NSButton *)cheatsButton:(NSString *)title action:(SEL)action {
    NSButton *b = [NSButton buttonWithTitle:title target:self action:action];
    b.controlSize = NSControlSizeSmall; b.bezelStyle = NSBezelStyleRounded; b.refusesFirstResponder = YES;
    return b;
}
/* A group's title over its rows (not focused): a file, or a heading the file itself has. */
- (void)cheatsHeading:(NSString *)title detail:(NSString *)detail problem:(NSString *)problem big:(BOOL)big {
    NSTextField *label = [NSTextField wrappingLabelWithString:title];
    label.font = big ? [NSFont systemFontOfSize:12 weight:NSFontWeightBold] : [NSFont systemFontOfSize:11 weight:NSFontWeightSemibold];
    label.textColor = big ? NSColor.labelColor : NSColor.secondaryLabelColor; label.preferredMaxLayoutWidth = PANEL_WIDTH - 60;
    NSStackView *box = [NSStackView stackViewWithViews:@[label]]; box.orientation = NSUserInterfaceLayoutOrientationVertical;
    box.alignment = NSLayoutAttributeLeading; box.spacing = 1; box.edgeInsets = NSEdgeInsetsMake(big ? 12 : 6, 12, 2, 12);
    for (int i = 0; i < 2; i++) {
        NSString *t = i ? problem : detail; if (!t.length) continue;
        NSTextField *l = [NSTextField wrappingLabelWithString:t];
        l.font = [NSFont systemFontOfSize:10.5]; l.textColor = i ? NSColor.systemOrangeColor : NSColor.tertiaryLabelColor;
        l.preferredMaxLayoutWidth = PANEL_WIDTH - 60; l.selectable = NO; [box addArrangedSubview:l];
    }
    [list addArrangedSubview:box];
}
- (void)addCheatsRow:(NSView *)row id:(int)rid { [list addArrangedSubview:row]; [rows addObject:row]; [rowSetting addObject:@(rid)]; }
/* The variants of the choice whose first variant is leader: all of them, and those this port can run. */
static int cheat_choice_members(int leader, int *all, int *usable, int max, int *nusable) {
    int n = 0; *nusable = 0;
    for (int k = leader, total = runtime_cheats_total(); k < total && n < max; k++) {
        RuntimeCheatInfo c; if (!runtime_cheats_info(k, &c) || c.hide || c.choice != leader) continue;
        all[n++] = k;
        if (!(c.state & CHEAT_REFUSED)) usable[(*nusable)++] = k;
    }
    return n;
}
/* The community cheat list: one heading per group, a switch per cheat, a menu per choice; the cheats this port cannot run
   folded into one row at the end. */
- (void)buildCheatList {
    int db = runtime_cheats_db_source(), total = runtime_cheats_total(), dl = settings_index("interface.download_cheats");
    NSString *dbStatus = [TOTKCheatDb status];
    int usable = 0;
    NSMutableArray<NSNumber *> *groups[CHEAT_GROUPS], *refused = [NSMutableArray array];
    for (int g = 0; g < CHEAT_GROUPS; g++) groups[g] = [NSMutableArray array];
    for (int k = 0; k < total && db >= 0; k++) {
        RuntimeCheatInfo c; if (!runtime_cheats_info(k, &c) || c.source != db || c.role != 0 || c.hide) continue;
        if (c.choice >= 0 && c.choice != k) continue;   /* a later variant: an option of its choice's row */
        int all[16], ok[16], nok = 0;
        if (c.choice >= 0) cheat_choice_members(k, all, ok, 16, &nok); else nok = !(c.state & CHEAT_REFUSED);
        if (!nok) { [refused addObject:@(k)]; continue; }
        [groups[c.group >= 0 && c.group < CHEAT_GROUPS ? c.group : CHEAT_GROUP_OTHER] addObject:@(k)];
        usable++;
    }
    NSString *about = db >= 0
        ? [NSString stringWithFormat:@"%d cheats for this game version from the community database " CDB_SOURCE ", by their authors. %@", usable, dbStatus]
        : [TOTKCheatDb busy] ? @"Downloading the community cheat list (" CDB_SOURCE ") for this game version…"
        : [NSString stringWithFormat:@"The community database (" CDB_SOURCE ") has no cheats for this game version yet, or the list could not be downloaded. %@", dbStatus];
    NSButton *update = [self cheatsButton:[TOTKCheatDb busy] ? @"Checking…" : @"Update" action:@selector(updateCheatList)];
    update.enabled = ![TOTKCheatDb busy];
    [self addCheatsRow:[self cheatsRow:@"Cheat list" detail:about color:[TOTKCheatDb problem] ? NSColor.systemOrangeColor : nil control:update] id:CHEATS_DB_ROW];
    if (dl >= 0) { NSView *row = [self rowFor:&SETTINGS[dl] index:dl]; [list addArrangedSubview:row]; [rows addObject:row]; [rowSetting addObject:@(dl)]; }
    for (int g = 0; g < CHEAT_GROUPS; g++) {
        if (!groups[g].count) continue;
        [self cheatsHeading:@(CHEAT_GROUP_NAMES[g]) detail:nil problem:nil big:NO];
        for (NSNumber *n in groups[g]) {
            int k = n.intValue; RuntimeCheatInfo c; runtime_cheats_info(k, &c);
            NSColor *color; NSString *detail;
            if (c.choice < 0) {
                detail = cheat_detail(&c, &color);
                NSSwitch *toggle = [NSSwitch new]; toggle.state = (c.state & CHEAT_ON) ? NSControlStateValueOn : NSControlStateValueOff;
                toggle.tag = k; toggle.target = self; toggle.action = @selector(cheatSwitched:); toggle.refusesFirstResponder = YES;
                [self addCheatsRow:[self cheatsRow:@(c.label) about:c.about ? @(c.about) : nil detail:detail color:color control:toggle] id:CHEAT_ROW - k];
                continue;
            }
            int all[16], ok[16], nok, on = -1;
            cheat_choice_members(k, all, ok, 16, &nok);
            NSPopUpButton *menu = [NSPopUpButton new]; [menu addItemWithTitle:@"Off"]; menu.lastItem.tag = -1;
            for (int i = 0; i < nok; i++) {
                RuntimeCheatInfo v; runtime_cheats_info(ok[i], &v);
                [menu addItemWithTitle:@(v.option ? v.option : v.label)]; menu.lastItem.tag = ok[i];
                if ((v.state & CHEAT_ON) && on < 0) { on = ok[i]; [menu selectItem:menu.lastItem]; }
            }
            RuntimeCheatInfo shown; runtime_cheats_info(on >= 0 ? on : ok[0], &shown);
            detail = cheat_detail(&shown, &color);
            if (on < 0 && !(shown.state & CHEAT_APPLIED)) {   /* Off: how a variant would apply, unless one turns off at the restart */
                for (int i = 0; i < nok; i++) { RuntimeCheatInfo v; runtime_cheats_info(ok[i], &v); if (v.state & CHEAT_APPLIED) { detail = cheat_detail(&v, &color); break; } }
            }
            menu.tag = k; menu.target = self; menu.action = @selector(cheatChoiceChanged:); menu.refusesFirstResponder = YES;
            [menu.widthAnchor constraintEqualToConstant:118].active = YES;
            [self addCheatsRow:[self cheatsRow:@(c.label) about:c.about ? @(c.about) : nil detail:detail color:color control:menu] id:CHEAT_ROW - k];
        }
    }
    if (refused.count) {
        NSButton *show = [self cheatsButton:cheatsUnavailableShown ? @"Hide" : @"Show" action:@selector(toggleCheatsUnavailable)];
        [self addCheatsRow:[self cheatsRow:[NSString stringWithFormat:@"%lu not available here", (unsigned long)refused.count]
                                    detail:@"They patch the game's code in a way this port cannot follow (under a button condition, or code it rewrote). Show says why for each."
                                     color:nil control:show] id:CHEATS_UNAVAILABLE_ROW];
        for (NSNumber *n in cheatsUnavailableShown ? refused : @[]) {
            RuntimeCheatInfo c; runtime_cheats_info(n.intValue, &c);
            [self cheatsHeading:@(c.label) detail:nil problem:@(c.problem) big:NO];
        }
    }
}
- (void)buildCheats {
    if (!cheatDbAsked) { cheatDbAsked = YES; [TOTKCheatDb update:NO]; }   /* the first look this run: check if the day is over */
    int total = runtime_cheats_total(), nsrc = runtime_cheats_sources(), db = runtime_cheats_db_source();
    const char *bid = runtime_cheats_build_id(), *folder = runtime_cheats_folder();
    NSString *file = [NSString stringWithFormat:@"%s.txt", *bid ? bid : "<build ID>"];
    BOOL hasFolder = folder && *folder && strcmp(folder, "0");
    NSString *where = hasFolder ? [@(folder) stringByAbbreviatingWithTildeInPath] : @"(off: TOTK_CHEATS_DIR=0)";
    if (runtime_cheats_count()) {
        NSButton *off = [self cheatsButton:@"Turn off" action:@selector(cheatsAllOff)]; off.enabled = cheats_any_on();
        [self addCheatsRow:[self cheatsRow:@"Turn off all cheats" detail:@"Live cheats stop at once; code patches after a restart. "
                            "The first cheat you turn on backs up your saves (profile-before-cheats)." color:nil control:off] id:CHEATS_OFF_ROW];
    }
    int pending = cheats_pending(), changed = runtime_cheats_files_changed();
    if (pending || changed) {
        NSString *why = pending ? [NSString stringWithFormat:@"%d code patch change%s waiting for the game to start again.", pending, pending == 1 ? "" : "s"]
                      : changed == 2 ? @"A newer cheat list was downloaded: it is read at the next start."
                                     : @"The cheat files changed since the game started: they are read again at the next start.";
        NSButton *restart = runtime_front_can_restart() ? [self cheatsButton:@"Restart now" action:@selector(restartGame)] : nil;
        restart.bezelColor = NSColor.systemOrangeColor;
        [self addCheatsRow:[self cheatsRow:@"Restart to apply" detail:why color:NSColor.systemOrangeColor control:restart] id:CHEATS_RESTART_ROW];
    }
    [self buildCheatList];
    [self cheatsHeading:@"Your own cheat files" detail:nil problem:nil big:YES];
    NSString *about = [NSString stringWithFormat:@"Add cheats of your own: %@ (Atmosphère format, as Yuzu and Ryujinx use; this game's build ID is %s) "
                       "in %@, or in a mod's cheats folder. Files are read when the game starts.", file, *bid ? bid : "unknown", where];
    [self addCheatsRow:[self cheatsRow:@"Cheats folder" detail:about color:nil control:hasFolder ? [self cheatsButton:@"Open" action:@selector(openCheatsFolder)] : nil]
                    id:CHEATS_FOLDER_ROW];
    for (int s = 0; s < nsrc; s++) {
        if (s == db) continue;   /* above */
        [self cheatsHeading:@(runtime_cheats_source_label(s)) detail:[@(runtime_cheats_source_path(s)) stringByAbbreviatingWithTildeInPath]
                    problem:@(runtime_cheats_source_problem(s)) big:YES];
        for (int k = 0; k < total; k++) {
            RuntimeCheatInfo c; if (!runtime_cheats_info(k, &c) || c.source != s) continue;
            if (c.role == 3) continue;   /* a group's end */
            if (c.role == 2) { [self cheatsHeading:@(c.name) detail:nil problem:nil big:NO]; continue; }
            NSColor *color; NSString *detail = cheat_detail(&c, &color);
            if (c.role == 1) {   /* a master code: always on with its file, nothing to switch */
                [self addCheatsRow:[self cheatsRow:@(c.name) detail:[@"Master code: always on. " stringByAppendingString:detail] color:color control:nil] id:CHEAT_ROW - k];
                continue;
            }
            NSSwitch *toggle = [NSSwitch new]; toggle.state = (c.state & CHEAT_ON) ? NSControlStateValueOn : NSControlStateValueOff;
            toggle.enabled = !(c.state & CHEAT_REFUSED); toggle.tag = k; toggle.target = self; toggle.action = @selector(cheatSwitched:);
            toggle.refusesFirstResponder = YES;
            [self addCheatsRow:[self cheatsRow:@(c.name) detail:detail color:color control:toggle] id:CHEAT_ROW - k];
        }
    }
}
- (void)rebuildKeepingPlace {   /* the focus stays on the same row, though Restart to apply may come or go above it */
    NSNumber *keep = focus < (NSInteger)rowSetting.count ? rowSetting[focus] : nil; CGFloat y = scroll.contentView.bounds.origin.y;
    [self buildPage];
    NSUInteger at = keep ? [rowSetting indexOfObject:keep] : NSNotFound;
    if (at != NSNotFound) focus = (NSInteger)at;
    [self showFocus];
    [scroll.contentView scrollToPoint:NSMakePoint(0, y)]; [scroll reflectScrolledClipView:scroll.contentView];
}
- (void)setCheat:(int)k on:(int)on {
    RuntimeCheatInfo c;
    if (!runtime_cheats_info(k, &c) || c.role != 0 || (c.state & CHEAT_REFUSED) || !!(c.state & CHEAT_ON) == !!on) return;
    runtime_cheats_set_entry(k, on);
    [self rebuildKeepingPlace];
}
- (void)cheatSwitched:(NSSwitch *)toggle {
    NSUInteger at = [rowSetting indexOfObject:@(CHEAT_ROW - (int)toggle.tag)]; if (at != NSNotFound) focus = at;
    [self setCheat:(int)toggle.tag on:toggle.state == NSControlStateValueOn];
    [self.window makeFirstResponder:self];
}
/* A choice: Off (chosen = -1) or one variant; the others are switched off first. */
- (void)setCheatChoice:(int)leader to:(int)chosen {
    int all[16], ok[16], nok, n = cheat_choice_members(leader, all, ok, 16, &nok);
    for (int i = 0; i < n; i++) {
        RuntimeCheatInfo c;
        if (all[i] != chosen && runtime_cheats_info(all[i], &c) && (c.state & CHEAT_ON)) runtime_cheats_set_entry(all[i], 0);
    }
    RuntimeCheatInfo c;
    if (chosen >= 0 && runtime_cheats_info(chosen, &c) && !(c.state & CHEAT_ON)) runtime_cheats_set_entry(chosen, 1);
    [self rebuildKeepingPlace];
}
- (void)cheatChoiceChanged:(NSPopUpButton *)menu {
    NSUInteger at = [rowSetting indexOfObject:@(CHEAT_ROW - (int)menu.tag)]; if (at != NSNotFound) focus = at;
    [self setCheatChoice:(int)menu.tag to:(int)menu.selectedItem.tag];
    [self.window makeFirstResponder:self];
}
/* Left / right: the previous / next option; Return / A: the next, round to Off. */
- (void)stepCheatChoice:(int)leader dir:(int)dir {
    int all[16], ok[16], nok, at = 0;
    cheat_choice_members(leader, all, ok, 16, &nok);
    for (int i = 0; i < nok; i++) { RuntimeCheatInfo c; if (runtime_cheats_info(ok[i], &c) && (c.state & CHEAT_ON)) { at = i + 1; break; } }
    int next = dir ? at + dir : (at + 1) % (nok + 1);
    if (next < 0 || next > nok || next == at) return;
    [self setCheatChoice:leader to:next ? ok[next - 1] : -1];
}
- (void)updateCheatList { [TOTKCheatDb update:YES]; }
- (void)toggleCheatsUnavailable { cheatsUnavailableShown = !cheatsUnavailableShown; [self rebuildKeepingPlace]; }
- (void)cheatsAllOff {
    for (int i = 0; i < runtime_cheats_count(); i++) if (runtime_cheats_state(i) & CHEAT_ON) runtime_cheats_set(i, 0);
    [self rebuildKeepingPlace];
}
- (void)openCheatsFolder {
    const char *folder = runtime_cheats_folder();
    if (folder && *folder && strcmp(folder, "0")) [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:@(folder) isDirectory:YES]];
}
+ (void)showCheats {
    TOTKSettingsPanel *p = [self shared];
    p->page = CHEATS_PAGE; p->focus = 0;
    if (p.superview) [p buildPage]; else [p show];
}

- (void)buildPage {
    BOOL modsPage = page == MODS_PAGE;
    scroll.hidden = modsPage; mods.hidden = !modsPage; reset.hidden = modsPage || page == CHEATS_PAGE;   /* Reset all: settings only */
    hint.stringValue = modsPage ? MODS_HINT : SETTINGS_HINT;
    if (self.superview) [self place];
    if (modsPage) { tabs.selectedSegment = page; [mods refresh]; [self refreshStatus]; return; }
    for (NSView *v in list.arrangedSubviews.copy) { [list removeArrangedSubview:v]; [v removeFromSuperview]; }
    [rows removeAllObjects]; [rowSetting removeAllObjects];
    for (int i = 0; i < SETTINGS_COUNT && page < SETTINGS_NPAGES; i++) {
        const SettingDef *d = &SETTINGS[i];
        if (strcmp(d->page, SETTINGS_PAGES[page]) || !settings_row_shown(d)) continue;
        NSView *row = [self rowFor:d index:i];
        [list addArrangedSubview:row]; [rows addObject:row]; [rowSetting addObject:@(i)];
    }
    if (page == CHEATS_PAGE) [self buildCheats];
    else if (!strcmp(SETTINGS_PAGES[page], "Interface")) {
        NSView *saves = [self savesRow];
        [list addArrangedSubview:saves]; [rows addObject:saves]; [rowSetting addObject:@(SAVES_ROW)];
    }
    if (!rows.count) {
        NSTextField *none = [NSTextField labelWithString:@"Nothing to set here on the Mac."]; none.textColor = NSColor.secondaryLabelColor;
        [list addArrangedSubview:none];
    }
    if (focus >= (NSInteger)rows.count) focus = rows.count ? rows.count - 1 : 0;
    tabs.selectedSegment = page;
    [self refreshStatus]; [self showFocus];
}

- (void)refreshStatus {
    const char *s = settings_display_status();
    status.stringValue = s && *s ? @(s) : @"";
    int pending = mods_changed_since_start();   /* Restart now: a setting marked (after restart), or mod changes */
    for (int i = 0; i < SETTINGS_COUNT && !pending; i++) pending = settings_pending_restart(SETTINGS[i].key);
    if (!pending) pending = cheats_pending() || runtime_cheats_files_changed();   /* code patches switched, cheat files changed */
    restartNow.hidden = !pending || !runtime_front_can_restart();
}
- (void)restartGame { [self hide]; runtime_front_restart(); }

- (void)showFocus {
    for (NSUInteger k = 0; k < rows.count; k++)
        rows[k].layer.backgroundColor = (NSInteger)k == focus ? panel_color(1, 0.10).CGColor : NSColor.clearColor.CGColor;
    if (focus < (NSInteger)rows.count) {
        [list layoutSubtreeIfNeeded];
        [rows[focus] scrollRectToVisible:NSInsetRect(rows[focus].bounds, 0, -8)];
    }
}

/* ---- changes ---- */

- (void)apply:(int)i value:(int)v {
    const SettingDef *d = &SETTINGS[i];
    if (!settings_valid(d, v) || v == settings_get(d->key)) return;
    settings_set(d->key, v);
    if (!strcmp(d->key, "interface.download_cheats") && v) [TOTKCheatDb update:NO];   /* turned on: check if the day is over */
    NSInteger keep = focus; CGFloat y = scroll.contentView.bounds.origin.y;
    [self buildPage];   /* the row's label (next start), rows that depend on it, the status line */
    focus = keep < (NSInteger)rows.count ? keep : focus; [self showFocus];
    [scroll.contentView scrollToPoint:NSMakePoint(0, y)]; [scroll reflectScrolledClipView:scroll.contentView];
}

- (void)changed:(NSControl *)control {
    int i = (int)control.tag; const SettingDef *d = &SETTINGS[i]; int v;
    if ([control isKindOfClass:NSSwitch.class]) v = ((NSSwitch *)control).state == NSControlStateValueOn;
    else if ([control isKindOfClass:NSPopUpButton.class]) v = d->choices[((NSPopUpButton *)control).indexOfSelectedItem].value;
    else v = d->min + (int)lround((((NSSlider *)control).doubleValue - d->min) / d->step) * d->step;
    NSUInteger at = [rowSetting indexOfObject:@(i)]; if (at != NSNotFound) focus = at;
    if (d->type == SET_RANGE) {   /* a drag: change the value and its text, keep the slider (rebuilding would end the drag) */
        for (NSView *v2 in control.superview.subviews) if ([v2.identifier isEqual:@"value"]) ((NSTextField *)v2).stringValue = settings_value_text(d, v);
        if (v != settings_get(d->key)) settings_set(d->key, v);
        [self refreshStatus]; [self showFocus];
        return;
    }
    [self apply:i value:v];
    [self.window makeFirstResponder:self];
}

/* One step left (-1) or right (+1) on the focused row; 0 = activate (toggle, next choice). */
- (void)step:(int)dir {
    if (page == MODS_PAGE) { [mods step:dir]; return; }
    if (focus >= (NSInteger)rows.count) return;
    int i = rowSetting[focus].intValue;
    if (i == SAVES_ROW) { if (!dir) [self sendSaves]; return; }
    if (i == CHEATS_FOLDER_ROW) { if (!dir) [self openCheatsFolder]; return; }
    if (i == CHEATS_OFF_ROW) { if (!dir) [self cheatsAllOff]; return; }
    if (i == CHEATS_RESTART_ROW) { if (!dir && runtime_front_can_restart()) [self restartGame]; return; }
    if (i == CHEATS_DB_ROW) { if (!dir) [self updateCheatList]; return; }
    if (i == CHEATS_UNAVAILABLE_ROW) { if (!dir) [self toggleCheatsUnavailable]; return; }
    if (i <= CHEAT_ROW) {   /* Return / A switches it, left off, right on; a choice steps through its options */
        RuntimeCheatInfo c; int k = CHEAT_ROW - i;
        if (runtime_cheats_info(k, &c) && c.choice == k) [self stepCheatChoice:k dir:dir];
        else if (runtime_cheats_info(k, &c)) [self setCheat:k on:dir ? dir > 0 : !(c.state & CHEAT_ON)];
        return;
    }
    const SettingDef *d = &SETTINGS[i]; int v = settings_get(d->key);
    if (d->type == SET_BOOL) v = dir ? dir > 0 : !v;
    else if (d->type == SET_RANGE) { if (!dir) return; v += dir * d->step; v = v < d->min ? d->min : v > d->max ? d->max : v; }
    else {
        int k = 0; while (k < d->nchoices && d->choices[k].value != v) k++;
        k = dir ? k + dir : (k + 1) % d->nchoices;
        if (k < 0 || k >= d->nchoices) return;
        v = d->choices[k].value;
    }
    [self apply:i value:v];
}

- (void)moveFocus:(int)dir {
    if (page == MODS_PAGE) { [mods moveFocus:dir]; return; }
    if (!rows.count) return;
    focus = (focus + dir + (NSInteger)rows.count) % (NSInteger)rows.count; [self showFocus];
}
- (void)movePage:(int)dir { page = (page + dir + NPAGES) % NPAGES; focus = 0; [self buildPage]; }
- (void)tabClicked:(NSSegmentedControl *)s { page = s.selectedSegment; focus = 0; [self buildPage]; [self.window makeFirstResponder:self]; }

- (void)reset {
    for (int i = 0; i < SETTINGS_COUNT; i++) if (SETTINGS[i].platforms & SET_MAC) settings_set(SETTINGS[i].key, SETTINGS[i].def_mac);
    [self buildPage];
}

- (void)keyDown:(NSEvent *)e {
    if (page == MODS_PAGE && [mods key:e]) return;
    switch (e.keyCode) {
    case 126: [self moveFocus:-1]; return;   /* up */
    case 125: [self moveFocus:1]; return;    /* down */
    case 123: [self step:-1]; return;        /* left */
    case 124: [self step:1]; return;         /* right */
    case 36: case 76: case 49: if (!e.isARepeat) [self step:0]; return;   /* Return, Enter, Space */
    case 12: [self movePage:-1]; return;     /* Q */
    case 14: case 48: [self movePage:(e.modifierFlags & NSEventModifierFlagShift) ? -1 : 1]; return;   /* E, Tab */
    case 53: case 122: if (!e.isARepeat) [self hide]; return;   /* Escape, F1 */
    default: return;   /* every other key stays with the menu, away from the game */
    }
}
- (void)keyUp:(NSEvent *)e { (void)e; }
- (void)flagsChanged:(NSEvent *)e { (void)e; }

/* ---- controller ---- */

- (void)poll {
    uint64_t now = 0; float ly = 0, lx = 0;
    for (GCController *c in GCController.controllers) {
        GCExtendedGamepad *p = c.extendedGamepad; if (!p) continue;
        if (p.dpad.up.isPressed) now |= 1; if (p.dpad.down.isPressed) now |= 2;
        if (p.dpad.left.isPressed) now |= 4; if (p.dpad.right.isPressed) now |= 8;
        if (p.buttonA.isPressed) now |= 16; if (p.buttonB.isPressed) now |= 32;
        if (p.leftShoulder.isPressed) now |= 64; if (p.rightShoulder.isPressed) now |= 128;
        if (p.buttonMenu.isPressed) now |= 256;
        if (p.buttonX.isPressed) now |= 512; if (p.buttonY.isPressed) now |= 1024;
        if (fabsf(p.leftThumbstick.yAxis.value) > fabsf(ly)) ly = p.leftThumbstick.yAxis.value;
        if (fabsf(p.leftThumbstick.xAxis.value) > fabsf(lx)) lx = p.leftThumbstick.xAxis.value;
    }
    if (ly > 0.6f) now |= 1; if (ly < -0.6f) now |= 2; if (lx < -0.6f) now |= 4; if (lx > 0.6f) now |= 8;
    uint64_t edge = now & ~padPrev; padPrev = now;
    double t = CACurrentMediaTime();
    int dir = now & 1 ? 1 : now & 2 ? 2 : now & 4 ? 3 : now & 8 ? 4 : 0;   /* held direction, repeated */
    if (!dir) padDir = 0;
    else if (dir != padDir) { padDir = dir; padRepeatAt = t + 0.35; edge |= 1u << (dir - 1); }
    else if (t >= padRepeatAt) { padRepeatAt = t + 0.09; edge |= 1u << (dir - 1); }
    else edge &= ~15u;
    if (edge & 1) [self moveFocus:-1];
    if (edge & 2) [self moveFocus:1];
    if (edge & 4) [self step:-1];
    if (edge & 8) [self step:1];
    if (edge & 16) [self step:0];
    if (edge & 64) [self movePage:-1];
    if (edge & 128) [self movePage:1];
    if (page == MODS_PAGE && (edge & 512)) [mods moveMod:-1];   /* X / Y: the mod's place in the load order */
    if (page == MODS_PAGE && (edge & 1024)) [mods moveMod:1];
    if (edge & (32 | 256)) [self hide];
}

/* ---- showing ---- */

- (void)place {
    NSView *host = settings_window().contentView;
    CGFloat width = page == MODS_PAGE ? MIN(MODS_PANEL_WIDTH, MAX(PANEL_WIDTH, host.bounds.size.width - 2 * PANEL_MARGIN)) : PANEL_WIDTH;
    self.frame = NSMakeRect(PANEL_MARGIN, PANEL_MARGIN, width, MAX(320, host.bounds.size.height - 2 * PANEL_MARGIN));
    self.autoresizingMask = NSViewHeightSizable | NSViewMaxXMargin;
}

- (void)show {
    NSWindow *w = settings_window(); if (!w || self.superview) return;
    [self place]; [self buildPage];
    [w.contentView addSubview:self positioned:NSWindowAbove relativeTo:nil];
    [w makeFirstResponder:self];
    runtime_input_hold(1);
    padPrev = ~0ull;   /* the buttons that opened the menu do nothing until released */
    pollTimer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 60 target:self selector:@selector(poll) userInfo:nil repeats:YES];
    fprintf(stderr, "[settings] menu open\n");
}

- (void)hide {
    if (!self.superview) return;
    [pollTimer invalidate]; pollTimer = nil;
    NSWindow *w = self.window;
    [self removeFromSuperview];
    [w makeFirstResponder:w.contentView];
    runtime_input_hold(0);
    fprintf(stderr, "[settings] menu closed\n");
}

+ (BOOL)snapshotTo:(NSString *)path {
    TOTKSettingsPanel *p = [self shared];
    BOOL shown = p.superview != nil;
    if (!shown) { [p place]; [p buildPage]; }
    [p layoutSubtreeIfNeeded];
    NSBitmapImageRep *rep = [p bitmapImageRepForCachingDisplayInRect:p.bounds];
    if (!rep) return NO;
    __block BOOL ok = NO;
    [p.appearance performAsCurrentDrawingAppearance:^{
        [p cacheDisplayInRect:p.bounds toBitmapImageRep:rep];
        NSBitmapImageRep *out = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:rep.pixelsWide pixelsHigh:rep.pixelsHigh
            bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
        out.size = p.bounds.size;
        [NSGraphicsContext saveGraphicsState];
        NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithBitmapImageRep:out];
        [[NSColor colorWithCalibratedRed:0.07 green:0.08 blue:0.09 alpha:1] setFill]; NSRectFill(p.bounds);   /* the layer's own background */
        [rep drawInRect:p.bounds fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
        [NSGraphicsContext restoreGraphicsState];
        ok = [[out representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES];
    }];
    return ok;
}
@end
