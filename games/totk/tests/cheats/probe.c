/* Unit test for runtime/cheat_vm.c (the Atmosphère cheat format): parsing, instruction lengths, pointer chains, static
   and register stores, button conditions, register conditionals with else, loops, arithmetic, and the failure paths
   (unreadable pointer: silent; refused write and malformed program: reported). Memory is a local buffer whose first
   0x100 bytes stand for read-only code. A negative control checks that a wrong chain offset writes nothing.
   Prints one JSON line; exit 1 on failure. */
#include "cheat_vm.h"
#include "guest_range.h"
#include "cheat_code.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL: " __VA_ARGS__); fputc('\n', stderr); } } while (0)

static uint8_t mem[0x10000] __attribute__((aligned(16)));
static int writes;
static int in_mem(uint64_t a, unsigned n) { uint64_t b = (uintptr_t)mem; return a >= b && a - b + n <= sizeof mem; }
static int rd(void *ctx, uint64_t a, void *out, unsigned n) { (void)ctx; if (!in_mem(a, n)) return 0; memcpy(out, (void *)(uintptr_t)a, n); return 1; }
static int wr(void *ctx, uint64_t a, const void *in, unsigned n) {
    (void)ctx;
    if (!in_mem(a, n) || a - (uintptr_t)mem < 0x100) return 0;   // "code" is not writable
    memcpy((void *)(uintptr_t)a, in, n); writes++; return 1;
}
static void put64(unsigned off, uint64_t v) { memcpy(mem + off, &v, 8); }
static void put32(unsigned off, uint32_t v) { memcpy(mem + off, &v, 4); }
static uint32_t get32(unsigned off) { uint32_t v; memcpy(&v, mem + off, 4); return v; }
static uint64_t get64(unsigned off) { uint64_t v; memcpy(&v, mem + off, 8); return v; }

static Cheat cs[8];
static CheatVm vm;
static CheatEnv env_for(uint64_t keys) { return (CheatEnv){.base = {(uintptr_t)mem, (uintptr_t)mem + 0x8000, 0, 0, 0}, .keys = keys, .read = rd, .write = wr}; }
/* Parses one section and runs it on fresh registers; -1 when the section did not parse. */
static int run_text(const char *text, uint64_t keys, char *err, size_t errn) {
    int n = cheat_parse(text, cs, 8, NULL, 0);
    if (n != 1 || cs[0].error[0]) { snprintf(err, errn, "parse: %d %s", n, n == 1 ? cs[0].error : ""); return -1; }
    CheatEnv env = env_for(keys);
    cheat_vm_tick(&vm);
    return cheat_run(&cs[0], &vm, &env, err, errn);
}

/* Spec coverage added after the audit: parser tolerance, offset-register forms, master registers, C3, C4, float
   arithmetic, memory type 4, debug-log lengths and the step cap. */
