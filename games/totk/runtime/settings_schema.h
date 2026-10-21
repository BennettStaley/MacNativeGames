/* Player settings (the in-game settings menu): one table that every platform shares. Each row is a setting with its type,
   range or choices, default, platforms and how it applies: SET_LIVE takes effect at once through settings_apply (runtime/
   settings.c), SET_RESTART is handed to the game as its environment variable at the next start. The file is settings.txt
   (Mac: ~/Library/Application Support/MacNativeGames/TOTK/, iPhone / iPad: the app's Documents, visible in Files), one
   "key = value" line per setting in the controls.txt grammar; # starts a comment. Unknown keys and bad values are ignored
   (the default stays), so a file from a newer build still starts an older one. A variable already in the environment wins
   over the file (open --env TOTK_UNCAPPED_HZ=60 ... runs a test without touching the player's settings).
   Plain C, header only: the Mac launcher (app/launcher.m), the runtime and tests/test_settings.c include it. */
#pragma once
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SET_MAC = 1, SET_IPAD = 2, SET_IPHONE = 4, SET_IOS = SET_IPAD | SET_IPHONE, SET_ALL = SET_MAC | SET_IOS };
enum { SET_BOOL, SET_RANGE, SET_CHOICE };
enum { SET_LIVE = 1, SET_RESTART = 2 };

/* token: the word in settings.txt; env: the environment value at start (NULL: leave the variable unset). */
typedef struct { const char *label, *token, *env; int value; } SettingChoice;
typedef struct {
    const char *key, *page, *label, *help;
    const char *env;                 /* the variable the game reads at start, or NULL for a live-only setting */
    int type, apply, platforms;
    int def_mac, def_ios;            /* defaults: the behaviour without a settings file */
    int min, max, step;              /* SET_RANGE */
    const char *unit;                /* SET_RANGE: shown after the value ("%", " fps") */
    const SettingChoice *choices; int nchoices;
    const char *unavailable;         /* why the row is greyed on a platform outside `platforms` */
    const char *shown_key; int shown_value;   /* shown only while that setting has that value (NULL: always) */
} SettingDef;

static const char *const SETTINGS_PAGES[] = {"Display", "Graphics", "Frame rate", "Interface", "Controls", "Audio", "World"};
enum { SETTINGS_NPAGES = sizeof SETTINGS_PAGES / sizeof *SETTINGS_PAGES };

static const SettingChoice SET_CAP[] = {{"30 fps", "30", "30", 30}, {"40 fps", "40", "40", 40}, {"60 fps", "60", "60", 60},
                                        {"120 fps", "120", "120", 120}, {"Display refresh rate", "display", "display", 0}};
static const SettingChoice SET_INTERP[] = {{"Off", "off", "0", 0}, {"30 + 30", "interp30", "interp30", 1},
                                           {"Half the refresh rate", "half", "interp", 2}, {"Every frame", "max", "interpmax", 3}};
/* Display (settings.c display_apply): the output is the 16:9 picture the game composes, text and icons included (live up to
   the headroom); the render scale sizes the 3D scene against it; the aspect ratio is the shape it is shown at. */
enum { SET_OUT_WINDOW = 0, SET_OUT_NATIVE = 1, SET_OUT_CUSTOM = 2 };
static const SettingChoice SET_OUTPUT[] = {{"Match the window", "window", NULL, SET_OUT_WINDOW}, {"Display native", "native", NULL, SET_OUT_NATIVE},
                                           {"4K (3840 x 2160)", "2160", NULL, 2160}, {"1440p (2560 x 1440)", "1440", NULL, 1440},
                                           {"1080p (game's own)", "1080", NULL, 1080}, {"900p (1600 x 900)", "900", NULL, 900},
                                           {"720p (1280 x 720)", "720", NULL, 720}, {"Custom width", "custom", NULL, SET_OUT_CUSTOM}};
static const SettingChoice SET_SCALE[] = {{"The game's own", "game", NULL, 0}, {"25%", "25", NULL, 25}, {"33%", "33", NULL, 33},
                                          {"50%", "50", NULL, 50}, {"67%", "67", NULL, 67}, {"75%", "75", NULL, 75}, {"85%", "85", NULL, 85},
                                          {"100%", "100", NULL, 100}, {"125%", "125", NULL, 125}, {"150%", "150", NULL, 150},
                                          {"175%", "175", NULL, 175}, {"200%", "200", NULL, 200}};
