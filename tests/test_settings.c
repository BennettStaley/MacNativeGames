/* Test the settings table and settings.txt (games/totk/runtime/settings_schema.h) with synthetic files. */
#include "../games/totk/runtime/settings_schema.h"
#include <stdio.h>

static int failures;
static void expect(int ok, const char *what) { if (!ok) { fprintf(stderr, "FAIL: %s\n", what); failures++; } }
static int value(const int *values, const char *key) { return values[settings_index(key)]; }

int main(void) {
    int v[SETTINGS_COUNT], again[SETTINGS_COUNT];
    for (int i = 0; i < SETTINGS_COUNT; i++) {
        const SettingDef *d = &SETTINGS[i];
        expect(settings_valid(d, d->def_mac) && settings_valid(d, d->def_ios), "every default is a valid value");
        int on_page = 0;
        for (int p = 0; p < SETTINGS_NPAGES; p++) on_page |= !strcmp(d->page, SETTINGS_PAGES[p]);
        expect(on_page, "every setting is on a known page");
        for (int j = 0; j < i; j++) expect(strcmp(d->key, SETTINGS[j].key) != 0, "keys are unique");
        expect(d->platforms != 0 && (d->platforms == SET_ALL || d->unavailable), "a setting missing on a platform says why");
        if (d->type == SET_RANGE) expect(d->step > 0 && (d->max - d->min) % d->step == 0, "a range is whole steps");
    }

    settings_defaults(v, 1);
    expect(value(v, "framerate.cap") == 0 && value(v, "interface.shader_counter") == 0, "iOS defaults: display rate, no shader counter");
    settings_defaults(v, 0);
    expect(value(v, "framerate.cap") == 120 && value(v, "audio.volume") == 100, "Mac defaults");

    int n = settings_parse_text("# comment\n framerate.cap = 60 \naudio.volume=47\ninterface.readout = off # trailing\n"
                                "unknown.key = 3\ngraphics.interpolation = bogus\ninterface.touch_opacity = 500\n", v);
    expect(n == 4, "four known keys with good values");
    expect(value(v, "framerate.cap") == 60, "a choice by its word");
    expect(value(v, "audio.volume") == 45, "a range is rounded down to its step");
    expect(value(v, "interface.readout") == 0, "off, with a trailing comment");
    expect(value(v, "graphics.interpolation") == 0, "a bad word keeps the default");
    expect(value(v, "interface.touch_opacity") == 100, "a range is clamped");

    settings_defaults(v, 0);
    expect(value(v, "controls.camera_speed_x") == 100 && value(v, "controls.camera_speed_y") == 100 && value(v, "graphics.bloom") == 100 &&
           value(v, "graphics.dof") == 1 && value(v, "interface.hide_hud") == 0 &&
           value(v, "interface.readout_detail") == 0, "the effects and the camera start as the game's own");
    expect(settings_parse_text("controls.camera_speed_x = 999\ncontrols.camera_speed_y = 12\ngraphics.bloom = 55\ninterface.readout_detail = detailed\n", v) == 4,
           "camera speed, bloom and readout detail read");
    expect(value(v, "controls.camera_speed_x") == 300 && value(v, "controls.camera_speed_y") == 25 && value(v, "graphics.bloom") == 50 &&
           value(v, "interface.readout_detail") == 1, "camera speed clamped to 25..300, bloom in steps of 10");
    {
        const SettingDef *d = &SETTINGS[settings_index("interface.readout_detail")];
        expect(d->shown_key && !strcmp(d->shown_key, "interface.readout") && d->shown_value == 1, "readout detail shows only with the readout on");
    }
    settings_parse_text("framerate.cap = 60\naudio.volume=47\ninterface.readout = off\ninterface.touch_opacity = 500\n", v);

    char text[4096]; int len = settings_format_text(v, text, sizeof text);
    expect(len > 0 && len < (int)sizeof text, "the file fits");
    settings_defaults(again, 1);   /* other defaults underneath: everything must come from the text */
    expect(settings_parse_text(text, again) == SETTINGS_COUNT, "the written file sets every setting");
    expect(!memcmp(v, again, sizeof v), "write then read gives the same values");
    expect(settings_format_text(v, text, 8) > 8, "a short buffer reports the full length");

    setenv("TOTK_UNCAPPED_HZ", "display", 1); setenv("TOTK_FACE_BUTTONS", "label", 1); setenv("TOTK_MFX", "interp", 1);
    settings_take_env(v);
    expect(value(v, "framerate.cap") == 0 && value(v, "controls.face_buttons") == 2 && value(v, "graphics.interpolation") == 2,
           "the environment wins, in the game's own words");
    unsetenv("TOTK_UNCAPPED_HZ"); unsetenv("TOTK_FACE_BUTTONS"); unsetenv("TOTK_MFX"); unsetenv("TOTK_HINTS");
    setenv("TOTK_SHADER_OVERLAY", "1", 1);
    v[settings_index("interface.readout")] = 0; v[settings_index("framerate.cap")] = 40; v[settings_index("interface.shader_counter")] = 0;
    settings_export_env(v);
    expect(getenv("TOTK_HINTS") && !strcmp(getenv("TOTK_HINTS"), "0"), "a bool goes out as 0 / 1");
    expect(getenv("TOTK_UNCAPPED_HZ") && !strcmp(getenv("TOTK_UNCAPPED_HZ"), "40"), "a choice goes out in the game's words");
    expect(!strcmp(getenv("TOTK_SHADER_OVERLAY"), "1"), "a variable already set is kept");
    v[settings_index("controls.face_buttons")] = 0; unsetenv("TOTK_FACE_BUTTONS"); settings_export_env(v);
    expect(!getenv("TOTK_FACE_BUTTONS"), "automatic leaves the variable unset");

    /* Fog (live, every platform) and shadow resolution (restart, Mac): defaults, the file's words, the game's variables. */
    settings_defaults(v, 0);
    expect(value(v, "graphics.fog") == 100 && value(v, "graphics.shadow_res") == 0, "fog and shadows default to the game's own");
    settings_defaults(v, 1);
    expect(value(v, "graphics.fog") == 100 && value(v, "graphics.shadow_res") == 0, "iOS defaults for fog and shadows");
    expect(settings_parse_text("graphics.fog = 55\ngraphics.shadow_res = 4\n", v) == 2 && value(v, "graphics.fog") == 50 &&
           value(v, "graphics.shadow_res") == 4, "fog rounds to its step, shadows by their word");
    expect(settings_parse_text("graphics.shadow_res = 3\n", v) == 0 && value(v, "graphics.shadow_res") == 4, "no 3x shadows");
    expect(SETTINGS[settings_index("graphics.fog")].apply == SET_LIVE && SETTINGS[settings_index("graphics.shadow_res")].apply == SET_RESTART &&
           SETTINGS[settings_index("graphics.shadow_res")].platforms == SET_MAC, "fog is live everywhere, shadows restart on the Mac");
    unsetenv("TOTK_FOG"); unsetenv("TOTK_SHADOW_RES"); settings_export_env(v);
    expect(getenv("TOTK_FOG") && !strcmp(getenv("TOTK_FOG"), "50") && getenv("TOTK_SHADOW_RES") && !strcmp(getenv("TOTK_SHADOW_RES"), "4"),
           "fog and shadows go out as TOTK_FOG / TOTK_SHADOW_RES");
    unsetenv("TOTK_SHADOW_RES"); v[settings_index("graphics.shadow_res")] = 0; settings_export_env(v);
    expect(!getenv("TOTK_SHADOW_RES"), "the game's own shadows leave the variable unset");
    setenv("TOTK_SHADOW_RES", "2", 1); settings_take_env(v);
    expect(value(v, "graphics.shadow_res") == 2, "TOTK_SHADOW_RES=2 shows as 2x");
    unsetenv("TOTK_FOG"); unsetenv("TOTK_SHADOW_RES");

    if (failures) { fprintf(stderr, "%d failures\n", failures); return 1; }
    printf("settings: all checks passed (%d settings)\n", SETTINGS_COUNT);
    return 0;
}