static void extra(char *err, size_t errn) {
    static Cheat p[8];
    char warn[160], prog[256];
    /* One bad section does not reject the file; BOM, CRLF and comments are accepted; an unterminated name says its line. */
    int n = cheat_parse("\xEF\xBB\xBF[Good]\r\n// note\r\n04000000 00000200 0000002A # trailing\r\n[Bad]\r\n0400000G 00000200 1\r\n"
                        "[Also good]\r\n20000000\r\n[Open\r\n", p, 8, warn, sizeof warn);
    CHECK(n == 4 && !p[0].error[0] && p[0].n == 3 && p[1].error[0] && !p[2].error[0] && p[2].n == 1, "per-section errors: n %d (%s / %s)", n, p[0].error, p[1].error);
    CHECK(n == 4 && strstr(p[3].error, "line 8"), "unterminated section names its line (%s)", n == 4 ? p[3].error : "");
    CHECK(n >= 1 && !strcmp(p[0].name, "Good"), "BOM stripped from the first name (%s)", p[0].name);
    /* Duplicate names get distinct keys; a long name is cut at a character boundary. */
    char text[600] = "[Same]\n[Same]\n[Same (2)]\n[";
    size_t at = strlen(text);
    for (int i = 0; i < 100; i++) { memcpy(text + at, "\xC3\xA9", 2); at += 2; }   // 100 x e-acute (200 bytes)
    memcpy(text + at, "]\n", 3);
    n = cheat_parse(text, p, 8, warn, sizeof warn);
    char k0[CHEAT_NAME + 16], k1[CHEAT_NAME + 16], k2[CHEAT_NAME + 16];
    cheat_choice_name(&p[0], k0, sizeof k0); cheat_choice_name(&p[1], k1, sizeof k1); cheat_choice_name(&p[2], k2, sizeof k2);
    CHECK(n == 4 && !strcmp(p[0].name, p[1].name) && strcmp(k0, k1) && strcmp(k1, k2) && strcmp(k0, k2) && !strcmp(k0, "Same"),
          "repeated names kept, choice keys distinct (%s | %s | %s)", k0, k1, k2);
    size_t ln = strlen(p[3].name);
    CHECK(ln < CHEAT_NAME && ln % 2 == 0 && ln > 120, "long name cut at a UTF-8 boundary (%zu bytes)", ln);
    /* Too many sections: the rest are ignored with a warning, not a failure. */
    n = cheat_parse("[a]\n[b]\n[c]\n", p, 2, warn, sizeof warn);
    CHECK(n == 2 && strstr(warn, "ignored"), "sections past the limit: %d (%s)", n, warn);
    /* Reserved / unknown instructions and zero-count loops are refused at parse time. */
    n = cheat_parse("[B]\nB0000000\n[F]\nF0000000\n[Loop0]\n300F0000 00000000\n310F0000\n", p, 8, warn, sizeof warn);
    CHECK(n == 3 && p[0].error[0] && p[1].error[0] && p[2].error[0], "reserved / unknown / zero loops refused at parse (%s | %s | %s)", p[0].error, p[1].error, p[2].error);

    /* Type 1 with an offset register (1TMCXrAA, X = 1): compares [heap + reg[r] + A]. */
    put32(0x8140, 7); put32(0x8100, 0); put32(0x8200, 0);
    int r = run_text("[C1r]\n400A0000 00000000 00000040\n14151A00 00000100 00000007\n04100000 00000200 00000001\n20000000\n", 0, err, errn);
    CHECK(r == 1 && get32(0x8200) == 1, "type 1 with offset register (%s)", err);
    put32(0x8200, 0);   /* negative control: without the register the compare reads heap+0x100, which is 0 */
    r = run_text("[C1]\n400A0000 00000000 00000040\n14150000 00000100 00000007\n04100000 00000200 00000001\n20000000\n", 0, err, errn);
    CHECK(r == 1 && get32(0x8200) == 0, "type 1 without offset register skips the block");

    /* Type 5, I = 2 (reg[S] + A) and I = 3 (base(M) + reg[S] + A). */
    put64(0x900, 0x1122334455667788ull); put64(0xA00, 0);
    snprintf(prog, sizeof prog, "[L2]\n400B0000 %08X %08X\n58012B00 00000100\nA8100400 00000A00\n",
             (unsigned)(((uintptr_t)mem + 0x800) >> 32), (unsigned)((uintptr_t)mem + 0x800));
    r = run_text(prog, 0, err, errn);
    CHECK(r == 1 && get64(0xA00) == 0x1122334455667788ull, "type 5 I=2 loads reg[S]+A: %016llx (%s)", (unsigned long long)get64(0xA00), err);
    put64(0xA00, 0);
    r = run_text("[L3]\n400B0000 00000000 00000800\n58023B00 00000100\nA8200400 00000A00\n", 0, err, errn);
    CHECK(r == 1 && get64(0xA00) == 0x1122334455667788ull, "type 5 I=3 loads main+reg[S]+A: %016llx (%s)", (unsigned long long)get64(0xA00), err);
    /* Loads zero-extend from their width: a 1-byte load clears the register's upper bytes. */
    put64(0xA00, 0);
    r = run_text("[Z]\n400B0000 FFFFFFFF FFFFFFFF\n510B0000 00000900\nA8B00400 00000A00\n", 0, err, errn);
    CHECK(r == 1 && get64(0xA00) == 0x88, "1-byte load zero-extends: %016llx", (unsigned long long)get64(0xA00));

    /* Master code registers carry into the cheats run after it in the same tick; a new tick clears them. */
    CheatEnv env = env_for(0);
    n = cheat_parse("{M}\n400E0000 00000000 00000A00\n[Use]\n400F0000 00000000 00000077\nA4FE0300\n", p, 8, warn, sizeof warn);
    put32(0xA00, 0);
    cheat_vm_tick(&vm);
    int ok1 = cheat_run(&p[0], &vm, &env, err, errn) && cheat_run(&p[1], &vm, &env, err, errn);
    CHECK(n == 2 && p[0].master && ok1 && get32(0xA00) == 0x77, "cheat stores at main + register set by the master code: %08x (%s)", get32(0xA00), err);
    cheat_vm_tick(&vm); put32(0xA00, 0);
    ok1 = cheat_run(&p[1], &vm, &env, err, errn);
    CHECK(!ok1 && get32(0xA00) == 0, "after a new tick without the master code the register is 0 (main+0 is code: refused)");

    /* C3 static registers: written from a register, kept across ticks, read back. */
    n = cheat_parse("[W]\n400A0000 00000000 00000123\nC300080A\n[R]\nC3000056\n", p, 8, warn, sizeof warn);
    cheat_vm_tick(&vm); r = cheat_run(&p[0], &vm, &env, err, errn);
    cheat_vm_tick(&vm);
    vm.statics[0x05] = 0xBEEF;   // an index below 0x80 is set from outside the VM
    int r2 = cheat_run(&p[1], &vm, &env, err, errn);
    CHECK(n == 2 && r == 1 && r2 == 1 && vm.statics[0x80] == 0x123 && vm.reg[6] == 0xBEEF, "static registers %llx %llx",
          (unsigned long long)vm.statics[0x80], (unsigned long long)vm.reg[6]);

    /* C4: auto-repeat 0 fires once per press, 1 while held; 64-bit masks. */
    n = cheat_parse("[Once]\nC4000000 00000001 00000000\n94000100 00000001\n20000000\n[Hold]\nC4100000 00000000 00000004\n94011100 00000001\n20000000\n", p, 8, warn, sizeof warn);
    cheat_vm_tick(&vm);
    uint64_t presses[] = {0, 1ull << 32, 1ull << 32, 0, 1ull << 32};
    for (int k = 0; k < 5; k++) {
        CheatEnv e = env_for(presses[k] | 4);
        cheat_run(&p[0], &vm, &e, err, errn); cheat_run(&p[1], &vm, &e, err, errn);
    }
    CHECK(n == 2 && !p[0].error[0] && vm.reg[0] == 2 && vm.reg[1] == 5, "C4 once-per-press %llu (want 2), held %llu (want 5) %s",
          (unsigned long long)vm.reg[0], (unsigned long long)vm.reg[1], p[0].error);

    /* Type 9 float arithmetic: single 1.5 + 2.25, double 3 * 0.5. */
    n = cheat_parse("[F]\n40010000 00000000 3FC00000\n94A21100 40100000\n40030000 40080000 00000000\n98C43100 3FE00000 00000000\n", p, 8, warn, sizeof warn);
    cheat_vm_tick(&vm); r = cheat_run(&p[0], &vm, &env, err, errn);
    float fs; uint32_t fb = (uint32_t)vm.reg[2]; memcpy(&fs, &fb, 4);
    double fd; memcpy(&fd, &vm.reg[4], 8);
    CHECK(n == 1 && !p[0].error[0] && r == 1 && fs == 3.75f && fd == 1.5, "float add %g, double mul %g (%s %s)", fs, fd, p[0].error, err);
    n = cheat_parse("[F]\n92A21100 40100000\n", p, 8, warn, sizeof warn);
    CHECK(n == 1 && p[0].error[0], "float arithmetic on 2 bytes refused at parse");

    /* Memory type 4 (non-relative): the register holds a host address. */
    snprintf(prog, sizeof prog, "[Abs]\n400C0000 %08X %08X\n044C0000 00000000 00000099\n", (unsigned)(((uintptr_t)mem + 0xB00) >> 32), (unsigned)((uintptr_t)mem + 0xB00));
    put32(0xB00, 0);
    r = run_text(prog, 0, err, errn);
    CHECK(r == 1 && get32(0xB00) == 0x99, "memory type 4 + register (%s)", err);

    /* FFF debug log forms are 2 words for operand types 0 and 2; FF0 / FF1 are one. */
    uint32_t dl[] = {0xFFF41000, 0x00000010, 0xFFF41400, 0xFF000000, 0xFF100000};
    CHECK(cheat_insn_len(dl, 5, 0) == 2 && cheat_insn_len(dl, 5, 2) == 1 && cheat_insn_len(dl, 5, 3) == 1 && cheat_insn_len(dl, 5, 4) == 1, "debug log / pause lengths");

    /* The step cap stops a long loop with a message. */
    n = cheat_parse("[Long]\n300F0000 FFFFFFFF\n310F0000\n", p, 8, warn, sizeof warn);
    cheat_vm_tick(&vm); env.max_steps = 1000;
    r = cheat_run(&p[0], &vm, &env, err, errn);
    CHECK(r == 0 && strstr(err, "steps"), "step cap (%s)", err);
}