static const SettingChoice SET_ASPECT[] = {{"Fill the screen", "auto", NULL, 0}, {"16:9 (game's own)", "16:9", NULL, 1},
                                           {"16:10", "16:10", NULL, 2}, {"21:9", "21:9", NULL, 3}, {"32:9", "32:9", NULL, 4}};
static const SettingChoice SET_HEADROOM[] = {{"Auto (4K on 16 GB)", "auto", NULL, 0}, {"1440p", "1440", "2560x1440", 1440},
                                             {"4K", "2160", "3840x2160", 2160}};
static const SettingChoice SET_AA[] = {{"The game's own", "game", "0", 0}, {"Off", "off", "1", 1}, {"FXAA", "fxaa", "2", 2}, {"SMAA", "smaa", "3", 3}};
static const SettingChoice SET_SHADOW[] = {{"The game's own", "game", NULL, 0}, {"2x", "2", "2", 2}, {"4x", "4", "4", 4}};
static const SettingChoice SET_ANISO[] = {{"The game's", "game", "0", 0}, {"2x", "2", "2", 2}, {"4x", "4", "4", 4}, {"8x", "8", "8", 8}, {"16x", "16", "16", 16}};
static const SettingChoice SET_TOUCH[] = {{"Always", "always", NULL, 0}, {"Hide with a controller", "controller", NULL, 1},
                                          {"Until touched", "touch", NULL, 2}, {"Off", "off", NULL, 3}};
static const SettingChoice SET_PROMPTS[] = {{"Automatic (your controller)", "auto", NULL, 0}, {"Switch (the game's own)", "switch", NULL, 1},
                                            {"Xbox", "xbox", NULL, 2}, {"PlayStation", "playstation", NULL, 3}};
static const SettingChoice SET_DETAIL[] = {{"Simple", "simple", NULL, 0}, {"Detailed (average, 1% low)", "detailed", NULL, 1}};
static const SettingChoice SET_FACE[] = {{"Automatic", "auto", NULL, 0}, {"By position", "position", "position", 1},
                                         {"By label", "label", "label", 2}};
/* World (runtime/world_patch.h): the time of day's speed, in percent of the game's own (0 = the clock stands still). */
static const SettingChoice SET_TIME_SPEED[] = {{"Paused", "paused", "0", 0}, {"0.25x", "0.25", "25", 25}, {"0.5x", "0.5", "50", 50},
                                               {"Normal (1x)", "1", "100", 100}, {"2x", "2", "200", 200}, {"4x", "4", "400", 400},
                                               {"8x", "8", "800", 800}};
#define SET_N(a) (int)(sizeof a / sizeof *a)

