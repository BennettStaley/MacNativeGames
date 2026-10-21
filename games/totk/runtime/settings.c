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
#include <math.h>
#include <stdint.h>
#include "settings.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int values[SETTINGS_COUNT], loaded, changed_since_start[SETTINGS_COUNT];
static char path[1024];
static void (*observer)(const char *key, int value);

int settings_ios(void) { return TARGET_OS_IPHONE ? 1 : 0; }

/* Display (settings_schema.h): the output (the game's 16:9 canvas, Mac: live up to the swapchain the headroom set at start),
   the 3D scene as a share of it (up to the scene reserve), and the shape the picture is shown at. The frontend reports the
   window's drawable and the screen's native pixels (settings_display_size), which "Match the window", "Display native"
   and "Fill the screen" follow. Returns -1 when the game cannot take a size now. */
static unsigned disp_w, disp_h, native_w;
static char display_status[160];
static int display_apply(const char *changed) {
    extern int runtime_output_request(unsigned w, unsigned h);
    extern void runtime_output_max(unsigned *w, unsigned *h);
    extern int runtime_render_request(unsigned w, unsigned h);
    extern void runtime_render_reserve(unsigned *w, unsigned *h);
    extern void runtime_aspect_set(float ratio);
    pthread_mutex_lock(&lock);
    int out = values[settings_index("display.output")], custom = values[settings_index("display.custom_width")];
    int scale = values[settings_index("display.scale")], aspect = values[settings_index("display.aspect")];
    unsigned dw = disp_w, dh = disp_h, nw = native_w;
    pthread_mutex_unlock(&lock);
    int r = 0; unsigned cw = 1280, ch = 720;
    if (!settings_ios()) {
        unsigned mw, mh; runtime_output_max(&mw, &mh);
        unsigned want = out == SET_OUT_WINDOW ? dw : out == SET_OUT_NATIVE ? nw : out == SET_OUT_CUSTOM ? (unsigned)custom : (unsigned)out * 16 / 9;
        if (!want) want = 1920;   /* the window is not up yet */
        if (want > mw) want = mw;
        want &= ~15u; if (want < 640) want = 640;
        cw = want; ch = want * 9 / 16;
        if (runtime_output_request(cw, ch)) r = -1;
    }
    unsigned sw = 0, sh = 0, rw, rh; runtime_render_reserve(&rw, &rh);
    /* The game's own scene grows with the output (1600 x 900 for each 1920 of width); above the reserve it is asked for
       explicitly, at the most the reserve holds. */
    int own_fits = !rw || cw <= 1920 || (uint64_t)(1600 * cw / 1920) * (900 * cw / 1920) <= (uint64_t)rw * rh;
    if (scale || !own_fits) {
        unsigned pct = scale ? (unsigned)scale : 83;
        sw = cw * pct / 100; sh = ch * pct / 100;
        if ((uint64_t)sw * sh > (uint64_t)rw * rh) { double f = sqrt((double)rw * rh / ((double)sw * sh)); sw = (unsigned)(sw * f); sh = (unsigned)(sh * f); }
        sw &= ~7u; sh &= ~7u; if (sw < 64) sw = 64; if (sh < 64) sh = 64;
    }
    if (runtime_render_request(sw, sh)) r = -1;
    if (!settings_ios() && (!changed || !strcmp(changed, "display.aspect") || aspect == 0)) {
        static const float ratios[] = {0, 16.0f / 9, 16.0f / 10, 21.0f / 9, 32.0f / 9};
        float a = aspect == 0 ? (dw && dh ? (float)dw / dh : 0) : ratios[aspect < 5 ? aspect : 1];
        if (a && fabsf(a / (16.0f / 9) - 1) < 0.01f) a = 0;   /* 16:9 is the game's own: no stretch */
        if (changed || a) runtime_aspect_set(a);
        /* Vert+: on a screen taller than 16:9 the game keeps its horizontal view and sees more above and below (the
           vertical angle widened by the shapes' ratio, tan-corrected for the game's usual ~50 degrees), instead of
           losing the sides. */
        extern void runtime_fov_aspect(float k);
        float k = 1;
        if (a && a < 16.0f / 9) { const float half = 0.436f; k = atanf(tanf(half) * (16.0f / 9) / a) / half; }
        if (changed || k != 1) runtime_fov_aspect(k);
    }
    pthread_mutex_lock(&lock);
    if (sw) snprintf(display_status, sizeof display_status, "Output %u x %u, 3D scene %u x %u", cw, ch, sw, sh);
    else snprintf(display_status, sizeof display_status, "Output %u x %u, 3D scene the game's own", cw, ch);
    pthread_mutex_unlock(&lock);
    fprintf(stderr, "[settings] display: %s%s\n", display_status, r ? " (a size waits for the next start)" : "");
    return r;
}
const char *settings_display_status(void) { return display_status; }
void settings_display_size(unsigned drawable_w, unsigned drawable_h, unsigned screen_w) {
    pthread_mutex_lock(&lock);
    int same = drawable_w == disp_w && drawable_h == disp_h && screen_w == native_w, ready = loaded;
    disp_w = drawable_w; disp_h = drawable_h; native_w = screen_w;
    int out = loaded ? values[settings_index("display.output")] : -1, aspect = loaded ? values[settings_index("display.aspect")] : -1;
    pthread_mutex_unlock(&lock);
    if (!same && ready && (out == SET_OUT_WINDOW || out == SET_OUT_NATIVE || aspect == 0)) display_apply("window");
}

