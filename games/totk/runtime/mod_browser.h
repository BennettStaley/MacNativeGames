/* The mod browser (runtime/mod_browser.m): finds Tears of the Kingdom mods on GameBanana and installs them into the mods
   folder (runtime/mods.h has the layout). Shared by the Mac menu (runtime/mods_panel_mac.h) and the iPhone / iPad menu
   (ios/Sources/mods_menu.m); Foundation only, so it builds for both.

   GameBanana's public API (apiv11, no account, no key) is read for the lists (Mod/Index, filtered to the game 7617), the
   categories and one mod's page (Mod/<id>/ProfilePage: text, pictures, license, files). Lists and pages are kept a few
   minutes, pictures for good, in a small cache under Caches/TOTKModBrowser. A file is downloaded straight from GameBanana
   to the device, checked against its MD5, unpacked (Mac: zip, 7z and RAR with /usr/bin/bsdtar; iPhone / iPad: zip, with
   the small extractor here) and arranged as <mods>/<Mod>/romfs|exefs + mod.json. Nothing is re-hosted. docs/mod-browser.md. */
#pragma once
#import <Foundation/Foundation.h>

typedef NS_ENUM(NSInteger, TOTKModSort) {
    TOTKModSortMostDownloaded, TOTKModSortNewest, TOTKModSortLatestUpdated, TOTKModSortMostLiked,
};
NSArray<NSString *> *TOTKModSortLabels(void);   /* "Most downloaded", ... in TOTKModSort order */

extern NSString *const TOTKModsChangedNotification;   /* posted on the main queue after an install */
extern NSString *const TOTKModBrowserFooter;           /* "Mods are downloaded from GameBanana ... respect each mod's license" */
extern NSString *const TOTKModArchiveNotOnIOS;         /* the notice for 7z / RAR on iPhone and iPad */

/* One result in a list. */
@interface TOTKModSummary : NSObject
@property (nonatomic) int64_t modId;
@property (nonatomic, copy) NSString *name, *author, *category, *version, *thumbURL, *profileURL;
@property (nonatomic, strong) NSDate *updated;
@property (nonatomic) NSInteger likes, views;
@end

/* One downloadable file of a mod, with what its name and description say about the game version. */
@interface TOTKModFile : NSObject
@property (nonatomic) int64_t fileId;
@property (nonatomic, copy) NSString *name, *fileDescription, *downloadURL, *md5, *avResult;
@property (nonatomic) int64_t bytes;
@property (nonatomic) NSInteger downloads;
@property (nonatomic, strong) NSDate *added;
@property (nonatomic, copy) NSString *gameVersions;   /* "1.4.x", "1.2.1", "all versions", "" (not stated) */
@property (nonatomic) int versionScore;                /* 3 names 1.4.3 / all versions ... 0 names only older versions */
@property (nonatomic) BOOL recommended, oldVersion, tkmmOnly;
@property (nonatomic, readonly) NSString *archiveKind;   /* "zip", "7z", "rar" or "" (from the name) */
@property (nonatomic, readonly) BOOL canInstallHere;     /* an archive this device can open */
@property (nonatomic, readonly) NSString *versionLabel;  /* "For 1.4.x", "All game versions", "Version not stated" */
@property (nonatomic, readonly) NSString *notice;        /* the warning line, or nil */
@property (nonatomic, readonly) NSString *sizeText;      /* "1.4 MB" */
@end

/* One mod's page. */
@interface TOTKModDetails : NSObject
@property (nonatomic) int64_t modId;
@property (nonatomic, copy) NSString *name, *author, *version, *category, *summary, *text, *license, *profileURL;
@property (nonatomic, strong) NSDate *updated;
@property (nonatomic) NSInteger downloads, likes, views;
@property (nonatomic, copy) NSArray<NSString *> *imageURLs, *thumbURLs;   /* 530 px and 220 px versions, same order */
@property (nonatomic, copy) NSArray<TOTKModFile *> *files;                 /* best first; the first may be .recommended */
@end

typedef void (^TOTKModProgress)(double fraction, NSString *stage);   /* fraction < 0: no total known; main queue */
typedef void (^TOTKModInstalled)(NSString *folder, NSString *note, NSError *error);   /* note: extra info, or nil */

@interface TOTKModBrowser : NSObject
+ (instancetype)shared;
+ (BOOL)canOpenArchiveKind:(NSString *)kind;   /* Mac: zip, 7z, rar; iPhone / iPad: zip */
/* All completions run on the main queue. */
- (void)listPage:(NSInteger)page sort:(TOTKModSort)sort search:(NSString *)text category:(int64_t)category
      completion:(void (^)(NSArray<TOTKModSummary *> *mods, NSInteger total, NSError *error))done;
- (void)categories:(void (^)(NSArray<NSDictionary *> *categories, NSError *error))done;   /* @{@"id", @"name", @"count"} */
- (void)details:(int64_t)modId completion:(void (^)(TOTKModDetails *mod, NSError *error))done;
- (void)image:(NSString *)url completion:(void (^)(NSData *data))done;   /* nil on failure */
- (void)install:(TOTKModFile *)file of:(TOTKModDetails *)mod progress:(TOTKModProgress)progress completion:(TOTKModInstalled)done;
- (void)cancelInstall:(int64_t)fileId;
- (BOOL)isInstalling:(int64_t)fileId;
- (NSString *)installedFolderFor:(int64_t)modId;   /* the folder of an installed mod from GameBanana, or nil */
@end

/* The parts the probe (tests/mod_browser_probe.m) calls directly. */
NSString *mod_browser_versions(NSString *text, int *score, BOOL *old, BOOL *tkmm);   /* gameVersions for a name + description */
NSString *mod_browser_plain_text(NSString *html);
BOOL mod_browser_unzip(NSString *zip, NSString *dest, NSError **error);
BOOL mod_browser_extract(NSString *archive, NSString *dest, NSError **error);   /* by its first bytes */
/* Unpacks a downloaded archive and arranges it under `modsDir` (mod.json written); returns the folder names made, the
   one to switch on first. Does not touch mods.txt (the caller scans and saves). */
NSArray<NSString *> *mod_browser_install_archive(NSString *archive, NSString *modsDir, TOTKModDetails *mod, TOTKModFile *file,
                                                 NSString **note, NSError **error);
