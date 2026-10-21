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
#else
#include "runtime.h"
#include "cheat_vm.h"
#include "guest_range.h"
#include "cheat_code.h"
#include <copyfile.h>
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

/* Cheats baked into the app (build_app.py --cheats: the Atmosphère cheat file for this game's build ID, at
   cheats/cheats.txt; TOTK_CHEATS_FILE overrides). Nothing here is specific to one game.
   - The code words a cheat writes into the main module (with a code cave in the padding after its text) are applied
     once at launch, before the modules are sealed r-x: the code is never made writable while guest threads run it, so
     toggling such a cheat takes effect at the next launch. cheat_code.c decides which words those are and refuses a
     patch that cannot be applied safely (under a condition, on an instruction the Apple-silicon rewrite changed or
     moved, over the runtime's own patches, branching where it cannot reach; see there).
   - Everything else (pointer chains into the heap, button conditions, and the data part of a cheat that also patches
     code) runs about 30 times a second on a host thread, reading and writing guest memory with mach_vm_read/write
     through a guard that admits only memory the game has mapped, so a stale pointer fails instead of crashing. On the
     thread a code word already applied at launch is only compared, never written.
   The chosen cheats are saved beside the profile (cheats-enabled.txt). The first time one is enabled the profile is copied
   to profile-before-cheats, as the launcher does before a version upgrade. */

static Cheat *cheats;
static int ncheat, listed[CHEAT_MAX], nlisted;
static _Atomic int state[CHEAT_MAX];
static char problem[CHEAT_MAX][112];
static uint8_t *main_base;
static uint64_t main_size, main_rx, main_text, main_patch, main_data, main_key;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static Module *main_module;
static CheatWord patched[1024];   // code words written at launch, to catch overlaps
static unsigned npatched;
static unsigned char runs[CHEAT_MAX];   // the cheat has work for the cheat thread (not only code words)
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
static int image_word(void *ctx, uint64_t off, uint32_t *out) { return runtime_module_image_word(ctx, off, out); }
/* Classifies cheat k (masters: the master codes that run before it) and, when it is on, writes all of its code words or
   none. Returns -1 when refused, 0 when it writes no code, 1 when it does (applied or not). */
static int launch_code(int k, const Cheat *const *masters, int nmasters) {
    CheatMain mm = {.mem = main_base, .text_end = main_text, .rx_end = main_rx, .image_end = main_key,
                    .changed = main_module->changed, .nchanged = main_module->nchanged, .moved = main_module->moved,
                    .nmoved = main_module->nmoved, .image_word = image_word, .ctx = main_module};
    static CheatCode code;
    char err[CHEAT_ERR];
    if (!cheat_code_scan(&mm, masters, nmasters, &cheats[k], patched, npatched, &code, err, sizeof err)) {
        snprintf(problem[k], sizeof problem[k], "%s", err);
        return -1;
    }
    runs[k] = code.runtime || !code.n;
    if (!code.n) return 0;
    atomic_fetch_or(&state[k], CHEAT_AT_LAUNCH);
    if (!(atomic_load(&state[k]) & CHEAT_ON)) return 1;
    if (code.conflict >= 0) snprintf(problem[k], sizeof problem[k], "conflicts with \"%s\"; turn one off", cheats[code.conflict].name);
    else if (npatched + code.n > sizeof patched / sizeof *patched) snprintf(problem[k], sizeof problem[k], "too many code words from all cheats together");
    else {
        for (unsigned j = 0; j < code.n; j++) {
            memcpy(main_base + code.w[j].off, &code.w[j].value, 4);
            patched[npatched++] = (CheatWord){code.w[j].off, code.w[j].value, k};
        }
        atomic_fetch_or(&state[k], CHEAT_APPLIED);
        return 1;
    }
    fprintf(stderr, "[totk] cheat \"%s\" not applied: %s\n", cheats[k].name, problem[k]);
    return 1;
}

/* --- guarded guest memory ---------------------------------------------------------------------------------------- */
/* Every access is checked where it happens, after any pointer arithmetic, against memory the game actually has: the main
   module (reads before the runtime's key page; writes only to its data and bss), and through runtime_guest_access the
   heap at its current size and the alias pages mapped with MapPhysicalMemory. Anything else (the unmapped rest of the
   reservations, the stack region, host allocations) is refused. */
