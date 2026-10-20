/* The settings menu on the Mac (included by runtime/frontend.m only): a sidebar drawn inside the game window, so it works
   in full screen and leaves the picture beside it in view while a change takes effect. Built from the shared settings
   table (runtime/settings_schema.h), one tab per page. Opened with Command-comma, F1, the app menu's Settings item, or a
   controller's Home button (or Options and Menu together). Mouse, keyboard (arrows choose and change, Return toggles,
   Q / E or Tab switch tabs, Escape closes) and controller (D-pad or left stick, A, B, shoulder buttons) all work. While it
   is open the game reads a neutral controller (runtime_input_hold). The iPhone / iPad menu is ios/Sources/settings_menu.m. */
#pragma once
#import <Cocoa/Cocoa.h>
#import <GameController/GameController.h>
#include "runtime.h"
#include "settings.h"

@interface TOTKFlippedView : NSView @end   /* the scrolling rows start at the top */
@implementation TOTKFlippedView
- (BOOL)isFlipped { return YES; }
@end

@interface TOTKSettingsPanel : NSView
+ (void)toggle;
+ (BOOL)snapshotTo:(NSString *)path;   /* the menu as a PNG (test runs, TOTK_INPUT_FILE "settings-shot") */
+ (void)key:(NSEvent *)event;          /* a key for the open menu (test runs, TOTK_INPUT_FILE "menukey") */
@end

static NSWindow *settings_window(void);   /* frontend.m: the game window */

@implementation TOTKSettingsPanel {
    NSSegmentedControl *tabs;
    NSScrollView *scroll;
    NSStackView *list;
    NSTextField *status;
    NSMutableArray<NSView *> *rows;       /* the rows of the page shown, in order */
    NSMutableArray<NSNumber *> *rowSetting;
    NSInteger page, focus;
    NSTimer *pollTimer;
    uint64_t padPrev; double padRepeatAt; int padDir;
}
static TOTKSettingsPanel *settingsPanel;
enum { PANEL_WIDTH = 440, PANEL_MARGIN = 16 };

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
    tabs = [NSSegmentedControl segmentedControlWithLabels:labels trackingMode:NSSegmentSwitchTrackingSelectOne target:self action:@selector(tabClicked:)];
    tabs.segmentStyle = NSSegmentStyleAutomatic; tabs.selectedSegment = 0; tabs.refusesFirstResponder = YES;
    tabs.segmentDistribution = NSSegmentDistributionFillEqually; tabs.controlSize = NSControlSizeSmall;
    tabs.font = [NSFont systemFontOfSize:11];

    list = [NSStackView new]; list.orientation = NSUserInterfaceLayoutOrientationVertical;
    list.alignment = NSLayoutAttributeLeading; list.spacing = 4; list.translatesAutoresizingMaskIntoConstraints = NO;
    list.edgeInsets = NSEdgeInsetsMake(4, 0, 12, 0);
    scroll = [NSScrollView new]; scroll.hasVerticalScroller = YES; scroll.drawsBackground = NO; scroll.autohidesScrollers = YES;
    NSView *doc = [TOTKFlippedView new]; doc.translatesAutoresizingMaskIntoConstraints = NO;
    [doc addSubview:list]; scroll.documentView = doc;

    NSTextField *hint = [NSTextField wrappingLabelWithString:@"↑↓ choose   ←→ change   Q E tabs   Esc close\nController: D-pad, A, B, shoulder buttons"];
    hint.font = [NSFont systemFontOfSize:10.5]; hint.textColor = NSColor.tertiaryLabelColor;
    NSButton *reset = [NSButton buttonWithTitle:@"Reset all" target:self action:@selector(reset)];
    reset.controlSize = NSControlSizeSmall; reset.refusesFirstResponder = YES; reset.bezelStyle = NSBezelStyleRounded;

    for (NSView *v in @[title, status, tabs, scroll, hint, reset]) { v.translatesAutoresizingMaskIntoConstraints = NO; [self addSubview:v]; }
    [NSLayoutConstraint activateConstraints:@[
        [title.topAnchor constraintEqualToAnchor:self.topAnchor constant:18], [title.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:20],
        [status.firstBaselineAnchor constraintEqualToAnchor:title.firstBaselineAnchor], [status.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-20],
        [status.leadingAnchor constraintGreaterThanOrEqualToAnchor:title.trailingAnchor constant:12],
        [tabs.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:14],
        [tabs.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:16], [tabs.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-16],
        [scroll.topAnchor constraintEqualToAnchor:tabs.bottomAnchor constant:10],
        [scroll.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:8], [scroll.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-8],
        [scroll.bottomAnchor constraintEqualToAnchor:hint.topAnchor constant:-10],
        [hint.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:20], [hint.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-14],
        [hint.trailingAnchor constraintLessThanOrEqualToAnchor:reset.leadingAnchor constant:-10],
        [reset.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-16], [reset.centerYAnchor constraintEqualToAnchor:hint.centerYAnchor],
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

- (void)buildPage {
    for (NSView *v in list.arrangedSubviews.copy) { [list removeArrangedSubview:v]; [v removeFromSuperview]; }
    [rows removeAllObjects]; [rowSetting removeAllObjects];
    for (int i = 0; i < SETTINGS_COUNT; i++) {
        const SettingDef *d = &SETTINGS[i];
        if (strcmp(d->page, SETTINGS_PAGES[page]) || !settings_row_shown(d)) continue;
        NSView *row = [self rowFor:d index:i];
        [list addArrangedSubview:row]; [rows addObject:row]; [rowSetting addObject:@(i)];
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
}

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
    if (focus >= (NSInteger)rows.count) return;
    int i = rowSetting[focus].intValue; const SettingDef *d = &SETTINGS[i]; int v = settings_get(d->key);
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
    if (!rows.count) return;
    focus = (focus + dir + (NSInteger)rows.count) % (NSInteger)rows.count; [self showFocus];
}
- (void)movePage:(int)dir { page = (page + dir + SETTINGS_NPAGES) % SETTINGS_NPAGES; focus = 0; [self buildPage]; }
- (void)tabClicked:(NSSegmentedControl *)s { page = s.selectedSegment; focus = 0; [self buildPage]; [self.window makeFirstResponder:self]; }

- (void)reset {
    for (int i = 0; i < SETTINGS_COUNT; i++) if (SETTINGS[i].platforms & SET_MAC) settings_set(SETTINGS[i].key, SETTINGS[i].def_mac);
    [self buildPage];
}

- (void)keyDown:(NSEvent *)e {
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
    if (edge & (32 | 256)) [self hide];
}

/* ---- showing ---- */

- (void)place {
    NSView *host = settings_window().contentView;
    self.frame = NSMakeRect(PANEL_MARGIN, PANEL_MARGIN, PANEL_WIDTH, MAX(320, host.bounds.size.height - 2 * PANEL_MARGIN));
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
