# Save sync between the Mac and an iPhone / iPad

Three ways, from the simplest:

1. **By hand** with Finder or the Files app ([below](#by-hand-finder-and-the-files-app)).
2. **AirDrop** (or Messages, Mail, a USB stick): *Send saves* in the game's settings makes one `.totksave` file; opening it
   on the other device brings the saves in ([below](#airdrop-totksave-files)). No Xcode, no cable.
3. **`save_sync.py`** over the Xcode connection, by hand or every 2 minutes ([below](#save_syncpy)).

## By hand: Finder and the Files app

The iOS app shares its Documents folder: on the iPhone / iPad it is *Files › On My iPhone › Tears of the Kingdom*; on a
Mac with the device connected, Finder's sidebar › the device › *Files* tab › *Tears of the Kingdom* (drag folders in and
out). The saves are in `save/save/<uid>/` there, and in `~/Library/Application Support/MacNativeGames/TOTK/profile/save/<uid>/`
on the Mac.

**The `<uid>` folder name differs per device**: each game makes its own account (`account.uid`, 16 random bytes) on its
first start and only opens the folder of that account. Copy the *contents* of one device's `<uid>` folder (the `slot_*`
folders, `album`, `storage`, `option.sav`) into the other device's own `<uid>` folder; never copy `account.uid`, and a
folder with another device's name is ignored by the game. Do it with the game closed on the receiving side (on iOS, swipe
the app away), keep a copy of what you replace, and only from a game version to the same or a newer one.

## AirDrop: `.totksave` files

**Send.** Save in the game first (a manual save, or let it autosave): the newest saves on disk are what is sent, every
slot at once.
- Mac: Settings (⌘,) › *Interface* › *Send saves* › *Send…*: the share menu, AirDrop first (Messages, Mail and the rest
  follow). Return or A on that row does the same.
- iPhone / iPad: Settings › *Saves* › *Send Saves…*: the share sheet (AirDrop at the top).

The package is refused while the game is writing a save (`save/.busy`, see below): try again a moment later.

**Receive.**
- Mac: AirDrop puts the file in Downloads. Quit the game, then double-click the file: the app opens, checks it, shows where
  it comes from and its slots, and on *Replace Saves and Play* puts them in place and starts the game. If the game is
  already running it says to quit first (a running game would save over them).
- iPhone / iPad: accept the AirDrop (or tap the file in Files or Messages) and choose Tears of the Kingdom. The app checks
  it, asks, and puts the saves in place while the game writes no save. The game runs in the same app and still has the old
  save in memory, so close it (*Close the Game*, or swipe it away) and open it again, then load a save from the title
  screen; until then do not save in the game, or it saves its old state over a slot.

**What is checked** (runtime/save_package.h has the details): the file is a `.totksave` zip with `manifest.json`
(format 1, title 0100F2C0115B6000); the game version that sent it is exactly this app's (a 1.4.3 save goes only to a
1.4.3 app, else it is refused with both versions named); the zip holds exactly the files the manifest lists, each with
its size and SHA-256 (a changed, missing or extra file refuses it); no path leaves the save folder. The package carries
only the save folder (`slot_*`, `album`, `storage`, `option.sav`): no `account.uid`, cache, temp or shader lists. Its
files go into *this* device's own `<uid>` folder (a device that never started the game gets an account made first).

**Backups.** Before anything is replaced the current save folder is copied to `save-backups/<time>-before-import`: on the
Mac `~/Library/Application Support/MacNativeGames/TOTK/save-backups/`, on iOS *Files › Tears of the Kingdom ›
save-backups* (the Saves page shows the last one). The newest 10 are kept. The new folder is complete before the old one
moves aside, and what was written is checked against the manifest. To go back: close the game, then copy a backup's
contents over the `<uid>` folder (Finder on the Mac; on iOS through the Files app or Finder's Files tab, as in
[By hand](#by-hand-finder-and-the-files-app)).

## save_sync.py

`games/totk/ios/tools/save_sync.py` copies the game's saves between the Mac app and the iOS app over the same connection
Xcode uses (`xcrun devicectl`, cable or the same Wi-Fi). It needs the Mac that built (or at least can see) the iOS app,
with Xcode installed.

```
python3 games/totk/ios/tools/save_sync.py status            # both sides, and what auto would do
python3 games/totk/ios/tools/save_sync.py pull               # iPhone / iPad -> Mac
python3 games/totk/ios/tools/save_sync.py push               # Mac -> iPhone / iPad
python3 games/totk/ios/tools/save_sync.py auto               # whichever side changed since the last sync
python3 games/totk/ios/tools/save_sync.py auto --dry-run     # say what it would copy, change nothing
python3 games/totk/ios/tools/save_sync.py install            # run auto every 2 minutes (LaunchAgent); uninstall removes it
```

`--device <udid|name>` picks one device when several are connected (by default `status` and `auto` go through every
connected device with the game; `pull` and `push` want exactly one).

## What is copied

A profile holds `account.uid` (16 random bytes made by the game on each device, `runtime/account.c`) and
`save/<uid>/` with everything the game saves: the `slot_*` folders, `album`, `storage`.

| | Mac | iPhone / iPad |
|---|---|---|
| profile | `~/Library/Application Support/MacNativeGames/TOTK/profile` | app container `Documents/save` |
| save folder | `profile/save/<uid>` | `Documents/save/save/<uid>` |

The game only opens the save folder of its own account (`runtime/fs_host.c`), so the tool copies the *contents* of one
side's save folder into the other side's own `<uid>` folder. `account.uid` is never copied. The whole folder is the unit:
there is no merging of single slots.

## When it refuses

- **A save is in progress** on the side being copied from. The game writes save files in place and then commits, so a copy
  in between would be half old, half new. The game keeps `save/.busy` from the first change of a save until its commit; a
  marker older than 10 minutes was left by a game that stopped mid-save (the next start removes it) and is ignored. Every
  copy is also taken twice and only used when both copies are identical.
- **The game runs on the receiving side.** A running game holds its save in memory and would overwrite the copy at its next
  save. On the Mac: a `totk-native` process, or a held game slot (`runtime/game_slot.c`). On iOS: devicectl's process list;
  when that cannot be read, `--force` copies anyway after you closed the game yourself. Pulling from a device whose game is
  running is allowed (it is not being saved to).
- **The receiving game is older than the save.** A save written by 1.4.3 cannot be loaded by 1.2.1. The save's version is
  `save/.version` (written by the game at each commit), else the app's version: on the Mac the app's
  `CFBundleShortVersionString`, on iOS the installed app's version from `devicectl device info apps` (IPAs built before
  this tool report `1.0`, which counts as unknown). An unknown version needs `--force`.
- The receiving side has never started the game (no `account.uid` yet), or the sending side has no save.

## How `auto` decides

`~/Library/Application Support/MacNativeGames/TOTK/save-sync/<udid>.json` records, per device, the hash of the save both
sides held after the last sync, the versions, the time and a cheap fingerprint (file names, sizes, times) of each side.

- Neither fingerprint changed: nothing to do (no copy is pulled from the device).
- Only one side's save changed since the last sync: it is copied to the other side.
- Both changed (or they differ and were never synced): the save with the newest file time wins; the other one is parked as
  `save-sync/backups/save-conflict-<Mac|device>-<time>`, and the tool says so (the LaunchAgent also shows a notification).
- One side has no save: the other one is copied.

Before any save is replaced, a copy goes to `save-sync/backups/<time>-<side>-<device>` (the last 10 are kept; parked
conflicts are kept until you delete them). To go back, copy a backup's contents over the save folder (while the game is
closed). After a copy the tool reads the receiving side again and checks that it holds exactly what was sent.

The Mac launcher's own `profile-before-<version>` copy (made once before a version upgrade) is untouched.

## Limits

- The LaunchAgent only acts while the device is reachable by devicectl and the game is closed on the side receiving a save;
  close the game on the iPhone (swipe it away) before you expect the Mac's progress there.
- `storage/CacheStorageKey.dat` is copied with the rest of the folder; the game's cache storage itself (`profile/cache`) is
  not (it is per device, like `account.uid`).
- A device save written in the same second as the previous sync's check can be missed by the fingerprint until the next
  save on that side; nothing is lost (the side that is overwritten later is backed up first).
