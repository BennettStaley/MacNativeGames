/* The shared shader list, kept current by the running Mac app (docs/shader-list.md): players send the variants their game
   met (shader-list-contribution.txt), the maintainers merge them into games/totk/assets/shader_list.tsv, and every Mac app
   downloads that list (download only: the game never uploads anything) and prepares the variants it lacks from the
   player's own game files, in the background, so nobody stutters on a variant another player already reported.

     fetch     at the start (totk_shader_list_boot, from the warm-up), on a utility queue: GET the list at most once a day
               with If-None-Match / If-Modified-Since, 20 s timeout, 48 MB cap; offline just means the next start tries
               again. The file is checked strictly (totk_shader_list.h) before it replaces the previous one.
     diff      the list's V rows (shader key + deps) against the translation cache's <key>.variants.
     find      the shader bytes in the game files (totk_shader_scan.h: BNSH, agl .sharcb, SARC, zstd with the game's
               dictionaries) through the RomFS the game itself reads (mods included). The first time, every shader of
               the game is indexed (key -> file, members, offsets) and the index is kept, so later days read only the
               files a new variant needs and never look for the keys of another game version.
     prepare   after the warm-up (the title screen is up), one variant at a time: translated by a translator process of its
               own (totk_rt_prepare, utility priority, never holding the game's translation lock) and compiled with Metal
               (the warm-up's path: the shader goes to the game's shader table, so its first use is warm). It waits while
               the game is busy: a translation of the game's own in the last 3 s (a new area loading) or fewer than 20
               frames in the last half second.

   Files (TOTK_SHADER_LIST_DIR; the launcher sets <Application Support>/MacNativeGames/TOTK/shader-list, nothing happens
   without it): shader_list.tsv (the last good download), state.json (URL, ETag, Last-Modified, when checked),
   index-<RomFS identity>.tsv, failed.tsv (variants the translator could not make, not tried again for this chain).
   TOTK_SHADER_LIST_URL: another list (https, file:// or http on this Mac, for tests); TOTK_SHADER_LIST_FORCE=1: check
   even when the last check is less than a day old. The setting Interface > Download new shaders (off: never fetch,
   preparation stops) is live. iOS cannot translate at run time: everything here is the Mac's. */
#import <Foundation/Foundation.h>
#include <TargetConditionals.h>
#include <CommonCrypto/CommonDigest.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include "totk_shader_rt.h"
#include "totk_shader_list.h"
#include "totk_shader_scan.h"
#include "dmsh_chain.h"
#include "../../host/hle.h"

static _Atomic int64_t g_pending;
int64_t totk_shader_list_pending(void) { return atomic_load(&g_pending); }

#if TARGET_OS_IPHONE
void totk_shader_list_boot(void) {}
void totk_shader_list_warm_done(int (*compile)(NSString *, int)) { (void)compile; }
void totk_shader_list_set_enabled(int on) { (void)on; }
#else

#define TSL_DEFAULT_URL "https://raw.githubusercontent.com/ly05010419/MacNativeGames/main/games/totk/assets/shader_list.tsv"
enum { TSL_CHECK_EVERY_S = 24 * 3600, TSL_TIMEOUT_S = 20, TSL_RESOURCE_S = 120 };

extern int settings_get(const char *key);
extern size_t runtime_romfs_file_count(void);
extern const char *runtime_romfs_file_name(size_t i);
extern uint8_t *runtime_romfs_host_read(const char *path, size_t *n);
extern uint8_t *runtime_romfs_zs_decompress(const uint8_t *src, size_t n, size_t *out);
extern void runtime_romfs_identity(char out[65]);
extern const char *runtime_game_dir(void);
uint64_t dm_nvn_frame_count(void);

static dispatch_queue_t g_queue;
static _Atomic int g_enabled = -1, g_booted, g_warm_done, g_preparing;
static int (*g_compile)(NSString *, int);
static NSString *g_list_path;   // the list this run uses (set on g_queue)

