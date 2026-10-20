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
- the files it replaces or adds (the first 16), and files left to the port's own data patches (the port needs its own
  copy of a few files, such as the heap sizes for 1440p and 4K output; a mod's copy of those is skipped, the game starts);
- the ResourceSizeTable: the game's memory table is adjusted for the mods' files.

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
| Code patches (`exefs/`: `.pchtxt`, `.ips`, `subsdk`) | not applied: the port's code is translated ahead of time; coming later |
| TKMM packages (`.tkcl`) | listed but not loaded; coming later |
| Two mods changing the same file (merging packs, text, parameters) | the first in the order wins; merging is coming later |
| Mods made for 1.0-1.2.x | load, flagged: files whose format changed in 1.4.3 can crash or look wrong |
| Cheats | not supported |

## The older layout

Before the mods folder, the port loaded one folder, `mods/<v0|v112|v121|v143>`, with a `manifest.tsv` written by
`games/totk/scripts/make_mod_manifest.py` (each file's size and SHA-256 and its original's, checked at start). It still
loads, before the mods folder's mods; it is not listed as a mod.
