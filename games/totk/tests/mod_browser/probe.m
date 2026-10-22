/* The mod browser's probe (runtime/mod_browser.m): offline checks of the version guesses, the HTML text, the zip
   extractor (path traversal, deflate, stored) and how archives are arranged into mod folders; with --live it also lists
   GameBanana, reads one mod's page and installs its recommended file into TOTK_MODS_DIR (set it to a scratch folder).
     make mod-browser-probe && TOTK_MODS_DIR=/tmp/mods build/tests/mod_browser_probe [--live [mod id]] [--file <mod id> <file id> ...] */
#import <Foundation/Foundation.h>
#include <zlib.h>
#import <objc/runtime.h>
#import "mod_browser.h"
#include "mods.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static void wait_for(BOOL *done) { while (!*done) [NSRunLoop.mainRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]]; }

/* ---- a tiny zip writer (stored entries) for the extractor's checks ---- */
static void put16(NSMutableData *d, unsigned v) { uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)}; [d appendBytes:b length:2]; }
static void put32(NSMutableData *d, uint32_t v) { put16(d, v & 0xffff); put16(d, v >> 16); }
static NSData *stored_zip(NSDictionary<NSString *, NSString *> *files) {
    NSMutableData *z = [NSMutableData data], *cd = [NSMutableData data];
    for (NSString *name in [files.allKeys sortedArrayUsingSelector:@selector(compare:)]) {
        NSData *n = [name dataUsingEncoding:NSUTF8StringEncoding], *body = [files[name] dataUsingEncoding:NSUTF8StringEncoding];
        uint32_t crc = (uint32_t)crc32(0, body.bytes, (uInt)body.length), off = (uint32_t)z.length;
        put32(z, 0x04034b50); put16(z, 20); put16(z, 0x800); put16(z, 0); put32(z, 0); put32(z, crc);
        put32(z, (uint32_t)body.length); put32(z, (uint32_t)body.length); put16(z, (unsigned)n.length); put16(z, 0);
        [z appendData:n]; [z appendData:body];
        put32(cd, 0x02014b50); put16(cd, 20); put16(cd, 20); put16(cd, 0x800); put16(cd, 0); put32(cd, 0); put32(cd, crc);
        put32(cd, (uint32_t)body.length); put32(cd, (uint32_t)body.length); put16(cd, (unsigned)n.length); put16(cd, 0); put16(cd, 0);
        put16(cd, 0); put16(cd, 0); put32(cd, 0); put32(cd, off); [cd appendData:n];
    }
    uint32_t cdoff = (uint32_t)z.length; [z appendData:cd];
    put32(z, 0x06054b50); put16(z, 0); put16(z, 0); put16(z, (unsigned)files.count); put16(z, (unsigned)files.count);
    put32(z, (uint32_t)cd.length); put32(z, cdoff); put16(z, 0);
    return z;
}

static NSString *tree(NSString *dir) {
    NSMutableString *s = [NSMutableString string];
    for (NSString *rel in [[NSFileManager.defaultManager enumeratorAtPath:dir].allObjects sortedArrayUsingSelector:@selector(compare:)]) [s appendFormat:@"  %@\n", rel];
    return s;
}

static TOTKModDetails *fake_mod(NSString *name) {
    TOTKModDetails *m = [TOTKModDetails new]; m.modId = 1; m.name = name; m.author = @"probe"; m.version = @"1"; m.license = @"test";
    m.profileURL = @"https://gamebanana.com/mods/1"; return m;
}
static TOTKModFile *fake_file(NSString *name) { TOTKModFile *f = [TOTKModFile new]; f.fileId = 2; f.name = name; f.gameVersions = @""; return f; }

