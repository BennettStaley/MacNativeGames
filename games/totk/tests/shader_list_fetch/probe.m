// The shared shader list as the Mac app downloads and prepares it (totk_shader_list.h / totk_shader_scan.h /
// totk_shader_list.m), with the game's side stubbed. Usage:
//   probe units <a real shader_list.tsv> <scratch dir>   parser (malformed input), scanner, diff, preparation; prints JSON
//   probe fetch <list dir> <url>                          one fetch (TOTK_SHADER_LIST_FORCE from the environment); prints JSON
#import <Foundation/Foundation.h>
#include <CommonCrypto/CommonDigest.h>
#include <time.h>
#include "totk_shader_list.h"
#include "totk_shader_scan.h"

NSString *totk_shader_list_fetch_now(void);
void totk_shader_list_prepare_now(NSString *list, int (*compile)(NSString *, int));

static int failures, checked;
static void check(int ok, const char *what) { checked++; if (!ok) { failures++; fprintf(stderr, "FAIL: %s\n", what); } }

/* ---------- the game's side, stubbed ---------- */
static NSString *g_cache, *g_root;
static NSMutableDictionary<NSString *, NSData *> *g_romfs;   // RomFS path -> bytes
static int g_reads, g_prepared, g_compiled;
static NSMutableArray<NSString *> *g_prepare_calls;
int settings_get(const char *key) { (void)key; return 1; }
size_t runtime_romfs_file_count(void) { return g_romfs.count; }
const char *runtime_romfs_file_name(size_t i) { return [g_romfs.allKeys sortedArrayUsingSelector:@selector(compare:)][i].UTF8String; }
uint8_t *runtime_romfs_host_read(const char *path, size_t *n) {
    NSData *d = g_romfs[@(path)]; if (!d) return NULL;
    g_reads++;
    uint8_t *b = malloc(d.length); memcpy(b, d.bytes, d.length); *n = d.length; return b;
}
uint8_t *runtime_romfs_zs_decompress(const uint8_t *src, size_t n, size_t *out) { (void)src; (void)n; (void)out; return NULL; }
void runtime_romfs_identity(char out[65]) { memset(out, 'a', 64); out[64] = 0; }
const char *runtime_game_dir(void) { return "v143"; }
uint64_t dm_nvn_frame_count(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 16666667ull; }   // 60 frames a second
uint64_t totk_rt_last_translation_ns(void) { return 0; }
NSString *totk_rt_root(void) { return g_root; }
NSString *totk_rt_dir(void) { return g_cache; }
static NSString *hex_sha(const uint8_t *a, size_t an, const uint8_t *b, size_t bn) {
    CC_SHA256_CTX c; CC_SHA256_Init(&c); CC_SHA256_Update(&c, a, (CC_LONG)an); CC_SHA256_Update(&c, b, (CC_LONG)bn);
    uint8_t d[32]; CC_SHA256_Final(d, &c);
    NSMutableString *s = [NSMutableString string]; for (int i = 0; i < 32; i++) [s appendFormat:@"%02x", d[i]]; return s;
}
NSString *totk_rt_prepare(NSString *shader, int stage, const uint8_t *control, const uint8_t *code, size_t code_len,
                          NSDictionary *answers, int *fresh, NSString **why) {
    uint32_t len; memcpy(&len, control + 0x704, 4);
    check(len <= code_len && [hex_sha(control, 0x880, code, len) isEqualToString:shader], "prepare gets the shader's own control and code");
    NSString *deps = tsl_canonical(answers);
    [g_prepare_calls addObject:[NSString stringWithFormat:@"%@ %d %@", shader, stage, deps]];
    NSString *path = [g_cache stringByAppendingFormat:@"/%@.variants", shader];
    NSMutableArray *vs = [NSMutableArray arrayWithArray:[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:path] ?: [@"[]" dataUsingEncoding:NSUTF8StringEncoding] options:0 error:nil]];
    NSString *variant = [NSString stringWithFormat:@"%@-%012x", shader, (unsigned)vs.count];
    [vs addObject:@{@"variant": variant, @"deps": answers}];
    [[NSJSONSerialization dataWithJSONObject:vs options:0 error:nil] writeToFile:path atomically:YES];
    g_prepared++; *fresh = 1; (void)why;
    return variant;
}
static int compile(NSString *variant_b, int stage) { (void)stage; g_compiled += [variant_b hasSuffix:@"_b"]; return 1; }