static double now_s(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1e9; }
static dispatch_queue_t queue(void) {
    static dispatch_once_t once;
    dispatch_once(&once, ^{ g_queue = dispatch_queue_create("totk.shader-list", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, 0)); });
    return g_queue;
}
static int enabled(void) {
    int e = atomic_load(&g_enabled);
    if (e < 0) { int v = settings_get("interface.download_shaders"); e = v != 0; atomic_store(&g_enabled, e); }
    return e;
}
static NSString *list_dir(void) {
    const char *d = getenv("TOTK_SHADER_LIST_DIR");
    return d && *d ? @(d) : nil;
}
static NSString *sha256_hex(const void *p, size_t n) {
    uint8_t d[32]; CC_SHA256(p, (CC_LONG)n, d);
    NSMutableString *s = [NSMutableString stringWithCapacity:64];
    for (int i = 0; i < 32; i++) [s appendFormat:@"%02x", d[i]];
    return s;
}
static NSString *sha256_hex_pair(const uint8_t *control, const uint8_t *code, size_t n) {   // the runtime's shader key
    CC_SHA256_CTX c; CC_SHA256_Init(&c); CC_SHA256_Update(&c, control, TSS_CONTROL); CC_SHA256_Update(&c, code, (CC_LONG)n);
    uint8_t d[32]; CC_SHA256_Final(d, &c);
    NSMutableString *s = [NSMutableString stringWithCapacity:64];
    for (int i = 0; i < 32; i++) [s appendFormat:@"%02x", d[i]];
    return s;
}

/* ---------- fetch ---------- */
@interface TSLFetch : NSObject <NSURLSessionDataDelegate>
@property(nonatomic) NSMutableData *data;
@property(nonatomic) NSHTTPURLResponse *response;
@property(nonatomic) NSError *error;
@property(nonatomic) NSString *refused;
@property(nonatomic) dispatch_semaphore_t done;
@end
@implementation TSLFetch
- (void)URLSession:(NSURLSession *)s dataTask:(NSURLSessionDataTask *)t didReceiveResponse:(NSURLResponse *)r completionHandler:(void (^)(NSURLSessionResponseDisposition))h {
    (void)s; (void)t;
    self.response = [r isKindOfClass:[NSHTTPURLResponse class]] ? (NSHTTPURLResponse *)r : nil;
    if (r.expectedContentLength > TSL_MAX_BYTES) { self.refused = [NSString stringWithFormat:@"%lld bytes announced", r.expectedContentLength]; h(NSURLSessionResponseCancel); return; }
    h(NSURLSessionResponseAllow);
}
- (void)URLSession:(NSURLSession *)s dataTask:(NSURLSessionDataTask *)t didReceiveData:(NSData *)d {
    (void)s;
    if (self.data.length + d.length > TSL_MAX_BYTES) { self.refused = @"larger than the cap"; [t cancel]; return; }
    [self.data appendData:d];
}
- (void)URLSession:(NSURLSession *)s task:(NSURLSessionTask *)t didCompleteWithError:(NSError *)e {
    (void)s; (void)t; self.error = e; dispatch_semaphore_signal(self.done);
}
- (void)URLSession:(NSURLSession *)s task:(NSURLSessionTask *)t willPerformHTTPRedirection:(NSHTTPURLResponse *)r newRequest:(NSURLRequest *)q completionHandler:(void (^)(NSURLRequest *))h {
    (void)s; (void)t; (void)r;
    h([q.URL.scheme isEqualToString:@"https"] ? q : nil);   // never leave https
}
@end

