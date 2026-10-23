/* Production AppKit panel, keyboard dispatch, mode state and output helpers.
   No guest, save files, game window or GPU commands are used. */
#include "../games/totk/runtime/frontend.m"
#include "../games/totk/runtime/input.c"
#include "../games/totk/third_party/d2r_gfx/gfx/nvn/fps_overlay.h"
#include <assert.h>

void mfx_set(int mode);
int mfx_latch(void);
void mfxup_set(int on);
void mfxup_output_set(int on);
void shortcut_test_output_init(void);
void shortcut_test_output_size(unsigned w, unsigned h);

void dm_gfx_set_flag(const char *name, int value) {
    if (!strcmp(name, "mfx_interp")) mfx_set(value);
    else if (!strcmp(name, "mfx_output")) mfxup_output_set(value);
    else if (!strcmp(name, "mfx_upscale")) mfxup_set(value);
    else abort();
}
void shortcut_test_aspect_init(double view);
/* runtime/gamepad.m's key 6 state without GameController (gamepad_layout.h's real cycle): a DualSense is connected. */
static int fakePadType = GAMEPAD_TYPE_AUTO, fakePadDetected = GAMEPAD_TYPE_PLAYSTATION;
int runtime_gamepad_type_cycle(void) { return fakePadType = gamepad_type_next(fakePadType); }
int runtime_gamepad_type(int *detected) { if (detected) *detected = fakePadDetected; return fakePadType; }
/* runtime/gamepad.m's mouse mode (key 7) without GameController; the window never has the keyboard here. */
static int fakeMouse;
int runtime_mouse_toggle(void) { return fakeMouse = !fakeMouse; }
int runtime_mouse_enabled(void) { return fakeMouse; }
void runtime_mouse_set_hook(void (*hook)(void)) { (void)hook; }
void runtime_mouse_move(double dx, double dy) { (void)dx; (void)dy; }
void runtime_mouse_button(int button, int down) { (void)button; (void)down; }
void runtime_mouse_release(void) {}
static void press(unsigned key) {
    runtime_input_key_synthetic(key, 1);
    runtime_input_key_synthetic(key, 0);
}
static void tick(void) {
    [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    [((TOTKShortcutHelp *)shortcutHelp).timer fire];
}
/* Key 3's picture mode as the box used to name it: off is 1080p without MetalFX, "native" any other size. */
static int picture(unsigned w, unsigned h) {
    uint64_t size = runtime_output_size_current();
    return !mfxup_output_enabled() && (uint32_t)size == w && (uint32_t)(size >> 32) == h;
}
static void expect(TOTKShortcutHelp *panel, NSString *text) {
    if (![panel.label.stringValue containsString:text]) {
        fprintf(stderr, "expected %s in:\n%s\n", text.UTF8String, panel.label.stringValue.UTF8String);
        abort();
    }
}

int main(int argc, const char **argv) { @autoreleasepool {
    [NSApplication sharedApplication];
    shortcut_test_output_init();
    int initial = mfx_enabled();
    assert(initial == ((!getenv("TOTK_MFX") || !strcmp(getenv("TOTK_MFX"), "0")) ? MFX_OFF : MFX_HALF));
    NSView *host = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 900, 540)];
    TOTKShortcutHelp *panel = [[TOTKShortcutHelp alloc] initWithFrame:NSZeroRect];
    [host addSubview:panel]; shortcutHelp = panel;
    assert(panel.timer && panel.timer.valid);
    assert(panel.timer.timeInterval == 0.5);
    [panel refresh];
    expect(panel, @"Settings");   /* the box points at the menu; keys 1-5 are no longer listed, so their modes are read directly */

    /* A request is not the current frame's interpolation mode until latched. */
    mfx_set(MFX_OFF); mfx_latch();
    [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.6]];
    assert(mfx_enabled() == MFX_OFF);
    press(19); tick(); assert(mfx_enabled() == MFX_OFF);
    mfx_latch(); tick(); assert(mfx_enabled() == MFX_30);
    runtime_input_key_synthetic(19, 1); runtime_input_key_synthetic(19, 1);
    runtime_input_key_synthetic(19, 0); mfx_latch(); tick();
    assert(mfx_enabled() == MFX_HALF);
    press(19); mfx_latch(); tick(); assert(mfx_enabled() == MFX_MAX);
    press(19); mfx_latch(); tick(); assert(mfx_enabled() == MFX_OFF);

    mfxup_output_set(0); tick(); assert(picture(1920, 1080));
    press(20); tick(); assert(mfxup_output_enabled());
    press(20); tick(); assert(picture(2560, 1440));
    press(20); tick(); assert(picture(1920, 1080));
    assert(!taa_enabled());
    press(23); tick(); assert(taa_enabled());
    press(23); tick(); assert(!taa_enabled());
    assert(!runtime_aspect_now());
    shortcut_test_aspect_init(1.6);   /* key 4 becomes available; a 16:10 window */
    extern float shortcut_test_aspect_followed;   /* what key 4 wrote through to the setting (-1 fit, 0 16:9, a ratio) */
    press(21); tick(); assert(runtime_aspect_step() == 1); assert(shortcut_test_aspect_followed == -1);
    press(21); tick(); assert(runtime_aspect_step() == 2); assert(fabsf(shortcut_test_aspect_followed - 21.0f / 9) < 1e-4f);
    press(21); tick(); assert(!runtime_aspect_now()); assert(shortcut_test_aspect_followed == 0);
    /* Startup/diagnostic paths can set the canvas without a key press. */
    shortcut_test_output_size(3840, 2160); tick(); assert(picture(3840, 2160));
    shortcut_test_output_size(2560, 1440);
    mfx_set(MFX_HALF); mfx_latch();
    tick();
    expect(panel, @"Settings");

    if (hud_compact()) {
        assert(panel.frame.size.width < host.bounds.size.width - 20);
        assert(panel.frame.size.height < host.bounds.size.height - 20);
        assert(panel.frame.origin.x >= 0 && panel.frame.origin.y >= 0);
        assert(NSWidth(panel.label.frame) >= panel.textSize.width);
    } else {
        NSSize fit = [panel.label sizeThatFits:panel.label.bounds.size];
        assert(fit.height <= panel.label.bounds.size.height);
    }
    /* Key 1: all -> minimal (FPS only) -> off -> all. */
    hud_publish(HUD_FPS, "FPS 60  2560x1440", 0); hud_publish(HUD_SHADER, "New shaders 3 / total 9", 0);
    press(18); tick(); assert(fps_overlay_hints_enabled() && !fps_overlay_full_hud());
    if (hud_compact()) {
        assert(!panel.hidden); expect(panel, @"FPS 60  2560x1440");
        assert(![panel.label.stringValue containsString:@"New shaders"] && ![panel.label.stringValue containsString:@"Settings"]);
    } else assert(panel.hidden);   // corners: the FPS corner stays in the picture, the shortcut box goes
    press(18); tick(); assert(panel.hidden && !fps_overlay_hints_enabled());
    mfx_set(MFX_MAX); mfx_latch(); tick();
    press(18); tick(); assert(!panel.hidden && fps_overlay_full_hud());
    expect(panel, @"Settings");
    hud_publish(0, "60 FPS (30 + 30)", 0); tick();
    if (hud_compact()) expect(panel, @"60 FPS (30 + 30)");

    if (argc == 2) {
        NSBitmapImageRep *rep = [panel bitmapImageRepForCachingDisplayInRect:panel.bounds];
        [panel cacheDisplayInRect:panel.bounds toBitmapImageRep:rep];
        assert([[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}]
                writeToFile:@(argv[1]) atomically:YES]);
    }
    [panel.timer invalidate];
    puts("pass: current modes, keyboard cycling, frame latch, both layouts and key 1 all/minimal/off");
    return 0;
}}