typedef struct { uint64_t a; void *buf; unsigned n; int write; } Access;
static int host_access(void *p) {
    Access *x = p;
    if (x->write) return mach_vm_write(mach_task_self(), x->a, (vm_offset_t)(uintptr_t)x->buf, x->n) == KERN_SUCCESS;
    mach_vm_size_t got = 0;
    return mach_vm_read_overwrite(mach_task_self(), x->a, x->n, (mach_vm_address_t)(uintptr_t)x->buf, &got) == KERN_SUCCESS && got == x->n;
}
static int accept(void *p) { (void)p; return 1; }
/* write: 0 read, 1 write, 2 only check that a write would be allowed */
static int guest_access(uint64_t a, void *buf, unsigned n, int write) {
    Access x = {a, buf, n, write == 1};
    switch (guest_main_access((uintptr_t)main_base, main_data, main_rx, main_key, a, n, write != 0)) {
    case 1: return write == 2 || host_access(&x);
    case 2: return buf && !memcmp((const void *)(uintptr_t)a, buf, n);   // code: never written; equal to what launch applied
    default: return runtime_guest_access(a, n, write == 2 ? accept : host_access, &x);
    }
}
static int guest_read(void *ctx, uint64_t a, void *out, unsigned n) { (void)ctx; return guest_access(a, out, n, 0); }
static int guest_write(void *ctx, uint64_t a, const void *in, unsigned n) {
    if (dry_run) {   // log a target when first seen and whenever its value changes (a save loaded after the title)
        typedef struct { uint64_t addr, value; int logs; } Seen;
        static Seen seen[4096]; static int nseen, full;
        uint64_t now = 0, v = 0;
        int readable = guest_read(NULL, a, &now, n);
        memcpy(&v, in, n);
        int k = 0;
        while (k < nseen && seen[k].addr != a) k++;
        if (k == nseen && nseen < 4096) seen[nseen++] = (Seen){a, ~now, 0};
        else if (k == nseen && !full++) fprintf(stderr, "[totk] cheat dry run: more than 4096 targets, the rest are not logged\n");
        int ok = guest_access(a, (void *)(uintptr_t)in, n, 2);
        if (k < nseen && seen[k].value != now && seen[k].logs < 8) {
            seen[k].value = now; seen[k].logs++;
            fprintf(stderr, "[totk] cheat dry run \"%s\": %#llx (%u bytes) is %s%#llx, would write %#llx%s\n", ctx ? (const char *)ctx : "?",
                    (unsigned long long)a, n, readable ? "" : "unreadable ", (unsigned long long)now, (unsigned long long)v, ok ? "" : " (refused)");
        }
        return ok;
    }
    (void)ctx;
    return guest_access(a, (void *)(uintptr_t)in, n, 1);
}

/* One tick: as in Atmosphère, the master code runs first and the enabled cheats after it on the same registers (which
   start at 0 every tick); each cheat keeps its own conditional blocks and loops. */
static void *cheat_thread(void *arg) {
    (void)arg;
    pthread_setname_np("cheats");
    static CheatVm vm;   // static registers (C3) persist across ticks
    for (;;) {
        usleep(33000);
        CheatEnv env = {.ctx = NULL, .read = guest_read, .write = guest_write};
        uint64_t b[3], s[3];
        runtime_guest_regions(b, s);
        env.base[0] = (uintptr_t)main_base; env.base[1] = b[0]; env.base[2] = b[1]; env.base[3] = b[2]; env.base[4] = 0;
        env.keys = runtime_input_buttons();
        cheat_vm_tick(&vm);
        for (int pass = 0; pass < 2; pass++)
            for (int k = 0; k < ncheat; k++) {
                int st = atomic_load(&state[k]);
                if (cheats[k].master != !pass || !(st & CHEAT_ON) || (st & CHEAT_REFUSED) || !runs[k] || !cheats[k].n) continue;
                if ((st & CHEAT_AT_LAUNCH) && !(st & CHEAT_APPLIED)) continue;   // its code part is not in: wait for the next launch
                char err[112] = "";
                env.ctx = cheats[k].name;
                if (!cheat_run(&cheats[k], &vm, &env, err, sizeof err) && err[0] && strcmp(err, problem[k])) {   // a failed read (pointer not set up yet) is silent
                    pthread_mutex_lock(&mu); snprintf(problem[k], sizeof problem[k], "%s", err); pthread_mutex_unlock(&mu);
                    fprintf(stderr, "[totk] cheat \"%s\": %s\n", cheats[k].name, err);
                }
            }
    }
    return NULL;
}