static NSMutableDictionary *state_read(NSString *dir) {
    NSData *d = [NSData dataWithContentsOfFile:[dir stringByAppendingPathComponent:@"state.json"]];
    id s = d.length && d.length < 65536 ? [NSJSONSerialization JSONObjectWithData:d options:0 error:nil] : nil;
    return [s isKindOfClass:[NSDictionary class]] ? [s mutableCopy] : [NSMutableDictionary dictionary];
}
static void state_write(NSString *dir, NSDictionary *s) {
    NSData *d = [NSJSONSerialization dataWithJSONObject:s options:NSJSONWritingSortedKeys | NSJSONWritingPrettyPrinted error:nil];
    if (d) [d writeToFile:[dir stringByAppendingPathComponent:@"state.json"] atomically:YES];
}
static int url_allowed(NSURL *u) {
    if ([u.scheme isEqualToString:@"https"] && u.host.length) return 1;
    if ([u.scheme isEqualToString:@"file"]) return 1;   // tests (TOTK_SHADER_LIST_URL)
    return [u.scheme isEqualToString:@"http"] && ([u.host isEqualToString:@"127.0.0.1"] || [u.host isEqualToString:@"localhost"]);
}
/* The list to use this run (the new download, or the previous one when nothing new came), or nil. */
static NSString *fetch(void) {
    NSString *dir = list_dir();
    if (!dir) { LOG("[shader-list] TOTK_SHADER_LIST_DIR is not set: no shared list this run"); return nil; }
    NSFileManager *fm = NSFileManager.defaultManager;
    if (![fm createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil]) { LOG("[shader-list] cannot create %s", dir.UTF8String); return nil; }
    const char *ue = getenv("TOTK_SHADER_LIST_URL"), *force = getenv("TOTK_SHADER_LIST_FORCE");
    NSURL *url = [NSURL URLWithString:ue && *ue ? @(ue) : @TSL_DEFAULT_URL];
    if (!url || !url_allowed(url)) { LOG("[shader-list] %s is not a list address this app fetches (https only)", ue ? ue : "?"); return nil; }
    NSString *path = [dir stringByAppendingPathComponent:@"shader_list.tsv"];
    NSMutableDictionary *st = state_read(dir);
    BOOL same = [st[@"url"] isEqual:url.absoluteString] && [fm fileExistsAtPath:path];
    double checked = [st[@"checked"] doubleValue], now = (double)time(NULL);
    if (same && now - checked >= 0 && now - checked < TSL_CHECK_EVERY_S && !(force && !strcmp(force, "1"))) {
        LOG("[shader-list] checked %.1f h ago; the next check is in %.1f h", (now - checked) / 3600, (TSL_CHECK_EVERY_S - (now - checked)) / 3600);
        return path;
    }
    double t0 = now_s();
    NSData *body = nil; NSInteger status = 0; NSString *etag = nil, *modified = nil;
    if (url.isFileURL) {
        NSDictionary *a = [fm attributesOfItemAtPath:url.path error:nil];
        if (a && [a fileSize] <= TSL_MAX_BYTES) body = [NSData dataWithContentsOfURL:url];
        status = body ? 200 : 404;
        if (body && same && [sha256_hex(body.bytes, body.length) isEqualToString:st[@"sha256"]]) status = 304;
    } else {
        NSMutableURLRequest *rq = [NSMutableURLRequest requestWithURL:url cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:TSL_TIMEOUT_S];
        [rq setValue:@"NativeTOTK-shader-list/1" forHTTPHeaderField:@"User-Agent"];
        if (same && [st[@"etag"] isKindOfClass:[NSString class]]) [rq setValue:st[@"etag"] forHTTPHeaderField:@"If-None-Match"];
        if (same && [st[@"last_modified"] isKindOfClass:[NSString class]]) [rq setValue:st[@"last_modified"] forHTTPHeaderField:@"If-Modified-Since"];
        NSURLSessionConfiguration *c = NSURLSessionConfiguration.ephemeralSessionConfiguration;
        c.URLCache = nil; c.HTTPCookieStorage = nil; c.HTTPShouldSetCookies = NO; c.URLCredentialStorage = nil;
        c.timeoutIntervalForRequest = TSL_TIMEOUT_S; c.timeoutIntervalForResource = TSL_RESOURCE_S; c.waitsForConnectivity = NO;
        c.TLSMinimumSupportedProtocolVersion = tls_protocol_version_TLSv12;
        TSLFetch *f = [TSLFetch new]; f.data = [NSMutableData data]; f.done = dispatch_semaphore_create(0);
        NSURLSession *session = [NSURLSession sessionWithConfiguration:c delegate:f delegateQueue:nil];
        [[session dataTaskWithRequest:rq] resume];
        dispatch_semaphore_wait(f.done, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(TSL_RESOURCE_S + 5) * NSEC_PER_SEC));
        [session invalidateAndCancel];
        if (f.refused) { LOG("[shader-list] download refused (%s); keeping the previous list", f.refused.UTF8String); return same ? path : nil; }
        if (f.error || !f.response) {
            LOG("[shader-list] download failed (%s); offline is fine, the next start tries again",
                f.error ? f.error.localizedDescription.UTF8String : "no response");
            return same ? path : nil;
        }
        status = f.response.statusCode; body = f.data;
        etag = f.response.allHeaderFields[@"Etag"] ?: f.response.allHeaderFields[@"ETag"];
        modified = f.response.allHeaderFields[@"Last-Modified"];
    }
    if (status == 304) {
        st[@"checked"] = @(now); state_write(dir, st);
        LOG("[shader-list] the shared list has not changed since the last download (%s, %.2f s)", url.isFileURL ? "same file" : "HTTP 304", now_s() - t0);
        return path;
    }
    if (status != 200) { LOG("[shader-list] download answered HTTP %ld; keeping the previous list", (long)status); return same ? path : nil; }
    NSString *why = nil; NSDictionary<NSString *, NSArray<NSString *> *> *list = tsl_parse(body, &why);
    if (!list) { LOG("[shader-list] [red] rejected the downloaded list: %s; keeping the previous one", why.UTF8String); return same ? path : nil; }
    if (![body writeToFile:path atomically:YES]) { LOG("[shader-list] cannot write %s", path.UTF8String); return nil; }
    NSUInteger variants = 0; for (NSString *k in list) variants += list[k].count;
    NSMutableDictionary *ns = [@{@"url": url.absoluteString, @"checked": @(now), @"sha256": sha256_hex(body.bytes, body.length),
                                 @"variants": @(variants), @"shaders": @(list.count)} mutableCopy];
    if ([etag isKindOfClass:[NSString class]] && etag.length < 256) ns[@"etag"] = etag;
    if ([modified isKindOfClass:[NSString class]] && modified.length < 256) ns[@"last_modified"] = modified;
    state_write(dir, ns);
    LOG("[shader-list] downloaded the shared list: %lu KB, %lu variants over %lu shaders, in %.2f s",
        (unsigned long)(body.length >> 10), (unsigned long)variants, (unsigned long)list.count, now_s() - t0);
    return path;
}

