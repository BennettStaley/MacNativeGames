# Mods

The port loads RomFS mods: mods that replace or add game data files (models, textures, layouts, text, parameters,
packs). It reads them from a mods folder at every start; changes to the folder or to the order apply at the next start.

## Where

| | Folder |
|---|---|
| Mac | `~/Library/Application Support/MacNativeGames/TOTK/mods` |
| iPhone / iPad | the app's `mods` folder in the Files app (On My iPhone → Tears of the Kingdom → mods) |
| Tests | `TOTK_MODS_DIR=<folder>` (`0` = no mods) |

## Installing a mod by hand

Mods made for Yuzu, Ryujinx or a modded Switch ship a `romfs` folder. Unzip the mod into the mods folder so that each
mod has its own folder with `romfs` inside:

```
mods/
  mods.txt
  Base Defogger/
    romfs/Pack/EnvPalette.pack.zs
    romfs/Pack/CavePalette.pack.zs
  Some Other Mod/
    Some Other Mod v2/romfs/...        an extra folder from the archive is fine (up to three levels)
  Texture Pack/
    Model/...                          a folder whose top level already is RomFS folders counts as romfs/
```

- A mod with several `romfs` folders side by side (options, "pick one") is not loaded: keep the option you want and
  delete the others.
- Paths are case-sensitive in the game. A file whose path differs from the game's only in case (`pack/` for `Pack/`) is
  loaded under the game's spelling, and the log says so.
- A file the game does not have is added.
- `mod.json` (optional; the mod browser writes it): `name`, `author`, `version`, `game_versions`, `gamebanana_id`.

## mods.txt

One mod folder per line, in load order. The first line wins a file that two mods both change. `-` in front of a name
keeps the mod installed but off. `#` starts a comment line. Folders not listed are loaded after the listed ones, in
name order, and turned on. A line naming a folder that is gone is dropped the next time the list is saved.

```
# Mods load order: the first line wins a file two mods both change. "-" in front = installed but off.
Texture Pack
-Base Defogger
Some Other Mod
```

## What happens at start

The log (`~/Library/Logs/MacNativeGames/totk.log`, iOS `Documents/log.txt`) lists, under `[totk] mods:`:

- each mod, whether it is on, its file count and notes (code patches not applied, its own ResourceSizeTable left out,
  made for an older game version, files a mod higher in the list already changes);
- the files it replaces or adds (the first 16), and what happened to files the port's own data patches also change (see
  below);
- the ResourceSizeTable: the game's memory table is adjusted for the mods' files.

### Files the port also patches

The port ships a few fixed game files of its own (`games/totk/scripts/make_romfs_patches.py` and the heap sizes for
1440p / 4K output). When a mod changes one of them:

| File | What is used | Log line |
|---|---|---|
| `UI/LayoutArchive/Common.Product.140.Nin_NX_NVN.blarc.zs` (the HUD and menu layouts: button glyph mods such as Xbox UI or PlayStation UI replace it) | the mod's copy, with the port's quick-menu background fix applied to it again | `... the mod's copy is used, with the port's quick-menu background fix applied to it again`, then `... fix is applied to the mod's copy (4 visibility keys moved ...)` |
| `Event/EventFlow/DmF_SY_GameOver.bfevfl.zs` (a game over fix only 1.0.0 runs) | the mod's copy | `... the mod's copy is used and the port's patch is left out` |
| `System/HeapSize/Product.Nin_NX_NVN.HeapDef.byml.zs` (only with 1440p / 4K output) | the port's copy: the game needs the bigger heaps for that output | `... which needs its own copy (the heap sizes for the output resolution); the mod's copy is left out` |

The quick-menu background fix: the weapon / shield / material quick menus (hold a d-pad direction) and the Autobuild
screen show a capture of the game picture behind them. The game takes it when the capture panes become visible at
animation frame 1.0; at 60 fps that comes after the world has stopped drawing, so the background is black. The fix moves
those four visibility keys to frame 0.0 (in `anim/ShortCutBG_00_In.bflan` and `anim/AutoBuilder_00_In.bflan`). For a
mod's archive the runtime decompresses it with the game's zstd dictionaries, moves the keys only where they are exactly
the game's (hidden at 0.0, shown at 1.0) and serves the result uncompressed (zstd raw blocks), cached in
`mods/.cache/capture-fix-*.blarc.zs` until the mod's file changes. If the mod changed those animations, its archive is
used as it is and the log says `the port's quick-menu background fix was not applied (...)`: the quick menus can then
show a black background at 60 fps.

Mod files are not hashed at start, so a start with mods is about as fast as one without. If a mod file changes or is
deleted while the game runs, the game stops when it next opens that file instead of reading something unchecked.

### The ResourceSizeTable

The game reserves memory for each resource from `System/Resource/ResourceSizeTable.Product.143.Nin_NX_NVN.rsizetable.zs`.
Mods that bring their own copy made it for one game version and one set of mods, so the port never uses it: it takes
the game's table and raises the entry of every file the mods change or add (and of each changed file inside a replaced
`.pack`) to an estimate from its size and type, using the calculator of [TkSharp](https://github.com/TKMM-Team/TkSharp)
(TKMM's library, MIT). The result is cached in `mods/.cache` until the mods' files change.

## What works, what doesn't yet

| | |
|---|---|
| RomFS files (replace and add), any number of mods, load order, turning mods off | works |
| Archives with an extra top folder, folders without `romfs/` whose top level is RomFS folders | works |
| A mod's own ResourceSizeTable | left out; the game's table is adjusted instead |
| Files the port also patches (UI layouts, the game over flow) | the mod's copy; the port's quick-menu fix is applied to the UI layouts again |
| Code patches (`exefs/`: `.pchtxt`, `.ips`, `subsdk`) | not applied: the port's code is translated ahead of time; coming later |
| TKMM packages (`.tkcl`) | listed but not loaded; coming later |
| Two mods changing the same file (merging packs, text, parameters) | the first in the order wins; merging is coming later |
| Mods made for 1.0-1.2.x | load, flagged: files whose format changed in 1.4.3 can crash or look wrong |
| Cheats | not supported |

## The older layout

Before the mods folder, the port loaded one folder, `mods/<v0|v112|v121|v143>`, with a `manifest.tsv` written by
`games/totk/scripts/make_mod_manifest.py` (each file's size and SHA-256 and its original's, checked at start). It still
loads, before the mods folder's mods; it is not listed as a mod.

## Button-icon mods

A mod that changes the game's button-icon archive (`UI/LayoutArchive/Common.Product.140.Nin_NX_NVN.blarc.zs`) and whose
name (or `mod.json` `"prompts": "xbox"` / `"playstation"`) says Xbox or PlayStation is a *button prompts* mod. It is not
switched in the Mods tab: **Settings › Controls › Button prompts** picks the style at each start (Automatic follows the
last controller connected) and turns on the first installed mod of that style and every other button-icon mod off.
Switch (the game's own icons) turns them all off. A mod that names both (a "Controller UI (Xbox & PS)" pack with options
inside) is an ordinary mod.