/* Code-patch classification (cheat_code.c) on a synthetic main module: code 0..0xF00, executable padding to 0x1000,
   image (data) to 0x8000. The rewrite changed 0x200 and 0x300..0x308 and moved the window 0x400..0x410; the runtime
   patched 0x100 itself and put a veneer at 0xF00. */
static uint8_t cmem[0x8000], cimg[0x8000];
static int cimg_ok = 1;
static int cimage_word(void *ctx, uint64_t off, uint32_t *out) { (void)ctx; if (!cimg_ok || off + 4 > sizeof cimg) return 0; memcpy(out, cimg + off, 4); return 1; }
static const uint32_t cchanged[] = {0x200, 0x204, 0x300, 0x308}, cmoved[] = {0x400, 0x410};
static CheatMain cmain(void) {
    return (CheatMain){.mem = cmem, .text_end = 0xF00, .rx_end = 0x1000, .image_end = 0x8000, .changed = cchanged, .nchanged = 2,
                       .moved = cmoved, .nmoved = 1, .image_word = cimage_word};
}
static Cheat cp[4];
static CheatCode cc;
/* Scans the last section of text with the {master} sections before it. Returns the scan result; err gets the reason. */
static int scan(const char *text, const CheatWord *prior, unsigned nprior, char *err, size_t errn) {
    int n = cheat_parse(text, cp, 4, NULL, 0);
    const Cheat *masters[4]; int nm = 0;
    for (int k = 0; k < n - 1; k++) if (cp[k].master) masters[nm++] = &cp[k];
    if (n < 1 || cp[n - 1].error[0]) { snprintf(err, errn, "parse %s", n ? cp[n - 1].error : ""); return -1; }
    CheatMain m = cmain();
    return cheat_code_scan(&m, masters, nm, &cp[n - 1], prior, nprior, &cc, err, errn);
}
static int scan_word(uint32_t at, uint32_t word, char *err, size_t errn) {
    char t[96];
    snprintf(t, sizeof t, "[W]\n04000000 %08X %08X\n", at, word);
    return scan(t, NULL, 0, err, errn);
}
static uint32_t b_to(uint32_t from, uint32_t to) { return 0x14000000 | (((to - from) / 4) & 0x3FFFFFF); }

