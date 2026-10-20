# Save sync between the Mac and an iPhone / iPad

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
