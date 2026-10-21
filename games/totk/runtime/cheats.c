#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
/* iPhone and iPad have no cheats yet: no Cheats menu or panel there (frontend.m is Mac-only), and the iOS SDK has no
   mach_vm_read / mach_vm_write for reading guest memory safely. build_app.py refuses --cheats for --platform ios. */
#include "runtime.h"
void runtime_cheats_launch(Module *main_module) { (void)main_module; }
int runtime_cheats_count(void) { return 0; }
const char *runtime_cheats_name(int i) { (void)i; return ""; }
int runtime_cheats_state(int i) { (void)i; return 0; }
const char *runtime_cheats_problem(int i) { (void)i; return ""; }
int runtime_cheats_set(int i, int on) { (void)i; (void)on; return 0; }
int runtime_cheats_total(void) { return 0; }
int runtime_cheats_info(int k, RuntimeCheatInfo *out) { (void)k; (void)out; return 0; }
int runtime_cheats_set_entry(int k, int on) { (void)k; (void)on; return 0; }
int runtime_cheats_sources(void) { return 0; }
const char *runtime_cheats_source_label(int s) { (void)s; return ""; }
const char *runtime_cheats_source_path(int s) { (void)s; return ""; }
const char *runtime_cheats_source_problem(int s) { (void)s; return ""; }
const char *runtime_cheats_build_id(void) { return ""; }
const char *runtime_cheats_folder(void) { return ""; }
int runtime_cheats_files_changed(void) { return 0; }
#else
#include "runtime.h"
#include "cheat_vm.h"
#include "cheat_files.h"
#include "game_version.h"
#include <copyfile.h>
#include <ctype.h>
#include <libgen.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Atmosphère cheat files for this game's build ID, read at launch from the app (build_app.py --cheats: cheats/cheats.txt;
   TOTK_CHEATS_FILE overrides), the cheats folder and the mods that are on (cheat_files.h). Nothing here is specific to
   one game; the Mac settings menu's Cheats tab (settings_panel_mac.h) lists and switches them.
   - Cheats that write instructions into the main module's code (with a code cave in the padding after its text) are
     applied once at launch, before the modules are sealed r-x: the code is never made writable while guest threads run
     it, so toggling one takes effect at the next launch. A patch is refused when its site is a rewritten instruction
     (a branch into the rewrite's patch area), when a branch it writes leaves the code (into data, which is not
     executable here), when it is conditional, or when its cave would overwrite padding the runtime's own patches
     already use (game_patches*.c and aspect_patch.h put veneers at the start of main's text padding).
   - Every other cheat (pointer chains into the heap, button conditions) runs about 30 times a second on a host thread,
     reading and writing guest memory with mach_vm_read/write, so a stale pointer fails instead of crashing.
   The chosen cheats are saved beside the profile (cheats-enabled.txt). The first time one is enabled the profile is copied
   to profile-before-cheats, as the launcher does before a version upgrade. */

static Cheat *cheats;
static int ncheat, listed[CHEAT_MAX], nlisted;
static _Atomic int state[CHEAT_MAX];
static char problem[CHEAT_MAX][112];
static uint8_t *main_base;
static uint64_t main_size, main_rx, main_text, main_patch;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
typedef struct { uint64_t off; uint32_t value; int cheat; } Patched;
static Patched patched[512];   // code words written at launch, to catch overlaps
static int npatched;
static int dry_run;   // TOTK_CHEATS_DRYRUN=1: memory cheats log their first write per target and change nothing

static const char *cheat_file(void) { const char *e = getenv("TOTK_CHEATS_FILE"); return e && *e ? e : "cheats/cheats.txt"; }
static int profile_dir(char *out, size_t n) {
    const char *p = getenv("TOTK_PROFILE_DIR");
    if (!p || !*p || strlen(p) >= n) return 0;
    snprintf(out, n, "%s", p);
    size_t l = strlen(out);
    while (l > 1 && out[l - 1] == '/') out[--l] = 0;
    return 1;
}
static int state_file(char *out, size_t n) {
    const char *e = getenv("TOTK_CHEATS_STATE");
    if (e && *e) { snprintf(out, n, "%s", e); return 1; }
    char p[1024];
    if (!profile_dir(p, sizeof p)) return 0;
    return snprintf(out, n, "%s/cheats-enabled.txt", dirname(p)) < (int)n;
}

/* --- launch-time code patches ------------------------------------------------------------------------------------ */
static uint32_t word_at(uint64_t off) { uint32_t v; memcpy(&v, main_base + off, 4); return v; }
static int branch_target(uint32_t insn, uint64_t at, uint64_t *to) {   // B / BL imm26
    if ((insn & 0x7C000000) != 0x14000000) return 0;
    int64_t imm = (int64_t)(insn & 0x03FFFFFF) << 38 >> 36;
    *to = at + (uint64_t)imm;
    return 1;
}
/* A padding word another cheat wrote this launch (identical or not: different values are left to conflicts()). */
static int patched_by_cheat(uint64_t off) { for (int p = 0; p < npatched; p++) if (patched[p].off == off) return 1; return 0; }
/* Classifies cheat k (CHEAT_AT_LAUNCH when it writes main-module code) and checks every code write. apply: write them,
   unless a word was already written with a different value by another cheat this launch (then nothing of k is written). */
static int conflicts(int k);
static int code_patches(int k, int apply) {
    if (apply && conflicts(k)) return -1;
    const Cheat *c = &cheats[k];
    int depth = 0, code = 0;
    for (unsigned i = 0; i < c->n;) {
        unsigned len = cheat_insn_len(c->w, c->n, i);
        if (!len) { snprintf(problem[k], sizeof problem[k], "malformed instruction %08x", c->w[i]); return -1; }
        uint32_t x = c->w[i];
        unsigned op = x >> 28, t = x >> 24 & 0xF;
        if (op == 1 || op == 8 || (op == 0xC && t == 0)) depth++;
        else if (op == 2 && t == 0) depth--;
        else if (op == 0 && (x >> 20 & 0xF) == 0) {   // store static value into the main module
            uint64_t off = (uint64_t)(x & 0xFF) << 32 | c->w[i + 1];
            if (off < main_rx) {
                code = 1;
                if (depth) { snprintf(problem[k], sizeof problem[k], "conditional code patch at main+%#llx", (unsigned long long)off); return -1; }
                if ((t != 4 && t != 8) || (off & 3) || off + t > main_rx) { snprintf(problem[k], sizeof problem[k], "unaligned code patch at main+%#llx", (unsigned long long)off); return -1; }
                uint32_t nw[2] = {t == 8 ? c->w[i + 3] : c->w[i + 2], c->w[i + 2]};   // a 64-bit value is stored low word first
                for (unsigned j = 0; j < t / 4; j++) {
                    uint64_t at = off + 4 * j, to;
                    if (at < main_text && branch_target(word_at(at), at, &to) && to >= main_patch) {
                        snprintf(problem[k], sizeof problem[k], "patches a rewritten instruction at main+%#llx", (unsigned long long)at); return -1;
                    }
                    if (at >= main_text && word_at(at) && !patched_by_cheat(at)) {   // the runtime's own veneers live here too
                        snprintf(problem[k], sizeof problem[k], "overlaps the game runtime's own patch code at main+%#llx", (unsigned long long)at); return -1;
                    }
                    if (branch_target(nw[j], at, &to) && to >= main_rx) {
                        snprintf(problem[k], sizeof problem[k], "branches out of the game's code (main+%#llx)", (unsigned long long)to); return -1;
                    }
                }
                if (apply) {
                    memcpy(main_base + off, nw, t);
                    for (unsigned j = 0; j < t / 4 && npatched < (int)(sizeof patched / sizeof *patched); j++)
                        patched[npatched++] = (Patched){off + 4 * j, nw[j], k};
                }
            }
        }
        i += len;
    }
    return code;
}

/* Another cheat already applied this launch wrote one of k's code words with a different value. */
static int conflicts(int k) {
    const Cheat *c = &cheats[k];
    for (unsigned i = 0; i < c->n;) {
        unsigned len = cheat_insn_len(c->w, c->n, i);
        if (!len) return 0;
        uint32_t x = c->w[i];
        if (x >> 28 == 0 && (x >> 20 & 0xF) == 0) {
            unsigned t = x >> 24 & 0xF;
            uint64_t off = (uint64_t)(x & 0xFF) << 32 | c->w[i + 1];
            uint32_t nw[2] = {t == 8 ? c->w[i + 3] : c->w[i + 2], c->w[i + 2]};
            for (unsigned j = 0; off < main_rx && j < t / 4; j++)
                for (int p = 0; p < npatched; p++)
                    if (patched[p].off == off + 4 * j && patched[p].value != nw[j]) {
                        snprintf(problem[k], sizeof problem[k], "conflicts with \"%s\"; turn one off", cheats[patched[p].cheat].name);
                        return 1;
                    }
        }
        i += len;
    }
    return 0;
}

/* --- guarded guest memory ---------------------------------------------------------------------------------------- */
static int in_range(uint64_t a, unsigned n, uint64_t base, uint64_t size) { return base && a >= base && a - base <= size && n <= size - (a - base); }
static int allowed(uint64_t a, unsigned n, int write) {
    uint64_t b[3], s[3];
    runtime_guest_regions(b, s);
    if (in_range(a, n, (uintptr_t)main_base + (write ? main_rx : 0), main_size - (write ? main_rx : 0))) return 1;
    for (int i = 0; i < 3; i++) if (in_range(a, n, b[i], s[i])) return 1;
    return 0;
}
static int guest_read(void *ctx, uint64_t a, void *out, unsigned n) {
    (void)ctx;
    mach_vm_size_t got = 0;
    return allowed(a, n, 0) && mach_vm_read_overwrite(mach_task_self(), a, n, (mach_vm_address_t)(uintptr_t)out, &got) == KERN_SUCCESS && got == n;
}
static int guest_write(void *ctx, uint64_t a, const void *in, unsigned n) {
    if (dry_run) {   // log a target when first seen and whenever its value changes (a save loaded after the title)
        typedef struct { uint64_t addr, value; int logs; } Seen;
        static Seen seen[256]; static int nseen;
        uint64_t now = 0, v = 0;
        int readable = guest_read(NULL, a, &now, n);
        memcpy(&v, in, n);
        int k = 0;
        while (k < nseen && seen[k].addr != a) k++;
        if (k == nseen && nseen < 256) seen[nseen++] = (Seen){a, ~now, 0};
        if (k < nseen && seen[k].value != now && seen[k].logs < 8) {
            seen[k].value = now; seen[k].logs++;
            fprintf(stderr, "[totk] cheat dry run \"%s\": %#llx (%u bytes) is %s%#llx, would write %#llx\n", ctx ? (const char *)ctx : "?",
                    (unsigned long long)a, n, readable ? "" : "unreadable ", (unsigned long long)now, (unsigned long long)v);
        }
        return allowed(a, n, 1);
    }
    (void)ctx;
    return allowed(a, n, 1) && mach_vm_write(mach_task_self(), a, (vm_offset_t)(uintptr_t)in, n) == KERN_SUCCESS;
}

static void *cheat_thread(void *arg) {
    (void)arg;
    pthread_setname_np("cheats");
    for (;;) {
        usleep(33000);
        CheatEnv env = {.ctx = NULL, .read = guest_read, .write = guest_write};
        uint64_t b[3], s[3];
        runtime_guest_regions(b, s);
        env.base[0] = (uintptr_t)main_base; env.base[1] = b[0]; env.base[2] = b[1]; env.base[3] = b[2];
        env.keys = runtime_input_buttons();
        for (int k = 0; k < ncheat; k++) {
            int st = atomic_load(&state[k]);
            if (!(st & CHEAT_ON) || (st & (CHEAT_AT_LAUNCH | CHEAT_REFUSED)) || !cheats[k].n) continue;
            char err[112] = "";
            env.ctx = cheats[k].name;
            if (!cheat_run(&cheats[k], &env, err, sizeof err) && err[0] && strcmp(err, problem[k])) {   // a failed read (pointer not set up yet) is silent
                pthread_mutex_lock(&mu); snprintf(problem[k], sizeof problem[k], "%s", err); pthread_mutex_unlock(&mu);
                fprintf(stderr, "[totk] cheat \"%s\": %s\n", cheats[k].name, err);
            }
        }
    }
    return NULL;
}

/* --- where the cheats come from (cheat_files.h) ------------------------------------------------------------------ */
static CheatSource sources[CHEAT_SOURCES_MAX];
static int nsources, source_of[CHEAT_MAX];
static char source_problem[CHEAT_SOURCES_MAX][112];
static char build_id[20], folder_path[1100];
static pthread_once_t folder_once = PTHREAD_ONCE_INIT;

/* The main module's build ID: TOTK_CHEATS_BUILD_ID (tests), the extracted NSO when the game runs from a build tree,
   else the one game_version.h records for this version. */
static void find_build_id(void) {
    const char *e = getenv("TOTK_CHEATS_BUILD_ID");
    if (e && *e) snprintf(build_id, sizeof build_id, "%s", e);
#ifdef TOTK_GAME_DIR
    else {
        uint8_t id[8]; FILE *f = fopen("extracted/" TOTK_GAME_DIR "/exefs/main", "rb");
        if (f && !fseek(f, 0x40, SEEK_SET) && fread(id, 1, sizeof id, f) == sizeof id)
            for (int i = 0; i < 8; i++) snprintf(build_id + 2 * i, sizeof build_id - 2 * i, "%02X", id[i]);
        if (f) fclose(f);
    }
#endif
#ifdef GAME_BUILD_ID
    if (!*build_id) snprintf(build_id, sizeof build_id, "%s", GAME_BUILD_ID);
#endif
    for (char *p = build_id; *p; p++) *p = (char)toupper((unsigned char)*p);
}
/* The player's cheats folder (TOTK_CHEATS_DIR, which the launcher sets; 0 = none), made on first use. */
static void make_folder(void) {
    const char *e = getenv("TOTK_CHEATS_DIR"), *home = getenv("HOME");
    if (e && *e) snprintf(folder_path, sizeof folder_path, "%s", e);
    else snprintf(folder_path, sizeof folder_path, "%s/Library/Application Support/MacNativeGames/" GAME_FOLDER "/cheats", home ? home : ".");
    if (!strcmp(folder_path, "0")) return;
    char p[1100]; snprintf(p, sizeof p, "%s", folder_path);
    for (char *s = p + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }
    mkdir(p, 0755);
}
static const char *cheat_folder(void) { pthread_once(&folder_once, make_folder); return folder_path; }
static const char *app_file(void) { return access(cheat_file(), R_OK) == 0 ? cheat_file() : NULL; }

/* Parses every source into cheats[] (at most CHEAT_MAX sections in all); a file that does not parse is skipped with its
   reason. Returns the number of sections. */
static int load_sources(void) {
    find_build_id();
    nsources = cheat_sources(app_file(), cheat_folder(), build_id, sources, CHEAT_SOURCES_MAX);
    if (!nsources) {
        fprintf(stderr, "[totk] cheats: none (cheat files for this game are named %s.txt; the cheats folder is %s)\n", *build_id ? build_id : "<build ID>", cheat_folder());
        return 0;
    }
    static char text[1 << 20];
    Cheat *one = malloc(CHEAT_MAX * sizeof *one);
    cheats = calloc(CHEAT_MAX, sizeof *cheats);
    if (!one || !cheats) { free(one); fprintf(stderr, "[totk] cheats: out of memory\n"); return 0; }
    for (int s = 0; s < nsources; s++) {
        FILE *f = fopen(sources[s].path, "rb");
        if (!f) { snprintf(source_problem[s], sizeof source_problem[s], "cannot be read"); continue; }
        size_t got = fread(text, 1, sizeof text - 1, f);
        int too_big = got == sizeof text - 1 && fgetc(f) != EOF;
        fclose(f);
        text[got] = 0;
        char err[128] = "";
        int n = too_big ? -1 : cheat_parse(text, one, CHEAT_MAX, err, sizeof err);
        if (n < 0) {
            snprintf(source_problem[s], sizeof source_problem[s], "not a cheat file this game can read: %s", too_big ? "larger than 1 MB" : err);
            fprintf(stderr, "[totk] cheats: %s: %s\n", sources[s].path, source_problem[s]);
            continue;
        }
        int take = n < CHEAT_MAX - ncheat ? n : CHEAT_MAX - ncheat;
        if (take < n) snprintf(source_problem[s], sizeof source_problem[s], "%d of its cheats left out (at most %d in all files)", n - take, CHEAT_MAX);
        memcpy(cheats + ncheat, one, (size_t)take * sizeof *one);
        for (int j = 0; j < take; j++) source_of[ncheat + j] = s;
        ncheat += take;
        fprintf(stderr, "[totk] cheats: %d sections from %s (%s)\n", take, sources[s].path, sources[s].label);
    }
    free(one);
    return ncheat;
}

/* --- chosen cheats ----------------------------------------------------------------------------------------------- */
/* cheats-enabled.txt holds one key per chosen cheat (cheat_key); lines for files not read this launch (a mod that is off)
   are kept as they were. */
static char *kept_choices;
static void key_of(int k, char *out, size_t n) { cheat_key(sources[source_of[k]].id, cheats[k].name, out, n); }
static int source_loaded(const char *line) {
    const char *tab = strchr(line, '\t');
    for (int s = 0; s < nsources; s++)
        if (tab ? strlen(sources[s].id) == (size_t)(tab - line) && !strncmp(sources[s].id, line, (size_t)(tab - line)) : !strcmp(sources[s].id, "app")) return 1;
    return 0;
}
static void load_choices(void) {
    char path[1100], line[400 + CHEAT_NAME], key[400 + CHEAT_NAME];
    if (!state_file(path, sizeof path)) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    size_t kept = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!*line) continue;
        int found = 0;
        for (int k = 0; k < ncheat; k++) {
            if (cheats[k].master) continue;
            key_of(k, key, sizeof key);
            if (!strcmp(key, line)) { atomic_fetch_or(&state[k], CHEAT_ON); found = 1; }
        }
        if (!found && !source_loaded(line)) {
            char *more = realloc(kept_choices, kept + strlen(line) + 2);
            if (more) { kept_choices = more; snprintf(kept_choices + kept, strlen(line) + 2, "%s\n", line); kept += strlen(line) + 1; }
        }
    }
    fclose(f);
}
static void save_choices(void) {
    char path[1100], tmp[1120], key[400 + CHEAT_NAME];
    if (!state_file(path, sizeof path)) return;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    for (int k = 0; k < ncheat; k++)
        if (!cheats[k].master && (atomic_load(&state[k]) & CHEAT_ON)) { key_of(k, key, sizeof key); fprintf(f, "%s\n", key); }
    if (kept_choices) fputs(kept_choices, f);
    if (fclose(f) == 0) rename(tmp, path); else unlink(tmp);
}
static int backup_profile(char *why, size_t n) {
    char p[1024], backup[1100], tmp[1120];
    struct stat st;
    if (!profile_dir(p, sizeof p) || stat(p, &st) != 0) return 1;   // no profile yet: nothing to protect
    snprintf(backup, sizeof backup, "%s-before-cheats", p);
    if (stat(backup, &st) == 0) return 1;
    if (runtime_fs_save_busy()) { snprintf(why, n, "the game is saving; try again in a moment"); return 0; }
    snprintf(tmp, sizeof tmp, "%s.copy-%d", backup, getpid());
    if (copyfile(p, tmp, NULL, COPYFILE_ALL | COPYFILE_RECURSIVE) != 0 || rename(tmp, backup) != 0) {
        snprintf(why, n, "could not back up the profile first"); return 0;
    }
    fprintf(stderr, "[totk] cheats: profile backed up to %s\n", backup);
    return 1;
}