/* --- chosen cheats ----------------------------------------------------------------------------------------------- */
static void load_choices(void) {
    char path[1100], line[CHEAT_NAME + 4];
    if (!state_file(path, sizeof path)) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        for (int k = 0; k < ncheat; k++) if (!cheats[k].master && !strcmp(cheats[k].name, line)) atomic_fetch_or(&state[k], CHEAT_ON);
    }
    fclose(f);
}
static void save_choices(void) {
    char path[1100], tmp[1120];
    if (!state_file(path, sizeof path)) return;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    for (int k = 0; k < ncheat; k++) if (!cheats[k].master && (atomic_load(&state[k]) & CHEAT_ON)) fprintf(f, "%s\n", cheats[k].name);
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
    FILE *f = fopen(cheat_file(), "rb");
    if (!f) return;
    static char text[1 << 20];
    size_t got = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[got] = 0;
    cheats = calloc(CHEAT_MAX, sizeof *cheats);
    char warn[160];
    if (!cheats) { fprintf(stderr, "[totk] cheats: out of memory\n"); return; }
    ncheat = cheat_parse(text, cheats, CHEAT_MAX, warn, sizeof warn);
    if (warn[0]) fprintf(stderr, "[totk] cheats: %s: %s\n", cheat_file(), warn);
    const char *dr = getenv("TOTK_CHEATS_DRYRUN");
    dry_run = dr && !strcmp(dr, "1");
    if (dry_run) fprintf(stderr, "[totk] cheats: dry run (memory cheats log their targets and write nothing)\n");
    main_base = m->base; main_size = m->size; main_text = m->text_end; main_patch = m->patch_off; main_data = m->data_off; main_key = m->key_off;
    main_rx = (m->text_end + 0xFFF) & ~0xFFFULL;   // the code and the padding after it, up to the next 4 KiB segment
    main_module = m;
    load_choices();
    int applied = 0, at_launch = 0, nmasters = 0, seen = 0;
    static const Cheat *masters[CHEAT_MAX];
    for (int k = 0; k < ncheat; k++) if (cheats[k].master && !cheats[k].error[0]) masters[nmasters++] = &cheats[k];
    for (int k = 0; k < ncheat; k++) {
        if (!cheats[k].n && !cheats[k].error[0]) continue;   // titles and notes
        if (!cheats[k].master) listed[nlisted++] = k; else atomic_fetch_or(&state[k], CHEAT_ON);
        if (cheats[k].error[0]) {   // this section did not parse; the others are unaffected
            snprintf(problem[k], sizeof problem[k], "%s", cheats[k].error);
            atomic_fetch_or(&state[k], CHEAT_REFUSED); fprintf(stderr, "[totk] cheat \"%s\" refused: %s\n", cheats[k].name, problem[k]); continue;
        }
        int code = launch_code(k, masters, cheats[k].master ? seen++ : nmasters);   // a master code sees the ones before it
        if (code < 0) { atomic_fetch_or(&state[k], CHEAT_REFUSED); fprintf(stderr, "[totk] cheat \"%s\" refused: %s\n", cheats[k].name, problem[k]); continue; }
        at_launch += code;
        applied += !!(atomic_load(&state[k]) & CHEAT_APPLIED);
    }
    fprintf(stderr, "[totk] cheats: %d in %s (%d patch code at launch, %d of them applied now)\n", nlisted, cheat_file(), at_launch, applied);
    pthread_t th;
    if (pthread_create(&th, NULL, cheat_thread, NULL) == 0) pthread_detach(th);
}
#endif
