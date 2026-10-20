/* The settings menu on the Mac (included by runtime/frontend.m only): a panel over the game window built from the shared
   settings table (runtime/settings_schema.h), one section per page, opened with Command-comma, F1 or the app menu's
   Settings item. Each row shows its help under its name; rows only iPhone and iPad have (touch controls) are left out. While the panel
   is open the game reads a neutral controller (runtime_input_hold). The iPhone / iPad menu is ios/Sources/settings_menu.m. */
#pragma once
#import <Cocoa/Cocoa.h>
#include "runtime.h"
#include "settings.h"

@interface TOTKFlippedView : NSView @end   /* the panel's scrolling contents start at the top */
@implementation TOTKFlippedView
- (BOOL)isFlipped { return YES; }
@end

@interface TOTKSettingsPanel : NSObject <NSWindowDelegate>
+ (void)toggle;
+ (BOOL)snapshotTo:(NSString *)path;   /* the panel's contents as a PNG, without showing it (test runs, TOTK_INPUT_FILE "settings-shot") */
@end

@implementation TOTKSettingsPanel {
    NSPanel *panel;
    NSMutableDictionary<NSString *, NSTextField *> *labels, *values;
}
static TOTKSettingsPanel *settingsPanel;

+ (void)toggle {
    if (!settingsPanel) settingsPanel = [TOTKSettingsPanel new];
    if (settingsPanel->panel.visible) [settingsPanel->panel close]; else [settingsPanel show];
}

static NSString *settings_value_text(const SettingDef *d, int v) {
    if (d->type == SET_CHOICE) { const SettingChoice *c = settings_choice(d, v); return c ? @(c->label) : @"?"; }
    if (d->type == SET_RANGE) return [NSString stringWithFormat:@"%d%s", v, d->unit ? d->unit : ""];
    return v ? @"On" : @"Off";
}

- (NSString *)labelFor:(const SettingDef *)d {
    return settings_pending_restart(d->key) ? [NSString stringWithFormat:@"%s (next start)", d->label] : @(d->label);
}

- (void)changed:(NSControl *)control {
    const SettingDef *d = &SETTINGS[control.tag]; int v;
    if ([control isKindOfClass:NSSwitch.class]) v = ((NSSwitch *)control).state == NSControlStateValueOn;
    else if ([control isKindOfClass:NSPopUpButton.class]) v = d->choices[((NSPopUpButton *)control).indexOfSelectedItem].value;
    else { v = d->min + (int)lround((((NSSlider *)control).doubleValue - d->min) / d->step) * d->step; }
    values[@(d->key)].stringValue = settings_value_text(d, v);
    if (v != settings_get(d->key)) settings_set(d->key, v);
    labels[@(d->key)].stringValue = [self labelFor:d];
}

- (NSView *)controlFor:(const SettingDef *)d index:(int)i enabled:(BOOL)on {
    int v = settings_get(d->key); NSControl *c;
    if (d->type == SET_BOOL) { NSSwitch *s = [NSSwitch new]; s.state = v ? NSControlStateValueOn : NSControlStateValueOff; c = s; }
    else if (d->type == SET_CHOICE) {
        NSPopUpButton *p = [NSPopUpButton new];
        for (int k = 0; k < d->nchoices; k++) { [p addItemWithTitle:@(d->choices[k].label)]; if (d->choices[k].value == v) [p selectItemAtIndex:k]; }
        [p.widthAnchor constraintEqualToConstant:220].active = YES; c = p;
    } else {
        NSSlider *s = [NSSlider sliderWithValue:v minValue:d->min maxValue:d->max target:nil action:nil];
        s.numberOfTickMarks = (d->max - d->min) / d->step + 1; s.allowsTickMarkValuesOnly = YES; s.continuous = YES;
        [s.widthAnchor constraintEqualToConstant:200].active = YES; c = s;
    }
    c.tag = i; c.target = self; c.action = @selector(changed:); c.enabled = on;
    if (d->type != SET_RANGE) return c;
    NSTextField *value = [NSTextField labelWithString:settings_value_text(d, v)];
    value.font = [NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightRegular];
    [value.widthAnchor constraintEqualToConstant:52].active = YES; values[@(d->key)] = value;
    NSStackView *row = [NSStackView stackViewWithViews:@[c, value]]; row.spacing = 8;
    return row;
}