int runtime_cheats_count(void) { return nlisted; }
const char *runtime_cheats_name(int i) { return i >= 0 && i < nlisted ? cheats[listed[i]].name : ""; }
int runtime_cheats_state(int i) { return i >= 0 && i < nlisted ? atomic_load(&state[listed[i]]) : 0; }
const char *runtime_cheats_problem(int i) { return i >= 0 && i < nlisted ? problem[listed[i]] : ""; }
int runtime_cheats_set(int i, int on) {
    if (i < 0 || i >= nlisted) return 0;
    int k = listed[i];
    pthread_mutex_lock(&mu);
    if (on && !(atomic_load(&state[k]) & CHEAT_REFUSED)) {
        char why[112] = "";
        if (backup_profile(why, sizeof why)) { atomic_fetch_or(&state[k], CHEAT_ON); problem[k][0] = 0; }
        else snprintf(problem[k], sizeof problem[k], "%s", why);
    } else if (!on) atomic_fetch_and(&state[k], ~CHEAT_ON);
    save_choices();
    int st = atomic_load(&state[k]);
    pthread_mutex_unlock(&mu);
    fprintf(stderr, "[totk] cheat \"%s\" %s%s\n", cheats[k].name, st & CHEAT_ON ? "on" : "off",
            st & CHEAT_AT_LAUNCH ? " (code patch: takes effect at the next launch)" : "");
    return st;
}

