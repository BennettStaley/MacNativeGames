/* Save packages (.totksave): runtime/save_package.h has the format and the rules. Foundation, CommonCrypto and zlib; ARC. */
#import "save_package.h"
#import <CommonCrypto/CommonDigest.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

NSString *const TOTKSavePackageExtension = @"totksave";
NSString *const TOTKSavePackageType = @"org.macnativegames.totk.save";
static NSString *const TITLE_ID = @"0100F2C0115B6000";
static NSString *const BACKUP_SUFFIX = @"-before-import";
enum { FORMAT = 1, MAX_FILES = 4096 };
static const uint64_t MAX_FILE = 256ull << 20, MAX_TOTAL = 2ull << 30;   /* a profile's saves are about 20 MB */
static const NSTimeInterval BUSY_STALE = 10 * 60;                       /* as ios/tools/save_sync.py: older = left by a crash */

static NSError *fail(NSString *format, ...) NS_FORMAT_FUNCTION(1, 2);
static NSError *fail(NSString *format, ...) {
    va_list args; va_start(args, format);
    NSString *text = [[NSString alloc] initWithFormat:format arguments:args];
    va_end(args);
    return [NSError errorWithDomain:@"TOTKSavePackage" code:1 userInfo:@{NSLocalizedDescriptionKey: text}];
}
#define FAIL(...) do { if (error) *error = fail(__VA_ARGS__); return nil; } while (0)

static NSISO8601DateFormatter *iso(void) {
    static NSISO8601DateFormatter *f; static dispatch_once_t once;
    dispatch_once(&once, ^{ f = [NSISO8601DateFormatter new]; });
    return f;
}
static NSString *date_text(NSDate *d) {
    static NSDateFormatter *f; static dispatch_once_t once;
    dispatch_once(&once, ^{ f = [NSDateFormatter new]; f.dateStyle = NSDateFormatterMediumStyle; f.timeStyle = NSDateFormatterShortStyle; });
    return d ? [f stringFromDate:d] : @"?";
}

@implementation TOTKSavePackageInfo
- (NSString *)fromText {
    NSString *where = [self.fromPlatform isEqual:@"ios"] ? @"iPhone / iPad" : [self.fromPlatform isEqual:@"mac"] ? @"Mac" : self.fromPlatform;
    NSMutableArray *parts = [NSMutableArray array];
    if (self.fromModel.length) [parts addObject:self.fromModel];
    if (where.length && ![where isEqual:self.fromModel]) [parts addObject:where];
    NSString *name = self.fromDevice.length ? self.fromDevice : @"another device";
    return parts.count ? [NSString stringWithFormat:@"%@ (%@)", name, [parts componentsJoinedByString:@", "]] : name;
}
- (NSString *)slotsText {
    NSMutableArray *lines = [NSMutableArray array];
    for (NSUInteger i = 0; i < self.slots.count; i++) {
        NSDate *when = i < self.slotTimes.count && ![self.slotTimes[i] isEqual:NSDate.distantPast] ? self.slotTimes[i] : nil;
        [lines addObject:when ? [NSString stringWithFormat:@"%@ — %@", self.slots[i], date_text(when)] : self.slots[i]];
    }
    return [lines componentsJoinedByString:@"\n"];
}
@end

/* ---- the profile ---- */

