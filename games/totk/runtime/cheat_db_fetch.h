/* The community cheat list on the Mac (docs/settings.md, Cheats; included by settings_panel_mac.h, so only frontend.m has
   it): the game downloads sthetix/nx-cheats-db's file for its title (cheats/<title ID>.json, every build's cheats with
   their authors' attribution) and keeps this build's part as <build ID>.txt in the cheats-db folder (cheat_db.h), which
   runtime/cheats.c reads first at every start. No codes are in this repository or the app; only download, never upload.

     when     20 s after the window opens, on a utility queue, at most once a day (and when the Cheats tab opens, if the
              day is over); Update cheat list in the tab checks now. The setting Interface > Download cheat list (on by
              default) turns both off; the cached list keeps working.
     how      GET with If-None-Match / If-Modified-Since (an unchanged database answers 304), 20 s timeout, 16 MB cap,
              https only (redirects too). Offline or refused: the previous list stays and the tab says why.
     check    the JSON strictly (cheat_db.h); only this build's sections are kept. The file is rewritten only when they
              changed, so an unchanged list never asks for a restart.
     new      the first list arrives while the game runs: read at once (runtime_cheats_db_arrived), its memory cheats work
              now and its code patches from the next start. A newer list replaces the file and is read at the next start.

   Files (TOTK_CHEATS_DB_DIR; the launcher sets <Application Support>/MacNativeGames/TOTK/cheats-db; 0 = none): <BID>.txt,
   state.json (URL, ETag, Last-Modified, when checked, the last problem). TOTK_CHEATS_DB_URL: another database file (https,
   file:// or http on this Mac, for tests); TOTK_CHEATS_DB_FORCE=1: check even when the last check is less than a day old. */
#pragma once
#import <Foundation/Foundation.h>
#include <time.h>
#include "runtime.h"
#include "settings.h"
#include "game_version.h"
#include "cheat_db.h"

static NSString *const TOTKCheatDbChanged = @"TOTKCheatDbChanged";   /* posted on the main queue when the status changes */
#define CDB_DEFAULT_URL "https://raw.githubusercontent.com/sthetix/nx-cheats-db/main/cheats/" GAME_TITLE_ID ".json"
#define CDB_SOURCE "sthetix/nx-cheats-db"
enum { CDB_CHECK_EVERY_S = 24 * 3600, CDB_TIMEOUT_S = 20, CDB_RESOURCE_S = 90 };

