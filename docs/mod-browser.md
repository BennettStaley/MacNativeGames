# The mod browser

The settings menu has a **Mods** tab (Mac: the last tab of the sidebar, which widens while it shows; iPhone / iPad:
the Mods row). It lists the installed mods and can find and install mods from
[GameBanana](https://gamebanana.com/games/7617). What the game does with the mods folder (layouts, `mods.txt`, the
load order, what is supported) is in [mods.md](mods.md); this page is about the menu and the browser.

## Installed

Every mod in the mods folder, in load order (a mod higher in the list wins a file two mods both change):

- the switch turns it on or off; the arrows (Mac) or Edit (iPhone / iPad) change its place; the bin / Edit deletes its
  folder after asking;
- under its name: author, version, files, the game versions its `mod.json` names, and what the loader noticed:
  - *Code patches (exefs) aren't applied yet — coming in a later update*
  - *TKMM .tkcl packages aren't supported yet — coming later*
  - *Two mods change the same file: the first in the list wins. Merging is coming later*
  - *Made for 1.2.1: may not work on 1.4.3*
  - a mod's own ResourceSizeTable (left out) and a folder with nothing to load;
- *Restart the game to apply mod changes* shows while the list differs from what the game loaded.

Mac keys: ↑ ↓ choose, Return or Space switches a mod on / off, ⌘↑ ⌘↓ move it, Delete removes it, ← → Installed /
Browse, `/` or ⌘F search, Escape closes. Controller: D-pad, A on / off, X / Y move, B closes. On iPhone / iPad a
controller's B goes back a page.

## Browse GameBanana

Search (by name), sort (most downloaded, newest, recently updated, most liked) and, on the Mac, a category. A mod's page
shows its pictures, description, license and files. Each file shows its size, the game version its name and description
mention and its downloads; the file that best fits 1.4.3 is marked **Recommended**:

1. files naming 1.4.3, a 1.4.x range or "all (game update) versions" come first, then other 1.4 files, then files that
   name no version, then files naming only 1.0–1.2.x (with *Made for …: may not work on 1.4.3*);
2. TKMM-only files (`tkmm` / `tkcl` in the name or description) and archives the device can't open go last;
3. otherwise GameBanana's own order (the author's).

Only real game versions count (1.0.0, 1.1.0–1.1.2, 1.2.0, 1.2.1, 1.4.0–1.4.3, also written `143`, `v121`, `1.4.x`,
`1.4.0-1.4.3`), so a mod's own "v2.6.1" is not taken for one. It is a guess from free text: read the mod's page when in
doubt.

**Install** downloads the file straight from GameBanana, checks it against GameBanana's MD5, unpacks it and arranges it
in the mods folder:

- the folder holding `romfs/` (or `exefs/`) is found however deep the archive wraps it (Atmosphère's
  `atmosphere/contents/0100F2C0115B6000/romfs` too); a folder whose top level is RomFS folders (`Pack/`, `Model/`,
  `UI/`, `Sound/`, `System/`, `Mals/`, `Event/`, `Shader/`, ...) counts as `romfs/`; `exefs/` is kept so the loader
  can report it; `.tkcl` files are copied as they are;
- the mod gets a folder named after it (`Serfrost's Defogger (Native)`, a number added if taken) with a `mod.json`:
  `name`, `author`, `version`, `game_versions`, `gamebanana_id`, `file_id`, `file`, `file_description`, `variant`,
  `license`, `installed`, `source_url`, `download_url`, `md5`;
- an archive with several variants side by side (`1.2.1/` and `1.4.x/`, or options such as `Champion_Equipment/` next
  to `Unbreakable_Equipment/`) becomes one mod per variant, kept together at the end of the list; one is switched on
  (the best for 1.4.3, then the one named like the mod or the file, then the one with the most files) and the others
  are off;
- the new mod goes to the end of the load order, switched on. It is loaded at the next start.

An archive with nothing to load (a ReShade preset, a save, a tool) is refused with a message and nothing is written.

## Archives per platform

| | zip | 7z | RAR (4 and 5) |
|---|---|---|---|
| Mac | yes | yes | yes |
| iPhone / iPad | yes | no | no |