NSString *TOTKSaveFolder(NSString *profile, BOOL create, NSError **error) {
    NSString *path = [profile stringByAppendingPathComponent:@"account.uid"];
    uint64_t uid[2] = {0, 0};
    int fd = open(path.fileSystemRepresentation, O_RDONLY | O_NOFOLLOW);
    if (fd < 0 && errno == ENOENT && create) {   /* never started here: the account the game would make (runtime/account.c) */
        if (![NSFileManager.defaultManager createDirectoryAtPath:profile withIntermediateDirectories:YES attributes:nil error:nil])
            FAIL(@"Can't create %@.", profile);
        do arc4random_buf(uid, sizeof uid); while (!uid[0] && !uid[1]);
        int wfd = open(path.fileSystemRepresentation, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (wfd >= 0) {
            BOOL ok = write(wfd, uid, sizeof uid) == (ssize_t)sizeof uid && fsync(wfd) == 0;
            close(wfd);
            if (!ok) { unlink(path.fileSystemRepresentation); FAIL(@"Can't write %@.", path); }
        }
        fd = open(path.fileSystemRepresentation, O_RDONLY | O_NOFOLLOW);
    }
    if (fd < 0) FAIL(@"There is no account on this device yet: start the game once.");
    ssize_t n = pread(fd, uid, sizeof uid, 0); struct stat st; int bad = fstat(fd, &st) || st.st_size != (off_t)sizeof uid;
    close(fd);
    if (n != (ssize_t)sizeof uid || bad || (!uid[0] && !uid[1])) FAIL(@"%@ is damaged.", path);
    NSString *folder = [profile stringByAppendingPathComponent:[NSString stringWithFormat:@"save/%016llx%016llx",
                                                                (unsigned long long)uid[1], (unsigned long long)uid[0]]];
    if (create && ![NSFileManager.defaultManager createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil error:nil])
        FAIL(@"Can't create %@.", folder);
    return folder;
}

BOOL TOTKSaveBusy(NSString *profile) {
    struct stat st;
    if (stat([profile stringByAppendingPathComponent:@"save/.busy"].fileSystemRepresentation, &st)) return NO;
    return time(NULL) - st.st_mtimespec.tv_sec < BUSY_STALE;
}

/* A save path as the package and the folder hold it: relative, no "." / ".." / hidden parts, no backslash. */
static BOOL safe_rel(NSString *rel) {
    if (!rel.length || rel.length > 512 || [rel containsString:@"\\"] || [rel containsString:@"\0"] || [rel hasPrefix:@"/"]) return NO;
    for (NSString *part in [rel componentsSeparatedByString:@"/"]) if (!part.length || [part hasPrefix:@"."]) return NO;
    return YES;
}

/* The files of a save folder: relative path -> attributes (regular files; hidden files and folders are not part of a save). */
static NSDictionary<NSString *, NSDictionary *> *save_files(NSString *folder) {
    NSMutableDictionary *out = [NSMutableDictionary dictionary];
    NSDirectoryEnumerator *e = [NSFileManager.defaultManager enumeratorAtPath:folder];
    for (NSString *rel in e) {
        if ([rel.lastPathComponent hasPrefix:@"."]) { if ([e.fileAttributes.fileType isEqual:NSFileTypeDirectory]) [e skipDescendants]; continue; }
        if ([e.fileAttributes.fileType isEqual:NSFileTypeRegular]) out[rel] = e.fileAttributes;
    }
    return out;
}

static NSString *hex(const unsigned char *p, size_t n) {
    NSMutableString *s = [NSMutableString stringWithCapacity:n * 2];
    for (size_t i = 0; i < n; i++) [s appendFormat:@"%02x", p[i]];
    return s;
}
static NSString *sha256(NSData *d) {
    unsigned char out[CC_SHA256_DIGEST_LENGTH]; CC_SHA256(d.bytes, (CC_LONG)d.length, out);
    return hex(out, sizeof out);
}

/* slot_* -> newest file time, from relative paths and their times. */
static NSDictionary<NSString *, NSDate *> *slot_times(NSDictionary<NSString *, NSDate *> *times) {
    NSMutableDictionary *slots = [NSMutableDictionary dictionary];
    for (NSString *rel in times) {
        NSArray *parts = rel.pathComponents;
        if (parts.count < 2 || ![parts[0] hasPrefix:@"slot_"]) continue;
        NSDate *have = slots[parts[0]];
        if (!have || [times[rel] compare:have] == NSOrderedDescending) slots[parts[0]] = times[rel];
    }
    return slots;
}

NSString *TOTKSaveSummary(NSString *profile) {
    NSString *folder = TOTKSaveFolder(profile, NO, nil);
    NSDictionary *files = folder ? save_files(folder) : @{};
    NSMutableDictionary *times = [NSMutableDictionary dictionary];
    for (NSString *rel in files) times[rel] = [files[rel] fileModificationDate] ?: NSDate.distantPast;
    NSDictionary *slots = slot_times(times);
    if (!slots.count) return @"No saves yet";
    NSDate *newest = [slots.allValues valueForKeyPath:@"@max.self"];
    return [NSString stringWithFormat:@"%lu slot%s, last saved %@", (unsigned long)slots.count, slots.count == 1 ? "" : "s", date_text(newest)];
}

/* ---- zip: stored or deflated entries, no zip64 (a package stays far below 4 GB) ---- */

static void put16(NSMutableData *d, uint32_t v) { uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)}; [d appendBytes:b length:2]; }
static void put32(NSMutableData *d, uint32_t v) { put16(d, v & 0xffff); put16(d, v >> 16); }
static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