/* Live settings go to the code that owns them; the touch controls and the menu button are the iOS app's own views, which
   follow through settings_observe. Returns -1 when the game cannot take the value now (it applies at the next start). */
static int apply_live(const SettingDef *d, int v) {
    extern void dm_gfx_set_flag(const char *name, int value);
    extern void nvn_fps_cap_set(int hz);
    extern void fps_overlay_set_hints(int visible);
    extern void audio_master_volume_set(int percent);
    extern int runtime_hid_feedback_flag(const char *name, int v);
    extern void runtime_gamepad_relayout(void);
    extern void runtime_fov_set(int percent);
    extern void runtime_fog_set(int percent);
    extern int runtime_shadow_res_set(int factor);
    extern void runtime_time_speed_set(int percent);
    extern void runtime_render_distance_set(int percent);
    extern int runtime_render_request(unsigned w, unsigned h);
    extern void runtime_camera_speed_set(int axis, int percent);
    extern void fps_overlay_set_detail(int detailed);
    (void)runtime_render_request;
    if (!strncmp(d->key, "display.", 8)) return d->apply == SET_LIVE ? display_apply(d->key) : 0;
    if (!strcmp(d->key, "graphics.interpolation")) dm_gfx_set_flag("mfx_interp", v);
    else if (!strcmp(d->key, "graphics.fov")) runtime_fov_set(v);
    else if (!strcmp(d->key, "graphics.fog")) runtime_fog_set(v);
    else if (!strcmp(d->key, "graphics.shadow_res")) return runtime_shadow_res_set(v);
    else if (!strcmp(d->key, "graphics.render_distance")) runtime_render_distance_set(v);
    else if (!strcmp(d->key, "world.time_speed")) runtime_time_speed_set(v);
    else if (!strcmp(d->key, "graphics.aa")) dm_gfx_set_flag("aa_mode", v);
    else if (!strcmp(d->key, "graphics.sharpen")) dm_gfx_set_flag("sharpen", v);
    else if (!strcmp(d->key, "graphics.aniso")) dm_gfx_set_flag("aniso", v);
    else if (!strcmp(d->key, "graphics.lod_bias")) dm_gfx_set_flag("lod_bias", v);
    else if (!strcmp(d->key, "framerate.cap")) nvn_fps_cap_set(v);
    else if (!strcmp(d->key, "interface.readout")) fps_overlay_set_hints(v);
    else if (!strcmp(d->key, "interface.shader_counter")) { extern void shader_overlay_set(int on); shader_overlay_set(v); }
    else if (!strcmp(d->key, "interface.download_shaders")) { extern void totk_shader_list_set_enabled(int on); totk_shader_list_set_enabled(v); }
    else if (!strcmp(d->key, "interface.readout_detail")) fps_overlay_set_detail(v);
    else if (!strcmp(d->key, "interface.hide_hud")) dm_gfx_set_flag("hide_hud", v);
    else if (!strcmp(d->key, "graphics.dof")) dm_gfx_set_flag("dof", v);
    else if (!strcmp(d->key, "graphics.bloom")) dm_gfx_set_flag("bloom", v);
    else if (!strcmp(d->key, "graphics.lens_flare")) dm_gfx_set_flag("lens_flare", v);
    else if (!strcmp(d->key, "controls.camera_speed_x")) runtime_camera_speed_set(0, v);
    else if (!strcmp(d->key, "controls.camera_speed_y")) runtime_camera_speed_set(1, v);
    else if (!strcmp(d->key, "audio.volume")) audio_master_volume_set(v);
    else if (!strcmp(d->key, "controls.rumble")) runtime_hid_feedback_flag("rumble", v);
    else if (!strcmp(d->key, "controls.gyro")) runtime_hid_feedback_flag("gyro", v);
    else if (!strcmp(d->key, "controls.face_buttons")) {
        const SettingChoice *c = settings_choice(d, v);
        if (c && c->env) setenv("TOTK_FACE_BUTTONS", c->env, 1); else unsetenv("TOTK_FACE_BUTTONS");
        runtime_gamepad_relayout();
    }
    return 0;
}