static void code_tests(void) {
    char err[160];
    for (unsigned i = 0; i < sizeof cmem; i += 4) { uint32_t v = 0xAA000000 | i; if (i >= 0xF00 && i < 0x1000) v = 0; memcpy(cmem + i, &v, 4); }
    memcpy(cimg, cmem, sizeof cimg);
    uint32_t veneer = 0x14000010, runtime_fix = 0xD503201F;
    memcpy(cmem + 0xF00, &veneer, 4); memcpy(cmem + 0x100, &runtime_fix, 4);

    int r = scan_word(0x500, 0xD503201F, err, sizeof err);
    CHECK(r == 1 && cc.n == 1 && cc.w[0].off == 0x500 && !cc.runtime, "plain code patch (%s)", err);
    /* Register offsets are followed: 0TMR with R set by type 4, and R set by the master code. */
    r = scan("[R]\n400E0000 00000000 00000100\n040E0000 00000400 D503201F\n", NULL, 0, err, sizeof err);
    CHECK(r == 1 && cc.n == 1 && cc.w[0].off == 0x500, "code patch through a known register (%s) off %llx", err, cc.n ? (unsigned long long)cc.w[0].off : 0ull);
    r = scan("{M}\n400F0000 00000000 00000600\n[R]\n040F0000 00000000 D503201F\n", NULL, 0, err, sizeof err);
    CHECK(r == 1 && cc.n == 1 && cc.w[0].off == 0x600, "code patch through a register the master code set (%s)", err);
    /* Zero-register form most real files use (040E0000 with E never set). */
    r = scan("[E]\n040E0000 00000600 D503201F\n", NULL, 0, err, sizeof err);
    CHECK(r == 1 && cc.n == 1 && cc.w[0].off == 0x600, "register E is 0 at the start (%s)", err);
    /* An unknown register (loaded from memory) leaves the write to the runtime guard. */
    r = scan("[U]\n580E0000 00007000\n040E0000 00000600 D503201F\n", NULL, 0, err, sizeof err);
    CHECK(r == 1 && cc.n == 0 && cc.runtime, "unknown register offset: runtime only (%s)", err);
    /* Conditional and looped code patches are refused; a mixed cheat (code + button + data) is split. */
    r = scan("[C]\n80000001\n04000000 00000500 D503201F\n20000000\n", NULL, 0, err, sizeof err);
    CHECK(r == 0 && strstr(err, "condition"), "conditional code patch refused (%s)", err);
    r = scan("[L]\n30000000 00000002\n040F0000 00000500 D503201F\n31000000\n", NULL, 0, err, sizeof err);
    CHECK(r == 0 && strstr(err, "loop"), "looped code patch refused (%s)", err);
    r = scan("[Mix]\n04000000 00000500 D503201F\n80000001\n04100000 00000010 00000001\n20000000\n", NULL, 0, err, sizeof err);
    CHECK(r == 1 && cc.n == 1 && cc.runtime, "mixed cheat: code at launch, the rest on the thread (%s)", err);
    /* Rewritten, moved and runtime-patched instructions. */
    r = scan_word(0x200, 0xD503201F, err, sizeof err);
    CHECK(r == 0 && strstr(err, "rewrite changed"), "rewritten instruction refused (%s)", err);
    r = scan("[W8]\n08000000 000002FC D503201F D503201F\n", NULL, 0, err, sizeof err);
    CHECK(r == 0 && strstr(err, "0x300"), "64-bit write running onto a rewritten word refused (%s)", err);
    r = scan_word(0x408, 0xD503201F, err, sizeof err);
    CHECK(r == 0 && strstr(err, "rewrite changed"), "instruction inside a moved window refused (%s)", err);
    r = scan_word(0x100, 0xD503201F, err, sizeof err);
    CHECK(r == 0 && strstr(err, "runtime"), "runtime-patched instruction refused (%s)", err);
    r = scan_word(0xF00, 0xD503201F, err, sizeof err);
    CHECK(r == 0 && strstr(err, "runtime"), "runtime veneer in the padding refused (%s)", err);
    r = scan_word(0xF04, 0xD503201F, err, sizeof err);
    CHECK(r == 1, "free padding usable as a cave (%s)", err);
    r = scan_word(0x502, 0xD503201F, err, sizeof err);
    CHECK(r == 0 && strstr(err, "part of"), "unaligned code write refused (%s)", err);
    r = scan("[H]\n02000000 00000500 0000D503\n", NULL, 0, err, sizeof err);
    CHECK(r == 0 && strstr(err, "part of"), "2-byte code write refused (%s)", err);
    /* Branch and PC-relative targets of written instructions. */
    r = scan_word(0x500, b_to(0x500, 0x2000), err, sizeof err);
    CHECK(r == 0 && strstr(err, "out of"), "B out of the code refused (%s)", err);
    r = scan_word(0x500, b_to(0x500, 0x404), err, sizeof err);
    CHECK(r == 0 && strstr(err, "moved"), "B into a moved window refused (%s)", err);
    r = scan_word(0x500, b_to(0x500, 0x400), err, sizeof err);
    CHECK(r == 1, "B to the head of a moved window is fine (%s)", err);
    r = scan_word(0x500, b_to(0x500, 0xF10), err, sizeof err);
    CHECK(r == 0 && strstr(err, "padding"), "B into unused padding refused (%s)", err);
    r = scan("[Cave]\n04000000 00000500 14000284\n04000000 00000F10 D65F03C0\n", NULL, 0, err, sizeof err);
    CHECK(r == 1 && cc.n == 2, "B into a cave the cheat writes (%s)", err);
    r = scan_word(0x500, 0x54000000 | ((0x3000 - 0x500) / 4) << 5, err, sizeof err);
    CHECK(r == 0 && strstr(err, "out of"), "B.cond out of the code refused (%s)", err);
    r = scan_word(0x500, 0xB4000000 | (((uint32_t)((0x404 - 0x500) / 4)) & 0x7FFFF) << 5, err, sizeof err);
    CHECK(r == 0 && strstr(err, "moved"), "CBZ backwards into a moved window refused (%s)", err);
    r = scan_word(0x500, 0x36000000 | ((0x1800 - 0x500) / 4) << 5, err, sizeof err);
    CHECK(r == 0 && strstr(err, "out of"), "TBZ out of the code refused (%s)", err);
    r = scan_word(0x500, 0x90000000 | (0x10 >> 2) << 5, err, sizeof err);   // ADRP x0, page 0x10000
    CHECK(r == 0 && strstr(err, "outside"), "ADRP outside the module refused (%s)", err);
    r = scan_word(0x500, 0x90000000 | (1u << 29), err, sizeof err);         // ADRP x0, page 0x1000
    CHECK(r == 1, "ADRP into the module's data is fine (%s)", err);
    r = scan_word(0x500, 0x58000000 | ((0x7000 - 0x500) / 4) << 5, err, sizeof err);
    CHECK(r == 1, "LDR literal into the module is fine (%s)", err);
    r = scan_word(0x500, 0x10000000 | ((0x9000 - 0x500) >> 2) << 5, err, sizeof err);
    CHECK(r == 0 && strstr(err, "outside"), "ADR outside the module refused (%s)", err);
    r = scan_word(0x500, 0xD4000001, err, sizeof err);
    CHECK(r == 0 && strstr(err, "svc"), "svc refused (%s)", err);
    r = scan_word(0x500, 0xD53BD040, err, sizeof err);
    CHECK(r == 0 && strstr(err, "thread"), "mrs tpidr_el0 refused (%s)", err);
    /* A word another cheat already wrote: same value fine, different value is a conflict; its own write is accepted. */
    CheatWord prior[] = {{0x500, 0xD503201F, 3}};
    uint32_t nop = 0xD503201F;
    memcpy(cmem + 0x500, &nop, 4);
    r = scan("[P]\n04000000 00000500 D503201F\n", prior, 1, err, sizeof err);
    CHECK(r == 1 && cc.conflict == -1, "same word as an earlier cheat (%s)", err);
    r = scan("[P]\n04000000 00000500 52800020\n", prior, 1, err, sizeof err);
    CHECK(r == 1 && cc.conflict == 3, "conflict reported (%d)", cc.conflict);
    r = scan("[P]\n04000000 00000500 52800020\n", NULL, 0, err, sizeof err);
    CHECK(r == 0 && strstr(err, "runtime"), "a word changed by nobody known is refused (%s)", err);
    memcpy(cmem + 0x500, cimg + 0x500, 4);
    /* Fail closed without the rewrite map or the verified image. */
    CheatMain m = cmain(); m.changed = NULL; m.nchanged = 0;
    cheat_parse("[W]\n04000000 00000500 D503201F\n", cp, 4, NULL, 0);
    r = cheat_code_scan(&m, NULL, 0, &cp[0], NULL, 0, &cc, err, sizeof err);
    CHECK(r == 0 && strstr(err, "map"), "no rewrite map: refused (%s)", err);
    cimg_ok = 0;
    r = scan_word(0x500, 0xD503201F, err, sizeof err);
    CHECK(r == 0 && strstr(err, "image"), "no verified image: refused (%s)", err);
    cimg_ok = 1;
    /* Data writes into main above the code are runtime work, not code. */
    r = scan_word(0x6000, 1, err, sizeof err);
    CHECK(r == 1 && cc.n == 0 && cc.runtime, "main data write stays on the thread (%s)", err);
}

