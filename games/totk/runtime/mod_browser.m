/* The mod browser: GameBanana's public API, a small cache, downloads, unpacking and arranging a mod (runtime/mod_browser.h).
   Foundation only (Mac and iPhone / iPad); ARC. */
#import "mod_browser.h"
#import <CommonCrypto/CommonDigest.h>
#include <TargetConditionals.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#include "mods.h"

NSString *const TOTKModsChangedNotification = @"TOTKModsChanged";
NSString *const TOTKModBrowserFooter = @"Mods are downloaded straight from GameBanana (gamebanana.com) to your mods folder; nothing is "
    "re-hosted. Respect each mod's license and credit its authors.";
NSString *const TOTKModArchiveNotOnIOS = @"7z / RAR archives can't be opened on iPhone and iPad yet — install it on the Mac, or unpack "
    "it and copy the folder into Files › Tears of the Kingdom › mods";

static NSString *const GB_API = @"https://gamebanana.com/apiv11";
static NSString *const GB_TARGET = @"1.4.3";            /* the game version the port runs */
enum { GB_GAME = 7617, GB_PER_PAGE = 30 };
static const NSTimeInterval GB_LIST_TTL = 5 * 60, GB_PAGE_TTL = 15 * 60, GB_CATEGORY_TTL = 24 * 3600;
static const int64_t GB_MAX_DOWNLOAD = 4ll << 30, GB_MAX_UNPACKED = 12ll << 30;

NSArray<NSString *> *TOTKModSortLabels(void) { return @[@"Most downloaded", @"Newest", @"Recently updated", @"Most liked"]; }
static NSString *sort_token(TOTKModSort s) {
    return s == TOTKModSortNewest ? @"Generic_Newest" : s == TOTKModSortLatestUpdated ? @"Generic_LatestUpdated" :
           s == TOTKModSortMostLiked ? @"Generic_MostLiked" : @"Generic_MostDownloaded";
}

static NSError *gb_error(NSString *message) {
    return [NSError errorWithDomain:@"TOTKModBrowser" code:1 userInfo:@{NSLocalizedDescriptionKey: message}];
}

/* ---- JSON helpers: GameBanana leaves fields out or sends null ---- */

static NSString *jstr(id d, NSString *k) {
    id v = [d isKindOfClass:NSDictionary.class] ? d[k] : nil;
    return [v isKindOfClass:NSString.class] ? v : [v isKindOfClass:NSNumber.class] ? [v stringValue] : @"";
}
static int64_t jnum(id d, NSString *k) {
    id v = [d isKindOfClass:NSDictionary.class] ? d[k] : nil;
    return [v respondsToSelector:@selector(longLongValue)] ? [v longLongValue] : 0;
}
static id jobj(id d, NSString *k, Class c) {
    id v = [d isKindOfClass:NSDictionary.class] ? d[k] : nil;
    return [v isKindOfClass:c] ? v : nil;
}
static NSDate *jdate(id d, NSString *k) { int64_t t = jnum(d, k); return t ? [NSDate dateWithTimeIntervalSince1970:(double)t] : nil; }

static NSString *md5_hex(const void *bytes, size_t n, CC_MD5_CTX *ctx) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"   /* MD5 is what GameBanana publishes; it is not used for security */
    unsigned char out[CC_MD5_DIGEST_LENGTH];
    if (ctx) CC_MD5_Final(out, ctx); else CC_MD5(bytes, (CC_LONG)n, out);
#pragma clang diagnostic pop
    NSMutableString *s = [NSMutableString string];
    for (int i = 0; i < CC_MD5_DIGEST_LENGTH; i++) [s appendFormat:@"%02x", out[i]];
    return s;
}
static NSString *file_md5(NSString *path) {
    int fd = open(path.fileSystemRepresentation, O_RDONLY); if (fd < 0) return nil;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CC_MD5_CTX ctx; CC_MD5_Init(&ctx);
    unsigned char *buf = malloc(1 << 20); ssize_t n = -1;
    while (buf && (n = read(fd, buf, 1 << 20)) > 0) CC_MD5_Update(&ctx, buf, (CC_LONG)n);
#pragma clang diagnostic pop
    free(buf); close(fd);
    return n < 0 ? nil : md5_hex(NULL, 0, &ctx);
}

/* ---- HTML to plain text (descriptions, licenses) ---- */

static NSString *re_replace(NSString *s, NSString *pattern, NSString *with) {
    NSRegularExpression *re = [NSRegularExpression regularExpressionWithPattern:pattern options:NSRegularExpressionCaseInsensitive error:nil];
    return [re stringByReplacingMatchesInString:s options:0 range:NSMakeRange(0, s.length) withTemplate:with];
}