/* ---------- waiting while the game is busy ---------- */
/* 0 when the setting went off. The frame rate is measured over windows of at least a quarter of a second that include
   the work done in between (a call within a window keeps the last verdict). */
static double g_paused_s;
static int wait_idle(void) {
    static uint64_t t_prev, f_prev; static int idle;
    double t0 = now_s();
    for (;;) {
        if (!enabled()) return 0;
        uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW), f = dm_nvn_frame_count();
        if (!t_prev || now - t_prev > 2000000000ull || f < f_prev) { t_prev = now; f_prev = f; idle = 0; usleep(250000); continue; }
        if (now - t_prev < 250000000ull) {
            uint64_t last = totk_rt_last_translation_ns();
            if (idle && !(last && now - last < 3000000000ull)) break;
            usleep((useconds_t)((250000000ull - (now - t_prev)) / 1000)); continue;
        }
        double fps = (double)(f - f_prev) / ((now - t_prev) / 1e9);
        t_prev = now; f_prev = f;
        uint64_t last = totk_rt_last_translation_ns();
        int busy = (last && now - last < 3000000000ull) || fps < 20;   // a new area loading, a loading screen, a heavy scene
        idle = !busy;
        if (!busy) break;
        usleep(250000);
    }
    g_paused_s += now_s() - t0 > 0.3 ? now_s() - t0 : 0;
    return 1;
}

/* ---------- the game's shader index ---------- */
typedef struct { int stage; NSString *source, *members; uint64_t control, code, length; } Where;
@interface TSLWhere : NSObject { @public Where w; } @end
@implementation TSLWhere @end