/* The memory guard (guest_range.h, used by cheats.c and services.c runtime_guest_access): only mapped alias pages, only
   the main module's data for writes, code only as compare, no overflow wrap-around. */
static void guard_tests(void) {
    static unsigned char pages[16];
    pages[2] = pages[3] = 1;
    CHECK(guest_pages_mapped(pages, 0x10000, sizeof pages * 4096, 0x12000, 0x2000), "two mapped pages");
    CHECK(!guest_pages_mapped(pages, 0x10000, sizeof pages * 4096, 0x12ff8, 0x1010), "a range running onto an unmapped page is refused");
    CHECK(!guest_pages_mapped(pages, 0x10000, sizeof pages * 4096, 0x11ffc, 8), "a range starting on an unmapped page is refused");
    CHECK(!guest_pages_mapped(pages, 0x10000, sizeof pages * 4096, 0x0FFFC, 8), "below the region is refused");
    CHECK(!guest_range_in(UINT64_MAX - 3, 8, 0, UINT64_MAX), "wrap-around refused");
    CHECK(!guest_range_in(0x1000, 0, 0, 0x10000), "empty access refused");
    /* main: code [0, 0x1000) rx to 0x4000, data from 0x8000, key page at 0xC000 */
    uint64_t b = 0x100000;
    CHECK(guest_main_access(b, 0x8000, 0x4000, 0xC000, b + 0x10, 4, 0) == 1, "main code readable");
    CHECK(guest_main_access(b, 0x8000, 0x4000, 0xC000, b + 0x10, 4, 1) == 2, "main code write is compare-only");
    CHECK(guest_main_access(b, 0x8000, 0x4000, 0xC000, b + 0x5000, 4, 1) == 0, "read-only data not writable");
    CHECK(guest_main_access(b, 0x8000, 0x4000, 0xC000, b + 0x8000, 8, 1) == 1, "data writable");
    CHECK(guest_main_access(b, 0x8000, 0x4000, 0xC000, b + 0xBFFC, 8, 1) == 0, "a write running into the key page is refused");
    CHECK(guest_main_access(b, 0x8000, 0x4000, 0xC000, b + 0xC000, 4, 0) == 0, "the key page is not readable");
    CHECK(guest_main_access(b, 0x8000, 0x4000, 0xC000, b + 0x3FFC, 8, 1) == 0, "a write straddling code and padding end is refused");
}

