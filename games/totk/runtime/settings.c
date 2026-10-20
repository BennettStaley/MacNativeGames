/* The player's settings at run time (runtime/settings_schema.h has the table and the file format). settings_init loads
   settings.txt once, before the game thread starts on iOS (ios/Sources/main.m) and from the loader on the Mac, where the
   launcher already handed the start-time settings to the game as environment variables. settings_set is the menu's one
   entry: it checks the value, stores it, applies a live setting at once and writes the file. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <TargetConditionals.h>
#include "settings.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int values[SETTINGS_COUNT], loaded, changed_since_start[SETTINGS_COUNT];
static char path[1024];
static void (*observer)(const char *key, int value);

int settings_ios(void) { return TARGET_OS_IPHONE ? 1 : 0; }

/* Live settings go to the code that owns them; the touch controls and the menu button are the iOS app's own views, which
   follow through settings_observe. */
static void apply_live(const SettingDef *d, int v) {
    extern void dm_gfx_set_flag(const char *name, int value);
    extern void nvn_fps_cap_set(int hz);
    extern void fps_overlay_set_hints(int visible);
    extern void audio_master_volume_set(int percent);
    extern int runtime_hid_feedback_flag(const char *name, int v);
    extern void runtime_gamepad_relayout(void);
    if (!strcmp(d->key, "graphics.interpolation")) dm_gfx_set_flag("mfx_interp", v);
    else if (!strcmp(d->key, "framerate.cap")) nvn_fps_cap_set(v);
    else if (!strcmp(d->key, "interface.readout")) fps_overlay_set_hints(v);
    else if (!strcmp(d->key, "audio.volume")) audio_master_volume_set(v);
    else if (!strcmp(d->key, "controls.rumble")) runtime_hid_feedback_flag("rumble", v);
    else if (!strcmp(d->key, "controls.gyro")) runtime_hid_feedback_flag("gyro", v);
    else if (!strcmp(d->key, "controls.face_buttons")) {
        const SettingChoice *c = settings_choice(d, v);
        if (c && c->env) setenv("TOTK_FACE_BUTTONS", c->env, 1); else unsetenv("TOTK_FACE_BUTTONS");
        runtime_gamepad_relayout();
    }
}

void settings_init(const char *file) {
    pthread_mutex_lock(&lock);
    if (loaded) { pthread_mutex_unlock(&lock); return; }
    loaded = 1;
    settings_defaults(values, settings_ios());
    if (file && *file) {
        snprintf(path, sizeof path, "%s", file);
        FILE *f = fopen(path, "r");
        if (f) {
            char text[16384]; size_t n = fread(text, 1, sizeof text - 1, f); text[n] = 0; fclose(f);
            fprintf(stderr, "[settings] %d settings from %s\n", settings_parse_text(text, values), path);
        }
    }
    settings_take_env(values);
    settings_export_env(values);   /* iOS: the app is its own launcher; on the Mac these are already set */
    int start[SETTINGS_COUNT]; memcpy(start, values, sizeof start);
    pthread_mutex_unlock(&lock);
    /* Live-only settings (no variable for the game to read at start) are applied now. */
    for (int i = 0; i < SETTINGS_COUNT; i++)
        if (!SETTINGS[i].env && SETTINGS[i].apply == SET_LIVE && start[i] != settings_default(&SETTINGS[i], settings_ios())) apply_live(&SETTINGS[i], start[i]);
}

int settings_get(const char *key) {
    int i = settings_index(key); if (i < 0) return -1;
    pthread_mutex_lock(&lock); int v = values[i]; pthread_mutex_unlock(&lock);
    return v;
}

int settings_pending_restart(const char *key) {
    int i = settings_index(key); if (i < 0) return 0;
    pthread_mutex_lock(&lock); int r = changed_since_start[i] && SETTINGS[i].apply == SET_RESTART; pthread_mutex_unlock(&lock);
    return r;
}

int settings_save(void) {
    pthread_mutex_lock(&lock);
    if (!*path) { pthread_mutex_unlock(&lock); return -1; }
    char text[8192]; int n = settings_format_text(values, text, sizeof text);
    char tmp[1100]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w"); int ok = f && n > 0 && n < (int)sizeof text && fwrite(text, 1, (size_t)n, f) == (size_t)n;
    if (f) ok = !fclose(f) && ok;
    ok = ok && !rename(tmp, path);   /* never a half-written settings.txt */
    pthread_mutex_unlock(&lock);
    return ok ? 0 : -1;
}

int settings_set(const char *key, int value) {
    int i = settings_index(key); if (i < 0) return SETTINGS_INVALID;
    const SettingDef *d = &SETTINGS[i];
    if (!settings_valid(d, value)) return SETTINGS_INVALID;
    pthread_mutex_lock(&lock);
    int was = values[i]; values[i] = value; if (was != value) changed_since_start[i] = 1;
    pthread_mutex_unlock(&lock);
    if (was == value) return d->apply == SET_LIVE ? SETTINGS_APPLIED : SETTINGS_RESTART;
    fprintf(stderr, "[settings] %s = %d (was %d)\n", key, value, was);
    if (d->apply == SET_LIVE) apply_live(d, value);
    if (settings_save()) fprintf(stderr, "[settings] could not write %s\n", *path ? path : "(no settings file)");
    if (observer) observer(key, value);
    return d->apply == SET_LIVE ? SETTINGS_APPLIED : SETTINGS_RESTART;
}

void settings_observe(void (*fn)(const char *key, int value)) { observer = fn; }