NSString *mod_browser_plain_text(NSString *html) {
    if (!html.length) return @"";
    NSString *s = re_replace(html, @"[\\r\\n\\t]+", @" ");
    s = re_replace(s, @"<(br|hr)\\b[^>]*>", @"\n");
    s = re_replace(s, @"</(p|div|h[1-6]|li|ul|ol|blockquote|tr)>", @"\n");
    s = re_replace(s, @"<(p|div|h[1-6]|ul|ol|blockquote|tr)\\b[^>]*>", @"\n");
    s = re_replace(s, @"<li\\b[^>]*>", @"• ");
    s = re_replace(s, @"<[^>]*>", @"");
    NSDictionary *named = @{@"amp": @"&", @"lt": @"<", @"gt": @">", @"quot": @"\"", @"apos": @"'", @"nbsp": @" ", @"ndash": @"–",
                            @"mdash": @"—", @"hellip": @"…", @"rsquo": @"’", @"lsquo": @"‘", @"rdquo": @"”", @"ldquo": @"“"};
    NSRegularExpression *ent = [NSRegularExpression regularExpressionWithPattern:@"&(#x[0-9a-f]+|#[0-9]+|[a-z]+);" options:NSRegularExpressionCaseInsensitive error:nil];
    NSMutableString *out = [NSMutableString string]; __block NSUInteger at = 0;
    [ent enumerateMatchesInString:s options:0 range:NSMakeRange(0, s.length) usingBlock:^(NSTextCheckingResult *m, NSMatchingFlags f, BOOL *stop) {
        (void)f; (void)stop;
        NSString *name = [s substringWithRange:[m rangeAtIndex:1]], *rep = nil;
        if ([name hasPrefix:@"#"]) {
            unsigned long c = [name hasPrefix:@"#x"] || [name hasPrefix:@"#X"] ? strtoul(name.UTF8String + 2, NULL, 16) : strtoul(name.UTF8String + 1, NULL, 10);
            if (c && c < 0x110000) { UTF32Char u = (UTF32Char)c; rep = [[NSString alloc] initWithBytes:&u length:4 encoding:NSUTF32LittleEndianStringEncoding]; }
        } else rep = named[name.lowercaseString];
        [out appendString:[s substringWithRange:NSMakeRange(at, m.range.location - at)]];
        [out appendString:rep ?: [s substringWithRange:m.range]];
        at = NSMaxRange(m.range);
    }];
    [out appendString:[s substringFromIndex:at]];
    NSMutableArray *lines = [NSMutableArray array]; BOOL blank = YES;
    for (NSString *line in [out componentsSeparatedByString:@"\n"]) {
        NSString *t = [re_replace(line, @"[ \\u00a0]{2,}", @" ") stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        if (!t.length) { if (!blank) [lines addObject:@""]; blank = YES; continue; }
        [lines addObject:t]; blank = NO;
    }
    while (lines.count && ![lines.lastObject length]) [lines removeLastObject];
    return [lines componentsJoinedByString:@"\n"];
}

/* ---- the game versions a file names ---- */

static const int GAME_VERSIONS[] = {100, 110, 111, 112, 120, 121, 140, 141, 142, 143};
enum { NVERSIONS = sizeof GAME_VERSIONS / sizeof *GAME_VERSIONS };
static int version_bit(int v) { for (int i = 0; i < NVERSIONS; i++) if (GAME_VERSIONS[i] == v) return 1 << i; return 0; }
static int minor_bits(int minor) { int m = 0; for (int i = 0; i < NVERSIONS; i++) if (GAME_VERSIONS[i] / 10 == 10 + minor) m |= 1 << i; return m; }

/* Reads "1.4.3", "v1.2.1", "1.4.x", "1.4", "143", "v100", ranges ("1.4.0-1.4.3", "140-143", "1.0.0 - v1.1.2") and "all
   (game update) versions". Only real game versions count (1.0.0 ... 1.4.3), so a mod's own "v2.6.1" is not taken for one;
   a bare "v1.4" is read as the mod's version. score: 3 = names 1.4.3 or all versions, 2 = another 1.4, 1 = says nothing,
   0 = only older versions (old = YES); a TKMM-only file (tkmm, tkcl) loses 3. */
NSString *mod_browser_versions(NSString *text, int *score, BOOL *old, BOOL *tkmm) {
    NSString *lower = [text.lowercaseString stringByReplacingOccurrencesOfString:@"_" withString:@" "];
    const char *s = lower.UTF8String ?: ""; size_t n = strlen(s);
    int mask = 0, prev_bits = 0; size_t prev_end = 0; BOOL have_prev = NO;
    for (size_t i = 0; i < n; i++) {
        if (s[i] != '1' || (i && (isdigit((unsigned char)s[i - 1]) || s[i - 1] == '.'))) continue;
        BOOL vpre = i && s[i - 1] == 'v';
        BOOL glued = i && isalnum((unsigned char)s[i - 1]) && !vpre;
        int bits = 0; size_t j = i;
        if (s[i + 1] == '.' && isdigit((unsigned char)s[i + 2])) {
            int minor = s[i + 2] - '0', patch = -1; BOOL x = NO; j = i + 3;
            if (s[j] == '.' && (isdigit((unsigned char)s[j + 1]) || s[j + 1] == 'x')) { if (s[j + 1] == 'x') x = YES; else patch = s[j + 1] - '0'; j += 2; }
            if (isdigit((unsigned char)s[j]) || glued || (patch < 0 && !x && vpre)) { i = j; continue; }
            bits = patch >= 0 ? version_bit(100 + minor * 10 + patch) : minor_bits(minor);
        } else if (isdigit((unsigned char)s[i + 1]) && isdigit((unsigned char)s[i + 2]) && !isdigit((unsigned char)s[i + 3]) && !glued) {
            int v = 100 + (s[i + 1] - '0') * 10 + (s[i + 2] - '0'); j = i + 3;
            if (v != 100 || vpre) bits = version_bit(v);
        }
        if (!bits) continue;
        size_t start = i - (vpre ? 1 : 0);
        if (have_prev && start >= prev_end && start - prev_end <= 12) {   /* a range: "a - b", "a to b" */
            NSString *mid = [[[NSString alloc] initWithBytes:s + prev_end length:start - prev_end encoding:NSUTF8StringEncoding]
                             stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
            if ([@[@"-", @"–", @"—", @"~", @"to", @"through", @"thru"] containsObject:mid ?: @""]) {
                int lo = __builtin_ctz(prev_bits), hi = 31 - __builtin_clz(bits);
                for (int k = lo; k <= hi; k++) mask |= 1 << k;
            }
        }
        mask |= bits; prev_bits = bits; prev_end = j; have_prev = YES; i = j - 1;
    }
    BOOL all = !mask && [[NSRegularExpression regularExpressionWithPattern:
        @"\\b(all|any|every)( of the)?( game| totk)?( update)? ?versions?\\b|\\ball (game )?updates\\b|\\bversion[- ]independent\\b"
        options:0 error:nil] firstMatchInString:lower options:0 range:NSMakeRange(0, lower.length)] != nil;
    BOOL tk = [lower containsString:@"tkmm"] || [lower containsString:@"tkcl"];
    int sc = all || (mask & version_bit(143)) ? 3 : (mask & minor_bits(4)) ? 2 : !mask ? 1 : 0;
    if (old) *old = mask && sc == 0;
    if (tkmm) *tkmm = tk;
    if (score) *score = sc - (tk ? 3 : 0);
    if (all) return @"all versions";
    NSMutableArray *parts = [NSMutableArray array];
    for (int minor = 0; minor <= 4; minor++) {
        int group = minor_bits(minor), have = mask & group; if (!have) continue;
        if (have == group && __builtin_popcount(group) > 1) { [parts addObject:[NSString stringWithFormat:@"1.%d.x", minor]]; continue; }
        for (int i = 0; i < NVERSIONS; i++) if (have & 1 << i)
            [parts addObject:[NSString stringWithFormat:@"%d.%d.%d", GAME_VERSIONS[i] / 100, GAME_VERSIONS[i] / 10 % 10, GAME_VERSIONS[i] % 10]];
    }
    return [parts componentsJoinedByString:@", "];
}

/* ---- model ---- */

@implementation TOTKModSummary @end

@implementation TOTKModFile
- (NSString *)archiveKind {
    NSString *e = self.name.pathExtension.lowercaseString;
    return [@[@"zip", @"7z", @"rar"] containsObject:e] ? e : @"";
}
- (BOOL)canInstallHere { return [TOTKModBrowser canOpenArchiveKind:self.archiveKind]; }
- (NSString *)versionLabel {
    if ([self.gameVersions isEqual:@"all versions"]) return @"All game versions";
    return self.gameVersions.length ? [@"For " stringByAppendingString:self.gameVersions] : @"Game version not stated";
}
- (NSString *)sizeText { return [NSByteCountFormatter stringFromByteCount:self.bytes countStyle:NSByteCountFormatterCountStyleFile]; }
- (NSString *)notice {
    NSMutableArray *lines = [NSMutableArray array];
    if (self.tkmmOnly) [lines addObject:@"TKMM .tkcl packages aren't supported yet — coming later"];
    if (self.oldVersion) [lines addObject:[NSString stringWithFormat:@"Made for %@: may not work on %@", self.gameVersions, GB_TARGET]];
    if (!self.archiveKind.length) [lines addObject:@"Not a zip, 7z or RAR archive: can't be installed from here"];
    else if (!self.canInstallHere) [lines addObject:TOTKModArchiveNotOnIOS];
    if (self.avResult.length && ![self.avResult isEqual:@"clean"]) [lines addObject:[NSString stringWithFormat:@"GameBanana's virus scan: %@", self.avResult]];
    return lines.count ? [lines componentsJoinedByString:@"\n"] : nil;
}
@end

@implementation TOTKModDetails @end

/* ---- downloads (one delegate session; progress per task) ---- */

@interface TOTKModDownload : NSObject
@property (nonatomic, copy) NSString *dest;
@property (nonatomic) int64_t expected;
@property (nonatomic, copy) void (^progress)(double);
@property (nonatomic, copy) void (^finished)(NSError *);
@property (nonatomic) BOOL saved;
@property (nonatomic, strong) NSError *saveError;
@end
@implementation TOTKModDownload @end

@interface TOTKModBrowser () <NSURLSessionDownloadDelegate>
@end

@implementation TOTKModBrowser {
    NSURLSession *session, *downloads;
    NSString *cacheDir;
    NSCache<NSString *, NSData *> *images;
    NSMutableDictionary<NSString *, NSMutableArray *> *imageWaiters;
    NSMutableDictionary<NSNumber *, TOTKModDownload *> *tasks;      /* task id -> download (delegate queue) */
    NSMutableDictionary<NSNumber *, NSURLSessionTask *> *installing; /* file id -> task (main queue) */
    NSMutableSet<NSNumber *> *cancelled;
    dispatch_queue_t work;
    NSUInteger imageWrites;
}

+ (instancetype)shared {
    static TOTKModBrowser *b; static dispatch_once_t once;
    dispatch_once(&once, ^{ b = [TOTKModBrowser new]; });
    return b;
}

+ (BOOL)canOpenArchiveKind:(NSString *)kind {
#if TARGET_OS_IPHONE
    return [kind isEqual:@"zip"];
#else
    return [@[@"zip", @"7z", @"rar"] containsObject:kind ?: @""];
#endif
}

static NSString *user_agent(void) {
    NSString *v = [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"] ?: @"1.0";
    return [NSString stringWithFormat:@"TOTK-Mac-Native/%@ (mod browser)", v];
}

- (instancetype)init {
    if (!(self = [super init])) return nil;
    const char *env = getenv("TOTK_MOD_BROWSER_CACHE");
    cacheDir = env && *env ? @(env) : [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject
                                         stringByAppendingPathComponent:@"TOTKModBrowser"];
    for (NSString *sub in @[@"json", @"img"])
        [NSFileManager.defaultManager createDirectoryAtPath:[cacheDir stringByAppendingPathComponent:sub] withIntermediateDirectories:YES attributes:nil error:nil];
    NSURLSessionConfiguration *c = NSURLSessionConfiguration.defaultSessionConfiguration;
    c.HTTPAdditionalHeaders = @{@"User-Agent": user_agent(), @"Accept": @"application/json"};
    c.HTTPMaximumConnectionsPerHost = 4; c.timeoutIntervalForRequest = 30;
    c.URLCache = nil;   /* the cache here keeps what GameBanana marks no-store */
    session = [NSURLSession sessionWithConfiguration:c];
    NSURLSessionConfiguration *d = NSURLSessionConfiguration.defaultSessionConfiguration;
    d.HTTPAdditionalHeaders = @{@"User-Agent": user_agent()}; d.URLCache = nil; d.timeoutIntervalForRequest = 60;
    NSOperationQueue *q = [NSOperationQueue new]; q.maxConcurrentOperationCount = 1;
    downloads = [NSURLSession sessionWithConfiguration:d delegate:self delegateQueue:q];
    images = [NSCache new]; images.totalCostLimit = 48 << 20;
    imageWaiters = [NSMutableDictionary dictionary]; tasks = [NSMutableDictionary dictionary];
    installing = [NSMutableDictionary dictionary]; cancelled = [NSMutableSet set];
    work = dispatch_queue_create("totk.mod-browser", DISPATCH_QUEUE_SERIAL);
    return self;
}

/* ---- requests: a short retry on 429 / 502 / 503 / 504, a disk cache for JSON ---- */

- (void)fetch:(NSString *)url attempt:(int)attempt completion:(void (^)(NSData *, NSError *))done {
    NSURL *u = [NSURL URLWithString:url];
    if (!u) { done(nil, gb_error(@"Bad address")); return; }
    [[session dataTaskWithURL:u completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
        NSInteger code = [response isKindOfClass:NSHTTPURLResponse.class] ? ((NSHTTPURLResponse *)response).statusCode : 0;
        if (!error && (code == 429 || code == 502 || code == 503 || code == 504) && attempt < 2) {
            double wait = [[(NSHTTPURLResponse *)response valueForHTTPHeaderField:@"Retry-After"] doubleValue];
            wait = wait > 0 ? MIN(wait, 10) : attempt ? 3 : 1;
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(wait * NSEC_PER_SEC)), self->work, ^{
                [self fetch:url attempt:attempt + 1 completion:done]; });
            return;
        }
        if (!error && code != 200) error = gb_error(code == 429 ? @"GameBanana asks to slow down: try again in a minute." :
                                                    [NSString stringWithFormat:@"GameBanana answered %ld.", (long)code]);
        done(error ? nil : data, error);
    }] resume];
}