int main(void) {
    char err[160];
    /* Parsing: titles and notes keep n = 0, {master} is marked, words are collected per section. */
    static Cheat p[8];
    int n = cheat_parse("[Game v0 BID=0123]\n\n[One]\n04000000 00000200 0000002A\n{Master}\n20000000\n[-NOTE- x]\n", p, 8, err, sizeof err);
    CHECK(n == 4, "parsed %d sections (want 4)", n);
    CHECK(n == 4 && p[0].n == 0 && p[1].n == 3 && p[2].master && p[2].n == 1 && p[3].n == 0, "section word counts / master flag");
    CHECK(n == 4 && !strcmp(p[1].name, "One") && !strcmp(p[3].name, "-NOTE- x"), "section names");
    n = cheat_parse("[X]\n0400000 00000200\n", p, 8, err, sizeof err);
    CHECK(n == 1 && strstr(p[0].error, "line 2"), "a 7-digit word marks its section bad (%s)", p[0].error);
    n = cheat_parse("04000000\n[A]\n", p, 8, err, sizeof err);
    CHECK(n == 1 && strstr(err, "before the first") && !p[0].error[0], "words before the first section are ignored with a warning (%s)", err);

    /* Instruction lengths. */
    uint32_t l[] = {0x08000000, 0, 0, 0, 0x580F1000, 0, 0x640F1000, 0, 0, 0x989BF000, 0xC046B400, 0xFFFFFFFF, 0xA4FA0200, 8, 0x80000004};
    unsigned want[] = {4, 0, 0, 0, 2, 0, 3, 0, 0, 1, 2, 0, 2, 0, 1};
    for (unsigned i = 0; i < sizeof l / 4; i++) if (want[i]) CHECK(cheat_insn_len(l, sizeof l / 4, i) == want[i], "length of %08x: %u", l[i], cheat_insn_len(l, sizeof l / 4, i));
    CHECK(cheat_insn_len(l, 2, 0) == 0, "a truncated instruction has length 0");

    /* Pointer chain: [main+0x400] -> A; [A+0x10] -> B; store 998 at B (type 6 with increment). */
    put64(0x400, (uintptr_t)mem + 0x1000); put64(0x1010, (uintptr_t)mem + 0x2000); put32(0x2000, 5);
    int r = run_text("[Arrows]\n580F0000 00000400\n580F1000 00000010\n780F0000 00000000\n640F1000 00000000 000003E6\n", 0, err, sizeof err);
    CHECK(r == 1 && get32(0x2000) == 998, "pointer chain store: run %d, value %u (%s)", r, get32(0x2000), err);
    /* Negative control: a wrong first offset leads to an unreadable pointer, so nothing is written and no error is reported. */
    put32(0x2000, 5); writes = 0;
    r = run_text("[Arrows]\n580F0000 00000408\n580F1000 00000010\n640F1000 00000000 000003E6\n", 0, err, sizeof err);
    CHECK(r == 0 && writes == 0 && get32(0x2000) == 5 && !err[0], "wrong offset: run %d writes %d value %u err \"%s\"", r, writes, get32(0x2000), err);

    /* Button condition: write only while X (bit 2) is held; heap base (memory type 1) + 0x10. */
    put32(0x8010, 0);
    const char *jump = "[Jump]\n80000004\n04100000 00000010 41200000\n20000000\n";
    r = run_text(jump, 0x1, err, sizeof err);
    CHECK(r == 1 && get32(0x8010) == 0, "button not held: no write (%u)", get32(0x8010));
    r = run_text(jump, 0x4 | 0x1, err, sizeof err);
    CHECK(r == 1 && get32(0x8010) == 0x41200000, "button held: heap write (%08x)", get32(0x8010));

    /* Loop over 4 slots of 8 bytes: slot value != -1 -> store 98 (and step 4), else step 8 (the "98 of everything" shape). */
    for (unsigned i = 0; i < 4; i++) { put32(0x3000 + 8 * i, i == 2 ? 0xFFFFFFFF : 1); put32(0x3004 + 8 * i, 0); }
    put64(0x500, (uintptr_t)mem + 0x3000);
    r = run_text("[Fill]\n580F0000 00000500\n300C0000 00000004\n989BF000\n540B1000 00000000\nC046B400 FFFFFFFF\n640F1000 00000000 00000062\n"
                 "780F0000 00000004\n21000000\n780F0000 00000008\n20000000\n310C0000\n", 0, err, sizeof err);
    CHECK(r == 1 && get32(0x3000) == 98 && get32(0x3008) == 98 && get32(0x3010) == 0xFFFFFFFF && get32(0x3018) == 98,
          "loop/else: run %d slots %u %u %08x %u (%s)", r, get32(0x3000), get32(0x3008), get32(0x3010), get32(0x3018), err);

    /* Register store with offset (type A, O=2) and arithmetic (type 9 immediate add, register move). */
    put64(0x600, (uintptr_t)mem + 0x4000); put32(0x4000, 0);
    r = run_text("[Store]\n580A0000 00000600\n400F0000 00000000 00000005\n940FF100 00000025\nA4FA0200 00000008\n", 0, err, sizeof err);
    CHECK(r == 1 && get32(0x4008) == 0x2A, "type A store of 5+0x25: %08x (%s)", get32(0x4008), err);
    r = run_text("[Mov]\n580A0000 00000600\n400F0000 00000001 00000002\n9899F000\nA89A0000\n", 0, err, sizeof err);
    CHECK(r == 1 && get64(0x4000) == 0x100000002ull, "register move and 64-bit store: %016llx (%s)", (unsigned long long)get64(0x4000), err);

    /* Static store into the main module: allowed above the "code", refused inside it (reported). */
    r = run_text("[Data]\n04000000 00000700 12345678\n", 0, err, sizeof err);
    CHECK(r == 1 && get32(0x700) == 0x12345678, "static main store (%s)", err);
    r = run_text("[Code]\n04000000 00000010 D503201F\n", 0, err, sizeof err);
    CHECK(r == 0 && err[0] && get32(0x10) == 0, "write into code refused and reported: run %d err \"%s\"", r, err);
    /* Malformed: unknown opcode type B and an unknown arithmetic operation (refused when parsed), a loop end without a
       start (reported when run). */
    r = run_text("[Bad]\nB0000000\n", 0, err, sizeof err);
    CHECK(r == -1 && strstr(err, "unknown"), "unknown opcode reported (%s)", err);
    r = run_text("[Bad]\n310C0000\n", 0, err, sizeof err);
    CHECK(r == 0 && err[0], "loop end without start reported (%s)", err);
    r = run_text("[Bad]\n98F0F100 00000001\n", 0, err, sizeof err);
    CHECK(r == -1 && err[0], "unknown arithmetic reported (%s)", err);
    extra(err, sizeof err);
    guard_tests();
    code_tests();

    printf("{\"status\":\"%s\",\"probe\":\"probe-cheat-vm\",\"checked\":%d,\"failures\":%d}\n", failures ? "fail" : "pass", checks, failures);
    return failures ? 1 : 0;
}