static const SettingDef SETTINGS[] = {
    {"graphics.interpolation", "Graphics", "Frame interpolation", "MetalFX makes a frame between two game frames.",
     "TOTK_MFX", SET_CHOICE, SET_LIVE, SET_MAC | SET_IPAD, 0, 0, 0, 0, 0, NULL, SET_INTERP, SET_N(SET_INTERP),
     "Not tested on iPhone yet.", NULL, 0},
    {"display.output", "Display", "Output resolution", "The picture the game composes, text and icons included. Display native is your screen's pixels.",
     NULL, SET_CHOICE, SET_LIVE, SET_MAC, 1080, 1080, 0, 0, 0, NULL, SET_OUTPUT, SET_N(SET_OUTPUT), "iPhone and iPad show 720p.", NULL, 0},
    {"display.custom_width", "Display", "Custom width", "The output's width in pixels; the height follows at 16:9.",
     NULL, SET_RANGE, SET_LIVE, SET_MAC, 2560, 2560, 640, 3840, 64, " px", NULL, 0, "iPhone and iPad show 720p.", "display.output", SET_OUT_CUSTOM},
    {"display.scale", "Display", "3D render scale", "The 3D scene against the output: 50% is half the width and height (four times fewer pixels), above 100% supersamples.",
     NULL, SET_CHOICE, SET_LIVE, SET_ALL, 0, 0, 0, 0, 0, NULL, SET_SCALE, SET_N(SET_SCALE), NULL, NULL, 0},
    {"display.aspect", "Display", "Aspect ratio", "Fill the screen shows more of the world on a wider or taller screen; text and icons stretch to it.",
     NULL, SET_CHOICE, SET_LIVE, SET_MAC, 1, 1, 0, 0, 0, NULL, SET_ASPECT, SET_N(SET_ASPECT), "Not on iPhone and iPad yet.", NULL, 0},
    {"display.headroom", "Display", "Maximum output", "The biggest output resolution you can switch to while playing. Higher sets more memory aside; takes effect after a restart.",
     "TOTK_OUTPUT_RES", SET_CHOICE, SET_RESTART, SET_MAC, 0, 0, 0, 0, 0, NULL, SET_HEADROOM, SET_N(SET_HEADROOM), "Fixed on iPhone and iPad.", NULL, 0},
    {"graphics.fov", "Graphics", "Field of view", "How wide the camera sees, against the game's own (version 1.4.3).",
     "TOTK_FOV", SET_RANGE, SET_LIVE, SET_ALL, 100, 100, 70, 140, 5, "%", NULL, 0, NULL, NULL, 0},
    {"graphics.fog", "Graphics", "Cave fog", "The fog in caves and other enclosed places, against the game's own: 50% reaches it at twice the distance, 0% has none. The outdoor haze is separate and not changed yet (version 1.4.3).",
     "TOTK_FOG", SET_RANGE, SET_LIVE, SET_ALL, 100, 100, 0, 100, 10, "%", NULL, 0, NULL, NULL, 0},
    {"graphics.shadow_res", "Graphics", "Shadow resolution", "The sun's shadow map: 2x and 4x give sharper shadow edges and use more memory (version 1.4.3).",
     "TOTK_SHADOW_RES", SET_CHOICE, SET_LIVE, SET_MAC, 0, 0, 0, 0, 0, NULL, SET_SHADOW, SET_N(SET_SHADOW), "Not on iPhone and iPad yet.", NULL, 0},
    {"graphics.aa", "Graphics", "Anti-aliasing", "Smooths the 3D scene's edges before the game draws its text and icons on top (version 1.4.3).",
     "TOTK_AA", SET_CHOICE, SET_LIVE, SET_ALL, 0, 0, 0, 0, 0, NULL, SET_AA, SET_N(SET_AA), NULL, NULL, 0},
    {"graphics.sharpen", "Graphics", "Sharpening", "AMD FidelityFX CAS on the finished picture, under the text and icons.",
     "TOTK_SHARPEN", SET_RANGE, SET_LIVE, SET_ALL, 0, 0, 0, 100, 5, "%", NULL, 0, NULL, NULL, 0},
    {"graphics.aniso", "Graphics", "Anisotropic filtering", "Sharper ground and walls seen at an angle.",
     "TOTK_ANISO", SET_CHOICE, SET_LIVE, SET_ALL, 0, 0, 0, 0, 0, NULL, SET_ANISO, SET_N(SET_ANISO), NULL, NULL, 0},
    {"graphics.dof", "Graphics", "Depth of field", "The game's blur of the distance. Off keeps far away sharp (version 1.4.3).",
     NULL, SET_BOOL, SET_LIVE, SET_ALL, 1, 1, 0, 1, 1, NULL, NULL, 0, NULL, NULL, 0},
    {"graphics.bloom", "Graphics", "Bloom", "The glow around bright light, against the game's own (version 1.4.3).",
     NULL, SET_RANGE, SET_LIVE, SET_ALL, 100, 100, 0, 100, 10, "%", NULL, 0, NULL, NULL, 0},
    {"graphics.lens_flare", "Graphics", "Lens flare", "The game's rings and glints across the picture when the sun is in view (version 1.4.3).",
     NULL, SET_BOOL, SET_LIVE, SET_ALL, 1, 1, 0, 1, 1, NULL, NULL, 0, NULL, NULL, 0},
    {"graphics.render_distance", "Graphics", "Render distance", "How far the camera draws before distant land is cut off, against the game's own 25 km (version 1.4.3). It does not change the detail of distant land.",
     "TOTK_RENDER_DISTANCE", SET_RANGE, SET_LIVE, SET_ALL, 100, 100, 50, 300, 25, "%", NULL, 0, NULL, NULL, 0},
    {"graphics.lod_bias", "Graphics", "Texture detail bias", "Below 0 picks sharper texture levels, in hundredths of a level; too low shimmers.",
     "TOTK_LOD_BIAS", SET_RANGE, SET_LIVE, SET_ALL, 0, 0, -100, 0, 25, NULL, NULL, 0, NULL, NULL, 0},
    {"framerate.cap", "Frame rate", "Frame rate limit", "The most frames a second the game draws. 40 needs a 120 Hz display.",
     "TOTK_UNCAPPED_HZ", SET_CHOICE, SET_LIVE, SET_ALL, 120, 0, 0, 0, 0, NULL, SET_CAP, SET_N(SET_CAP), NULL, NULL, 0},
    {"interface.readout", "Interface", "Performance readout", "Frames per second and the game picture's size.",
     "TOTK_HINTS", SET_BOOL, SET_LIVE, SET_ALL, 1, 1, 0, 1, 1, NULL, NULL, 0, NULL, NULL, 0},
    {"interface.readout_detail", "Interface", "Readout detail", "Detailed adds the average and the 1% low frame rate over the last 5 seconds.",
     NULL, SET_CHOICE, SET_LIVE, SET_ALL, 0, 0, 0, 0, 0, NULL, SET_DETAIL, SET_N(SET_DETAIL), NULL, "interface.readout", 1},
    {"interface.hide_hud", "Interface", "Hide the HUD", "Hides the game's hearts, map, prompts and menus for clean screenshots (F2 on a Mac keyboard). Turn it off to use menus.",
     NULL, SET_BOOL, SET_LIVE, SET_ALL, 0, 0, 0, 1, 1, NULL, NULL, 0, NULL, NULL, 0},
    {"interface.shader_counter", "Interface", "Shader counter", "New shaders collected while you play.",
     "TOTK_SHADER_OVERLAY", SET_BOOL, SET_LIVE, SET_ALL, 1, 0, 0, 1, 1, NULL, NULL, 0, NULL, NULL, 0},
    /* The shared shader list (third_party/d2r_gfx/gfx/nvn/totk_shader_list.m): once a day the list of variants players met
       is downloaded and the new ones are prepared from the player's own game files in the background. Download only. */
    {"interface.download_shaders", "Interface", "Download new shaders", "Once a day, gets the shared list of shaders other players met and prepares the new ones from your game files in the background, so you do not stutter there. Download only: nothing is uploaded.",
     NULL, SET_BOOL, SET_LIVE, SET_MAC, 1, 0, 0, 1, 1, NULL, NULL, 0, "iPhone and iPad cannot prepare shaders while playing; their app carries the list from its build.", NULL, 0},
    {"interface.touch_controls", "Interface", "Touch controls", "When the on-screen controller shows.",
     NULL, SET_CHOICE, SET_LIVE, SET_IOS, 1, 1, 0, 0, 0, NULL, SET_TOUCH, SET_N(SET_TOUCH), "Only on iPhone and iPad.", NULL, 0},
    {"interface.touch_opacity", "Interface", "Touch control opacity", NULL,
     NULL, SET_RANGE, SET_LIVE, SET_IOS, 32, 32, 10, 100, 5, "%", NULL, 0, "Only on iPhone and iPad.", NULL, 0},
    {"interface.touch_size", "Interface", "Touch control size", NULL,
     NULL, SET_RANGE, SET_LIVE, SET_IOS, 100, 100, 75, 150, 5, "%", NULL, 0, "Only on iPhone and iPad.", NULL, 0},
    {"interface.menu_button", "Interface", "Settings button", "A small gear at the top of the screen. Three fingers also open the menu.",
     NULL, SET_BOOL, SET_LIVE, SET_IOS, 1, 1, 0, 1, 1, NULL, NULL, 0, "Only on iPhone and iPad.", NULL, 0},
    {"controls.prompts", "Controls", "Button prompts", "The button icons the game shows. Xbox and PlayStation use a button-icon mod from the Mods tab (e.g. \"Xbox UI for v1.4.3\"). Automatic follows the last controller you connected.",
     NULL, SET_CHOICE, SET_RESTART, SET_ALL, 0, 0, 0, 0, 0, NULL, SET_PROMPTS, SET_N(SET_PROMPTS), NULL, NULL, 0},
    {"controls.face_buttons", "Controls", "Face buttons", "PlayStation and Xbox buttons by position (as the game's prompts show them) or by their letters.",
     "TOTK_FACE_BUTTONS", SET_CHOICE, SET_LIVE, SET_ALL, 0, 0, 0, 0, 0, NULL, SET_FACE, SET_N(SET_FACE), NULL, NULL, 0},
    {"controls.camera_speed_x", "Controls", "Camera speed, horizontal", "The right stick's left and right against the game's own. Above 100% a partial push turns faster, up to the game's top speed; the arrow keys are already at it and only slow down below 100%.",
     NULL, SET_RANGE, SET_LIVE, SET_ALL, 100, 100, 25, 300, 5, "%", NULL, 0, NULL, NULL, 0},
    {"controls.camera_speed_y", "Controls", "Camera speed, vertical", "The right stick's up and down against the game's own. Above 100% a partial push turns faster, up to the game's top speed.",
     NULL, SET_RANGE, SET_LIVE, SET_ALL, 100, 100, 25, 300, 5, "%", NULL, 0, NULL, NULL, 0},
    {"controls.rumble", "Controls", "Rumble", NULL,
     "TOTK_RUMBLE", SET_BOOL, SET_LIVE, SET_MAC, 1, 1, 0, 1, 1, NULL, NULL, 0, "Not on iPhone and iPad yet.", NULL, 0},
    {"controls.gyro", "Controls", "Motion controls", "Gyro aiming with a controller that has a gyroscope.",
     "TOTK_GYRO", SET_BOOL, SET_LIVE, SET_MAC, 1, 1, 0, 1, 1, NULL, NULL, 0, "Not on iPhone and iPad yet.", NULL, 0},
    {"audio.volume", "Audio", "Volume", NULL,
     NULL, SET_RANGE, SET_LIVE, SET_ALL, 100, 100, 0, 100, 5, "%", NULL, 0, NULL, NULL, 0},
    {"world.time_speed", "World", "Time of day speed", "How fast the day passes: the clock, the sun and the sky (version 1.4.3). Paused stops the time of day.",
     "TOTK_TIME_SPEED", SET_CHOICE, SET_LIVE, SET_ALL, 100, 100, 0, 0, 0, NULL, SET_TIME_SPEED, SET_N(SET_TIME_SPEED), NULL, NULL, 0},
};
enum { SETTINGS_COUNT = sizeof SETTINGS / sizeof *SETTINGS };