- (NSString *)cachePath:(NSString *)kind url:(NSString *)url {
    NSData *d = [url dataUsingEncoding:NSUTF8StringEncoding];
    return [[cacheDir stringByAppendingPathComponent:kind] stringByAppendingPathComponent:md5_hex(d.bytes, d.length, NULL)];
}

/* JSON from the cache while it is younger than ttl, else from GameBanana; an old copy when GameBanana can't be reached. */
- (void)json:(NSString *)url ttl:(NSTimeInterval)ttl completion:(void (^)(id, NSError *))done {
    NSString *path = [self cachePath:@"json" url:url];
    NSDate *when = [NSFileManager.defaultManager attributesOfItemAtPath:path error:nil].fileModificationDate;
    NSData *cached = when ? [NSData dataWithContentsOfFile:path] : nil;
    id (^parse)(NSData *) = ^id(NSData *data) {
        id obj = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
        return [obj isKindOfClass:NSDictionary.class] && obj[@"_sErrorCode"] ? nil : obj;
    };
    if (cached && -when.timeIntervalSinceNow < ttl) {
        id obj = parse(cached);
        if (obj) { dispatch_async(dispatch_get_main_queue(), ^{ done(obj, nil); }); return; }
    }
    [self fetch:url attempt:0 completion:^(NSData *data, NSError *error) {
        id obj = parse(data);
        if (obj) [data writeToFile:path atomically:YES];
        else if (cached) obj = parse(cached);
        if (!obj && !error) error = gb_error(@"GameBanana sent something unexpected.");
        dispatch_async(dispatch_get_main_queue(), ^{ done(obj, obj ? nil : error); });
    }];
}

/* ---- lists, categories, pages ---- */

static NSDictionary *first_image(id media) {
    for (id im in jobj(media, @"_aImages", NSArray.class)) if ([im isKindOfClass:NSDictionary.class] && jstr(im, @"_sFile").length) return im;
    return nil;
}
static NSString *image_url(NSDictionary *im, NSArray *sizes) {
    for (NSString *k in sizes) if (jstr(im, k).length) return [NSString stringWithFormat:@"%@/%@", jstr(im, @"_sBaseUrl"), jstr(im, k)];
    return nil;
}