void runtime_cheats_launch(Module *m) {
    if (!load_sources()) return;
    const char *dr = getenv("TOTK_CHEATS_DRYRUN");
    dry_run = dr && !strcmp(dr, "1");
    if (dry_run) fprintf(stderr, "[totk] cheats: dry run (memory cheats log their targets and write nothing)\n");
    main_base = m->base; main_size = m->size; main_text = m->text_end; main_patch = m->patch_off;
    main_rx = (m->text_end + 0xFFF) & ~0xFFFULL;   // the code and the padding after it, up to the next 4 KiB segment
    load_choices();
    int applied = 0, at_launch = 0;
    for (int k = 0; k < ncheat; k++) {
        if (!cheats[k].n) continue;
        if (!cheats[k].master) listed[nlisted++] = k; else atomic_fetch_or(&state[k], CHEAT_ON);
        int code = code_patches(k, 0);
        if (code < 0) { atomic_fetch_or(&state[k], CHEAT_REFUSED); fprintf(stderr, "[totk] cheat \"%s\" refused: %s\n", cheats[k].name, problem[k]); continue; }
        if (!code) continue;
        atomic_fetch_or(&state[k], CHEAT_AT_LAUNCH); at_launch++;
        if (atomic_load(&state[k]) & CHEAT_ON) {
            if (code_patches(k, 1) < 0) { fprintf(stderr, "[totk] cheat \"%s\" not applied: %s\n", cheats[k].name, problem[k]); continue; }
            atomic_fetch_or(&state[k], CHEAT_APPLIED); applied++;
        }
    }
    fprintf(stderr, "[totk] cheats: %d in %d file%s for build %s (%d patch code at launch, %d of them applied now)\n", nlisted, nsources,
            nsources == 1 ? "" : "s", build_id, at_launch, applied);
    pthread_t th;
    if (pthread_create(&th, NULL, cheat_thread, NULL) == 0) pthread_detach(th);
}