static inline int settings_index(const char *key) {
    for (int i = 0; i < SETTINGS_COUNT; i++) if (!strcmp(SETTINGS[i].key, key)) return i;
    return -1;
}
static inline int settings_default(const SettingDef *d, int ios) { return ios ? d->def_ios : d->def_mac; }
static inline const SettingChoice *settings_choice(const SettingDef *d, int value) {
    for (int i = 0; i < d->nchoices; i++) if (d->choices[i].value == value) return &d->choices[i];
    return NULL;
}
static inline int settings_valid(const SettingDef *d, int v) {
    if (d->type == SET_BOOL) return v == 0 || v == 1;
    if (d->type == SET_CHOICE) return settings_choice(d, v) != NULL;
    return v >= d->min && v <= d->max;
}
/* A value from its settings.txt word: 0 on success. Ranges are clamped and rounded to their step. */
static inline int settings_parse_value(const SettingDef *d, const char *word, int *out) {
    if (d->type == SET_BOOL) {
        if (!strcmp(word, "on") || !strcmp(word, "1") || !strcmp(word, "true")) { *out = 1; return 0; }
        if (!strcmp(word, "off") || !strcmp(word, "0") || !strcmp(word, "false")) { *out = 0; return 0; }
        return -1;
    }
    if (d->type == SET_CHOICE) {
        for (int i = 0; i < d->nchoices; i++) if (!strcmp(word, d->choices[i].token)) { *out = d->choices[i].value; return 0; }
        return -1;
    }
    char *end; long v = strtol(word, &end, 10);
    if (end == word || *end) return -1;
    if (v < d->min) v = d->min;
    if (v > d->max) v = d->max;
    *out = d->min + (int)((v - d->min) / d->step) * d->step;
    return 0;
}
/* The value from the environment variable's own words (the game's meaning): 0 on success. */
static inline int settings_parse_env(const SettingDef *d, const char *text, int *out) {
    if (d->type == SET_CHOICE) {
        for (int i = 0; i < d->nchoices; i++) if (d->choices[i].env && !strcmp(text, d->choices[i].env)) { *out = d->choices[i].value; return 0; }
        return -1;
    }
    return settings_parse_value(d, text, out);
}
static inline const char *settings_token(const SettingDef *d, int v, char *buf, size_t n) {
    if (d->type == SET_BOOL) return v ? "on" : "off";
    if (d->type == SET_CHOICE) { const SettingChoice *c = settings_choice(d, v); return c ? c->token : "?"; }
    snprintf(buf, n, "%d", v); return buf;
}
/* What the game reads at start for this value, or NULL to leave the variable unset. */
static inline const char *settings_env_text(const SettingDef *d, int v, char *buf, size_t n) {
    if (d->type == SET_BOOL) return v ? "1" : "0";
    if (d->type == SET_CHOICE) { const SettingChoice *c = settings_choice(d, v); return c ? c->env : NULL; }
    snprintf(buf, n, "%d", v); return buf;
}