typedef struct { CFMutableDictionaryRef index; unsigned files, shaders; } ScanCtx;
static void scan_found(void *ctx, const char *source, const char *members, int stage, const uint8_t *control, const uint8_t *code,
                       uint32_t length, size_t control_off, size_t code_off) {
    ScanCtx *s = ctx;
    CC_SHA256_CTX c; CC_SHA256_Init(&c); CC_SHA256_Update(&c, control, TSS_CONTROL); CC_SHA256_Update(&c, code, length);
    uint8_t d[32]; CC_SHA256_Final(d, &c);
    char key[65]; for (int i = 0; i < 32; i++) snprintf(key + 2 * i, 3, "%02x", d[i]);
    NSString *k = @(key);
    s->shaders++;
    if (CFDictionaryContainsKey(s->index, (__bridge CFStringRef)k)) return;
    TSLWhere *w = [TSLWhere new];
    w->w = (Where){stage, @(source), @(members), control_off, code_off, length};
    CFDictionarySetValue(s->index, (__bridge CFStringRef)k, (__bridge CFTypeRef)w);
}
/* The game files that can hold shaders: those the build found them in (the app's seed-manifest.json lists them), else
   every file under the folders they live in. */
static NSArray<NSString *> *scan_sources(void) {
    const char *sd = getenv("TOTK_SHADER_SEED_DIR");
    NSString *seed = sd && *sd ? @(sd) : [totk_rt_root() stringByAppendingPathComponent:@"build/shader-seed"];
    for (NSString *m in @[[seed stringByAppendingPathComponent:@"seed-manifest.json"],
                          [seed stringByAppendingFormat:@"/%s/seed-manifest.json", runtime_game_dir()]]) {
        NSData *d = [NSData dataWithContentsOfFile:m];
        NSDictionary *j = d ? [NSJSONSerialization JSONObjectWithData:d options:0 error:nil] : nil;
        NSArray *a = [j isKindOfClass:[NSDictionary class]] ? j[@"archives"] : nil;
        if (![a isKindOfClass:[NSArray class]] || !a.count) continue;
        NSMutableArray *out = [NSMutableArray array];
        for (NSDictionary *e in a) if ([e isKindOfClass:[NSDictionary class]] && [e[@"path"] isKindOfClass:[NSString class]]) [out addObject:e[@"path"]];
        if (out.count) return out;
    }
    NSMutableArray *out = [NSMutableArray array];
    for (size_t i = 0, n = runtime_romfs_file_count(); i < n; i++) {
        const char *p = runtime_romfs_file_name(i);
        if (!strncmp(p, "Shader/", 7) || !strncmp(p, "Effect/", 7) || !strncmp(p, "Lib/", 4) || !strncmp(p, "UI/", 3) ||
            !strcmp(p, "Pack/Bootup.Nin_NX_NVN.pack.zs")) [out addObject:@(p)];
    }
    return out;
}
static NSString *modules_dir(void) { return [totk_rt_root() stringByAppendingFormat:@"/rewritten/%s", runtime_game_dir()]; }
static uint8_t *source_bytes(NSString *source, size_t *n) {   // a RomFS path or module:<name>, as stored
    if ([source hasPrefix:@"module:"]) {
        NSString *name = [source substringFromIndex:7];
        if (!tsl_word(name, 0)) return NULL;
        NSData *d = [NSData dataWithContentsOfFile:[modules_dir() stringByAppendingFormat:@"/%@.patched.bin", name] options:NSDataReadingMappedIfSafe error:nil];
        if (!d) return NULL;
        uint8_t *b = malloc(d.length ? d.length : 1); if (!b) return NULL;
        memcpy(b, d.bytes, d.length); *n = d.length; return b;
    }
    return runtime_romfs_host_read(source.UTF8String, n);
}
static NSString *index_path(NSString *dir) {
    char id[65]; runtime_romfs_identity(id);
    return [dir stringByAppendingFormat:@"/index-%s.tsv", id];
}
static NSDictionary<NSString *, TSLWhere *> *index_load(NSString *path) {
    NSString *text = [NSString stringWithContentsOfFile:path encoding:NSASCIIStringEncoding error:nil];
    if (![text hasPrefix:@"# shader index v1\n"]) return nil;
    NSMutableDictionary *out = [NSMutableDictionary dictionary];
    for (NSString *line in [text componentsSeparatedByString:@"\n"]) {
        if (!line.length || [line hasPrefix:@"#"]) continue;
        NSArray<NSString *> *f = [line componentsSeparatedByString:@"\t"];
        if (f.count != 7 || !tsl_key(f[0]) || !([f[1] isEqualToString:@"0"] || [f[1] isEqualToString:@"1"] || [f[1] isEqualToString:@"5"]) ||
            !tsl_source(f[2]) || !tsl_hex(f[4].UTF8String, f[4].length, 1, 12) || !tsl_hex(f[5].UTF8String, f[5].length, 1, 12) ||
            !tsl_hex(f[6].UTF8String, f[6].length, 1, 8)) return nil;   // not ours: index again
        TSLWhere *w = [TSLWhere new];
        w->w = (Where){f[1].intValue, f[2], [f[3] isEqualToString:@"-"] ? @"" : f[3], strtoull(f[4].UTF8String, NULL, 16),
                       strtoull(f[5].UTF8String, NULL, 16), strtoull(f[6].UTF8String, NULL, 16)};
        out[f[0]] = w;
    }
    return out;
}
static NSDictionary<NSString *, TSLWhere *> *index_build(NSString *path) {
    double t0 = now_s();
    NSMutableArray<NSString *> *sources = [scan_sources() mutableCopy];
    for (NSString *m in [[NSFileManager.defaultManager contentsOfDirectoryAtPath:modules_dir() error:nil] sortedArrayUsingSelector:@selector(compare:)])
        if ([m hasSuffix:@".patched.bin"]) [sources addObject:[@"module:" stringByAppendingString:[m substringToIndex:m.length - 12]]];
    LOG("[shader-list] indexing this game's shaders once (%lu files)…", (unsigned long)sources.count);
    ScanCtx ctx = {CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks), 0, 0};
    tss_scan s = {scan_found, runtime_romfs_zs_decompress, &ctx, 0};
    unsigned missing = 0;
    for (NSUInteger i = 0; i < sources.count; i++) @autoreleasepool {
        if (i % 64 == 0 && !wait_idle()) { CFRelease(ctx.index); return nil; }
        size_t n = 0; uint8_t *raw = source_bytes(sources[i], &n);
        if (!raw) { missing++; continue; }
        tss_file(&s, sources[i].UTF8String, raw, n); free(raw); ctx.files++;
    }
    NSDictionary *index = CFBridgingRelease(ctx.index);
    NSMutableString *text = [NSMutableString stringWithFormat:@"# shader index v1\n# %s: every shader of this game's files (key, stage, file, members, control, code, length)\n", DMSH_CHAIN];
    for (NSString *key in [index.allKeys sortedArrayUsingSelector:@selector(compare:)]) {
        Where w = ((TSLWhere *)index[key])->w;
        [text appendFormat:@"%@\t%d\t%@\t%@\t%llx\t%llx\t%llx\n", key, w.stage, w.source, w.members.length ? w.members : @"-",
            (unsigned long long)w.control, (unsigned long long)w.code, (unsigned long long)w.length];
    }
    [text writeToFile:path atomically:YES encoding:NSASCIIStringEncoding error:nil];
    LOG("[shader-list] indexed %lu shaders in %u files (%u records skipped, %u files missing) in %.1f s",
        (unsigned long)index.count, ctx.files, s.skipped, missing, now_s() - t0);
    return index;
}