+ (BOOL)snapshotTo:(NSString *)path {
    if (!settingsPanel) settingsPanel = [TOTKSettingsPanel new];
    [settingsPanel build];
    NSView *view = settingsPanel->panel.contentView; [view layoutSubtreeIfNeeded];
    NSBitmapImageRep *rep = [view bitmapImageRepForCachingDisplayInRect:view.bounds], *out = [rep copy];
    if (!rep) return NO;
    __block BOOL ok = NO;
    [settingsPanel->panel.appearance performAsCurrentDrawingAppearance:^{   /* the dark panel, on its window background */
        [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
        [NSGraphicsContext saveGraphicsState];
        NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithBitmapImageRep:out];
        [NSColor.windowBackgroundColor setFill]; NSRectFill(view.bounds);
        [rep drawInRect:view.bounds fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
        [NSGraphicsContext restoreGraphicsState];
        ok = [[out representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES];
    }];
    return ok;
}

- (void)show {
    [self build];
    runtime_input_hold(1);
    [panel makeKeyAndOrderFront:nil];
}

- (void)reset {
    for (int i = 0; i < SETTINGS_COUNT; i++) if (SETTINGS[i].platforms & SET_MAC) settings_set(SETTINGS[i].key, SETTINGS[i].def_mac);
    NSRect frame = panel.frame; BOOL shown = panel.visible;
    [panel orderOut:nil]; panel.delegate = nil; panel = nil; [self build]; [panel setFrame:frame display:NO];
    if (shown) [panel makeKeyAndOrderFront:nil];
}

- (void)build {
    if (!panel) {
        labels = [NSMutableDictionary dictionary]; values = [NSMutableDictionary dictionary];
        NSStackView *stack = [NSStackView new]; stack.orientation = NSUserInterfaceLayoutOrientationVertical;
        stack.alignment = NSLayoutAttributeLeading; stack.spacing = 10; stack.edgeInsets = NSEdgeInsetsMake(16, 20, 20, 20);
        for (int p = 0; p < SETTINGS_NPAGES; p++) {
            NSTextField *head = [NSTextField labelWithString:@(SETTINGS_PAGES[p])]; head.font = [NSFont boldSystemFontOfSize:13];
            NSGridView *grid = [NSGridView gridViewWithNumberOfColumns:2 rows:0];
            grid.columnSpacing = 20; grid.rowSpacing = 12; [grid columnAtIndex:0].width = 250; grid.yPlacement = NSGridCellPlacementCenter;
            for (int i = 0; i < SETTINGS_COUNT; i++) {
                const SettingDef *d = &SETTINGS[i]; if (strcmp(d->page, SETTINGS_PAGES[p]) || !(d->platforms & SET_MAC)) continue;
                NSTextField *label = [NSTextField labelWithString:[self labelFor:d]]; labels[@(d->key)] = label;
                NSStackView *name = [NSStackView stackViewWithViews:@[label]]; name.orientation = NSUserInterfaceLayoutOrientationVertical;
                name.alignment = NSLayoutAttributeLeading; name.spacing = 2;
                if (d->help) {
                    NSTextField *help = [NSTextField wrappingLabelWithString:@(d->help)];
                    help.font = [NSFont systemFontOfSize:11]; help.textColor = NSColor.secondaryLabelColor; help.preferredMaxLayoutWidth = 250;
                    [name addArrangedSubview:help];
                }
                [grid addRowWithViews:@[name, [self controlFor:d index:i enabled:YES]]];
            }
            if (!grid.numberOfRows) continue;
            if (stack.arrangedSubviews.count) [stack setCustomSpacing:22 afterView:stack.arrangedSubviews.lastObject];
            [stack addArrangedSubview:head]; [stack addArrangedSubview:grid];
        }
        [stack setCustomSpacing:22 afterView:stack.arrangedSubviews.lastObject];
        NSButton *reset = [NSButton buttonWithTitle:@"Reset to defaults" target:self action:@selector(reset)];
        NSTextField *foot = [NSTextField wrappingLabelWithString:@"Saved in ~/Library/Application Support/MacNativeGames/TOTK/settings.txt. \"(next start)\" marks a change the game takes when it starts again."];
        foot.textColor = NSColor.secondaryLabelColor; foot.font = [NSFont systemFontOfSize:11]; foot.preferredMaxLayoutWidth = 520;
        [stack addArrangedSubview:reset]; [stack addArrangedSubview:foot];
        NSScrollView *scroll = [NSScrollView new]; scroll.hasVerticalScroller = YES; scroll.drawsBackground = NO;
        NSView *doc = [TOTKFlippedView new]; doc.translatesAutoresizingMaskIntoConstraints = NO; stack.translatesAutoresizingMaskIntoConstraints = NO;
        [doc addSubview:stack]; scroll.documentView = doc;
        [NSLayoutConstraint activateConstraints:@[[stack.topAnchor constraintEqualToAnchor:doc.topAnchor], [stack.leadingAnchor constraintEqualToAnchor:doc.leadingAnchor],
            [stack.trailingAnchor constraintEqualToAnchor:doc.trailingAnchor], [stack.bottomAnchor constraintEqualToAnchor:doc.bottomAnchor],
            [doc.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor]]];
        panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 600, 700) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
        panel.title = @"Settings"; panel.contentView = scroll; panel.delegate = self; panel.releasedWhenClosed = NO;
        panel.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
        [panel center];
    }
}

- (void)windowWillClose:(NSNotification *)note { (void)note; runtime_input_hold(0); }
@end