@interface TOTKCheatDbFetch : NSObject <NSURLSessionDataDelegate>
@property(nonatomic) NSMutableData *data;
@property(nonatomic) NSHTTPURLResponse *response;
@property(nonatomic) NSError *error;
@property(nonatomic) NSString *refused;
@property(nonatomic) dispatch_semaphore_t done;
@end
@implementation TOTKCheatDbFetch
- (void)URLSession:(NSURLSession *)s dataTask:(NSURLSessionDataTask *)t didReceiveResponse:(NSURLResponse *)r completionHandler:(void (^)(NSURLSessionResponseDisposition))h {
    (void)s; (void)t;
    self.response = [r isKindOfClass:[NSHTTPURLResponse class]] ? (NSHTTPURLResponse *)r : nil;
    if (r.expectedContentLength > CHEAT_DB_MAX_JSON) { self.refused = @"the file is larger than 16 MB"; h(NSURLSessionResponseCancel); return; }
    h(NSURLSessionResponseAllow);
}
- (void)URLSession:(NSURLSession *)s dataTask:(NSURLSessionDataTask *)t didReceiveData:(NSData *)d {
    (void)s;
    if (self.data.length + d.length > CHEAT_DB_MAX_JSON) { self.refused = @"the file is larger than 16 MB"; [t cancel]; return; }
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

@interface TOTKCheatDb : NSObject
+ (void)boot;                 /* the window is open: check in the background once the game is under way */
+ (void)update:(BOOL)force;   /* check now (force) or if the day is over; nothing when the setting is off (unless forced) */
+ (BOOL)busy;
+ (NSString *)status;         /* one line for the Cheats tab: where the list is from, when it was checked, the last problem */
+ (BOOL)problem;              /* the last check failed */
@end

@implementation TOTKCheatDb
static BOOL cdb_busy;               /* main queue */
static NSDictionary *cdb_state;     /* main queue: the last state.json read */

static NSString *cdb_dir(void) { const char *d = runtime_cheats_db_dir(); return d && *d ? @(d) : nil; }
static NSMutableDictionary *cdb_state_read(NSString *dir) {
    NSData *d = dir ? [NSData dataWithContentsOfFile:[dir stringByAppendingPathComponent:@"state.json"]] : nil;
    id s = d.length && d.length < 65536 ? [NSJSONSerialization JSONObjectWithData:d options:0 error:nil] : nil;
    return [s isKindOfClass:[NSDictionary class]] ? [s mutableCopy] : [NSMutableDictionary dictionary];
}
static void cdb_state_write(NSString *dir, NSDictionary *s) {
    NSData *d = [NSJSONSerialization dataWithJSONObject:s options:NSJSONWritingSortedKeys | NSJSONWritingPrettyPrinted error:nil];
    if (d) [d writeToFile:[dir stringByAppendingPathComponent:@"state.json"] atomically:YES];
}
static int cdb_url_allowed(NSURL *u) {
    if ([u.scheme isEqualToString:@"https"] && u.host.length) return 1;
    if ([u.scheme isEqualToString:@"file"]) return 1;   // tests (TOTK_CHEATS_DB_URL)
    return [u.scheme isEqualToString:@"http"] && ([u.host isEqualToString:@"127.0.0.1"] || [u.host isEqualToString:@"localhost"]);
}
static void cdb_log(NSString *s) { fprintf(stderr, "[cheat-list] %s\n", s.UTF8String); }

/* One check (utility queue). Returns 1 when the list file changed. */
static int cdb_check(BOOL force) {
    NSString *dir = cdb_dir();
    const char *bid = runtime_cheats_build_id();
    if (!dir || !bid || strlen(bid) != 16) { cdb_log(@"no cheat list folder or build ID: nothing to download"); return 0; }
    NSFileManager *fm = NSFileManager.defaultManager;
    [fm createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    const char *ue = getenv("TOTK_CHEATS_DB_URL"), *fe = getenv("TOTK_CHEATS_DB_FORCE");
    NSURL *url = [NSURL URLWithString:ue && *ue ? @(ue) : @CDB_DEFAULT_URL];
    NSMutableDictionary *st = cdb_state_read(dir);
    NSString *path = [dir stringByAppendingPathComponent:[NSString stringWithFormat:@"%s.txt", bid]];
    if (!url || !cdb_url_allowed(url)) { st[@"problem"] = @"the cheat list address is not https"; cdb_state_write(dir, st); return 0; }
    BOOL same = [st[@"url"] isEqual:url.absoluteString] && [st[@"build"] isEqual:@(bid)];
    BOOL have = [fm fileExistsAtPath:path];
    double checked = [st[@"checked"] doubleValue], now = (double)time(NULL);
    if (same && now - checked >= 0 && now - checked < CDB_CHECK_EVERY_S && !force && !(fe && !strcmp(fe, "1"))) {
        cdb_log([NSString stringWithFormat:@"checked %.1f h ago; the next check is in %.1f h", (now - checked) / 3600, (CDB_CHECK_EVERY_S - (now - checked)) / 3600]);
        return 0;
    }
    NSData *body = nil; NSInteger status = 0; NSString *etag = nil, *modified = nil, *problem = nil;
    if (url.isFileURL) {
        NSDictionary *a = [fm attributesOfItemAtPath:url.path error:nil];
        if (a && [a fileSize] <= CHEAT_DB_MAX_JSON) body = [NSData dataWithContentsOfURL:url];
        status = body ? 200 : 404;
    } else {
        NSMutableURLRequest *rq = [NSMutableURLRequest requestWithURL:url cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:CDB_TIMEOUT_S];
        [rq setValue:@"NativeTOTK-cheat-list/1" forHTTPHeaderField:@"User-Agent"];
        if (same && have && [st[@"etag"] isKindOfClass:[NSString class]]) [rq setValue:st[@"etag"] forHTTPHeaderField:@"If-None-Match"];
        if (same && have && [st[@"last_modified"] isKindOfClass:[NSString class]]) [rq setValue:st[@"last_modified"] forHTTPHeaderField:@"If-Modified-Since"];
        NSURLSessionConfiguration *c = NSURLSessionConfiguration.ephemeralSessionConfiguration;
        c.URLCache = nil; c.HTTPCookieStorage = nil; c.HTTPShouldSetCookies = NO; c.URLCredentialStorage = nil;
        c.timeoutIntervalForRequest = CDB_TIMEOUT_S; c.timeoutIntervalForResource = CDB_RESOURCE_S; c.waitsForConnectivity = NO;
        c.TLSMinimumSupportedProtocolVersion = tls_protocol_version_TLSv12;
        TOTKCheatDbFetch *f = [TOTKCheatDbFetch new]; f.data = [NSMutableData data]; f.done = dispatch_semaphore_create(0);
        NSURLSession *session = [NSURLSession sessionWithConfiguration:c delegate:f delegateQueue:nil];
        [[session dataTaskWithRequest:rq] resume];
        dispatch_semaphore_wait(f.done, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(CDB_RESOURCE_S + 5) * NSEC_PER_SEC));
        [session invalidateAndCancel];
        if (f.refused) problem = f.refused;
        else if (f.error || !f.response) problem = f.error ? f.error.localizedDescription : @"no answer";
        else {
            status = f.response.statusCode; body = f.data;
            etag = f.response.allHeaderFields[@"Etag"] ?: f.response.allHeaderFields[@"ETag"];
            modified = f.response.allHeaderFields[@"Last-Modified"];
        }
    }
    int wrote = 0;
    if (!problem && status == 304) cdb_log(@"the cheat database has not changed (HTTP 304)");
    else if (!problem && status != 200) problem = [NSString stringWithFormat:@"the server answered %ld", (long)status];
    else if (!problem) {
        static char text[CHEAT_DB_MAX_TEXT + 1];
        char err[200];
        size_t used = (size_t)snprintf(text, sizeof text,
            "# The community cheat list for build %s, from https://github.com/" CDB_SOURCE " (%s), kept by the game.\n"
            "# The codes are their authors' (see the database's attribution); none come with the app. Rewritten when the list changes.\n\n",
            bid, url.lastPathComponent.UTF8String);
        int n = cheat_db_extract(body.bytes, body.length, bid, text, sizeof text, &used, err, sizeof err);
        if (n < 0) problem = [NSString stringWithFormat:@"the download was refused: %s", err];
        else if (n == 0) { st[@"sections"] = @0; cdb_log([NSString stringWithFormat:@"the database has no cheats for build %s", bid]); }
        else {
            NSData *now_text = [NSData dataWithBytes:text length:used];
            if (![[NSData dataWithContentsOfFile:path] isEqualToData:now_text]) {
                if ([now_text writeToFile:path atomically:YES]) { wrote = 1; st[@"changed"] = @(now); }
                else problem = @"cannot write the cheat list";
            }
            st[@"sections"] = @(n);
            cdb_log([NSString stringWithFormat:@"%s: %d sections for build %s (%lu KB downloaded)", wrote ? "new cheat list" : "the cheat list is unchanged",
                     n, bid, (unsigned long)(body.length >> 10)]);
        }
        if (!problem) {
            if ([etag isKindOfClass:[NSString class]] && etag.length < 256) st[@"etag"] = etag; else [st removeObjectForKey:@"etag"];
            if ([modified isKindOfClass:[NSString class]] && modified.length < 256) st[@"last_modified"] = modified; else [st removeObjectForKey:@"last_modified"];
        }
    }
    if (problem) {
        st[@"problem"] = problem; st[@"problem_at"] = @(now);
        cdb_log([NSString stringWithFormat:@"could not check the cheat list (%@); %@", problem, have ? @"the cached list stays" : @"the next start tries again"]);
    } else {
        [st removeObjectForKey:@"problem"]; [st removeObjectForKey:@"problem_at"];
        st[@"checked"] = @(now); st[@"url"] = url.absoluteString; st[@"build"] = @(bid);
    }
    cdb_state_write(dir, st);
    return wrote;
}

static dispatch_queue_t cdb_queue(void) {
    static dispatch_queue_t q; static dispatch_once_t once;
    dispatch_once(&once, ^{ q = dispatch_queue_create("totk.cheat-list", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, 0)); });
    return q;
}
+ (void)boot {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{ [self update:NO]; });
}
+ (void)update:(BOOL)force {
    if (cdb_busy || !cdb_dir() || (!force && !settings_get("interface.download_cheats"))) return;
    cdb_busy = YES;
    [NSNotificationCenter.defaultCenter postNotificationName:TOTKCheatDbChanged object:nil];
    dispatch_async(cdb_queue(), ^{
        int wrote = cdb_check(force);
        NSDictionary *st = cdb_state_read(cdb_dir());
        dispatch_async(dispatch_get_main_queue(), ^{
            cdb_busy = NO; cdb_state = st;
            if (wrote) runtime_cheats_db_arrived();   /* the first list: read now; a newer one: at the next start */
            [NSNotificationCenter.defaultCenter postNotificationName:TOTKCheatDbChanged object:nil];
        });
    });
}
+ (BOOL)busy { return cdb_busy; }
+ (BOOL)problem { if (!cdb_state) cdb_state = cdb_state_read(cdb_dir()); return [cdb_state[@"problem"] isKindOfClass:[NSString class]]; }
+ (NSString *)status {
    if (!cdb_state) cdb_state = cdb_state_read(cdb_dir());
    if (cdb_busy) return @"Checking for a newer list…";
    NSString *(^ago)(double) = ^NSString *(double t) {
        NSRelativeDateTimeFormatter *f = [NSRelativeDateTimeFormatter new]; f.unitsStyle = NSRelativeDateTimeFormatterUnitsStyleFull;
        return [f localizedStringForDate:[NSDate dateWithTimeIntervalSince1970:t] relativeToDate:NSDate.date];
    };
    double checked = [cdb_state[@"checked"] doubleValue], changed = [cdb_state[@"changed"] doubleValue];
    NSMutableString *s = [NSMutableString string];
    if ([cdb_state[@"problem"] isKindOfClass:[NSString class]]) [s appendFormat:@"Could not check: %@. ", cdb_state[@"problem"]];
    if (checked > 0) [s appendFormat:@"Checked %@", ago(checked)];
    if (changed > 0) [s appendFormat:@"%@list updated %@", checked > 0 ? @"; " : @"List updated ", ago(changed)];
    if (checked > 0 || changed > 0) [s appendString:@"."];
    if (!s.length) [s appendString:@"Not downloaded yet."];
    return s;
}
@end