/* ---------- prepare ---------- */
/* Rows of (key, deps) under a "# <chain>" line: failed.tsv in the list folder (variants this chain's translator could not
   make: not tried again), shader-list-done.tsv in the translation cache (list entries already prepared, also those whose
   translation gave the deps of a variant the cache had: not translated again every start; it goes with the cache). */
static NSMutableSet<NSString *> *rows_read(NSString *path) {
    NSMutableSet *out = [NSMutableSet set];
    NSString *text = [NSString stringWithContentsOfFile:path encoding:NSASCIIStringEncoding error:nil];
    NSString *head = [NSString stringWithFormat:@"# %s\n", DMSH_CHAIN];
    if (![text hasPrefix:head]) return out;   // another translation chain may make them differently
    for (NSString *line in [text componentsSeparatedByString:@"\n"]) if (line.length && ![line hasPrefix:@"#"]) [out addObject:line];
    return out;
}
static void rows_add(NSString *path, NSString *key, NSString *deps) {
    NSString *head = [NSString stringWithFormat:@"# %s\n", DMSH_CHAIN];
    NSString *text = [NSString stringWithContentsOfFile:path encoding:NSASCIIStringEncoding error:nil];
    if (![text hasPrefix:head]) [head writeToFile:path atomically:YES encoding:NSASCIIStringEncoding error:nil];
    FILE *f = fopen(path.fileSystemRepresentation, "a");
    if (f) { fprintf(f, "%s\t%s\n", key.UTF8String, deps.UTF8String); fclose(f); }
}
static void prepare(void) {
    NSString *dir = list_dir(), *path = g_list_path;
    if (!dir || !path || !g_compile || !enabled()) return;
    double t0 = now_s(); g_paused_s = 0;
    NSData *data = [NSData dataWithContentsOfFile:path];
    NSString *why = nil; NSDictionary<NSString *, NSArray<NSString *> *> *list = data ? tsl_parse(data, &why) : nil;
    if (!list) { LOG("[shader-list] [red] the saved list does not read (%s); it is fetched again next time", why ? why.UTF8String : "missing"); [NSFileManager.defaultManager removeItemAtPath:[dir stringByAppendingPathComponent:@"state.json"] error:nil]; return; }
    NSString *cache = totk_rt_dir();
    NSString *failedPath = [dir stringByAppendingPathComponent:@"failed.tsv"], *donePath = [cache stringByAppendingPathComponent:@"shader-list-done.tsv"];
    NSMutableSet *skip = rows_read(failedPath); [skip unionSet:rows_read(donePath)];
    NSArray<NSArray<NSString *> *> *missing = tsl_missing(list, ^NSSet<NSString *> *(NSString *key) { return tsl_cache_deps(cache, key); });
    NSMutableArray *todo0 = [NSMutableArray array];
    for (NSArray<NSString *> *m in missing) if (![skip containsObject:[m componentsJoinedByString:@"\t"]]) [todo0 addObject:m];
    if (!todo0.count) { LOG("[shader-list] the cache has every variant of the shared list (%.1f s)", now_s() - t0); return; }
    NSString *ip = index_path(dir);
    NSDictionary<NSString *, TSLWhere *> *index = index_load(ip);
    if (!index) index = index_build(ip);
    if (!index) return;   // turned off while indexing
    NSMutableArray<NSArray<NSString *> *> *todo = [NSMutableArray array];
    for (NSArray<NSString *> *m in todo0) if (index[m[0]]) [todo addObject:m];
    if (!todo.count) {
        LOG("[shader-list] shader list: nothing new for this game (%lu listed variants are of shaders another game version has) (%.1f s)",
            (unsigned long)todo0.count, now_s() - t0);
        return;
    }
    LOG("[shader-list] shader list: %lu new variants from the shared list, preparing… (%lu more listed are of shaders another game version has)",
        (unsigned long)todo.count, (unsigned long)(todo0.count - todo.count));
    atomic_store(&g_pending, (int64_t)todo.count);
    /* by file and members, so each file is read and decompressed once */
    NSMutableDictionary<NSString *, NSMutableArray<NSArray<NSString *> *> *> *groups = [NSMutableDictionary dictionary];
    for (NSArray<NSString *> *m in todo) {
        Where w = index[m[0]]->w;
        NSString *g = [NSString stringWithFormat:@"%@\t%@", w.source, w.members];
        if (!groups[g]) groups[g] = [NSMutableArray array];
        [groups[g] addObject:m];
    }
    unsigned translated = 0, same = 0, bad = 0, compiled = 0, broken = 0;
    for (NSString *g in [groups.allKeys sortedArrayUsingSelector:@selector(compare:)]) @autoreleasepool {
        NSArray<NSString *> *gs = [g componentsSeparatedByString:@"\t"];
        size_t n = 0; uint8_t *raw = source_bytes(gs[0], &n), *flat = NULL;
        size_t fn = n; const uint8_t *file = raw;
        if (raw && tss_is_zstd(raw, n)) { flat = runtime_romfs_zs_decompress(raw, n, &fn); file = flat; }
        size_t mn = 0; uint8_t *owned = NULL;
        const uint8_t *bytes = file ? tss_member_bytes(runtime_romfs_zs_decompress, file, fn, gs[1].UTF8String, &mn, &owned) : NULL;
        for (NSArray<NSString *> *m in groups[g]) @autoreleasepool {
            if (!wait_idle()) {
                free(owned); free(flat); free(raw);
                LOG("[shader-list] stopped (Download new shaders is off); %lld variants left for later", (long long)atomic_load(&g_pending));
                atomic_store(&g_pending, 0);
                return;
            }
            Where w = index[m[0]]->w;
            NSString *err = nil, *variant = nil; int fresh = 0;
            if (!bytes || w.control + TSS_CONTROL > mn || w.code > mn || w.length > mn - w.code) err = @"the game file changed since it was indexed";
            else if (![sha256_hex_pair(bytes + w.control, bytes + w.code, w.length) isEqualToString:m[0]]) err = @"the bytes there no longer hash to the key";
            else {
                NSMutableDictionary *answers = [@{@"tex": @{}, @"img": @{}, @"attr": @{}, @"fout": @{}} mutableCopy];
                [answers addEntriesFromDictionary:[NSJSONSerialization JSONObjectWithData:[m[1] dataUsingEncoding:NSUTF8StringEncoding] options:0 error:nil]];
                variant = totk_rt_prepare(m[0], w.stage, bytes + w.control, bytes + w.code, mn - w.code, answers, &fresh, &err);
            }
            if (!variant) {
                bad++;
                LOG("[shader-list] could not prepare %s %s: %s", m[0].UTF8String, m[1].UTF8String, err.UTF8String);
                if ([err hasPrefix:@"translation failed"]) rows_add(failedPath, m[0], m[1]);
                if ([err containsString:@"translator"] && ++broken >= 3) {   // the translator itself does not work: not item by item
                    free(owned); free(flat); free(raw); atomic_store(&g_pending, 0);
                    LOG("[shader-list] [red] stopped: the translator does not work (%s); the next start tries again", err.UTF8String);
                    return;
                }
            } else {
                if (fresh) translated++; else same++;
                broken = 0;
                rows_add(donePath, m[0], m[1]);
                if (g_compile([variant stringByAppendingString:@"_b"], w.stage)) compiled++;
            }
            int64_t left = atomic_fetch_sub(&g_pending, 1) - 1;
            if ((translated + same + bad) % 100 == 0 && left > 0)
                LOG("[shader-list] preparing: %u done, %lld left (%.0f s)", translated + same + bad, (long long)left, now_s() - t0);
        }
        free(owned); free(flat); free(raw);
    }
    atomic_store(&g_pending, 0);
    LOG("[shader-list] shader list: ready: %u variants from the shared list prepared (%u translated, %u same as a cached variant, %u failed), %u compiled with Metal, in %.1f s (%.1f s of it waiting while the game was busy)",
        translated + same, translated, same, bad, compiled, now_s() - t0, g_paused_s);
}