static inline void settings_defaults(int *values, int ios) {
    for (int i = 0; i < SETTINGS_COUNT; i++) values[i] = settings_default(&SETTINGS[i], ios);
}
/* Reads settings.txt text over values (start from settings_defaults). Returns the number of settings it set. */
static inline int settings_parse_text(const char *text, int *values) {
    int count = 0;
    while (text && *text) {
        const char *eol = strchr(text, '\n'); size_t len = eol ? (size_t)(eol - text) : strlen(text);
        char line[256]; if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, text, len); line[len] = 0; text = eol ? eol + 1 : text + len;
        char *hash = strchr(line, '#'); if (hash) *hash = 0;
        char *eq = strchr(line, '='); if (!eq) continue;
        *eq = 0; char *key = line, *word = eq + 1;
        while (isspace((unsigned char)*key)) key++;
        for (char *e = key + strlen(key); e > key && isspace((unsigned char)e[-1]); ) *--e = 0;
        while (isspace((unsigned char)*word)) word++;
        for (char *e = word + strlen(word); e > word && isspace((unsigned char)e[-1]); ) *--e = 0;
        int i = settings_index(key), v;
        if (i >= 0 && !settings_parse_value(&SETTINGS[i], word, &v)) { values[i] = v; count++; }
    }
    return count;
}
/* settings.txt for values: a header, then every setting, grouped by page. Returns the length (snprintf rules). */
static inline int settings_format_text(const int *values, char *out, size_t n) {
    size_t used = 0; char buf[16];
#define SET_PUT(...) do { int k = snprintf(out + (used < n ? used : n), used < n ? n - used : 0, __VA_ARGS__); if (k > 0) used += (size_t)k; } while (0)
    SET_PUT("# Tears of the Kingdom settings. The in-game settings menu writes this file; edits here apply at the next start.\n");
    for (int p = 0; p < SETTINGS_NPAGES; p++) {
        SET_PUT("\n# %s\n", SETTINGS_PAGES[p]);
        for (int i = 0; i < SETTINGS_COUNT; i++)
            if (!strcmp(SETTINGS[i].page, SETTINGS_PAGES[p])) SET_PUT("%s = %s\n", SETTINGS[i].key, settings_token(&SETTINGS[i], values[i], buf, sizeof buf));
    }
#undef SET_PUT
    return (int)used;
}
/* Values the environment already sets win over the file: the menu shows what the game is running with. */
static inline void settings_take_env(int *values) {
    for (int i = 0; i < SETTINGS_COUNT; i++) {
        const char *e = SETTINGS[i].env ? getenv(SETTINGS[i].env) : NULL; int v;
        if (e && *e && !settings_parse_env(&SETTINGS[i], e, &v)) values[i] = v;
    }
}
/* Hands every setting the game reads at start to it as its environment variable, unless the variable is already set. */
static inline void settings_export_env(const int *values) {
    char buf[16];
    for (int i = 0; i < SETTINGS_COUNT; i++) {
        const SettingDef *d = &SETTINGS[i];
        if (!d->env || getenv(d->env)) continue;
        const char *text = settings_env_text(d, values[i], buf, sizeof buf);
        if (text) setenv(d->env, text, 1);
    }
}