typedef struct { NSMutableData *out, *central; unsigned count; } ZipOut;

static BOOL zip_put(ZipOut *z, NSString *name, NSData *data, NSDate *when) {
    NSData *raw = [name dataUsingEncoding:NSUTF8StringEncoding];
    uLong crc = crc32(0, data.bytes, (uInt)data.length);
    NSData *body = data; uint16_t method = 0;
    if (data.length > 64) {   /* deflate when it helps (the slots do; the album's JPEGs do not) */
        z_stream s; memset(&s, 0, sizeof s);
        if (deflateInit2(&s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) return NO;
        NSMutableData *packed = [NSMutableData dataWithLength:deflateBound(&s, data.length)];
        s.next_in = (Bytef *)data.bytes; s.avail_in = (uInt)data.length;
        s.next_out = packed.mutableBytes; s.avail_out = (uInt)packed.length;
        int r = deflate(&s, Z_FINISH); packed.length = s.total_out; deflateEnd(&s);
        if (r == Z_STREAM_END && packed.length < data.length) { body = packed; method = 8; }
    }
    time_t t = (time_t)(when ?: NSDate.date).timeIntervalSince1970; struct tm tm; localtime_r(&t, &tm);
    uint16_t dtime = (uint16_t)(tm.tm_hour << 11 | tm.tm_min << 5 | tm.tm_sec / 2);
    uint16_t ddate = (uint16_t)((tm.tm_year < 80 ? 0 : tm.tm_year - 80) << 9 | (tm.tm_mon + 1) << 5 | tm.tm_mday);
    if (z->out.length + body.length + 1024 > 0xffffffffull) return NO;
    uint32_t offset = (uint32_t)z->out.length;
    NSMutableData *h = z->out;
    put32(h, 0x04034b50); put16(h, 20); put16(h, 0x0800); put16(h, method); put16(h, dtime); put16(h, ddate);
    put32(h, (uint32_t)crc); put32(h, (uint32_t)body.length); put32(h, (uint32_t)data.length); put16(h, (uint16_t)raw.length); put16(h, 0);
    [h appendData:raw]; [h appendData:body];
    NSMutableData *c = z->central;
    put32(c, 0x02014b50); put16(c, 3 << 8 | 20); put16(c, 20); put16(c, 0x0800); put16(c, method); put16(c, dtime); put16(c, ddate);
    put32(c, (uint32_t)crc); put32(c, (uint32_t)body.length); put32(c, (uint32_t)data.length); put16(c, (uint16_t)raw.length);
    put16(c, 0); put16(c, 0); put16(c, 0); put16(c, 0); put32(c, 0100644u << 16); put32(c, offset);
    [c appendData:raw];
    z->count++;
    return YES;
}

static NSData *zip_finish(ZipOut *z) {
    uint32_t at = (uint32_t)z->out.length;
    [z->out appendData:z->central];
    put32(z->out, 0x06054b50); put16(z->out, 0); put16(z->out, 0); put16(z->out, (uint16_t)z->count); put16(z->out, (uint16_t)z->count);
    put32(z->out, (uint32_t)z->central.length); put32(z->out, at); put16(z->out, 0);
    return z->out;
}

/* Unpacks a package into dest (new, empty): only manifest.json and save/<safe path>; sizes and CRCs checked. Returns the
   names unpacked, or nil. */
static NSArray<NSString *> *zip_read(NSString *zip, NSString *dest, NSError **error) {
    NSData *all = [NSData dataWithContentsOfFile:zip options:NSDataReadingMappedIfSafe error:nil];
    if (!all) FAIL(@"Can't read %@.", zip.lastPathComponent);
    const unsigned char *b = all.bytes; size_t size = all.length, eocd = SIZE_MAX;
    if (size > MAX_TOTAL) FAIL(@"The file is too large to be a save package.");
    for (size_t i = size >= 22 ? size - 22 : 0, stop = size > 65557 ? size - 65557 : 0; size >= 22; i--) {
        if (rd32(b + i) == 0x06054b50) { eocd = i; break; }
        if (i == stop) break;
    }
    if (eocd == SIZE_MAX) FAIL(@"This is not a save package (or a damaged one).");
    size_t count = rd16(b + eocd + 10), cdsize = rd32(b + eocd + 12), cdoff = rd32(b + eocd + 16);
    if (count > MAX_FILES || cdoff + cdsize > eocd) FAIL(@"The save package is damaged.");
    NSFileManager *fm = NSFileManager.defaultManager;
    NSMutableArray *names = [NSMutableArray array]; NSMutableSet *seen = [NSMutableSet set];
    uint64_t total = 0;
    for (size_t k = 0, p = cdoff; k < count; k++) {
        if (p + 46 > cdoff + cdsize || rd32(b + p) != 0x02014b50) FAIL(@"The save package is damaged.");
        const unsigned char *h = b + p;
        unsigned flags = rd16(h + 8), method = rd16(h + 10), nlen = rd16(h + 28), xlen = rd16(h + 30), clen = rd16(h + 32);
        uint32_t crc = rd32(h + 16); uint64_t csize = rd32(h + 20), usize = rd32(h + 24), lho = rd32(h + 42);
        if (p + 46 + nlen + xlen + clen > cdoff + cdsize) FAIL(@"The save package is damaged.");
        NSString *name = [[NSString alloc] initWithBytes:h + 46 length:nlen encoding:NSUTF8StringEncoding];
        p += 46 + nlen + xlen + clen;
        if ([name hasSuffix:@"/"] && safe_rel([name substringToIndex:name.length - 1])) continue;   /* a folder entry */
        if (!name || !([name isEqual:@"manifest.json"] || ([name hasPrefix:@"save/"] && safe_rel([name substringFromIndex:5]))))
            FAIL(@"The save package holds a file it should not: %@.", name ?: @"(unreadable name)");
        if ([seen containsObject:name]) FAIL(@"The save package holds %@ twice.", name);
        [seen addObject:name];
        if (flags & 1) FAIL(@"The save package is encrypted.");
        if (method != 0 && method != 8) FAIL(@"The save package uses an unknown compression.");
        if (usize > MAX_FILE || (total += usize) > MAX_TOTAL) FAIL(@"The save package is too large.");
        if (lho + 30 > cdoff || rd32(b + lho) != 0x04034b50) FAIL(@"The save package is damaged.");
        uint64_t data = lho + 30 + rd16(b + lho + 26) + rd16(b + lho + 28);
        if (data + csize > cdoff) FAIL(@"The save package is damaged.");
        NSMutableData *out = [NSMutableData dataWithLength:(NSUInteger)usize];
        if (method == 0) {
            if (csize != usize) FAIL(@"The save package is damaged.");
            if (usize) memcpy(out.mutableBytes, b + data, (size_t)usize);
        } else {
            z_stream s; memset(&s, 0, sizeof s);
            if (inflateInit2(&s, -MAX_WBITS) != Z_OK) FAIL(@"Can't unpack the save package.");
            s.next_in = (Bytef *)(b + data); s.avail_in = (uInt)csize;
            s.next_out = out.mutableBytes; s.avail_out = (uInt)usize;
            int r = inflate(&s, Z_FINISH); uLong got = s.total_out; inflateEnd(&s);
            if (r != Z_STREAM_END || got != usize) FAIL(@"%@ is damaged in the save package.", name);
        }
        if (crc32(0, out.bytes, (uInt)out.length) != crc) FAIL(@"%@ is damaged in the save package (checksum).", name);
        NSString *path = [dest stringByAppendingPathComponent:name];
        if (![fm createDirectoryAtPath:path.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil] ||
            ![out writeToFile:path options:0 error:nil])
            FAIL(@"Can't write %@.", path);
        [names addObject:name];
    }
    return names;
}

/* ---- the manifest and its checks ---- */

static NSString *jstr(NSDictionary *d, NSString *k) { id v = d[k]; return [v isKindOfClass:NSString.class] ? v : nil; }

/* Unpacks and checks a package into staging (a new folder; the saves land in staging/save). */
static TOTKSavePackageInfo *unpack_checked(NSString *package, NSString *gameVersion, NSString *staging, NSError **error) {
    NSFileManager *fm = NSFileManager.defaultManager;
    [fm removeItemAtPath:staging error:nil];
    if (![fm createDirectoryAtPath:staging withIntermediateDirectories:YES attributes:nil error:nil]) FAIL(@"Can't create %@.", staging);
    NSArray *names = zip_read(package, staging, error);
    if (!names) return nil;
    NSData *raw = [NSData dataWithContentsOfFile:[staging stringByAppendingPathComponent:@"manifest.json"]];
    id m = raw ? [NSJSONSerialization JSONObjectWithData:raw options:0 error:nil] : nil;
    if (![m isKindOfClass:NSDictionary.class]) FAIL(@"This is not a save package: it has no readable manifest.json.");
    if (![m[@"format"] isKindOfClass:NSNumber.class] || [m[@"format"] intValue] != FORMAT)
        FAIL(@"The save package was made by a newer version of the app (format %@): update this app first.", m[@"format"]);
    if (![jstr(m, @"title_id") isEqual:TITLE_ID]) FAIL(@"The save package is not for Tears of the Kingdom.");
    NSString *version = jstr(m, @"game_version");
    if (![version isEqual:gameVersion])
        FAIL(@"These saves come from game version %@; this app runs version %@. Saves only move between apps of the same game "
             @"version: update the older app (or build both from the same game update).", version ?: @"(unknown)", gameVersion);
    NSArray *files = [m[@"files"] isKindOfClass:NSArray.class] ? m[@"files"] : nil;
    if (!files.count) FAIL(@"The save package holds no saves.");
    NSMutableSet *listed = [NSMutableSet set]; NSMutableDictionary *times = [NSMutableDictionary dictionary];
    uint64_t bytes = 0;
    NSString *root = [staging stringByAppendingPathComponent:@"save"];
    for (NSDictionary *f in files) {
        NSString *rel = [f isKindOfClass:NSDictionary.class] ? jstr(f, @"path") : nil, *sum = rel ? jstr(f, @"sha256") : nil;
        if (!safe_rel(rel) || sum.length != 64 || ![f[@"size"] isKindOfClass:NSNumber.class] || [listed containsObject:rel])
            FAIL(@"The save package's manifest is damaged.");
        [listed addObject:rel];
        NSData *d = [NSData dataWithContentsOfFile:[root stringByAppendingPathComponent:rel]];
        if (!d) FAIL(@"The save package lacks %@.", rel);
        if (d.length != [f[@"size"] unsignedLongLongValue] || ![sha256(d) isEqual:sum.lowercaseString])
            FAIL(@"%@ in the save package is not what was sent (SHA-256 differs): send it again.", rel);
        bytes += d.length; times[rel] = NSDate.distantPast;
    }
    for (NSString *name in names)
        if (![name isEqual:@"manifest.json"] && ![listed containsObject:[name substringFromIndex:5]])
            FAIL(@"The save package holds %@, which its manifest does not list.", name);
    NSMutableDictionary<NSString *, NSDate *> *sent = [NSMutableDictionary dictionary];
    for (NSDictionary *s in [m[@"slots"] isKindOfClass:NSArray.class] ? m[@"slots"] : @[]) {
        NSString *name = [s isKindOfClass:NSDictionary.class] ? jstr(s, @"name") : nil, *when = name ? jstr(s, @"modified") : nil;
        NSDate *d = when ? [iso() dateFromString:when] : nil;
        if (name && d) sent[name] = d;
    }
    TOTKSavePackageInfo *info = [TOTKSavePackageInfo new];
    info.slots = [slot_times(times).allKeys sortedArrayUsingSelector:@selector(compare:)];
    NSMutableArray *slotTimes = [NSMutableArray array];
    for (NSString *s in info.slots) [slotTimes addObject:sent[s] ?: NSDate.distantPast];
    info.slotTimes = slotTimes;
    info.gameVersion = version; info.saveVersion = jstr(m, @"save_version"); info.titleId = jstr(m, @"title_id");
    info.fromDevice = jstr(m, @"from_device"); info.fromModel = jstr(m, @"from_model"); info.fromPlatform = jstr(m, @"from_platform");
    info.created = jstr(m, @"created") ? [iso() dateFromString:jstr(m, @"created")] : nil;
    info.fileCount = files.count; info.bytes = bytes;
    return info;
}

/* The files of folder are exactly the manifest's (after an import: what was written is what was sent). */
static BOOL same_as_manifest(NSString *folder, NSString *manifest) {
    NSDictionary *m = [NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:manifest] ?: NSData.data options:0 error:nil];
    NSArray *files = [m isKindOfClass:NSDictionary.class] ? m[@"files"] : nil;
    if (![files isKindOfClass:NSArray.class] || save_files(folder).count != files.count) return NO;
    for (NSDictionary *f in files) {
        NSData *d = [NSData dataWithContentsOfFile:[folder stringByAppendingPathComponent:f[@"path"]]];
        if (!d || ![sha256(d) isEqual:[f[@"sha256"] lowercaseString]]) return NO;
    }
    return YES;
}