/* ---------- synthetic game files ---------- */
static void put32(NSMutableData *d, size_t at, uint32_t v) { memcpy((uint8_t *)d.mutableBytes + at, &v, 4); }
static void put64(NSMutableData *d, size_t at, uint64_t v) { memcpy((uint8_t *)d.mutableBytes + at, &v, 8); }
/* A BNSH file with one shader of this stage; *key: its runtime key. */
static NSMutableData *bnsh(int stage, uint8_t fill, NSString **key) {
    NSMutableData *d = [NSMutableData dataWithLength:0xb80];
    uint8_t *p = d.mutableBytes;
    memcpy(p, "BNSH", 4); p[12] = 0xff; p[13] = 0xfe; put32(d, 28, 0xb80); memcpy(p + 96, "grsc", 4);
    put32(d, 124, 1); put64(d, 128, 0x100);
    put64(d, 0x100 + 16, 0x140);
    put64(d, 0x140 + (stage == 0 ? 8 : stage == 1 ? 40 : 48), 0x180);
    put64(d, 0x188, 0x200); put64(d, 0x190, 0xa80); put32(d, 0x198, 0x100); put32(d, 0x19c, 0x880);
    memset(p + 0x200, fill, 0x880); put32(d, 0x200 + 0x700, 0x80); put32(d, 0x200 + 0x704, 0x100); put32(d, 0x200 + 0x714, (uint32_t)stage);
    for (int i = 0; i < 0x100; i++) p[0xa80 + i] = (uint8_t)(fill * 7 + i);
    *key = hex_sha(p + 0x200, 0x880, p + 0xa80, 0x100);
    return d;
}
/* A SARC package of (name, bytes) pairs. */
static NSMutableData *sarc(NSArray<NSArray *> *members) {
    NSMutableData *names = [NSMutableData data], *body = [NSMutableData data];
    NSMutableArray *offs = [NSMutableArray array], *ranges = [NSMutableArray array];
    for (NSArray *m in members) {
        [offs addObject:@(names.length)];
        NSData *n = [m[0] dataUsingEncoding:NSUTF8StringEncoding]; [names appendData:n]; [names appendBytes:"\0\0\0\0" length:4 - n.length % 4];
        while (body.length % 8) [body appendBytes:"\0" length:1];
        [ranges addObject:@[@(body.length), @(body.length + [m[1] length])]]; [body appendData:m[1]];
    }
    size_t count = members.count, sfnt = 0x20 + 16 * count, start = (sfnt + 8 + names.length + 15) & ~(size_t)15;
    NSMutableData *d = [NSMutableData dataWithLength:start];
    uint8_t *p = d.mutableBytes;
    memcpy(p, "SARC", 4); p[4] = 0x14; p[6] = 0xff; p[7] = 0xfe; put32(d, 12, (uint32_t)start);
    memcpy(p + 0x14, "SFAT", 4); p[0x18] = 0x0c; p[0x1a] = (uint8_t)count;
    for (size_t i = 0; i < count; i++) {
        put32(d, 0x20 + 16 * i + 4, 0x01000000 | (uint32_t)([offs[i] unsignedIntValue] / 4));
        put32(d, 0x20 + 16 * i + 8, [ranges[i][0] unsignedIntValue]); put32(d, 0x20 + 16 * i + 12, [ranges[i][1] unsignedIntValue]);
    }
    memcpy(p + sfnt, "SFNT", 4); p[sfnt + 4] = 8;
    memcpy(p + sfnt + 8, names.bytes, names.length);
    [d appendData:body]; put32(d, 8, (uint32_t)d.length);
    return d;
}
/* An agl package (BAHS) with one record. */
static NSMutableData *bahs(uint8_t fill, NSString **key) {
    size_t start = 0x40, control = start, record = control + 0x880, pool = 0xa00, code = 0x10;
    NSMutableData *d = [NSMutableData dataWithLength:pool + 0x200];
    uint8_t *p = d.mutableBytes;
    memcpy(p, "BAHS", 4); put32(d, 20, (uint32_t)start); put32(d, 24, 0x200); put32(d, 28, (uint32_t)pool);
    memset(p + control, fill, 0x880); put32(d, control + 0x700, 0x40); put32(d, control + 0x704, 0x80); put32(d, control + 0x714, 1);
    put64(d, record, code); put32(d, record + 8, 0x880); int32_t rel = (int32_t)control - (int32_t)record; memcpy(p + record + 12, &rel, 4);
    for (int i = 0; i < 0x80; i++) p[pool + code + i] = (uint8_t)(0x55 ^ i);
    *key = hex_sha(p + control, 0x880, p + pool + code, 0x80);
    return d;
}
typedef struct { NSMutableDictionary *found; } Found;
static void on_found(void *ctx, const char *source, const char *members, int stage, const uint8_t *control, const uint8_t *code,
                     uint32_t length, size_t control_off, size_t code_off) {
    Found *f = ctx; (void)control_off; (void)code_off;
    f->found[hex_sha(control, 0x880, code, length)] = [NSString stringWithFormat:@"%s|%s|%d", source, members, stage];
}