static void offline(NSString *scratch) {
    struct { const char *text, *want; int score; } v[] = {
        {"serfrosts_base_defogger_v2-4.zip Base Defogger, All Game Update Versions", "all versions", 3},
        {"ultracam_261_beyond.zip Version 2.6.1", "", 1},
        {"the_second_quest_143.zip Game Version 1.4.3", "1.4.3", 3},
        {"the_second_quest_121.zip Game Version 1.2.1", "1.2.1", 0},
        {"the_second_quest_140-143_759d0.rar [TKMM Only] TKCL for Game Versions 1.4.0-1.4.3", "1.4.x", 0},
        {"unbreakable_v141_ab914.7z v1.4.1", "1.4.1", 2},
        {"nolootboxes-v3_0_0.zip For TotK v1.2.x", "1.2.x", 0},
        {"bettersagesmod_v112.zip Support for all versions 1.0.0 - v1.1.2", "1.0.0, 1.1.x", 0},
        {"rijumod_140_a5b2b.zip For versions 1.4.0 to 1.4.2", "1.4.0, 1.4.1, 1.4.2", 2},
        {"y04nn_totk_1_4.zip V1.4", "", 1},
        {"v2_double_the_durability_all_versions.zip", "all versions", 3},
        {"1korok_expandv111.zip", "1.1.1", 0},
        {"100_complete_save_legit.zip", "", 1},
        {"4k_v12s.zip 4K v1.2 w/ Shadow Scaling", "", 1},
    };
    for (size_t i = 0; i < sizeof v / sizeof *v; i++) {
        int score; BOOL old;
        NSString *got = mod_browser_versions(@(v[i].text), &score, &old, NULL);
        CHECK([got isEqual:@(v[i].want)] && score == v[i].score, "versions(%s) = \"%s\" score %d, want \"%s\" %d", v[i].text, got.UTF8String, score, v[i].want, v[i].score);
    }
    NSString *plain = mod_browser_plain_text(@"<b>Hi</b> &amp; welcome<br>Line two<ul><li>one</li><li>two&#8217;s</li></ul>&nbsp;");
    CHECK([plain isEqual:@"Hi & welcome\nLine two\n• one\n• two’s"], "plain text: %s", plain.UTF8String);

    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *t = [scratch stringByAppendingPathComponent:@"offline"]; [fm removeItemAtPath:t error:nil];
    [fm createDirectoryAtPath:t withIntermediateDirectories:YES attributes:nil error:nil];
    NSError *e = nil;
    /* path traversal and absolute names refuse the archive */
    for (NSString *bad in @[@"../evil.txt", @"a/../../evil.txt", @"/abs.txt", @"C:\\win.txt"]) {
        NSString *z = [t stringByAppendingPathComponent:@"bad.zip"]; [stored_zip(@{bad: @"x", @"ok.txt": @"y"}) writeToFile:z atomically:YES];
        e = nil; BOOL ok = mod_browser_unzip(z, [t stringByAppendingPathComponent:@"bad"], &e);
        CHECK(!ok && ![fm fileExistsAtPath:[scratch stringByAppendingPathComponent:@"evil.txt"]], "unzip accepted %s", bad.UTF8String);
    }
    /* deflate (made by /usr/bin/zip) through the built-in extractor, then each layout */
    NSDictionary *layouts = @{
        @"wrapped": @[@"Wrapped Mod/romfs/Pack/a.pack.zs", @"Wrapped Mod/exefs/main.pchtxt", @"Wrapped Mod/readme.txt"],
        @"bare": @[@"Pack/Actor/b.pack.zs", @"UI/c.bfres"],
        @"variants": @[@"1.2.1/Mod/romfs/Pack/x.zs", @"1.4.x/Mod/romfs/Pack/x.zs", @"__MACOSX/1.4.x/._x.zs"],
        @"tkcl": @[@"thing.tkcl"],
        @"atmosphere": @[@"atmosphere/contents/0100F2C0115B6000/romfs/Model/m.bfres"],
        @"nothing": @[@"preset.ini", @"readme.txt"]};
    for (NSString *kind in [layouts.allKeys sortedArrayUsingSelector:@selector(compare:)]) {
        NSString *src = [t stringByAppendingPathComponent:[@"src-" stringByAppendingString:kind]];
        for (NSString *f in layouts[kind]) {
            NSString *p = [src stringByAppendingPathComponent:f];
            [fm createDirectoryAtPath:p.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil];
            [[@"" stringByPaddingToLength:5000 withString:@"zelda " startingAtIndex:0] writeToFile:p atomically:YES encoding:NSUTF8StringEncoding error:nil];
        }
        NSString *zip = [t stringByAppendingPathComponent:[kind stringByAppendingPathExtension:@"zip"]];
        NSTask *task = [NSTask new]; task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/zip"]; task.currentDirectoryURL = [NSURL fileURLWithPath:src];
        task.arguments = @[@"-qr", zip, @"."]; [task launchAndReturnError:nil]; [task waitUntilExit];
        NSString *mods = [t stringByAppendingPathComponent:[@"mods-" stringByAppendingString:kind]];
        NSString *note = nil; e = nil;
        NSArray *made = mod_browser_install_archive(zip, mods, fake_mod(@"Probe: Mod"), fake_file(kind), &note, &e);
        printf("layout %-10s -> %s%s%s\n%s", kind.UTF8String, made.count ? [made componentsJoinedByString:@" | "].UTF8String : "(none)",
               e ? "  error: " : "", e ? e.localizedDescription.UTF8String : "", tree(mods).UTF8String);
        if (note) printf("  note: %s\n", note.UTF8String);
        if ([kind isEqual:@"nothing"]) { CHECK(!made.count && e, "an archive without mod files was installed"); continue; }
        CHECK(made.count, "%s: nothing installed (%s)", kind.UTF8String, e.localizedDescription.UTF8String);
        NSString *first = [mods stringByAppendingPathComponent:made.firstObject ?: @"?"];
        CHECK([fm fileExistsAtPath:[first stringByAppendingPathComponent:@"mod.json"]], "%s: no mod.json", kind.UTF8String);
        if ([kind isEqual:@"wrapped"]) CHECK([fm fileExistsAtPath:[first stringByAppendingPathComponent:@"romfs/Pack/a.pack.zs"]] && [fm fileExistsAtPath:[first stringByAppendingPathComponent:@"exefs/main.pchtxt"]], "wrapped layout");
        if ([kind isEqual:@"bare"]) CHECK([fm fileExistsAtPath:[first stringByAppendingPathComponent:@"romfs/Pack/Actor/b.pack.zs"]], "bare layout");
        if ([kind isEqual:@"variants"]) CHECK(made.count == 2 && [made[0] containsString:@"1.4.x"], "variants: 1.4.x first: %s", [made componentsJoinedByString:@","].UTF8String);
        if ([kind isEqual:@"tkcl"]) CHECK([fm fileExistsAtPath:[first stringByAppendingPathComponent:@"thing.tkcl"]], "tkcl copied");
        if ([kind isEqual:@"atmosphere"]) CHECK([fm fileExistsAtPath:[first stringByAppendingPathComponent:@"romfs/Model/m.bfres"]], "atmosphere layout");
        CHECK(![made.firstObject hasPrefix:@"-"] && ![made.firstObject containsString:@":"], "folder name sanitised: %s", [made.firstObject UTF8String]);
    }
    [fm removeItemAtPath:t error:nil];
}