/* ---- export ---- */

static NSString *file_name_part(NSString *s) {
    NSCharacterSet *bad = [NSCharacterSet characterSetWithCharactersInString:@"/:\\\0"];
    return [[[s componentsSeparatedByCharactersInSet:bad] componentsJoinedByString:@"-"] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
}

NSString *TOTKSaveExport(NSString *profile, NSString *dir, NSString *gameVersion, NSString *platform, NSString *device,
                         NSString *model, NSError **error) {
    NSString *folder = TOTKSaveFolder(profile, NO, nil);
    NSDictionary<NSString *, NSDictionary *> *before = folder ? save_files(folder) : @{};
    if (!before.count) FAIL(@"There are no saves on this device yet.");
    if (before.count > MAX_FILES) FAIL(@"The save folder holds too many files (%lu).", (unsigned long)before.count);
    if (TOTKSaveBusy(profile)) FAIL(@"The game is saving right now. Try again in a moment.");
    NSMutableArray *list = [NSMutableArray array]; NSMutableDictionary *contents = [NSMutableDictionary dictionary], *times = [NSMutableDictionary dictionary];
    uint64_t total = 0;
    for (NSString *rel in [before.allKeys sortedArrayUsingSelector:@selector(compare:)]) {
        if (!safe_rel(rel)) continue;
        NSData *d = [NSData dataWithContentsOfFile:[folder stringByAppendingPathComponent:rel]];
        if (!d) FAIL(@"Can't read %@.", rel);
        if (d.length > MAX_FILE || (total += d.length) > MAX_TOTAL) FAIL(@"The saves are too large to send (%@).", rel);
        contents[rel] = d; times[rel] = [before[rel] fileModificationDate] ?: NSDate.date;
        [list addObject:@{@"path": rel, @"size": @(d.length), @"sha256": sha256(d)}];
    }
    /* A save written while the files were read would mix old and new files: the folder must look the same afterwards. */
    NSDictionary *after = save_files(folder);
    BOOL changed = TOTKSaveBusy(profile) || after.count != before.count;
    for (NSString *rel in before) {
        NSDictionary *a = after[rel], *b = before[rel];
        if (!a || a.fileSize != b.fileSize || ![a.fileModificationDate isEqual:b.fileModificationDate]) changed = YES;
    }
    if (changed) FAIL(@"The game saved while the saves were being packed. Try again in a moment.");
    NSDictionary *slots = slot_times(times); NSMutableArray *slotList = [NSMutableArray array];
    for (NSString *s in [slots.allKeys sortedArrayUsingSelector:@selector(compare:)]) {
        NSUInteger n = 0; for (NSString *rel in contents) if ([rel hasPrefix:[s stringByAppendingString:@"/"]]) n++;
        [slotList addObject:@{@"name": s, @"modified": [iso() stringFromDate:slots[s]], @"files": @(n)}];
    }
    NSString *saveVersion = [[NSString stringWithContentsOfFile:[profile stringByAppendingPathComponent:@"save/.version"] encoding:NSUTF8StringEncoding error:nil]
                             stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    NSMutableDictionary *manifest = [@{@"format": @(FORMAT), @"title_id": TITLE_ID, @"game_version": gameVersion,
        @"from_device": device ?: @"", @"from_model": model ?: @"", @"from_platform": platform ?: @"",
        @"created": [iso() stringFromDate:NSDate.date], @"slots": slotList, @"files": list} mutableCopy];
    if (saveVersion.length) manifest[@"save_version"] = saveVersion;
    NSData *json = [NSJSONSerialization dataWithJSONObject:manifest options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:nil];
    ZipOut z = {NSMutableData.data, NSMutableData.data, 0};
    if (!json || !zip_put(&z, @"manifest.json", json, NSDate.date)) FAIL(@"Can't pack the saves.");
    for (NSDictionary *f in list)
        if (!zip_put(&z, [@"save/" stringByAppendingString:f[@"path"]], contents[f[@"path"]], times[f[@"path"]])) FAIL(@"Can't pack the saves.");
    NSData *zip = zip_finish(&z);

    NSFileManager *fm = NSFileManager.defaultManager;
    if (![fm createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil]) FAIL(@"Can't create %@.", dir);
    for (NSString *old in [fm contentsOfDirectoryAtPath:dir error:nil])
        if ([old.pathExtension isEqual:TOTKSavePackageExtension]) [fm removeItemAtPath:[dir stringByAppendingPathComponent:old] error:nil];
    NSDateFormatter *stamp = [NSDateFormatter new]; stamp.dateFormat = @"yyyy-MM-dd HH.mm";
    stamp.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
    NSString *who = file_name_part(device ?: @""); if (!who.length) who = [platform isEqual:@"ios"] ? @"iPhone" : @"Mac";
    NSString *path = [dir stringByAppendingPathComponent:[NSString stringWithFormat:@"TOTK saves - %@ - %@.%@", who,
                                                          [stamp stringFromDate:NSDate.date], TOTKSavePackageExtension]];
    NSError *why = nil;
    if (![zip writeToFile:path options:NSDataWritingAtomic error:&why]) FAIL(@"Can't write %@: %@", path, why.localizedDescription);
    return path;
}

/* ---- import ---- */

TOTKSavePackageInfo *TOTKSaveInspect(NSString *package, NSString *gameVersion, NSError **error) {
    NSString *staging = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSString stringWithFormat:@"totk-save-inspect-%@", NSUUID.UUID.UUIDString]];
    TOTKSavePackageInfo *info = unpack_checked(package, gameVersion, staging, error);
    [NSFileManager.defaultManager removeItemAtPath:staging error:nil];
    return info;
}