static NSDictionary *parse(NSString *text, NSString **why) { return tsl_parse([text dataUsingEncoding:NSUTF8StringEncoding], why); }

static void units(NSString *real, NSString *scratch) {
    NSString *H = @TSL_HEADER " (test)\n", *K = [@"" stringByPaddingToLength:64 withString:@"ab" startingAtIndex:0];
    NSString *D = @"{\"attr\":{},\"fout\":{\"0\":\"float\"},\"img\":{},\"tex\":{\"10\":\"2darray\",\"9\":\"2d\"}}";
    NSString *why = nil;
    /* the documented rows */
    NSString *good = [NSString stringWithFormat:@"%@# comment\n\nV\t%@\t%@\nV\t%@\t%@\nV\t%@\t{}\nL\t%@\tLib/agl/x.sarc.zs/a.sharcb\t1f\t2000\t800\nL\t%@\tmodule:main\t10\t20\t30\n"
                      "S\t%@-0123456789ab_b\t1\tm\nC\t%@-0123456789ab_b\ttm\nR\t00ff\tt\n", H, K, D, K, D, K, K, K, K, K];
    NSDictionary *l = parse(good, &why);
    check(l.count == 1 && [l[K] count] == 2 && [l[K][0] isEqualToString:D], "a list of every row kind: only V rows kept, repeats once");
    check(parse([good stringByReplacingOccurrencesOfString:@"\n" withString:@"\r\n"], &why).count == 1, "CRLF line ends read");
    check(parse(@"", &why) == nil, "empty: rejected");
    check(parse([NSString stringWithFormat:@"V\t%@\t%@\n", K, D], &why) == nil && [why containsString:@"header"], "no header: rejected");
    NSArray<NSString *> *bad = @[
        [NSString stringWithFormat:@"V\t%@\t%@\textra", K, D],                       // a fourth field
        [NSString stringWithFormat:@"V\t%@\t%@", K.uppercaseString, D],               // an uppercase key
        [NSString stringWithFormat:@"V\t%@\t%@", [K substringToIndex:63], D],         // a short key
        [NSString stringWithFormat:@"V\t%@\t{\"tex\":{\"9\":\"2d\"},\"attr\":{}}", K], // keys out of order (not canonical)
        [NSString stringWithFormat:@"V\t%@\t{\"tex\": {\"9\":\"2d\"}}", K],          // a space
        [NSString stringWithFormat:@"V\t%@\t{\"local\":{\"0\":\"1\"}}", K],          // not a deps kind
        [NSString stringWithFormat:@"V\t%@\t{\"tex\":{\"9\":2}}", K],                 // a number
        [NSString stringWithFormat:@"V\t%@\t{\"tex\":{\"9\":\"2\\u0064\"}}", K],      // an escape
        [NSString stringWithFormat:@"V\t%@\t{\"tex\":{\"09\":\"2d\"}}", K],           // a leading zero
        [NSString stringWithFormat:@"V\t%@\t{\"tex\":{\"x\":\"2d\"}}", K],            // a slot that is no number
        [NSString stringWithFormat:@"V\t%@\t{\"tex\":{\"9\":\"2D!\"}}", K],           // a strange type word
        [NSString stringWithFormat:@"V\t%@\t{\"tex\":{\"9\":\"2d\"},\"tex\":{}}", K], // a repeated key
        [NSString stringWithFormat:@"V\t%@\t{\"tex\":", K],                           // broken JSON
        [NSString stringWithFormat:@"V\t%@\t[]", K],                                  // not an object
        [NSString stringWithFormat:@"V\t%@\t{\"tex\":{\"9\":\"%@\"}}", K, [@"" stringByPaddingToLength:4100 withString:@"a" startingAtIndex:0]],   // too long
        [NSString stringWithFormat:@"L\t%@\t../etc/passwd\t1\t2\t3", K],              // a path out of the RomFS
        [NSString stringWithFormat:@"L\t%@\tmodule:Main\t1\t2\t3", K],                // a module name with a capital
        [NSString stringWithFormat:@"L\t%@\ta/b\t1\t2\t12345678901", K],              // an offset too large
        [NSString stringWithFormat:@"S\t%@-0123456789ab_b\t3\tm", K],                 // a stage that is none
        [NSString stringWithFormat:@"S\t%@-0123456789ab\t1\tm", K],                   // no _b
        [NSString stringWithFormat:@"C\t%@-0123456789ab_b\tx", K],                    // bad flags
        @"R\t0f0\tm",                                                                 // odd hex
        @"X\twhatever",                                                               // no such row
        @"V",                                                                         // a lone kind
        [NSString stringWithFormat:@"V\t%@\t%@ ", K, D],                              // trailing space
    ];
    for (NSString *row in bad) {
        why = nil;
        NSDictionary *r = parse([NSString stringWithFormat:@"%@V\t%@\t%@\n%@\n", H, K, D, row], &why);
        if (r) fprintf(stderr, "accepted: %s\n", row.UTF8String);
        check(r == nil && why.length, "a malformed row rejects the whole file");
    }
    NSMutableData *bin = [[H dataUsingEncoding:NSUTF8StringEncoding] mutableCopy]; [bin appendBytes:"V\t\0\n" length:4];
    check(tsl_parse(bin, &why) == nil, "a NUL byte: rejected");
    bin = [[H dataUsingEncoding:NSUTF8StringEncoding] mutableCopy]; [bin appendBytes:"# \xc3\xa9\n" length:5];
    check(tsl_parse(bin, &why) == nil, "a non-ASCII byte: rejected");
    check(parse([NSString stringWithFormat:@"%@#%@\n", H, [@"" stringByPaddingToLength:9000 withString:@"x" startingAtIndex:0]], &why) == nil, "a line over 8 KB: rejected");
    NSMutableData *huge = [NSMutableData dataWithLength:TSL_MAX_BYTES + 1]; memcpy(huge.mutableBytes, TSL_HEADER, strlen(TSL_HEADER));
    check(tsl_parse(huge, &why) == nil && [why containsString:@"MB"], "over 48 MB: rejected");
    NSData *repo = [NSData dataWithContentsOfFile:real];
    NSDictionary *rl = tsl_parse(repo, &why);
    NSUInteger rv = 0; for (NSString *k in rl) rv += [rl[k] count];
    check(rl.count > 1000 && rv >= rl.count, "the repository's shared list parses");
    if (!rl) fprintf(stderr, "repository list: %s\n", why.UTF8String);
    /* canonical deps from a cache's index, whatever its key order */
    check([tsl_canonical(@{@"tex": @{@"9": @"2d", @"10": @"3d"}, @"img": @{@"1": @{@"type": @"2d", @"fmt": @"R32F"}}}) isEqualToString:@"{\"img\":{\"1\":{\"fmt\":\"R32F\",\"type\":\"2d\"}},\"tex\":{\"10\":\"3d\",\"9\":\"2d\"}}"],
          "canonical deps: sorted, compact");
    /* the difference with a cache */
    NSString *cache = [scratch stringByAppendingPathComponent:@"cache"];
    [NSFileManager.defaultManager removeItemAtPath:scratch error:nil];
    [NSFileManager.defaultManager createDirectoryAtPath:cache withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *K2 = [@"" stringByPaddingToLength:64 withString:@"cd" startingAtIndex:0];
    [@"[{\"variant\":\"x\",\"deps\":{\"tex\":{\"9\":\"2d\",\"10\":\"2darray\"},\"fout\":{\"0\":\"float\"},\"img\":{},\"attr\":{}}}]" writeToFile:[cache stringByAppendingFormat:@"/%@.variants", K] atomically:YES encoding:NSUTF8StringEncoding error:nil];
    NSDictionary *list = @{K: @[D, @"{}"], K2: @[@"{}"]};
    NSArray *miss = tsl_missing(list, ^NSSet<NSString *> *(NSString *key) { return tsl_cache_deps(cache, key); });
    check(miss.count == 2 && [miss[0] isEqual:(@[K, @"{}"])] && [miss[1] isEqual:(@[K2, @"{}"])], "missing: what the cache lacks, the cached state matched whatever its key order");
    /* the scanner */
    NSString *ka, *kb, *kc, *kd;
    NSMutableData *a = bnsh(1, 0x21, &ka), *b = bnsh(5, 0x22, &kb), *c = bahs(0x23, &kc), *e = bnsh(0, 0x24, &kd);
    NSMutableData *blob = [NSMutableData dataWithLength:0x30]; [blob appendData:e]; [blob appendData:[NSMutableData dataWithLength:7]];   // an embedded BNSH
    NSMutableData *pkg = sarc(@[@[@"Shader/b.bnsh", b], @[@"inner.pack", sarc(@[@[@"agl.sharcb", c], @[@"fx.bin", blob]])]]);
    Found f = {[NSMutableDictionary dictionary]};
    tss_scan s = {on_found, NULL, &f, 0};
    tss_file(&s, "Shader/a.bnsh", a.bytes, a.length);
    tss_file(&s, "Pack/p.pack", pkg.bytes, pkg.length);
    check([f.found[ka] isEqual:@"Shader/a.bnsh||1"], "a BNSH file: its fragment shader");
    check([f.found[kb] isEqual:@"Pack/p.pack|Shader/b.bnsh|5"], "a SARC member (a name with a slash): its compute shader");
    check([f.found[kc] isEqual:@"Pack/p.pack|inner.pack|agl.sharcb|1"], "an agl package two packages deep");
    check([f.found[kd] isEqual:@"Pack/p.pack|inner.pack|fx.bin|0"], "a BNSH embedded in other bytes");
    check(f.found.count == 4 && s.skipped == 0, "nothing else found, nothing skipped");
    size_t mn = 0; uint8_t *owned = NULL;
    const uint8_t *mb = tss_member_bytes(NULL, pkg.bytes, pkg.length, "inner.pack|fx.bin", &mn, &owned);
    check(mb && mn == blob.length && !memcmp(mb, blob.bytes, mn), "member bytes: two levels down");
    free(owned);
    check(tss_member_bytes(NULL, pkg.bytes, pkg.length, "inner.pack|nope", &mn, &owned) == NULL, "member bytes: a missing member");
    NSMutableData *cut = [a mutableCopy]; put32(cut, 0x198, 0x10000);   // code size past the file
    Found g = {[NSMutableDictionary dictionary]}; tss_scan s2 = {on_found, NULL, &g, 0};
    tss_file(&s2, "x", cut.bytes, cut.length);
    check(g.found.count == 0 && s2.skipped == 1, "a record out of range: skipped, not read");
    NSMutableData *trunc = [pkg mutableCopy]; trunc.length = pkg.length - 0x400;
    Found h = {[NSMutableDictionary dictionary]}; tss_scan s3 = {on_found, NULL, &h, 0};
    tss_file(&s3, "y", trunc.bytes, trunc.length);   // a cut package: its members' ranges fail, nothing read past the end
    check(h.found.count == 0, "a truncated SARC: nothing");
    /* preparing from a list: index, find, translate, compile; the second time only the index is read */
    g_cache = cache; g_root = scratch; g_prepare_calls = [NSMutableArray array];
    g_romfs = [@{@"Shader/a.bnsh": a, @"Effect/p.pack": pkg, @"Sound/x.bwav": [NSData dataWithBytes:"RIFF" length:4]} mutableCopy];
    NSString *dir = [scratch stringByAppendingPathComponent:@"list"];
    [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    setenv("TOTK_SHADER_LIST_DIR", dir.UTF8String, 1);
    NSString *other = [@"" stringByPaddingToLength:64 withString:@"ef" startingAtIndex:0];
    NSString *listText = [NSString stringWithFormat:@"%@V\t%@\t{\"tex\":{\"3\":\"3d\"}}\nV\t%@\t{}\nV\t%@\t{\"fout\":{\"1\":\"uint\"}}\nV\t%@\t{}\n", H, ka, ka, kc, other];
    NSString *lp = [dir stringByAppendingPathComponent:@"shader_list.tsv"];
    [listText writeToFile:lp atomically:YES encoding:NSUTF8StringEncoding error:nil];
    [@"[{\"variant\":\"y\",\"deps\":{}}]" writeToFile:[cache stringByAppendingFormat:@"/%@.variants", ka] atomically:YES encoding:NSUTF8StringEncoding error:nil];
    totk_shader_list_prepare_now(lp, compile);
    check(g_prepared == 2 && g_compiled == 2, "two new variants of this game prepared and compiled (the cached one and the other version's skipped)");
    NSString *calls = [[g_prepare_calls sortedArrayUsingSelector:@selector(compare:)] componentsJoinedByString:@"\n"];
    NSString *want = [[@[[NSString stringWithFormat:@"%@ 1 {\"attr\":{},\"fout\":{},\"img\":{},\"tex\":{\"3\":\"3d\"}}", ka],
                         [NSString stringWithFormat:@"%@ 1 {\"attr\":{},\"fout\":{\"1\":\"uint\"},\"img\":{},\"tex\":{}}", kc]]
                       sortedArrayUsingSelector:@selector(compare:)] componentsJoinedByString:@"\n"];
    check([calls isEqualToString:want], "each with the listed state over empty defaults, as the build answers");
    NSArray *idx = [[NSFileManager.defaultManager contentsOfDirectoryAtPath:dir error:nil] filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"SELF BEGINSWITH 'index-'"]];
    check(idx.count == 1, "the game's shader index is kept");
    int reads = g_reads; g_prepared = g_compiled = 0;
    totk_shader_list_prepare_now(lp, compile);
    check(g_prepared == 0 && g_reads == reads, "the second time: nothing to prepare, no game file read (the other version's key is known absent)");
}

int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc == 4 && !strcmp(argv[1], "units")) {
            units(@(argv[2]), @(argv[3]));
            printf("{\"status\":\"%s\",\"checked\":%d,\"failures\":%d}\n", failures ? "fail" : "pass", checked, failures);
            return failures != 0;
        }
        if (argc == 4 && !strcmp(argv[1], "fetch")) {
            setenv("TOTK_SHADER_LIST_DIR", argv[2], 1); setenv("TOTK_SHADER_LIST_URL", argv[3], 1);
            NSString *path = totk_shader_list_fetch_now();
            NSData *st = [NSData dataWithContentsOfFile:[@(argv[2]) stringByAppendingPathComponent:@"state.json"]];
            id state = st ? [NSJSONSerialization JSONObjectWithData:st options:0 error:nil] : nil;
            NSData *out = [NSJSONSerialization dataWithJSONObject:@{@"path": path ?: [NSNull null], @"state": state ?: [NSNull null]} options:0 error:nil];
            printf("%s\n", [[NSString alloc] initWithData:out encoding:NSUTF8StringEncoding].UTF8String);
            return 0;
        }
        fprintf(stderr, "usage: probe units <list> <scratch> | probe fetch <dir> <url>\n");
        return 2;
    }
}