static void live(int64_t modId, NSArray<NSNumber *> *fileIds) {
    TOTKModBrowser *b = TOTKModBrowser.shared; __block BOOL done = NO;
    [b listPage:1 sort:TOTKModSortMostDownloaded search:nil category:0 completion:^(NSArray<TOTKModSummary *> *mods, NSInteger total, NSError *error) {
        printf("top mods (%ld on GameBanana)%s\n", (long)total, error ? error.localizedDescription.UTF8String : "");
        for (NSUInteger i = 0; i < MIN(10u, mods.count); i++)
            printf("  %2lu. %-50.50s %-18.18s %5ld likes %8ld views  [%lld]\n", (unsigned long)i + 1, mods[i].name.UTF8String, mods[i].author.UTF8String,
                   (long)mods[i].likes, (long)mods[i].views, mods[i].modId);
        CHECK(mods.count >= 10, "list returned %lu", (unsigned long)mods.count);
        done = YES;
    }];
    wait_for(&done); done = NO;
    [b listPage:1 sort:TOTKModSortMostDownloaded search:@"defogger" category:0 completion:^(NSArray<TOTKModSummary *> *mods, NSInteger total, NSError *error) {
        (void)error; printf("search \"defogger\": %ld:", (long)total); for (TOTKModSummary *m in mods) printf(" [%lld %s]", m.modId, m.name.UTF8String); printf("\n"); done = YES; }];
    wait_for(&done); done = NO;
    [b categories:^(NSArray<NSDictionary *> *cats, NSError *error) {
        (void)error; printf("categories:"); for (NSDictionary *c in cats) printf(" %s(%ld)", [c[@"name"] UTF8String], [c[@"count"] longValue]); printf("\n"); done = YES; }];
    wait_for(&done); done = NO;
    __block TOTKModDetails *mod = nil;
    [b details:modId completion:^(TOTKModDetails *m, NSError *error) { mod = m; if (error) printf("details: %s\n", error.localizedDescription.UTF8String); done = YES; }];
    wait_for(&done); done = NO;
    if (!mod) { CHECK(0, "no details"); return; }
    printf("\n%s by %s, version %s, %ld downloads, updated %s, %lu pictures\nlicense: %s\nsummary: %s\ntext: %.300s...\n",
           mod.name.UTF8String, mod.author.UTF8String, mod.version.UTF8String, (long)mod.downloads, mod.updated.description.UTF8String,
           (unsigned long)mod.imageURLs.count, mod.license.UTF8String, mod.summary.UTF8String, mod.text.UTF8String);
    for (TOTKModFile *f in mod.files)
        printf("  %s %-52s %9s  %-22s score %d%s%s\n", f.recommended ? "*" : " ", f.name.UTF8String, f.sizeText.UTF8String, f.versionLabel.UTF8String,
               f.versionScore, f.notice ? "  ! " : "", f.notice ? [f.notice stringByReplacingOccurrencesOfString:@"\n" withString:@" / "].UTF8String : "");
    __block NSData *pic = nil;
    [b image:mod.thumbURLs.firstObject completion:^(NSData *d) { pic = d; done = YES; }];
    wait_for(&done); done = NO;
    printf("thumbnail: %lu bytes\n", (unsigned long)pic.length);
    NSMutableArray<TOTKModFile *> *picks = [NSMutableArray array];
    if (mod.files.firstObject.recommended) [picks addObject:mod.files.firstObject];
    for (NSNumber *fid in fileIds) {   /* other mods' files: their page first */
        __block TOTKModDetails *owner = nil;
        int64_t owner_id = fid.longLongValue >> 32; int64_t file_id = fid.longLongValue & 0xffffffff;
        [b details:owner_id completion:^(TOTKModDetails *m, NSError *error) { (void)error; owner = m; done = YES; }];
        wait_for(&done); done = NO;
        for (TOTKModFile *f in owner.files) if (f.fileId == file_id) { [picks addObject:f]; objc_setAssociatedObject(f, "owner", owner, OBJC_ASSOCIATION_RETAIN); }
    }
    for (TOTKModFile *f in picks) {
        TOTKModDetails *owner = objc_getAssociatedObject(f, "owner") ?: mod;
        printf("\ninstalling %s (%s, %s) from %s\n", f.name.UTF8String, f.archiveKind.UTF8String, f.sizeText.UTF8String, owner.name.UTF8String);
        __block int last = -1;
        [b install:f of:owner progress:^(double fraction, NSString *stage) {
            int pct = fraction < 0 ? -1 : (int)(fraction * 100);
            if (pct != last && (pct < 0 || pct % 25 == 0)) { printf("  %s %d%%\n", stage.UTF8String, pct); last = pct; }
        } completion:^(NSString *folder, NSString *note, NSError *error) {
            printf("  -> %s%s%s\n", folder ? folder.UTF8String : "failed: ", error ? error.localizedDescription.UTF8String : "", note ? [@"\n  note: " stringByAppendingString:note].UTF8String : "");
            CHECK(folder, "install of %s failed", f.name.UTF8String);
            if (folder) {
                NSString *dir = [@(mods_dir()) stringByAppendingPathComponent:folder];
                printf("%s", tree(dir).UTF8String);
                printf("mod.json:\n%s\n", [NSString stringWithContentsOfFile:[dir stringByAppendingPathComponent:@"mod.json"] encoding:NSUTF8StringEncoding error:nil].UTF8String);
            }
            done = YES;
        }];
        wait_for(&done); done = NO;
    }
    printf("mods.txt:\n%s", [NSString stringWithContentsOfFile:[@(mods_dir()) stringByAppendingPathComponent:@"mods.txt"] encoding:NSUTF8StringEncoding error:nil].UTF8String);
    ModInfo info;
    for (int i = 0; i < mods_count(); i++) if (!mods_get(i, &info)) printf("  %d. %s [%s] %s gb=%lld\n", i + 1, info.folder, info.enabled ? "on" : "off", info.game_versions, info.gamebanana_id);
    printf("installedFolderFor(%lld) = %s\n", modId, [b installedFolderFor:modId].UTF8String);
}

int main(int argc, char **argv) {
    @autoreleasepool {
        NSString *scratch = NSTemporaryDirectory();
        offline(scratch);
        BOOL go = NO; int64_t modId = 446995; NSMutableArray *files = [NSMutableArray array];
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "--live")) { go = YES; if (i + 1 < argc && argv[i + 1][0] != '-') modId = atoll(argv[++i]); }
            else if (!strcmp(argv[i], "--file") && i + 2 < argc) { int64_t m = atoll(argv[++i]), f = atoll(argv[++i]); [files addObject:@(m << 32 | f)]; go = YES; }
        }
        if (go) { mods_scan(); live(modId, files); }
        printf("%s (%d failures)\n", failures ? "FAILED" : "ok", failures);
        return failures != 0;
    }
}