/* The last controller connected (gamepad.m, settings_note_controller), for Button prompts: Automatic. Next to settings.txt. */
static const char *controller_path(void) {
    static char p[1100]; const char *slash = strrchr(path, '/');
    if (slash) snprintf(p, sizeof p, "%.*s/last-controller.txt", (int)(slash - path), path); else snprintf(p, sizeof p, "last-controller.txt");
    return p;
}
void settings_note_controller(const char *kind) {
    if (!*path) return;
    char was[32] = ""; FILE *f = fopen(controller_path(), "r");
    if (f) { if (fgets(was, sizeof was, f)) was[strcspn(was, "\r\n")] = 0; fclose(f); }
    if (!strcmp(was, kind)) return;
    if ((f = fopen(controller_path(), "w"))) { fprintf(f, "%s\n", kind); fclose(f); }
    fprintf(stderr, "[settings] last controller: %s (Button prompts: Automatic follows it at the next start)\n", kind);
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
        if (!SETTINGS[i].env && SETTINGS[i].apply == SET_LIVE && strncmp(SETTINGS[i].key, "display.", 8) && start[i] != settings_default(&SETTINGS[i], settings_ios())) apply_live(&SETTINGS[i], start[i]);
    display_apply(NULL);   /* the start sizes: the patches take them when they install */
    /* Button prompts: which button-icon mod the mods folder loads this start (mods.c). */
    extern void mods_set_prompts(int style);
    int prompts = settings_get("controls.prompts");
    if (prompts == 0) {   /* Automatic: the last controller connected (settings_note_controller) */
        char kind[32] = ""; FILE *k = fopen(controller_path(), "r");
        if (k) { if (fgets(kind, sizeof kind, k)) kind[strcspn(kind, "\r\n")] = 0; fclose(k); }
        prompts = !strcmp(kind, "xbox") ? 2 : !strcmp(kind, "playstation") ? 3 : 1;
        fprintf(stderr, "[settings] button prompts: automatic, %s (last controller: %s)\n", prompts == 2 ? "Xbox" : prompts == 3 ? "PlayStation" : "Switch", *kind ? kind : "none yet");
    }
    mods_set_prompts(prompts);
}

int settings_get(const char *key) {
    int i = settings_index(key); if (i < 0) return -1;
    pthread_mutex_lock(&lock); int v = values[i]; pthread_mutex_unlock(&lock);
    return v;
}

int settings_pending_restart(const char *key) {
    int i = settings_index(key); if (i < 0) return 0;
    pthread_mutex_lock(&lock); int r = changed_since_start[i]; pthread_mutex_unlock(&lock);
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
    int was = values[i]; values[i] = value; if (was != value && d->apply == SET_RESTART) changed_since_start[i] = 1;
    pthread_mutex_unlock(&lock);
    if (was == value) return d->apply == SET_LIVE ? SETTINGS_APPLIED : SETTINGS_RESTART;
    fprintf(stderr, "[settings] %s = %d (was %d)\n", key, value, was);
    int live = d->apply == SET_LIVE && apply_live(d, value) == 0;
    pthread_mutex_lock(&lock); changed_since_start[i] = !live; pthread_mutex_unlock(&lock);   /* a live value the game took clears an earlier refusal */
    if (settings_save()) fprintf(stderr, "[settings] could not write %s\n", *path ? path : "(no settings file)");
    if (observer) observer(key, value);
    return live ? SETTINGS_APPLIED : SETTINGS_RESTART;
}

void settings_observe(void (*fn)(const char *key, int value)) { observer = fn; }

/* TOTK_INPUT_FILE "set <key> <value>" (runtime/input.c): the menu's own path, for test runs. Nonzero = unknown key or value. */
int runtime_setting_test(const char *key, const char *word, int *v) {
    int i = settings_index(key);
    if (i < 0 || settings_parse_value(&SETTINGS[i], word, v)) return -1;
    int r = settings_set(key, *v);
    fprintf(stderr, "[settings] test: %s = %s -> %s\n", key, word, r == SETTINGS_APPLIED ? "applied" : r == SETTINGS_RESTART ? "next start" : "invalid");
    return r == SETTINGS_INVALID;
}