- (void)listPage:(NSInteger)page sort:(TOTKModSort)sort search:(NSString *)text category:(int64_t)category
      completion:(void (^)(NSArray<TOTKModSummary *> *, NSInteger, NSError *))done {
    NSURLComponents *u = [NSURLComponents componentsWithString:[GB_API stringByAppendingString:@"/Mod/Index"]];
    NSMutableArray *q = [NSMutableArray arrayWithArray:@[
        [NSURLQueryItem queryItemWithName:@"_aFilters[Generic_Game]" value:@(GB_GAME).stringValue],
        [NSURLQueryItem queryItemWithName:@"_nPage" value:@(MAX(1, page)).stringValue],
        [NSURLQueryItem queryItemWithName:@"_nPerpage" value:@(GB_PER_PAGE).stringValue],
        [NSURLQueryItem queryItemWithName:@"_sSort" value:sort_token(sort)]]];
    NSString *search = [text stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
    if (search.length) [q addObject:[NSURLQueryItem queryItemWithName:@"_aFilters[Generic_Name]" value:[@"contains," stringByAppendingString:search]]];
    if (category > 0) [q addObject:[NSURLQueryItem queryItemWithName:@"_aFilters[Generic_Category]" value:@(category).stringValue]];
    u.queryItems = q;
    NSString *url = [u.URL.absoluteString stringByReplacingOccurrencesOfString:@"+" withString:@"%2B"];
    [self json:url ttl:GB_LIST_TTL completion:^(id obj, NSError *error) {
        NSMutableArray *mods = [NSMutableArray array];
        for (id r in jobj(obj, @"_aRecords", NSArray.class)) {
            if (![jstr(r, @"_sModelName") isEqual:@"Mod"]) continue;
            TOTKModSummary *m = [TOTKModSummary new];
            m.modId = jnum(r, @"_idRow"); m.name = jstr(r, @"_sName"); m.version = jstr(r, @"_sVersion");
            m.author = jstr(jobj(r, @"_aSubmitter", NSDictionary.class), @"_sName");
            m.category = jstr(jobj(r, @"_aRootCategory", NSDictionary.class), @"_sName");
            m.profileURL = jstr(r, @"_sProfileUrl");
            m.updated = jdate(r, @"_tsDateUpdated") ?: jdate(r, @"_tsDateModified") ?: jdate(r, @"_tsDateAdded");
            m.likes = (NSInteger)jnum(r, @"_nLikeCount"); m.views = (NSInteger)jnum(r, @"_nViewCount");
            m.thumbURL = image_url(first_image(jobj(r, @"_aPreviewMedia", NSDictionary.class)), @[@"_sFile220", @"_sFile100", @"_sFile"]);
            [mods addObject:m];
        }
        done(mods, (NSInteger)jnum(jobj(obj, @"_aMetadata", NSDictionary.class), @"_nRecordCount"), error);
    }];
}

- (void)categories:(void (^)(NSArray<NSDictionary *> *, NSError *))done {
    NSString *url = [NSString stringWithFormat:@"%@/Mod/Categories?_idGameRow=%d&_bShowEmpty=true&_sSort=a_to_z", GB_API, GB_GAME];
    [self json:url ttl:GB_CATEGORY_TTL completion:^(id obj, NSError *error) {
        NSMutableArray *out = [NSMutableArray array];
        for (id c in [obj isKindOfClass:NSArray.class] ? obj : @[])
            if (jnum(c, @"_idRow") && jnum(c, @"_nItemCount"))
                [out addObject:@{@"id": @(jnum(c, @"_idRow")), @"name": jstr(c, @"_sName"), @"count": @(jnum(c, @"_nItemCount"))}];
        done(out, error);
    }];
}

static NSString *license_text(id page) {
    NSString *s = mod_browser_plain_text(jstr(page, @"_sLicense"));
    if (s.length) return s;
    NSDictionary *check = jobj(page, @"_aLicenseChecklist", NSDictionary.class);
    NSMutableArray *parts = [NSMutableArray array];
    for (NSArray *k in @[@[@"yes", @"Allowed"], @[@"ask", @"Ask the author first"], @[@"no", @"Not allowed"]]) {
        NSArray *items = jobj(check, k[0], NSArray.class);
        if (items.count) [parts addObject:[NSString stringWithFormat:@"%@: %@.", k[1], [items componentsJoinedByString:@"; "]]];
    }
    return parts.count ? [parts componentsJoinedByString:@"\n"] : @"See the mod's page on GameBanana.";
}

- (void)details:(int64_t)modId completion:(void (^)(TOTKModDetails *, NSError *))done {
    NSString *url = [NSString stringWithFormat:@"%@/Mod/%lld/ProfilePage", GB_API, modId];
    [self json:url ttl:GB_PAGE_TTL completion:^(id p, NSError *error) {
        if (!p) { done(nil, error); return; }
        TOTKModDetails *m = [TOTKModDetails new];
        m.modId = jnum(p, @"_idRow") ?: modId; m.name = jstr(p, @"_sName"); m.version = jstr(p, @"_sVersion");
        m.author = jstr(jobj(p, @"_aSubmitter", NSDictionary.class), @"_sName");
        m.category = jstr(jobj(p, @"_aCategory", NSDictionary.class), @"_sName");
        m.summary = mod_browser_plain_text(jstr(p, @"_sDescription")); m.text = mod_browser_plain_text(jstr(p, @"_sText"));
        m.license = license_text(p);
        m.profileURL = jstr(p, @"_sProfileUrl").length ? jstr(p, @"_sProfileUrl") : [NSString stringWithFormat:@"https://gamebanana.com/mods/%lld", modId];
        m.updated = jdate(p, @"_tsDateUpdated") ?: jdate(p, @"_tsDateModified") ?: jdate(p, @"_tsDateAdded");
        m.downloads = (NSInteger)jnum(p, @"_nDownloadCount"); m.likes = (NSInteger)jnum(p, @"_nLikeCount"); m.views = (NSInteger)jnum(p, @"_nViewCount");
        NSMutableArray *big = [NSMutableArray array], *small = [NSMutableArray array];
        for (id im in jobj(jobj(p, @"_aPreviewMedia", NSDictionary.class), @"_aImages", NSArray.class)) {
            NSString *b = image_url(im, @[@"_sFile530", @"_sFile"]), *t = image_url(im, @[@"_sFile220", @"_sFile100", @"_sFile"]);
            if (b && t) { [big addObject:b]; [small addObject:t]; }
        }
        m.imageURLs = big; m.thumbURLs = small;
        NSMutableArray *files = [NSMutableArray array];
        for (id f in jobj(p, @"_aFiles", NSArray.class)) {
            if (![f isKindOfClass:NSDictionary.class] || jnum(f, @"_bIsArchived")) continue;
            TOTKModFile *x = [TOTKModFile new];
            x.fileId = jnum(f, @"_idRow"); x.name = jstr(f, @"_sFile"); x.fileDescription = mod_browser_plain_text(jstr(f, @"_sDescription"));
            x.bytes = jnum(f, @"_nFilesize"); x.downloadURL = jstr(f, @"_sDownloadUrl"); x.md5 = jstr(f, @"_sMd5Checksum").lowercaseString;
            x.avResult = jstr(f, @"_sAvResult"); x.downloads = (NSInteger)jnum(f, @"_nDownloadCount"); x.added = jdate(f, @"_tsDateAdded");
            int score; BOOL old, tk;
            x.gameVersions = mod_browser_versions([NSString stringWithFormat:@"%@ %@", x.name, x.fileDescription], &score, &old, &tk);
            if (!x.archiveKind.length || !x.canInstallHere) score -= 5;
            x.versionScore = score; x.oldVersion = old; x.tkmmOnly = tk;
            if (x.fileId && x.downloadURL.length) [files addObject:x];
        }
        NSArray *ranked = [files sortedArrayWithOptions:NSSortStable usingComparator:^NSComparisonResult(TOTKModFile *a, TOTKModFile *b) {
            return a.versionScore > b.versionScore ? NSOrderedAscending : a.versionScore < b.versionScore ? NSOrderedDescending : NSOrderedSame; }];
        TOTKModFile *best = ranked.firstObject;
        if (best && best.versionScore >= 1 && (!best.avResult.length || [best.avResult isEqual:@"clean"])) best.recommended = YES;
        m.files = ranked;
        done(m, nil);
    }];
}

/* ---- pictures: memory, then disk (kept, trimmed to ~150 MB), then GameBanana ---- */

- (void)image:(NSString *)url completion:(void (^)(NSData *))done {
    if (!url.length) { done(nil); return; }
    NSData *hit = [images objectForKey:url];
    if (hit) { done(hit); return; }
    NSMutableArray *waiting = imageWaiters[url];
    if (waiting) { [waiting addObject:[done copy]]; return; }
    imageWaiters[url] = [NSMutableArray arrayWithObject:[done copy]];
    NSString *path = [self cachePath:@"img" url:url];
    dispatch_async(work, ^{
        NSData *disk = [NSData dataWithContentsOfFile:path];
        void (^deliver)(NSData *) = ^(NSData *data) {
            dispatch_async(dispatch_get_main_queue(), ^{
                if (data) [self->images setObject:data forKey:url cost:data.length];
                for (void (^b)(NSData *) in self->imageWaiters[url]) b(data);
                [self->imageWaiters removeObjectForKey:url];
            });
        };
        if (disk) { deliver(disk); return; }
        [self fetch:url attempt:0 completion:^(NSData *data, NSError *error) {
            (void)error;
            if (data.length) dispatch_async(self->work, ^{ [data writeToFile:path atomically:YES]; if (++self->imageWrites % 64 == 0) [self trimImages]; });
            deliver(data.length ? data : nil);
        }];
    });
}

- (void)trimImages {   /* the work queue */
    NSString *dir = [cacheDir stringByAppendingPathComponent:@"img"];
    NSArray *keys = @[NSURLContentModificationDateKey, NSURLFileSizeKey];
    NSArray<NSURL *> *all = [NSFileManager.defaultManager contentsOfDirectoryAtURL:[NSURL fileURLWithPath:dir] includingPropertiesForKeys:keys options:0 error:nil];
    all = [all sortedArrayUsingComparator:^NSComparisonResult(NSURL *a, NSURL *b) {
        NSDate *x, *y; [a getResourceValue:&x forKey:NSURLContentModificationDateKey error:nil]; [b getResourceValue:&y forKey:NSURLContentModificationDateKey error:nil];
        return [y compare:x]; }];
    int64_t total = 0;
    for (NSURL *f in all) {
        NSNumber *size; [f getResourceValue:&size forKey:NSURLFileSizeKey error:nil]; total += size.longLongValue;
        if (total > (150ll << 20)) [NSFileManager.defaultManager removeItemAtURL:f error:nil];
    }
}

/* ---- installing ---- */

- (BOOL)isInstalling:(int64_t)fileId { return installing[@(fileId)] != nil; }

- (void)cancelInstall:(int64_t)fileId {
    [cancelled addObject:@(fileId)];
    [installing[@(fileId)] cancel];
}

- (NSString *)installedFolderFor:(int64_t)modId {
    ModInfo info;
    for (int i = 0, n = mods_count(); i < n; i++)
        if (!mods_get(i, &info) && info.gamebanana_id == modId) return @(info.folder);
    return nil;
}

- (void)install:(TOTKModFile *)file of:(TOTKModDetails *)mod progress:(TOTKModProgress)progress completion:(TOTKModInstalled)done {
    NSNumber *key = @(file.fileId);
    void (^fail)(NSString *) = ^(NSString *why) { dispatch_async(dispatch_get_main_queue(), ^{ done(nil, nil, gb_error(why)); }); };
    if (installing[key]) { fail(@"Already downloading."); return; }
    if (!file.archiveKind.length) { fail(@"Only zip, 7z and RAR archives can be installed from here."); return; }
    if (!file.canInstallHere) { fail(TOTKModArchiveNotOnIOS); return; }
    if (file.bytes > GB_MAX_DOWNLOAD) { fail(@"This file is larger than 4 GB: download it from the mod's page instead."); return; }
    NSString *modsDir = @(mods_dir());
    NSString *dir = [NSTemporaryDirectory() stringByAppendingPathComponent:[@"TOTKModBrowser-" stringByAppendingString:NSUUID.UUID.UUIDString]];
    [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *archive = [dir stringByAppendingPathComponent:file.name.lastPathComponent.length ? file.name.lastPathComponent : @"download"];
    [cancelled removeObject:key];
    progress(0, @"Downloading…");
    NSURLSessionDownloadTask *task = [downloads downloadTaskWithURL:[NSURL URLWithString:file.downloadURL]];
    TOTKModDownload *d = [TOTKModDownload new];
    d.dest = archive; d.expected = file.bytes;
    d.progress = ^(double f) { dispatch_async(dispatch_get_main_queue(), ^{ progress(f, @"Downloading…"); }); };
    d.finished = ^(NSError *error) {
        void (^end)(NSString *, NSString *, NSError *) = ^(NSString *folder, NSString *note, NSError *e) {
            [NSFileManager.defaultManager removeItemAtPath:dir error:nil];
            dispatch_async(dispatch_get_main_queue(), ^{
                [self->installing removeObjectForKey:key];
                if (!e && folder) [NSNotificationCenter.defaultCenter postNotificationName:TOTKModsChangedNotification object:nil];
                done(folder, note, e);
            });
        };
        if ([self->cancelled containsObject:key]) { end(nil, nil, gb_error(@"Cancelled.")); return; }
        if (error) { end(nil, nil, gb_error([@"The download failed: " stringByAppendingString:error.localizedDescription])); return; }
        dispatch_async(dispatch_get_main_queue(), ^{ progress(-1, @"Checking…"); });
        if (file.md5.length == 32 && ![file_md5(archive) isEqual:file.md5]) {
            end(nil, nil, gb_error(@"The download didn't match GameBanana's checksum: try again.")); return;
        }
        dispatch_async(dispatch_get_main_queue(), ^{ progress(-1, @"Unpacking…"); });
        NSString *note = nil; NSError *e = nil;
        NSArray *made = mod_browser_install_archive(archive, modsDir, mod, file, &note, &e);
        if (!made.count) { end(nil, nil, e ?: gb_error(@"Nothing was installed.")); return; }
        dispatch_sync(dispatch_get_main_queue(), ^{
            mods_scan();
            for (NSUInteger i = 1; i < made.count; i++) mods_set_enabled([made[i] fileSystemRepresentation], 0);
            mods_set_enabled([made[0] fileSystemRepresentation], 1);
            mods_save();
        });
        fprintf(stderr, "[mods] installed %s (GameBanana %lld, file %lld)\n", [made[0] UTF8String], mod.modId, file.fileId);
        end(made[0], note, nil);
    };
    @synchronized (tasks) { tasks[@(task.taskIdentifier)] = d; }
    installing[key] = task;
    [task resume];
}

/* The download session's delegate (its own serial queue). */
- (void)URLSession:(NSURLSession *)s downloadTask:(NSURLSessionDownloadTask *)task didWriteData:(int64_t)bytes
     totalBytesWritten:(int64_t)written totalBytesExpectedToWrite:(int64_t)expected {
    (void)s; (void)bytes;
    TOTKModDownload *d; @synchronized (tasks) { d = tasks[@(task.taskIdentifier)]; }
    int64_t total = expected > 0 ? expected : d.expected;
    if (d.progress) d.progress(total > 0 ? MIN(1.0, (double)written / (double)total) : -1);
    if (written > GB_MAX_DOWNLOAD) [task cancel];
}

- (void)URLSession:(NSURLSession *)s downloadTask:(NSURLSessionDownloadTask *)task didFinishDownloadingToURL:(NSURL *)location {
    (void)s;
    TOTKModDownload *d; @synchronized (tasks) { d = tasks[@(task.taskIdentifier)]; }
    if (!d) return;
    NSInteger code = [task.response isKindOfClass:NSHTTPURLResponse.class] ? ((NSHTTPURLResponse *)task.response).statusCode : 0;
    if (code != 200) { d.saveError = gb_error([NSString stringWithFormat:@"GameBanana answered %ld.", (long)code]); return; }
    NSError *e = nil;
    [NSFileManager.defaultManager removeItemAtPath:d.dest error:nil];
    d.saved = [NSFileManager.defaultManager moveItemAtURL:location toURL:[NSURL fileURLWithPath:d.dest] error:&e];
    d.saveError = e;
}

- (void)URLSession:(NSURLSession *)s task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error {
    (void)s;
    TOTKModDownload *d; @synchronized (tasks) { d = tasks[@(task.taskIdentifier)]; [tasks removeObjectForKey:@(task.taskIdentifier)]; }
    if (!d) return;
    NSError *e = error ?: d.saveError ?: (d.saved ? nil : gb_error(@"Nothing was received."));
    dispatch_async(work, ^{ d.finished(e); });
}
@end

/* ---- unpacking ---- */

static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t rd64(const unsigned char *p) { return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32; }
static BOOL read_at(int fd, void *buf, size_t n, uint64_t off) {
    for (size_t got = 0; got < n;) {
        ssize_t r = pread(fd, (char *)buf + got, n - got, (off_t)(off + got));
        if (r <= 0) return NO;
        got += (size_t)r;
    }
    return YES;
}

/* A zip entry's name as a safe relative path ("" = skip it, nil = refuse the archive). */
static NSString *safe_name(const unsigned char *raw, size_t n, BOOL utf8) {
    NSString *name = [[NSString alloc] initWithBytes:raw length:n encoding:NSUTF8StringEncoding];
    if (!name && !utf8) name = [[NSString alloc] initWithBytes:raw length:n encoding:NSWindowsCP1252StringEncoding];
    if (!name) name = [[NSString alloc] initWithBytes:raw length:n encoding:NSISOLatin1StringEncoding];
    name = [name stringByReplacingOccurrencesOfString:@"\\" withString:@"/"];
    if ([name hasPrefix:@"/"] || (name.length > 1 && [name characterAtIndex:1] == ':')) return nil;
    NSMutableArray *parts = [NSMutableArray array];
    for (NSString *p in [name componentsSeparatedByString:@"/"]) {
        if (!p.length || [p isEqual:@"."]) continue;
        if ([p isEqual:@".."]) return nil;
        if ([p isEqual:@"__MACOSX"]) return @"";
        [parts addObject:p];
    }
    return [parts componentsJoinedByString:@"/"];
}

/* A zip with stored and deflated entries (zip64 sizes and offsets read); no encryption. Caps the entry count, the
   unpacked total and each entry at its declared size; names with ".." or a leading "/" refuse the archive. */
BOOL mod_browser_unzip(NSString *zip, NSString *dest, NSError **error) {
    NSString *why = nil;
#define ZFAIL(...) do { why = [NSString stringWithFormat:__VA_ARGS__]; goto out; } while (0)
    int fd = open(zip.fileSystemRepresentation, O_RDONLY), ofd = -1; struct stat st;
    unsigned char *cd = NULL, *in = NULL, *outb = NULL; size_t cdlen = 0;
    NSFileManager *fm = NSFileManager.defaultManager;
    if (fd < 0 || fstat(fd, &st)) ZFAIL(@"Can't open the archive.");
    {
        uint64_t size = (uint64_t)st.st_size, tail = MIN(size, (uint64_t)65557), eocd = UINT64_MAX;
        unsigned char *t = malloc(tail);
        if (!t || !read_at(fd, t, tail, size - tail)) { free(t); ZFAIL(@"Can't read the archive."); }
        for (int64_t i = (int64_t)tail - 22; i >= 0; i--) if (rd32(t + i) == 0x06054b50) { eocd = size - tail + (uint64_t)i; break; }
        if (eocd == UINT64_MAX) { free(t); ZFAIL(@"Not a zip archive (or a damaged one)."); }
        const unsigned char *e = t + (eocd - (size - tail));
        uint64_t count = rd16(e + 10), cdsize = rd32(e + 12), cdoff = rd32(e + 16);
        free(t);
        if (count == 0xffff || cdsize == 0xffffffff || cdoff == 0xffffffff) {   /* zip64 */
            unsigned char loc[20], z[56];
            if (eocd < 20 || !read_at(fd, loc, 20, eocd - 20) || rd32(loc) != 0x07064b50) ZFAIL(@"A damaged zip64 archive.");
            if (!read_at(fd, z, 56, rd64(loc + 8)) || rd32(z) != 0x06064b50) ZFAIL(@"A damaged zip64 archive.");
            count = rd64(z + 32); cdsize = rd64(z + 40); cdoff = rd64(z + 48);
        }
        if (count > 200000 || cdsize > (256u << 20) || cdoff + cdsize > size) ZFAIL(@"The archive's index is damaged or too large.");
        cdlen = (size_t)cdsize; cd = malloc(cdlen ? cdlen : 1);
        if (!cd || !read_at(fd, cd, cdlen, cdoff)) ZFAIL(@"Can't read the archive's index.");
        in = malloc(1 << 18); outb = malloc(1 << 18);
        if (!in || !outb) ZFAIL(@"Out of memory.");
        uint64_t total = 0; size_t p = 0;
        for (uint64_t k = 0; k < count; k++) {
            if (p + 46 > cdlen || rd32(cd + p) != 0x02014b50) ZFAIL(@"The archive's index is damaged.");
            const unsigned char *h = cd + p;
            unsigned flags = rd16(h + 8), method = rd16(h + 10), nlen = rd16(h + 28), xlen = rd16(h + 30), clen = rd16(h + 32);
            uint32_t crc = rd32(h + 16), mode = rd32(h + 38) >> 16;
            uint64_t csize = rd32(h + 20), usize = rd32(h + 24), lho = rd32(h + 42);
            if (p + 46 + nlen + xlen + clen > cdlen) ZFAIL(@"The archive's index is damaged.");
            for (size_t x = p + 46 + nlen; x + 4 <= p + 46 + nlen + xlen;) {   /* zip64 extra: the fields that overflowed, in order */
                unsigned id = rd16(cd + x), len = rd16(cd + x + 2); size_t f = x + 4, end = x + 4 + len;
                if (end > p + 46 + nlen + xlen) break;
                if (id == 1) {
                    if (usize == 0xffffffff && f + 8 <= end) { usize = rd64(cd + f); f += 8; }
                    if (csize == 0xffffffff && f + 8 <= end) { csize = rd64(cd + f); f += 8; }
                    if (lho == 0xffffffff && f + 8 <= end) { lho = rd64(cd + f); f += 8; }
                }
                x = end;
            }
            NSString *name = safe_name(h + 46, nlen, (flags & 0x800) != 0);
            p += 46 + nlen + xlen + clen;
            if (!name) ZFAIL(@"The archive has an unsafe path in it.");
            if (!name.length || (mode & 0170000) == 0120000) continue;   /* __MACOSX, symbolic links */
            NSString *path = [dest stringByAppendingPathComponent:name];
            if (h[46 + nlen - 1] == '/' || h[46 + nlen - 1] == '\\') { [fm createDirectoryAtPath:path withIntermediateDirectories:YES attributes:nil error:nil]; continue; }
            if (flags & 1) ZFAIL(@"The archive is password-protected.");
            if (method != 0 && method != 8) ZFAIL(@"The archive uses a compression this device can't unpack (method %u): only zip deflate.", method);
            if ((total += usize) > (uint64_t)GB_MAX_UNPACKED) ZFAIL(@"The archive unpacks to more than 12 GB.");
            unsigned char lh[30];
            if (!read_at(fd, lh, 30, lho) || rd32(lh) != 0x04034b50) ZFAIL(@"The archive is damaged.");
            uint64_t data = lho + 30 + rd16(lh + 26) + rd16(lh + 28);
            if (data + csize > size) ZFAIL(@"The archive is damaged.");
            [fm createDirectoryAtPath:path.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil];
            ofd = open(path.fileSystemRepresentation, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
            if (ofd < 0) ZFAIL(@"Can't write %@.", name);
            uLong sum = crc32(0, Z_NULL, 0); uint64_t done_in = 0, done_out = 0;
            z_stream z; memset(&z, 0, sizeof z);
            if (method == 8 && inflateInit2(&z, -MAX_WBITS) != Z_OK) ZFAIL(@"Can't unpack.");
            int zr = Z_OK;
            while (method == 0 ? done_in < csize : zr != Z_STREAM_END) {
                size_t want = (size_t)MIN((uint64_t)(1 << 18), csize - done_in);
                if (want && !read_at(fd, in, want, data + done_in)) { if (method == 8) inflateEnd(&z); ZFAIL(@"The archive is damaged."); }
                done_in += want;
                if (method == 0) {
                    if ((done_out += want) > usize || write(ofd, in, want) != (ssize_t)want) ZFAIL(@"Can't write %@.", name);
                    sum = crc32(sum, in, (uInt)want); continue;
                }
                z.next_in = in; z.avail_in = (uInt)want;
                do {
                    z.next_out = outb; z.avail_out = 1 << 18;
                    zr = inflate(&z, Z_NO_FLUSH);
                    if (zr != Z_OK && zr != Z_STREAM_END && !(zr == Z_BUF_ERROR && !want)) { inflateEnd(&z); ZFAIL(@"%@ is damaged in the archive.", name); }
                    size_t got = (1 << 18) - z.avail_out;
                    if ((done_out += got) > usize) { inflateEnd(&z); ZFAIL(@"%@ is larger than the archive says.", name); }
                    if (got && write(ofd, outb, got) != (ssize_t)got) { inflateEnd(&z); ZFAIL(@"Can't write %@.", name); }
                    sum = crc32(sum, outb, (uInt)got);
                } while (z.avail_out == 0 && zr != Z_STREAM_END);
                if (!want && zr != Z_STREAM_END) { inflateEnd(&z); ZFAIL(@"%@ is cut short in the archive.", name); }
            }
            if (method == 8) inflateEnd(&z);
            close(ofd); ofd = -1;
            if (done_out != usize || sum != crc) ZFAIL(@"%@ is damaged in the archive (checksum).", name);
        }
    }
out:
#undef ZFAIL
    if (ofd >= 0) close(ofd);
    if (fd >= 0) close(fd);
    free(cd); free(in); free(outb);
    if (why && error) *error = gb_error(why);
    return why == nil;
}

BOOL mod_browser_extract(NSString *archive, NSString *dest, NSError **error) {
    unsigned char magic[8] = {0};
    int fd = open(archive.fileSystemRepresentation, O_RDONLY);
    if (fd < 0 || read(fd, magic, sizeof magic) < 4) { if (fd >= 0) close(fd); if (error) *error = gb_error(@"Can't read the download."); return NO; }
    close(fd);
    BOOL zip = !memcmp(magic, "PK\3\4", 4) || !memcmp(magic, "PK\5\6", 4);
    BOOL sevenz = !memcmp(magic, "7z\xbc\xaf\x27\x1c", 6), rar = !memcmp(magic, "Rar!\x1a\x07", 6);
    if (!zip && !sevenz && !rar) { if (error) *error = gb_error(@"The download isn't a zip, 7z or RAR archive."); return NO; }
    [NSFileManager.defaultManager createDirectoryAtPath:dest withIntermediateDirectories:YES attributes:nil error:nil];
#if TARGET_OS_IPHONE
    if (!zip) { if (error) *error = gb_error(TOTKModArchiveNotOnIOS); return NO; }
    return mod_browser_unzip(archive, dest, error);
#else
    const char *own = getenv("TOTK_MODS_UNZIP_BUILTIN");   /* test runs: the iPhone / iPad extractor on the Mac */
    if (zip && own && atoi(own)) return mod_browser_unzip(archive, dest, error);
    /* bsdtar (libarchive) reads zip, 7z and RAR 4 / 5; it refuses ".." and strips a leading "/" by default. */
    NSTask *t = [NSTask new];
    t.executableURL = [NSURL fileURLWithPath:@"/usr/bin/bsdtar"];
    t.arguments = @[@"-x", @"--no-same-owner", @"-f", archive, @"-C", dest];
    NSPipe *err = [NSPipe pipe]; t.standardError = err; t.standardOutput = NSFileHandle.fileHandleWithNullDevice;
    NSError *e = nil;
    if (![t launchAndReturnError:&e]) {
        if (zip) return mod_browser_unzip(archive, dest, error);
        if (error) *error = gb_error([@"Can't run bsdtar: " stringByAppendingString:e.localizedDescription]); return NO;
    }
    NSData *msg = [err.fileHandleForReading readDataToEndOfFile];
    [t waitUntilExit];
    if (t.terminationStatus) {
        NSString *s = [[[NSString alloc] initWithData:msg encoding:NSUTF8StringEncoding] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
        if (error) *error = gb_error([NSString stringWithFormat:@"Couldn't unpack the %@ archive%@%@", zip ? @"zip" : sevenz ? @"7z" : @"RAR",
                                      s.length ? @": " : @".", s.length > 300 ? [s substringToIndex:300] : s ?: @""]);
        return NO;
    }
    return YES;
#endif
}

/* ---- arranging a mod ---- */

static NSSet *romfs_top(void) {   /* RomFS top-level folders: a folder holding these is a romfs/ itself */
    static NSSet *s; static dispatch_once_t once;
    dispatch_once(&once, ^{ s = [NSSet setWithArray:@[@"pack", @"model", @"ui", @"sound", @"system", @"mals", @"event", @"shader", @"font",
        @"movie", @"effect", @"phive", @"logic", @"banc", @"ai", @"as", @"rsdb", @"gamedata", @"textogo", @"xlink", @"levelsensor"]]; });
    return s;
}

static BOOL is_dir(NSString *path) {
    NSDictionary *a = [NSFileManager.defaultManager attributesOfItemAtPath:path error:nil];   /* does not follow links */
    return [a.fileType isEqual:NSFileTypeDirectory];
}

/* Removes links, Finder and Explorer leftovers; returns the bytes left. */
static int64_t tidy(NSString *dir) {
    NSFileManager *fm = NSFileManager.defaultManager; int64_t bytes = 0;
    NSDirectoryEnumerator *e = [fm enumeratorAtPath:dir]; NSMutableArray *drop = [NSMutableArray array];
    for (NSString *rel in e) {
        NSString *name = rel.lastPathComponent;
        NSString *type = e.fileAttributes.fileType;
        if ([type isEqual:NSFileTypeSymbolicLink] || [@[@".DS_Store", @"Thumbs.db", @"desktop.ini", @"__MACOSX"] containsObject:name] || [name hasPrefix:@"._"]) {
            [drop addObject:rel]; if ([type isEqual:NSFileTypeDirectory]) [e skipDescendants];
            continue;
        }
        if ([type isEqual:NSFileTypeRegular]) bytes += (int64_t)e.fileAttributes.fileSize;
    }
    for (NSString *rel in drop) [fm removeItemAtPath:[dir stringByAppendingPathComponent:rel] error:nil];
    return bytes;
}

/* Mod roots: a folder holding romfs/ or exefs/ (any case), or one whose top level is RomFS paths. Not looked into further. */
static void find_roots(NSString *dir, NSString *rel, int depth, NSMutableArray *roots, NSMutableArray *tkcl) {
    NSArray *names = [[NSFileManager.defaultManager contentsOfDirectoryAtPath:dir error:nil] sortedArrayUsingSelector:@selector(localizedStandardCompare:)];
    NSMutableArray *dirs = [NSMutableArray array]; BOOL container = NO, bare = NO;
    for (NSString *n in names) {
        NSString *path = [dir stringByAppendingPathComponent:n];
        if (!is_dir(path)) { if ([n.pathExtension.lowercaseString isEqual:@"tkcl"]) [tkcl addObject:path]; continue; }
        NSString *l = n.lowercaseString;
        if ([l isEqual:@"romfs"] || [l isEqual:@"exefs"]) container = YES;
        else if ([romfs_top() containsObject:l]) bare = YES;
        [dirs addObject:n];
    }
    if (container || bare) { [roots addObject:@{@"path": dir, @"rel": rel, @"bare": @(!container)}]; return; }
    if (depth >= 6) return;
    for (NSString *n in dirs) find_roots([dir stringByAppendingPathComponent:n], rel.length ? [rel stringByAppendingPathComponent:n] : n, depth + 1, roots, tkcl);
}

static NSString *folder_name(NSString *name, int64_t modId) {
    NSMutableString *s = [NSMutableString string];
    NSCharacterSet *bad = [NSCharacterSet characterSetWithCharactersInString:@"/\\:*?\"<>|"];
    [name enumerateSubstringsInRange:NSMakeRange(0, name.length) options:NSStringEnumerationByComposedCharacterSequences
                          usingBlock:^(NSString *c, NSRange r, NSRange er, BOOL *stop) {
        (void)r; (void)er; (void)stop;
        unichar u = [c characterAtIndex:0];
        [s appendString:u < 32 || u == 127 || [c rangeOfCharacterFromSet:bad].location != NSNotFound ? @" " : c];
    }];
    NSString *t = re_replace(s, @"\\s+", @" ");
    t = [t stringByTrimmingCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@" .-"]];   /* "-" starts an off line in mods.txt */
    if (t.length > 64) t = [[t substringToIndex:[t rangeOfComposedCharacterSequenceAtIndex:64].location] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
    return t.length ? t : [NSString stringWithFormat:@"GameBanana %lld", modId];
}

static NSString *iso_now(void) {
    NSISO8601DateFormatter *f = [NSISO8601DateFormatter new];
    return [f stringFromDate:NSDate.date];
}

static BOOL move_into(NSString *from, NSString *to, NSError **error) {
    NSFileManager *fm = NSFileManager.defaultManager;
    if (![fm fileExistsAtPath:to]) {
        [fm createDirectoryAtPath:to.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil];
        return [fm moveItemAtPath:from toPath:to error:error];
    }
    for (NSString *n in [fm contentsOfDirectoryAtPath:from error:nil]) {   /* romfs/ and RomFS/ side by side: merge */
        NSString *a = [from stringByAppendingPathComponent:n], *b = [to stringByAppendingPathComponent:n];
        if (is_dir(a) && is_dir(b)) { if (!move_into(a, b, error)) return NO; }
        else { [fm removeItemAtPath:b error:nil]; if (![fm moveItemAtPath:a toPath:b error:error]) return NO; }
    }
    return YES;
}

NSArray<NSString *> *mod_browser_install_archive(NSString *archive, NSString *modsDir, TOTKModDetails *mod, TOTKModFile *file,
                                                 NSString **note, NSError **error) {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *work = [archive.stringByDeletingLastPathComponent stringByAppendingPathComponent:[@"unpack-" stringByAppendingString:NSUUID.UUID.UUIDString]];
    NSString *x = [work stringByAppendingPathComponent:@"x"], *out = [work stringByAppendingPathComponent:@"out"];
    NSMutableArray *made = [NSMutableArray array], *notes = [NSMutableArray array];
    NSError *e = nil;
    if (!mod_browser_extract(archive, x, &e)) goto fail;
    if (tidy(x) > GB_MAX_UNPACKED) { e = gb_error(@"The archive unpacks to more than 12 GB."); goto fail; }
    {
        NSMutableArray *roots = [NSMutableArray array], *tkcl = [NSMutableArray array];
        find_roots(x, @"", 0, roots, tkcl);
        if (!roots.count && !tkcl.count) {
            e = gb_error(@"Nothing the game can load in this archive: no romfs/ or exefs/ folder (it may be a ReShade preset, a save or a tool).");
            goto fail;
        }
        /* Several versions in one archive (1.2.1/, 1.4.x/, or options): each becomes a mod, the best for 1.4.3 is switched on. */
        [roots sortWithOptions:NSSortStable usingComparator:^NSComparisonResult(NSDictionary *a, NSDictionary *b) {
            int sa, sb; mod_browser_versions(a[@"rel"], &sa, NULL, NULL); mod_browser_versions(b[@"rel"], &sb, NULL, NULL);
            return sa > sb ? NSOrderedAscending : sa < sb ? NSOrderedDescending : NSOrderedSame; }];
        if (!roots.count) [roots addObject:@{@"path": x, @"rel": @"", @"bare": @NO}];   /* .tkcl only */
        NSMutableSet *taken = [NSMutableSet set];
        BOOL code = NO;
        for (NSUInteger i = 0; i < roots.count; i++) {
            NSDictionary *r = roots[i];
            NSString *variant = roots.count > 1 ? [[r[@"rel"] pathComponents] componentsJoinedByString:@" - "] : @"";
            NSString *display = variant.length ? [NSString stringWithFormat:@"%@ (%@)", mod.name, variant] : mod.name;
            NSString *base = folder_name(variant.length ? [NSString stringWithFormat:@"%@ - %@", mod.name, variant] : mod.name, mod.modId), *name = base;
            for (int k = 2; [taken containsObject:name.lowercaseString] || [fm fileExistsAtPath:[modsDir stringByAppendingPathComponent:name]]; k++)
                name = [NSString stringWithFormat:@"%@ (%d)", base, k];
            [taken addObject:name.lowercaseString];
            NSString *dst = [out stringByAppendingPathComponent:name];
            [fm createDirectoryAtPath:dst withIntermediateDirectories:YES attributes:nil error:nil];
            if ([r[@"bare"] boolValue]) { if (!move_into(r[@"path"], [dst stringByAppendingPathComponent:@"romfs"], &e)) goto fail; }
            else for (NSString *n in [fm contentsOfDirectoryAtPath:r[@"path"] error:nil]) {
                NSString *l = n.lowercaseString, *src = [r[@"path"] stringByAppendingPathComponent:n];
                if (([l isEqual:@"romfs"] || [l isEqual:@"exefs"]) && is_dir(src) && !move_into(src, [dst stringByAppendingPathComponent:l], &e)) goto fail;
            }
            if ([fm fileExistsAtPath:[dst stringByAppendingPathComponent:@"exefs"]]) code = YES;
            if (i == 0) for (NSString *t in tkcl) [fm copyItemAtPath:t toPath:[dst stringByAppendingPathComponent:t.lastPathComponent] error:nil];
            int score; BOOL old;
            NSString *versions = mod_browser_versions(variant, &score, &old, NULL);
            if (!versions.length) versions = file.gameVersions ?: @"";
            NSDictionary *json = @{
                @"name": display ?: name, @"author": mod.author ?: @"", @"version": mod.version ?: @"",
                @"gamebanana_id": @(mod.modId), @"file_id": @(file.fileId), @"file": file.name ?: @"",
                @"file_description": file.fileDescription ?: @"", @"variant": variant,
                @"game_versions": versions, @"license": mod.license ?: @"", @"installed": iso_now(),
                @"source_url": mod.profileURL ?: @"", @"download_url": file.downloadURL ?: @"", @"md5": file.md5 ?: @""};
            NSData *data = [NSJSONSerialization dataWithJSONObject:json options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys | NSJSONWritingWithoutEscapingSlashes error:&e];
            if (!data || ![data writeToFile:[dst stringByAppendingPathComponent:@"mod.json"] options:NSDataWritingAtomic error:&e]) goto fail;
            [made addObject:name];
        }
        if (roots.count > 1)
            [notes addObject:[NSString stringWithFormat:@"The archive holds %lu versions: all are installed, only “%@” is on. Switch in Installed.",
                              (unsigned long)roots.count, made[0]]];
        if (code) [notes addObject:@"Code patches (exefs) aren't applied yet — coming in a later update"];
        if (tkcl.count) [notes addObject:@"TKMM .tkcl packages aren't supported yet — coming later"];
        if (file.oldVersion) [notes addObject:[NSString stringWithFormat:@"Made for %@: may not work on %@", file.gameVersions, GB_TARGET]];
        [fm createDirectoryAtPath:modsDir withIntermediateDirectories:YES attributes:nil error:nil];
        for (NSUInteger i = 0; i < made.count; i++)
            if (![fm moveItemAtPath:[out stringByAppendingPathComponent:made[i]] toPath:[modsDir stringByAppendingPathComponent:made[i]] error:&e]) {
                for (NSUInteger k = 0; k < i; k++) [fm removeItemAtPath:[modsDir stringByAppendingPathComponent:made[k]] error:nil];
                goto fail;
            }
    }
    [fm removeItemAtPath:work error:nil];
    if (note) *note = notes.count ? [notes componentsJoinedByString:@"\n"] : nil;
    return made;
fail:
    [fm removeItemAtPath:work error:nil];
    if (error) *error = e ?: gb_error(@"Couldn't install the mod.");
    return @[];
}