About a third of popular TOTK mods ship as 7z or RAR only. On the Mac every archive is unpacked by `/usr/bin/bsdtar`
(libarchive, part of macOS), which refuses `..` paths and strips a leading `/`. iOS has no `bsdtar` and no way to start
another program, so the app has a small zip reader of its own (`mod_browser_unzip`, zlib's inflate): stored and
deflated entries, zip64 sizes and offsets, no encryption; it refuses absolute paths and `..`, skips links and
`__MACOSX`, stops an entry that grows past its declared size, and caps the archive at 200 000 entries and 12 GB
unpacked. For a 7z or RAR file the iPhone / iPad page says: *7z / RAR archives can't be opened on iPhone and iPad yet —
install it on the Mac, or unpack it and copy the folder into Files › Tears of the Kingdom › mods*.

## GameBanana's API

The public `apiv11` endpoints, no account and no key, with the User-Agent `TOTK-Mac-Native/<version> (mod browser)`:

| | Request |
|---|---|
| list | `https://gamebanana.com/apiv11/Mod/Index?_aFilters[Generic_Game]=7617&_nPage=N&_nPerpage=30&_sSort=Generic_MostDownloaded` (or `Generic_Newest`, `Generic_LatestUpdated`, `Generic_MostLiked`) |
| search | the list plus `&_aFilters[Generic_Name]=contains,<text>` |
| category | the list plus `&_aFilters[Generic_Category]=<id>` |
| categories | `https://gamebanana.com/apiv11/Mod/Categories?_idGameRow=7617&_bShowEmpty=true&_sSort=a_to_z` (`_sSort` is required) |
| a mod | `https://gamebanana.com/apiv11/Mod/<id>/ProfilePage`: `_sName`, `_aSubmitter`, `_sVersion`, `_sDescription`, `_sText` (HTML), `_aPreviewMedia._aImages[]` (`_sBaseUrl` + `_sFile530` / `_sFile220`), `_tsDateUpdated`, `_nDownloadCount`, `_sLicense` (HTML) / `_aLicenseChecklist`, `_aFiles[]` (`_idRow`, `_sFile`, `_nFilesize`, `_sDownloadUrl`, `_sMd5Checksum`, `_sDescription`, `_sAvResult`) |
| a file | `_sDownloadUrl` (`https://gamebanana.com/dl/<file id>`, redirected to `files.gamebanana.com`) |

Lists carry likes and views but no download count; a mod's page has its downloads. HTML text is turned into plain
text (paragraphs, list bullets, entities) by `mod_browser_plain_text`.

To be polite the browser never crawls: one list page per scroll, one mod page when it is opened, at most four
connections, a search per press of Return / Search (not per letter). Lists are kept 5 minutes, a mod's page 15 minutes,
categories a day, pictures for good (trimmed to about 150 MB), in `Caches/TOTKModBrowser` (Mac:
`~/Library/Caches/TOTKModBrowser`; `TOTK_MOD_BROWSER_CACHE` overrides); an old copy is shown when GameBanana can't be
reached. A 429 or 502–504 answer is retried twice after 1 and 3 seconds (or its `Retry-After`, up to 10).

## Licenses

Nothing is re-hosted or bundled: every file goes straight from GameBanana to the player's own mods folder, as a
download in a browser would. Each mod's page shows its license and `mod.json` keeps it; most TOTK mods allow downloading
and using them but not redistributing or changing them without asking. Respect each mod's license and credit its
authors; the menu says so under the list.

## Code

| | |
|---|---|
| `games/totk/runtime/mod_browser.h`, `.m` | the GameBanana client, the cache, version guesses, downloads, unpacking, arranging (Foundation; Mac and iOS) |
| `games/totk/runtime/mods_panel_mac.h` | the Mac Mods tab (included by `settings_panel_mac.h`) |
| `games/totk/ios/Sources/mods_menu.m` | the iPhone / iPad Mods pages (pushed from `settings_menu.m`) |
| `games/totk/runtime/mods.h`, `mods.c` | the mods folder, `mods.txt` and the loader's flags ([mods.md](mods.md)) |
| `games/totk/tests/mod_browser/probe.m` | `make probe-mod-browser` (offline checks), `LIVE=1` also lists GameBanana and installs a small mod into `build/tests/mods` |
