/* Save packages (.totksave): one file with a profile's saves, sent with AirDrop (or any other way) between the Mac app and
   the iPhone / iPad app (docs/save-sync.md). Foundation, CommonCrypto and zlib only, so the Mac launcher (app/launcher.m),
   the game's Mac menu (runtime/settings_panel_mac.h) and the iOS app (ios/Sources/saves_menu.m) share it.

   The file is a zip: manifest.json first, then save/<path> for every file of the profile's save folder (save/<uid>/:
   slot_*, album, storage, option.sav; hidden files such as the .busy / .version markers are left out, and nothing outside
   the save folder goes: no account.uid, cache, temp or pso list). manifest.json:
     {"format": 1, "title_id": "0100F2C0115B6000", "game_version": "1.4.3", "save_version": "1.4.3" (save/.version, if any),
      "from_device": "Bennett's MacBook Pro", "from_model": "Mac16,10", "from_platform": "mac" | "ios",
      "created": "2026-10-20T15:31:02Z", "slots": [{"name": "slot_00", "modified": "...", "files": 4}],
      "files": [{"path": "slot_00/progress.sav", "size": 1234, "sha256": "..."}]}
   An import checks the format, the title, that the package's game version is this app's own, that the zip holds exactly
   the files the manifest lists, each with its size and SHA-256, and that no path leaves the save folder. The account is
   mapped: the files go into THIS profile's own save/<uid>/ (the game opens only its own account's folder, fs_host.c);
   account.uid is never sent. The save folder it replaces is copied to <backups>/<time>-before-import first (the newest
   KEEP are kept), the new folder is complete before the old one moves aside, and save/.version is set to the package's. */
#pragma once
#import <Foundation/Foundation.h>

extern NSString *const TOTKSavePackageExtension;   /* "totksave" */
extern NSString *const TOTKSavePackageType;        /* "org.macnativegames.totk.save" (Info.plist: exported type, document type) */
enum { TOTK_SAVE_BACKUPS_KEPT = 10 };

/* What a package holds (TOTKSaveInspect), or what an import did (TOTKSaveImport: .backup). */
@interface TOTKSavePackageInfo : NSObject
@property (nonatomic, copy) NSString *gameVersion, *saveVersion, *titleId, *fromDevice, *fromModel, *fromPlatform;
@property (nonatomic, strong) NSDate *created;
@property (nonatomic, copy) NSArray<NSString *> *slots;          /* "slot_00", ... */
@property (nonatomic, copy) NSArray<NSDate *> *slotTimes;        /* same order; the newest file of each slot */
@property (nonatomic) NSUInteger fileCount;
@property (nonatomic) uint64_t bytes;
@property (nonatomic, copy) NSString *backup;                    /* after an import: the replaced save's copy (nil: there was none) */
@property (nonatomic, readonly) NSString *fromText;               /* "Bennett's iPhone (iPhone, iOS)" */
@property (nonatomic, readonly) NSString *slotsText;              /* one line per slot: "slot_00 — 20 Oct 2026 at 15:31" */
@end

/* <profile>/save/<uid hex> from <profile>/account.uid, or nil. create: make account.uid (16 random bytes, as
   runtime/account.c does on a first start) when there is none yet, and the folder. */
NSString *TOTKSaveFolder(NSString *profile, BOOL create, NSError **error);
/* YES while the game writes a save into this profile: <profile>/save/.busy younger than 10 minutes (runtime/fs_host.c). A
   game in the same process also asks runtime_fs_save_busy(). */
BOOL TOTKSaveBusy(NSString *profile);
/* Packs the profile's save folder into <dir>/<name>.totksave (dir is created; an older package there is replaced) and
   returns the file's path. gameVersion: this app's (TOTK_GAME_VERSION); platform "mac" or "ios". */
NSString *TOTKSaveExport(NSString *profile, NSString *dir, NSString *gameVersion, NSString *platform, NSString *device,
                         NSString *model, NSError **error);
/* Reads and checks a package against this app's game version (every check of an import, nothing is changed). */
TOTKSavePackageInfo *TOTKSaveInspect(NSString *package, NSString *gameVersion, NSError **error);
/* Checks the package, backs up the profile's save folder to backups/<time>-before-import, puts the package's files in
   its place (this profile's own uid folder) and keeps the newest TOTK_SAVE_BACKUPS_KEPT backups. Refuses while
   TOTKSaveBusy. The caller makes sure the game is not running, or (iOS: in the same process) tells the player to restart it. */
TOTKSavePackageInfo *TOTKSaveImport(NSString *package, NSString *profile, NSString *backups, NSString *gameVersion, NSError **error);
/* The newest backup in a backups folder (its path), or nil; *when: its time. */
NSString *TOTKSaveLastBackup(NSString *backups, NSDate **when);
/* The profile's slots and their newest file times, for the menus ("6 slots, newest 20 Oct 2026 at 15:31"). */
NSString *TOTKSaveSummary(NSString *profile);