/* --- the Cheats tab (settings_panel_mac.h): every section of every file, by its index k in cheats[] ----------------- */
int runtime_cheats_total(void) { return ncheat; }
int runtime_cheats_info(int k, RuntimeCheatInfo *out) {
    if (k < 0 || k >= ncheat || !out) return 0;
    out->name = cheats[k].name; out->source = source_of[k]; out->role = cheat_role(&cheats[k]);
    out->state = atomic_load(&state[k]);
    pthread_mutex_lock(&mu); snprintf(out->problem, sizeof out->problem, "%s", problem[k]); pthread_mutex_unlock(&mu);
    return 1;
}
int runtime_cheats_set_entry(int k, int on) {
    for (int i = 0; i < nlisted; i++) if (listed[i] == k) return runtime_cheats_set(i, on);
    return 0;
}
int runtime_cheats_sources(void) { return nsources; }
const char *runtime_cheats_source_label(int s) { return s >= 0 && s < nsources ? sources[s].label : ""; }
const char *runtime_cheats_source_path(int s) { return s >= 0 && s < nsources ? sources[s].path : ""; }
const char *runtime_cheats_source_problem(int s) { return s >= 0 && s < nsources ? source_problem[s] : ""; }
const char *runtime_cheats_build_id(void) { if (!*build_id) find_build_id(); return build_id; }
const char *runtime_cheats_folder(void) { return cheat_folder(); }
int runtime_cheats_files_changed(void) {
    static CheatSource now[CHEAT_SOURCES_MAX];   /* main queue only */
    int n = cheat_sources(app_file(), cheat_folder(), runtime_cheats_build_id(), now, CHEAT_SOURCES_MAX);
    return !cheat_sources_same(sources, nsources, now, n);
}
#endif
