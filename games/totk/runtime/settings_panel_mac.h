/* The settings menu on the Mac (included by runtime/frontend.m only): a panel over the game window built from the shared
   settings table (runtime/settings_schema.h), one section per page, opened with Command-comma, F1 or the app menu's
   Settings item. Rows another platform owns (touch controls) are greyed with the reason in their tooltip. While the panel
   is open the game reads a neutral controller (runtime_input_hold). The iPhone / iPad menu is ios/Sources/settings_menu.m. */
#pragma once
#import <Cocoa/Cocoa.h>
#include "runtime.h"
#include "settings.h"

@interface TOTKSettingsPanel : NSObject <NSWindowDelegate>
+ (void)toggle;
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
        c = p;
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

- (void)show {
    if (!panel) {
        labels = [NSMutableDictionary dictionary]; values = [NSMutableDictionary dictionary];
        NSStackView *stack = [NSStackView new]; stack.orientation = NSUserInterfaceLayoutOrientationVertical;
        stack.alignment = NSLayoutAttributeLeading; stack.spacing = 10; stack.edgeInsets = NSEdgeInsetsMake(16, 20, 20, 20);
        for (int p = 0; p < SETTINGS_NPAGES; p++) {
            NSTextField *head = [NSTextField labelWithString:@(SETTINGS_PAGES[p])]; head.font = [NSFont boldSystemFontOfSize:13];
            NSGridView *grid = [NSGridView gridViewWithNumberOfColumns:2 rows:0];
            grid.columnSpacing = 16; grid.rowSpacing = 8; [grid columnAtIndex:0].width = 220;
            for (int i = 0; i < SETTINGS_COUNT; i++) {
                const SettingDef *d = &SETTINGS[i]; if (strcmp(d->page, SETTINGS_PAGES[p])) continue;
                BOOL on = (d->platforms & SET_MAC) != 0;
                NSTextField *label = [NSTextField labelWithString:[self labelFor:d]]; label.textColor = on ? NSColor.labelColor : NSColor.tertiaryLabelColor;
                label.toolTip = on ? (d->help ? @(d->help) : nil) : @(d->unavailable ? d->unavailable : "Not on the Mac.");
                labels[@(d->key)] = label;
                NSGridRow *row = [grid addRowWithViews:@[label, [self controlFor:d index:i enabled:on]]];
                row.rowAlignment = NSGridRowAlignmentLastBaseline;
            }
            if (!grid.numberOfRows) continue;
            [stack addArrangedSubview:head]; [stack addArrangedSubview:grid];
        }
        NSTextField *foot = [NSTextField wrappingLabelWithString:@"Saved in ~/Library/Application Support/MacNativeGames/TOTK/settings.txt. Hover a setting for what it does."];
        foot.textColor = NSColor.secondaryLabelColor; foot.font = [NSFont systemFontOfSize:11]; [stack addArrangedSubview:foot];
        NSScrollView *scroll = [NSScrollView new]; scroll.hasVerticalScroller = YES; scroll.drawsBackground = NO;
        NSView *doc = [NSView new]; doc.translatesAutoresizingMaskIntoConstraints = NO; stack.translatesAutoresizingMaskIntoConstraints = NO;
        [doc addSubview:stack]; scroll.documentView = doc;
        [NSLayoutConstraint activateConstraints:@[[stack.topAnchor constraintEqualToAnchor:doc.topAnchor], [stack.leadingAnchor constraintEqualToAnchor:doc.leadingAnchor],
            [stack.trailingAnchor constraintEqualToAnchor:doc.trailingAnchor], [stack.bottomAnchor constraintEqualToAnchor:doc.bottomAnchor],
            [doc.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor]]];
        panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 560, 640) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
        panel.title = @"Settings"; panel.contentView = scroll; panel.delegate = self; panel.releasedWhenClosed = NO;
        panel.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
        [panel center];
    }
    runtime_input_hold(1);
    [panel makeKeyAndOrderFront:nil];
}

- (void)windowWillClose:(NSNotification *)note { (void)note; runtime_input_hold(0); }
@end