static void schedule_prepare(void) {
    if (atomic_exchange(&g_preparing, 1)) return;
    dispatch_async(queue(), ^{ @autoreleasepool { prepare(); } atomic_store(&g_preparing, 0); });
}
void totk_shader_list_boot(void) {
    if (!enabled()) { LOG("[shader-list] Download new shaders is off: no shared list"); return; }
    if (atomic_exchange(&g_booted, 1)) return;
    dispatch_async(queue(), ^{ @autoreleasepool { g_list_path = fetch(); } });
}
void totk_shader_list_warm_done(int (*compile)(NSString *, int)) {
    g_compile = compile;
    atomic_store(&g_warm_done, 1);
    if (enabled() && atomic_load(&g_booted)) schedule_prepare();   // after the fetch: the queue is serial
}
/* tests/shader_list_fetch/probe.m: the steps one at a time, on the calling thread. */
NSString *totk_shader_list_fetch_now(void) { return fetch(); }
void totk_shader_list_prepare_now(NSString *list, int (*compile)(NSString *, int)) { g_list_path = list; g_compile = compile; prepare(); }
void totk_shader_list_set_enabled(int on) {
    int was = atomic_exchange(&g_enabled, on ? 1 : 0);
    LOG("[shader-list] Download new shaders: %s", on ? "on" : "off");
    if (on && was != 1) {
        totk_shader_list_boot();
        if (atomic_load(&g_warm_done)) schedule_prepare();
    }
}
#endif