static NSArray<NSString *> *backups_in(NSString *backups) {
    NSMutableArray *out = [NSMutableArray array];
    for (NSString *name in [NSFileManager.defaultManager contentsOfDirectoryAtPath:backups error:nil])
        if ([name hasSuffix:BACKUP_SUFFIX] && name.length == 15 + BACKUP_SUFFIX.length) [out addObject:name];
    return [out sortedArrayUsingSelector:@selector(compare:)];   /* yyyyMMdd-HHmmss: oldest first */
}
static NSDateFormatter *stamp_format(void) {
    NSDateFormatter *f = [NSDateFormatter new]; f.dateFormat = @"yyyyMMdd-HHmmss"; f.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
    return f;
}

NSString *TOTKSaveLastBackup(NSString *backups, NSDate **when) {
    NSString *name = backups_in(backups).lastObject;
    if (when) *when = name ? [stamp_format() dateFromString:[name substringToIndex:15]] : nil;
    return name ? [backups stringByAppendingPathComponent:name] : nil;
}

TOTKSavePackageInfo *TOTKSaveImport(NSString *package, NSString *profile, NSString *backups, NSString *gameVersion, NSError **error) {
    NSFileManager *fm = NSFileManager.defaultManager;
    if (TOTKSaveBusy(profile)) FAIL(@"The game is saving right now. Try again in a moment.");
    NSString *folder = TOTKSaveFolder(profile, YES, error);
    if (!folder) return nil;
    NSString *uid = folder.lastPathComponent, *save = folder.stringByDeletingLastPathComponent;
    NSString *staging = [save stringByAppendingPathComponent:[NSString stringWithFormat:@".%@.import-new", uid]];
    NSString *old = [save stringByAppendingPathComponent:[NSString stringWithFormat:@".%@.import-old", uid]];
    TOTKSavePackageInfo *info = unpack_checked(package, gameVersion, staging, error);
    if (!info) { [fm removeItemAtPath:staging error:nil]; return nil; }
#define IFAIL(...) do { [fm removeItemAtPath:staging error:nil]; FAIL(__VA_ARGS__); } while (0)
    if (save_files(folder).count) {
        NSString *name = [[stamp_format() stringFromDate:NSDate.date] stringByAppendingString:BACKUP_SUFFIX];
        NSString *backup = [backups stringByAppendingPathComponent:name];
        for (int i = 1; [fm fileExistsAtPath:backup] && i < 60; i++)   /* two imports in one second: the next second's name */
            backup = [backups stringByAppendingPathComponent:[[stamp_format() stringFromDate:[NSDate dateWithTimeIntervalSinceNow:i]] stringByAppendingString:BACKUP_SUFFIX]];
        NSError *why = nil;
        if (![fm createDirectoryAtPath:backups withIntermediateDirectories:YES attributes:nil error:&why] ||
            ![fm copyItemAtPath:folder toPath:backup error:&why])
            IFAIL(@"Can't back up the saves on this device, so nothing was changed: %@", why.localizedDescription);
        info.backup = backup;
    }
    if (TOTKSaveBusy(profile)) IFAIL(@"The game started saving. Nothing was changed; try again in a moment.");
    [fm removeItemAtPath:old error:nil];
    if (rename(folder.fileSystemRepresentation, old.fileSystemRepresentation) && errno != ENOENT)
        IFAIL(@"Can't replace the saves (%s). Nothing was changed.", strerror(errno));
    if (rename([staging stringByAppendingPathComponent:@"save"].fileSystemRepresentation, folder.fileSystemRepresentation)) {
        int e = errno; rename(old.fileSystemRepresentation, folder.fileSystemRepresentation);
        IFAIL(@"Can't put the received saves in place (%s). Nothing was changed.", strerror(e));
    }
    [fm removeItemAtPath:old error:nil];
    NSString *versionFile = [save stringByAppendingPathComponent:@".version"];
    [[info.gameVersion stringByAppendingString:@"\n"] writeToFile:versionFile atomically:YES encoding:NSUTF8StringEncoding error:nil];
    BOOL same = same_as_manifest(folder, [staging stringByAppendingPathComponent:@"manifest.json"]);
    [fm removeItemAtPath:staging error:nil];
#undef IFAIL
    if (!same) FAIL(@"The saves written differ from the ones received. The previous saves are in %@.", info.backup ?: @"(none)");
    NSArray *all = backups_in(backups);
    for (NSUInteger i = 0; i + TOTK_SAVE_BACKUPS_KEPT < all.count; i++) [fm removeItemAtPath:[backups stringByAppendingPathComponent:all[i]] error:nil];
    return info;
}
