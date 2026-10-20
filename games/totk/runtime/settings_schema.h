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
} SettingDef;

static const char *const SETTINGS_PAGES[] = {"Display", "Graphics", "Frame rate", "Interface", "Controls", "Audio"};
enum { SETTINGS_NPAGES = sizeof SETTINGS_PAGES / sizeof *SETTINGS_PAGES };

static const SettingChoice SET_CAP[] = {{"30 fps", "30", "30", 30}, {"40 fps", "40", "40", 40}, {"60 fps", "60", "60", 60},
                                        {"120 fps", "120", "120", 120}, {"Display refresh rate", "display", "display", 0}};
static const SettingChoice SET_INTERP[] = {{"Off", "off", "0", 0}, {"30 + 30", "interp30", "interp30", 1},
                                           {"Half the refresh rate", "half", "interp", 2}, {"Every frame", "max", "interpmax", 3}};
static const SettingChoice SET_RES[] = {{"The game's own", "game", NULL, 0}, {"720p", "720", "1280x720", 720}, {"900p", "900", "1600x900", 900},
                                        {"1080p", "1080", "1920x1080", 1080}, {"1440p", "1440", "2560x1440", 1440}};
static const SettingChoice SET_AA[] = {{"The game's FXAA", "game", "0", 0}, {"Off", "off", "1", 1}, {"FXAA", "fxaa", "2", 2}, {"SMAA", "smaa", "3", 3}};
static const SettingChoice SET_ANISO[] = {{"The game's", "game", "0", 0}, {"2x", "2", "2", 2}, {"4x", "4", "4", 4}, {"8x", "8", "8", 8}, {"16x", "16", "16", 16}};
static const SettingChoice SET_TOUCH[] = {{"Always", "always", NULL, 0}, {"Hide with a controller", "controller", NULL, 1},
                                          {"Until touched", "touch", NULL, 2}, {"Off", "off", NULL, 3}};
static const SettingChoice SET_FACE[] = {{"Automatic", "auto", NULL, 0}, {"By position", "position", "position", 1},
                                         {"By label", "label", "label", 2}};
#define SET_N(a) (int)(sizeof a / sizeof *a)

static const SettingDef SETTINGS[] = {
    {"graphics.interpolation", "Graphics", "Frame interpolation", "MetalFX makes a frame between two game frames.",
     "TOTK_MFX", SET_CHOICE, SET_LIVE, SET_MAC | SET_IPAD, 0, 0, 0, 0, 0, NULL, SET_INTERP, SET_N(SET_INTERP),
     "Not tested on iPhone yet."},
    {"display.resolution", "Display", "3D resolution", "The size the game draws its 3D scene at (version 1.4.3). Above what was set aside at start, it applies at the next start.",
     "TOTK_RENDER_RES", SET_CHOICE, SET_LIVE, SET_ALL, 0, 0, 0, 0, 0, NULL, SET_RES, SET_N(SET_RES), NULL},
    {"graphics.fov", "Graphics", "Field of view", "How wide the camera sees, against the game's own (version 1.4.3).",
     "TOTK_FOV", SET_RANGE, SET_LIVE, SET_ALL, 100, 100, 70, 140, 5, "%", NULL, 0, NULL},
    {"graphics.aa", "Graphics", "Anti-aliasing", "Smooths the 3D scene's edges before the game draws its text and icons on top (version 1.4.3).",
     "TOTK_AA", SET_CHOICE, SET_LIVE, SET_ALL, 0, 0, 0, 0, 0, NULL, SET_AA, SET_N(SET_AA), NULL},
    {"graphics.sharpen", "Graphics", "Sharpening", "AMD FidelityFX CAS on the finished picture, under the text and icons.",
     "TOTK_SHARPEN", SET_RANGE, SET_LIVE, SET_ALL, 0, 0, 0, 100, 5, "%", NULL, 0, NULL},
    {"graphics.aniso", "Graphics", "Anisotropic filtering", "Sharper ground and walls seen at an angle.",
     "TOTK_ANISO", SET_CHOICE, SET_LIVE, SET_ALL, 0, 0, 0, 0, 0, NULL, SET_ANISO, SET_N(SET_ANISO), NULL},
    {"graphics.lod_bias", "Graphics", "Texture detail bias", "Below 0 picks sharper texture levels, in hundredths of a level; too low shimmers.",
     "TOTK_LOD_BIAS", SET_RANGE, SET_LIVE, SET_ALL, 0, 0, -100, 0, 25, NULL, NULL, 0, NULL},
    {"framerate.cap", "Frame rate", "Frame rate limit", "The most frames a second the game draws. 40 needs a 120 Hz display.",
     "TOTK_UNCAPPED_HZ", SET_CHOICE, SET_LIVE, SET_ALL, 120, 0, 0, 0, 0, NULL, SET_CAP, SET_N(SET_CAP), NULL},
    {"interface.readout", "Interface", "Performance readout", "Frames per second and the game picture's size.",
     "TOTK_HINTS", SET_BOOL, SET_LIVE, SET_ALL, 1, 1, 0, 1, 1, NULL, NULL, 0, NULL},
    {"interface.shader_counter", "Interface", "Shader counter", "New shaders collected while you play.",
     "TOTK_SHADER_OVERLAY", SET_BOOL, SET_RESTART, SET_ALL, 1, 0, 0, 1, 1, NULL, NULL, 0, NULL},
    {"interface.touch_controls", "Interface", "Touch controls", "When the on-screen controller shows.",
     NULL, SET_CHOICE, SET_LIVE, SET_IOS, 1, 1, 0, 0, 0, NULL, SET_TOUCH, SET_N(SET_TOUCH), "Only on iPhone and iPad."},
    {"interface.touch_opacity", "Interface", "Touch control opacity", NULL,
     NULL, SET_RANGE, SET_LIVE, SET_IOS, 32, 32, 10, 100, 5, "%", NULL, 0, "Only on iPhone and iPad."},
    {"interface.touch_size", "Interface", "Touch control size", NULL,
     NULL, SET_RANGE, SET_LIVE, SET_IOS, 100, 100, 75, 150, 5, "%", NULL, 0, "Only on iPhone and iPad."},
    {"interface.menu_button", "Interface", "Settings button", "A small gear at the top of the screen. Three fingers also open the menu.",
     NULL, SET_BOOL, SET_LIVE, SET_IOS, 1, 1, 0, 1, 1, NULL, NULL, 0, "Only on iPhone and iPad."},
    {"controls.face_buttons", "Controls", "Face buttons", "PlayStation and Xbox buttons by position (as the game's prompts show them) or by their letters.",
     "TOTK_FACE_BUTTONS", SET_CHOICE, SET_LIVE, SET_ALL, 0, 0, 0, 0, 0, NULL, SET_FACE, SET_N(SET_FACE), NULL},
    {"controls.rumble", "Controls", "Rumble", NULL,
     "TOTK_RUMBLE", SET_BOOL, SET_LIVE, SET_MAC, 1, 1, 0, 1, 1, NULL, NULL, 0, "Not on iPhone and iPad yet."},
    {"controls.gyro", "Controls", "Motion controls", "Gyro aiming with a controller that has a gyroscope.",
     "TOTK_GYRO", SET_BOOL, SET_LIVE, SET_MAC, 1, 1, 0, 1, 1, NULL, NULL, 0, "Not on iPhone and iPad yet."},
    {"audio.volume", "Audio", "Volume", NULL,
     NULL, SET_RANGE, SET_LIVE, SET_ALL, 100, 100, 0, 100, 5, "%", NULL, 0, NULL},
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
