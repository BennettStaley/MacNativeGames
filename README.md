# NativeTOTK-Apple-Silicon

**Breath of the Wild 1.6.0 is now officially supported on Mac!** (Mac only for now: no iPhone or iPad build yet.)
**Tears of the Kingdom 1.0.0, 1.2.1 and 1.4.3 are now officially supported on Mac, iPhone and iPad!**

> **Use your AI assistant to help you build the Mac app. We recommend Claude Code, Codex, or any other AI assistant you prefer.**

A fork of [ly05010419/MacNativeGames](https://github.com/ly05010419/MacNativeGames), the native Tears of the Kingdom port,
with an in-game settings menu, mods and save transfer added (see [This fork](#this-fork-settings-mods-and-save-transfer-status-2026-10-20)).
This README is in English; the original project's README also has instructions in Chinese
([中文说明](https://github.com/ly05010419/MacNativeGames#chinese)).

## Legal

**You must own the game.** Use this project only with a copy of The Legend of Zelda: Tears of the Kingdom that you bought, and with keys and system files dumped from your own Nintendo Switch. This is an independent, non-commercial project. It is not affiliated with, endorsed by, or sponsored by Nintendo. The Legend of Zelda and Nintendo Switch are trademarks of Nintendo.

The repository contains no game content and distributes none: no game program, art, text, audio, shaders or keys. Everything the game needs is made on your own computer from your own files when you build the app. The app you build contains the game, so it is for your own use only; do not share it. What the repository does hold is the information needed to make the port work: the patch code, the addresses and hashes used to check your files, and the [shared shader list](docs/shader-list.md), which names shaders and bind states but contains no shader code.

Play your own copy of **The Legend of Zelda: Tears of the Kingdom** natively on Apple silicon Mac, iPhone, and iPad. Bring your game file (XCI or NSP) and your `prod.keys`; one script builds a Mac app or an unsigned iPhone/iPad IPA. The iOS build requires iOS/iPadOS 26 and iPhone 14 or newer. It is not an emulator: the game's own ARM64 code runs directly on Apple chips (rewritten where the operating system requires it), the Switch system services are answered by a small host, and the game's NVN graphics are rebuilt on Metal. Nothing from the game is in this repository, and the project is not affiliated with Nintendo. Version 1.4.3 has been played on an iPhone 14 and an iPad; see [iPhone and iPad](#iphone-and-ipad).

## This fork: settings, mods and save transfer (status, 2026-10-20)

[BennettStaley/NativeTOTK-Apple-Silicon](https://github.com/BennettStaley/NativeTOTK-Apple-Silicon), branch `feat/settings-menu`, adds a
recomp-style in-game layer on top of the 1.4.3 port. Tested on an M-series MacBook Pro (macOS, background test runs on
a prologue save and an outdoor Sky Temple save) and installed on an iPad Pro 13" M4 (iPadOS 26); the iPad build has not
been played through yet.

| Area | What's there | Status |
|---|---|---|
| **Settings menu** ([docs](docs/settings.md)) | A sidebar inside the game window on the Mac (works in full screen; mouse, keyboard, controller; ⌘, / F1 / Home), a menu on iPad / iPhone; 34 settings, almost all live | Mac tested; iPad installed |
| Display | Output resolution up to display native or 4K, 3D render scale 25-200 %, aspect ratio with Vert+ on taller screens (HUD keeps its shape), maximum output | Tested live |
| Graphics | FOV, AA (game / off / FXAA / SMAA), CAS sharpening, anisotropic filtering, LOD bias, frame interpolation, bloom, depth of field, lens flare, cave fog, shadow resolution (live), render distance | Tested; fog only affects caves, render distance only cuts below 100 % |
| World / interface / controls | Time of day speed, frame rate limit (also on display-paced 120 Hz), Hide HUD (F2), readout detail (avg / 1 % low), shader counter (compiling / new / total), camera speed, face buttons, Button prompts (Switch / Xbox / PlayStation via icon mods) | Tested |
| **Mods** ([docs](docs/mods.md), [browser](docs/mod-browser.md)) | One folder per mod (Yuzu / Ryujinx layout), load order, a GameBanana browser in the Mods tab, the 1.4.3 size table rebuilt for mods, old-version and code-patch mods flagged | Tested with Defogger and Xbox UI for v1.4.3 |
| **Save transfer** ([docs](docs/save-sync.md)) | Files app / Finder Files tab; `.totksave` over AirDrop (Send saves…, double-click / Open in to import, with backups and version checks); `save_sync.py` over USB / Wi-Fi | AirDrop not yet tried on devices |
| Builds | Game data outside the Mac app: a code-only rebuild takes ~40 s instead of ~12 min; the iPad app carries the game's icon | Done |

Next: AirDrop and iPad on-device testing, merging mods that change the same file, TKMM `.tkcl` packages, 1.4.3 code
patches (pchtxt) on the Mac, 7z / RAR on iPad, free camera / photo mode, cheats, faster iPad re-installs (keep the game
data on the device between installs), outdoor haze, the AutoBuilder quick-menu check. Several settings follow ideas from
MaxLastBreath's UltraCam and NX Optimizer (no code of theirs is included; see Acknowledgements).

## Let's maintain the shader list together: no more stutter

> [!TIP]
> **Every shader in the shared list is ready before you play.** On a Mac the game does not stop to compile it, and on an iPhone or iPad nothing is missing from the picture. The more places players cover, the smoother the game gets for everyone.

The [shared shader list](docs/shader-list.md) now holds **45,980 shader variants** (39,137 shaders) from the Mac, iPhone and iPad. Every build makes them from the player's own game files, so the list holds identifiers only, never game data. Helping takes no setup:

1. **Play.** When your game meets a shader the list does not have yet, it notes it in `shader-list-contribution.txt`.
2. **On a Mac, every 500 new shaders it asks you once, before the game starts.** Click "Send": the GitHub form opens and the file is shown, ready to drag in. If the GitHub CLI (`gh`) is installed and signed in, the dialog offers "Submit automatically" instead, which sends this batch for you (a private gist and an issue) without opening the browser. An iPhone or iPad never asks during play: while it is connected to the Mac that built its app (cable or the same Wi-Fi), the Mac asks instead. "Don't remind me again" stops the reminders.
3. **Once merged, every Mac app picks them up by itself.** Once a day the running game downloads the shared list (download only) and prepares the variants it lacks from your own game files in the background, after the title screen is up: no rebuild needed (Settings › Interface › *Download new shaders*, on by default). iPhone and iPad apps carry them from their next build.

**[Send your shaders →](https://github.com/ly05010419/MacNativeGames/issues/new?template=shader-list.yml)** · The game never uploads anything by itself.

Update history:

| Date | Platform | Update |
|---|---|---|
| Oct 20 | Mac, iPad | **This fork:** in-game settings menu (34 settings, nearly all live), Mods tab with a GameBanana browser, Button prompts, AirDrop save transfer, Restart now, the game's icon on iPad |
| Oct 20 | Mac | **This fork:** game data outside the app: a code-only rebuild takes about 40 seconds instead of about 12 minutes |
| Oct 20 | iPhone, iPad | Sound plays with the silent switch on and comes back after a call, Siri or an alarm; the game follows the system language; a fresh install reaches the title screen (#97) |
| Oct 20 | iPhone, iPad | First launch after installing shows the game in about 7 seconds instead of about 4½ minutes: the shaders that come with the app are read where they are, without checking or copying them again (iPhone 14) |
| Oct 20 | Mac | Breath of the Wild above 30 fps: menus and the HUD at normal speed, physics steady, movies at their own speed; docked play renders at native 1080p (`TOTK_RENDER_RES=900` keeps 1600x900) (#103) |
| Oct 20 | Mac | Faster app builds: files are cloned in one call and the 16 GB of game data is signed once (#100) |
| Oct 20 | All | Shader translation update: vertex outputs a shader does not write start as (0, 0, 0, 1), which brings back missing markers and effects (#91), and shadow compares and depth reads match the Switch, which removes dark patches on the ground and walls and the dark halo behind Link (#89, #104). The first launch after updating translates shaders again, because the old cache is from the previous translation (on iPhone and iPad the old cache is removed) |
| Oct 20 | Mac | High frame rates (all four versions): trampolines bounce above 100 FPS (#61), and hair and cloth sway at the right speed above 30 FPS (#79) |
| Oct 20 | Mac | Key 5 smooths the stair-stepped edges of the 3D scene with MetalFX temporal anti-aliasing (1.4.3); `TOTK_ANISO=16` sharpens ground and wall textures seen at a grazing angle |
| Oct 20 | Mac | The live FPS in the menu bar; key 1 gains an FPS-only state |
| Oct 20 | Mac | Optional UI layouts for 21:9 and 32:9 displays with the HUD in the screen's corners (1.4.3, made from your own game files) |
| Oct 20 | Mac | Background warm-up prepares only the shaders this version of the game uses (about half the time); `TOTK_WARM_ALL=1` warms the whole cache |
| Oct 20 | Mac | Two memory leaks fixed for both games |
| Oct 20 | Mac | **Breath of the Wild 1.6.0** (early): a card image holding the base game, the update and the add-on content builds its own app with the same command; title, saves, the Great Plateau and the DLC packs work, keys 2 (frame interpolation) and 3 (MetalFX 2K) work |
| Oct 20 | iPhone, iPad | The launcher adopts the UIScene lifecycle, so an IPA built with Xcode 27 starts on iOS 27 (which ends apps that still make their window in the app delegate) |
| Oct 19 | Mac | Key 4 switches to a true 21:9 ultrawide view and back (1.2.1, 1.4.3); the HUD keeps its shape in the middle 16:9 |
| Oct 19 | iPhone, iPad | Tested with 1.4.3: a fresh install shows the game from the first launch, without a played cache |
| Oct 19 | iPhone, iPad | Touch controls laid out like a Switch controller, with every button and both sticks |
| Oct 19 | All | The game notes the shaders the shared list lacks; on a Mac, every 500 it asks to send them in a [GitHub issue](https://github.com/ly05010419/MacNativeGames/issues/new?template=shader-list.yml) |
| Oct 19 | All | One shared [shader list](docs/shader-list.md); the build reads every shader in the game data (15,266 prepared for 1.4.3) |
| Oct 19 | Mac | 1.2.1 support (the most widely used update), with the same build command |
| Oct 19 | Mac | FPS, shader and load readings and the shortcut reminder in one small box in the window's top-right corner (`TOTK_HUD=corners` keeps the old layout) |
| Oct 18 | iPhone, iPad | IPA build target |
| Oct 18 | Mac | 1.4.3 support and shader prewarming: the title screen waits only for its own shaders (about 2 seconds), the rest warm in the background |
| Oct 17 | Mac | 1.0.0 and 1.1.2 support; no first-launch shader prewarming yet, so new scenes could stutter |

**Stuttering when you first start playing or enter a new scene is expected while new shaders compile—don't be surprised.** Performance typically improves as the cache warms up, and every shader the [shared list](#lets-maintain-the-shader-list-together-no-more-stutter) gains is one nobody stutters on again.

## Audio fix

Streamed music and dialogue lookup for `content://Sound/...` paths has been fixed. Opening dialogue and music were verified locally on version 1.0.0; other versions and scenes still need validation. The renderer also runs all known audio effect types and starts output after 20 ms of queued audio (2026-10-18). Rebuild your app with the original game inputs to apply the fix. See [audio troubleshooting and validation](docs/audio-troubleshooting.md) for details.

## Local testing

Tested on **MacBook Air M4 and Mac mini M4**. Local testing has collected **8,582 shaders**, with performance at approximately **90 FPS after shader warm-up, reaching 120 FPS in menus**.

Those are earlier local observations, not a stable frame-rate promise for every version or scene. Version 1.4.3 measurements and compilation stalls are recorded separately in [the test notes](docs/v1.4.3-port.md).

Shaders are automatically collected, compiled and cached as you play. Returning to previously visited scenes typically brings noticeable improvements in frame rates and smoothness because cached shaders can be reused. **8,582 is the number collected so far in local testing, not the game's total shader count or a collection limit**; the cache continues to grow as you explore.

**Shader prewarming on the first launch:** Building the app prepares shaders from your selected game data and includes them in the app: every shader in the game data for its default bind state, plus every variant named in the [shared shader list](docs/shader-list.md). Even with an empty user cache, the first launch imports these shaders. Only what the title screen itself uses is warmed before the first frame (about 2 seconds once macOS has compiled them before); the rest, together with compatible entries in the pipeline manifest, warms in the background behind the title, and the first trip into the world waits up to 15 seconds for it. No preliminary play session or manual cache copy is needed. The current 1.4.3 build prepares 15,266. Building the app also compiles them for your Mac's GPU. With Xcode's Metal toolchain installed they are compiled into the app (about 6 minutes on an M4 for 1.4.3) and work wherever the app goes. Without it they are compiled into macOS's Metal cache (a few minutes), and Metal finds them only while the game runs from where the app was built: if you move the app afterwards (to /Applications, for example), if macOS clears the cache (it does when the system disk fills up), or if another program was using Metal's cache while the app was built (a running game, for example; the build says so), the background warm-up compiles them again, which takes a few minutes, and new scenes can stutter until it finishes. To place the app elsewhere, build it there with `-o`; see [preparation and test details](docs/first-launch-shaders.md). Variants players report after your build reach your Mac without a rebuild: once a day the running game downloads the shared list (`If-None-Match` / `If-Modified-Since`, so an unchanged list costs one small request; nothing is uploaded), checks it strictly, and prepares the variants your cache lacks from your own game files on a background thread after the title screen, pausing while the game loads new areas; each is compiled with Metal so its first use is warm (Settings › Interface › *Download new shaders* turns it off). Places no player has reported yet can still need new variants, which the Mac compiles while you play (a short stutter the first time). Playing adds them to your local cache, which is reused on later launches. The game also notes every variant the shared list does not have yet in `shader-list-contribution.txt`, and every 500 of them asks once whether to send the file in a [GitHub issue](https://github.com/ly05010419/MacNativeGames/issues/new?template=shader-list.yml) ("Send" opens the form and shows the file to drag in). It never uploads anything by itself. With git, you can also merge your cache and open a pull request, as [the list's guide](docs/shader-list.md) explains. The list holds shader and pipeline identifiers only, never game data, saves or compiled shaders; every build makes the shaders from the player's own game files.


## What you need

- A Mac with Apple silicon and **macOS 26** or later (the graphics use Metal 4); macOS 27 is supported. 16 GB of memory or more is recommended.
  - Still on macOS 15? A community fork by [1918662410-ship-it](https://github.com/1918662410-ship-it/MacNativeGames) adds a Metal 3 backend for macOS 15. It is maintained there; neither this fork nor the original project tests it.
- About **25 GB** of free disk space for the work folder, in addition to your game file. The large game data goes to `~/Library/Application Support/MacNativeGames/TOTK/game-data` as APFS clones of the work folder's files; if the work folder is on another volume (or you build with `--embed-data` to another volume), allow roughly **17 GB more** for that copy. (Breath of the Wild keeps its data inside its app, as in the original project: keep that app on the work folder's APFS volume to share the large files through cloning.)
- Your Tears of the Kingdom **base game 1.0.0** (`0100F2C0115B6000`) as an **XCI or NSP**, and your `prod.keys`. To build **1.1.2, 1.2.1 or 1.4.3**, supply its separate cumulative update as NSP or NSZ with `--update`; no intermediate update is required. NSZ is decompressed and its NCA hashes are verified before extraction. DLC and combined packages are unsupported.
- For NSP: its common ticket must be included unless the NCAs are rights-free. NSPs do not contain system shared fonts: supply an unpacked firmware NCA directory with `--firmware`, or previously extracted fonts with `--shared-fonts`. The six fonts are checked against this project’s reference dump; a valid BFTTF font from another firmware version is accepted and bound by its own bytes. Valid fonts already in the work folder are reused.

## Build the app

**1. Tools** (once): Xcode command line tools and [Homebrew](https://brew.sh). An internet connection is needed to install build dependencies and fetch pinned sources:

```sh
xcode-select --install
brew install opus libvpx python@3.14
```

For an iPhone/iPad build, install full **Xcode 26 or later** (including the iOS and Metal toolchains), then install the additional tools:

```sh
brew install xcodegen cmake ninja
```

Optional: with Xcode's Metal toolchain (`xcodebuild -downloadComponent MetalToolchain`), the build compiles the startup shaders into the app. It finds an Xcode kept outside `/Applications` too (on another volume, or set `DEVELOPER_DIR`). Without it, the build compiles them into the Mac's own Metal cache for the app's location instead, which does not move with the app: after moving it, the first launch compiles them again.

**2. Download or clone this repository** (`git clone https://github.com/BennettStaley/NativeTOTK-Apple-Silicon`), then open Terminal in its `NativeTOTK-Apple-Silicon` folder and run the script with your game file and keys:

```sh
python3.14 build_app.py ~/Games/totk.xci -k ~/keys/prod.keys
```

For an NSP, supply the shared fonts too:

```sh
python3.14 build_app.py ~/Games/totk.nsp -k ~/keys/prod.keys --firmware ~/Games/firmware
# Or reuse the six .bfttf files from a previous build:
python3.14 build_app.py ~/Games/totk.nsp -k ~/keys/prod.keys --shared-fonts /path/to/TOTK_build/totk/extracted/firmware/shared-fonts
```

`--firmware` accepts a directory of `.nca` files (unpack a firmware archive first), or an installed firmware `registered` directory. `--shared-fonts` accepts the extracted directory containing `0-standard.bfttf` through `5-nintendo-extension.bfttf`. No fonts or keys are downloaded. For an XCI, fonts are read from its system update partition by default; if that partition is missing, use one of these font options too. By default both input formats produce a macOS `.app`.

To build an unsigned iPhone/iPad IPA instead, add `--platform ios` and use an `.ipa` output path:

```sh
python3.14 build_app.py ~/Games/totk.xci -k ~/keys/prod.keys --platform ios -o ~/Games/TOTK.ipa
```

Add `--update ~/Games/1.4.3.nsp` to select an update, and keep the `--firmware` or `--shared-fonts` option when the base game is an NSP. The first iOS build downloads and compiles device versions of Opus and libvpx and compiles the shader seed for iOS, so it needs an internet connection and substantial temporary disk space (allow at least **45 GB free** for a fresh base build, or **60 GB** for a fresh build with an update). The resulting IPA is unsigned: sign and install it with your sideloading tool, keeping its entitlements (it uses the increased memory limit). iOS/iPadOS 26 and iPhone 14 or newer are required. See [iPhone and iPad](#iphone-and-ipad) for what has been tested.

Without `-o`, the selected output appears next to the game file (`Tears of the Kingdom.app` on Mac or `Tears of the Kingdom.ipa` for iOS). **How long it takes:** the first build of a version does all the work below (about 12 minutes for 1.4.3 on an M-series Mac); after that, a rebuild whose game data did not change (a code change, or this repository updated) takes about **40 seconds**, because the game data stays outside the app (see *Where the game data goes* below). The details of a first build: before full shader preparation was added, a local clean base-1.0.0 Mac build took about 6 minutes on an M4 Mac mini (this is not a timing estimate for the current build; hardware, disk and network conditions affect the time): about a minute each to rewrite the main code module and to decrypt the 17 GB of game data, and two to three minutes for macOS to check the finished app once. The current build then also prepares the startup shaders (about 3 minutes for 1.4.3 on an M4) and compiles them for your GPU (a few more minutes on an M4, but about 25 minutes on an 8 GB M1 MacBook Air, which runs out of memory and swaps during this step; without the Metal toolchain this step is skipped, with a message, while a game is running). A Mac build needs no .NET or CMake, and the finished game needs no Python: its shader translator is a native program that comes with this repository (`games/totk/prebuilt`). Every intermediate file is in a work folder next to the game file (`TOTK_build`, or `--work`); running the script again reuses what is already done.

The extracted code modules, rewritten modules, RomFS and shared fonts are checked against the selected version's `games/totk/assets/expected-v1.0.0.json`, `expected-v1.1.2.json`, `expected-v1.2.1.json` or `expected-v1.4.3.json`; the prebuilt shader translator has its own SHA-256 check. A mismatch stops the build, with one exception: a different genuine retail dump of the same version is accepted — its `main.npdm` is checked by Nintendo’s ACID signature (`npdm_signed()`) instead of the pinned hash, and its firmware’s shared fonts are checked for a valid BFTTF structure and bound by their actual SHA-256. Key values are never printed.

**Where the game data goes, and fast rebuilds.** The Mac app holds only the code and small files (about 150 MB). The 17 GB RomFS image and the prepared shaders go once into `~/Library/Application Support/MacNativeGames/TOTK/game-data`, in folders named by their content (`v143-romfs-<id>`, `v143-shaders-<id>`); on the work folder's volume they are APFS clones and take no extra space. The app finds them through its `Contents/Resources/totk/game-data.json`; if they are missing it says so and shows the command that makes them again. A rebuild whose game data and shaders did not change (after a code change, for example) only rebuilds the game binary and assembles the small app: about 45 seconds instead of about 12 minutes on an M4, and the macOS check of the new app takes seconds. The first game start after the data is written verifies it once (about 20 seconds); later rebuilds keep that verification. To copy the app to another Mac, build a self-contained app with `--embed-data` (the data inside it, as before). `python3.14 build_app.py --prune-data` deletes the game-data folders no app points to (shader folders of older shader lists, for example); see [where the game data lives](docs/how-it-works.md#where-the-game-data-lives).

To change an app built before this into the new layout, build it again with the same command and `-o` (the data moves out of the app into the game-data folder; with the work folder in place this takes a few minutes, and the first start verifies the data once).

**3. Play:** open the app from Finder. The build starts the launcher in `--check` mode, which checks that the game executable exists and allows macOS to perform its first-launch checks; it does not run a gameplay test. You can keep the app where it was built or move it to `/Applications`; the first start after a move compiles the shaders again in the background (macOS's Metal cache does not follow the app to its new place), so to keep it in `/Applications`, build it there with `-o`. Copying it to a different volume can use additional disk space and trigger another macOS check, during which no window may appear. The first game launch verifies the game data (about 20 seconds in local testing). Newly encountered shaders can cause stutters; cached shaders reduce this work on later visits.


### Build version 1.1.2, 1.2.1 or 1.4.3

```sh
python3.14 build_app.py ~/Games/totk.xci -k ~/keys/prod.keys --update ~/Games/1.1.2.nsp
# NSZ updates are also accepted:
python3.14 build_app.py ~/Games/totk.xci -k ~/keys/prod.keys --update ~/Games/1.1.2.nsz
# Or build 1.2.1 or 1.4.3 directly from the same base:
python3.14 build_app.py ~/Games/totk.xci -k ~/keys/prod.keys --update ~/Games/1.2.1.nsp
python3.14 build_app.py ~/Games/totk.xci -k ~/keys/prod.keys --update ~/Games/1.4.3.nsz
# With a base NSP, keep --firmware or --shared-fonts as described above.
```

This produces the same **Tears of the Kingdom.app**, containing only the selected version. Quit the running game, then rebuild to the same `-o` path to replace the old app; there is no second game entry or version-selection menu. The window title shows the selected version, such as **Ver. 1.4.3**. Without `--update`, the script still builds 1.0.0.

You do not need to download separate 1.2.1 or 1.4.3 runtime tables or a shader cache. The build generates the version-specific runtime tables and first-launch shader seed from the game data you provide, then includes the shader seed in the app. The repository supplies the patch code and verified hashes of each supported update; only your base game, update and keys are external inputs.

Allow about **40 GB free for a fresh update build**, or **19 GB extra** when the base data is already cached, with the app on the same APFS volume. The merge reads the base NCA through a bounded range of the original container, temporarily extracts the much smaller update NCA, and verifies every IVFC level. It fetches a pinned [hactool](https://github.com/SciresM/hactool) source and builds it with clang/make. Temporary ticket files are private and removed after the merge; key values are not passed on the command line or printed.

Saves stay in the same profile folder. Before the first updated launch, the app makes a complete backup beside it, such as `profile-before-1.1.2`, `profile-before-1.2.1` or `profile-before-1.4.3`; failure stops startup. Keep this backup if you need to return to an older version—do not assume an updated save is backward compatible.

To keep one desktop entry, link the packaged app with the optional installer:

```sh
python3.14 games/totk/tools/install_desktop_entry.py --app "/path/to/Tears of the Kingdom.app"
```

When replacing the older development launcher, the installer copies and verifies its saves into the standard user profile listed below and creates an upgrade backup. It preserves the original profile and archives the old launcher without adding another app entry. An existing, different standard profile is never overwritten.

The source runtime passed basic Mac checks: load, movement/jumps, menus/map, 2K, save/reload and death retry. Full-map, long-session and fair 1.0/1.1.2 performance comparisons remain pending; the earlier FPS observations are not a promise of 1.1.2 performance. Packaging validation is recorded in [the build notes](docs/how-it-works.md#112-validation).

The 1.4.3 port has separately passed scene loading, movement/jumps, menus/map, save/reload, native 2K canvas switching and MetalFX upscaling/interpolation on an M4 Mac. The single packaged app has been tested, including save/reload, death/retry and shrine entry/exit. A three-hour earlier build and a 30-minute final-build run are recorded separately; first-use compilation stalls, long-session memory growth and audio underruns remain under investigation. See [the 1.4.3 test record](docs/v1.4.3-port.md) for the scope and follow-ups; this is not full-game or stable-FPS certification.

The 1.2.1 port (October 19) passed the same rewrite verification (600 of 600 negative controls) and the loader, RomFS and startup probes. On an M4 Mac it loaded saves in the 1.0.0 and 1.1.2 formats and passed movement/jumps, menus/map/quick menus, manual save and reload in a new process, fall death and retry, and the three key-3 picture modes; the packaged app was also opened through its launcher. See [the 1.2.1 test record](docs/v1.2.1-port.md); this is not full-game or long-session certification.

### Breath of the Wild 1.6.0 (early)

The same script builds **The Legend of Zelda: Breath of the Wild 1.6.0** (`01007EF00011E000`) from a card image that holds the base game, the 1.6.0 update and the two add-on content titles together (no `--update`; the game is recognised by the title IDs in the file, never by its name). This card has no system update partition, so the six shared fonts must come from a Tears of the Kingdom build's work folder (`TOTK_build/totk/extracted/firmware/shared-fonts`) or another verified font folder. The startup shaders come from the game data and the game's own [shared shader list](docs/shader-list.md) (`games/botw/assets/shader_list.tsv`), as for Tears of the Kingdom: every shader of the boot and title packages for its default bind state, plus every bind state the list names, compiled ahead and waited for only as far as the title screen needs.

```sh
python3.14 build_app.py ~/Games/botw.xci -k ~/keys/prod.keys --shared-fonts ~/Games/TOTK_build/totk/extracted/firmware/shared-fonts
```

A separate base NSP or XCI and a **1.6.0 update NSP or NSZ** also work. This path builds the game without the expansion pass; DLC is not required. Common tickets must be included for rights-managed NSPs. Other update versions are rejected by the verified executable hashes.

```sh
python3.14 build_app.py ~/Games/botw-base.nsp -k ~/keys/prod.keys --update ~/Games/botw-1.6.0.nsp --shared-fonts ~/Games/TOTK_build/totk/extracted/firmware/shared-fonts
```

The work folder is `BotW_build` next to the game file; the app is `Breath of the Wild.app` (bundle `org.macnativegames.botw`), with its own data folder `~/Library/Application Support/MacNativeGames/BotW` and log `~/Library/Logs/MacNativeGames/botw.log`. Every step is checked against `games/botw/assets/expected-v1.6.0.json`; a fresh build takes about 16 minutes on an M4 (the 15 GB RomFS merge is most of it).

What works (October 20, an M4 Mac, a Great Plateau save): title, the add-on content list, loading and saving, the plateau at about 90–120 fps (the game advances by real time; held-button repeats count by real time too), the two DLC packs mounted and read, key 1 (overlays), key 2 (MetalFX frame interpolation: off → 30+30 → half the refresh rate → unlimited, every game frame interpolated on the plateau) and key 3 (MetalFX 2K on / off: MetalFX replaces the game's own upscale of the 1600x900 picture into the 1080p canvas and the window output above 1080p); a 30-minute session on the plateau at about 95 fps with memory steady (+170 MB). Not yet: native 2K, iPhone / iPad, the DLC quests themselves (only verified as mounted and read). This is an early port: expect rough edges.

### iPhone and iPad

Version 1.4.3 has been played on an **iPhone 14 (iOS 26.6.1)** and an **iPad with the A16 chip (iPadOS 26.7.1)**, both signed with a personal development team in Xcode. Versions 1.0.0, 1.1.2 and 1.2.1 have not been tested on iOS. Breath of the Wild has no iPhone/iPad build yet: `--platform ios` with its card image stops with an error.

- **This fork on iPad:** installed on an iPad Pro 13" M4 (iPadOS 26) with the game's own icon, the settings menu (the gear at the top, a three-finger tap, a controller's Home button, or − and + held together), the Mods tab (zip mods) and AirDrop saves. It has not been played through on the iPad yet. The output stays at 720p (the 3D render scale works); shadow resolution and aspect ratio are Mac only so far.
- **First launch:** the app checks the game data (about 15 seconds) and imports the shaders it carries, with a progress bar. On an iPhone 14 this takes about 3 minutes before the title appears. Later launches show the title in about 8 seconds.
- **Picture:** the game runs in handheld mode, as a Switch does without its dock: 1280×720.
- **Shaders:** an iPhone or iPad cannot translate shaders while playing. A draw whose shader variant the app does not carry is skipped, so objects can be missing in places no player has reported yet. The device records what it missed in `shader-list-contribution.txt` (Files app › On My iPhone › Tears of the Kingdom); the game never asks about it during play. While the device is connected to the Mac that built the app (cable or the same Wi-Fi), the Mac checks every 10 minutes and every 500 new shaders asks once to send the file in a [GitHub issue](https://github.com/ly05010419/MacNativeGames/issues/new?template=shader-list.yml) (`games/totk/ios/tools/shader_contrib_watch.py`, installed by the iOS build; `--no-shader-reminder` skips it, `shader_contrib_watch.py uninstall` removes it). Once merged into the [shared shader list](docs/shader-list.md), the next build carries them for everyone. With a Mac, `games/totk/ios/tools/fill_shaders.py` also translates them and sends them to the device right away.
- **Saves between Mac and iPhone / iPad by AirDrop:** Settings › *Send saves* (Mac: the Interface tab; iPhone / iPad: Saves) makes one `.totksave` file; open it on the other device (Mac: double-click it in Downloads with the game closed) to bring the saves in. Same game version only; every file's SHA-256 is checked and the replaced saves are kept in `save-backups` (the last 10). See [save sync](docs/save-sync.md).
- **Saves between Mac and iPhone / iPad over Xcode's connection:** `python3 games/totk/ios/tools/save_sync.py status` shows both saves; `pull` copies the device's save to the Mac, `push` the Mac's to the device, and `auto` copies whichever side changed since the last sync (`--dry-run` only says what it would do; `install` runs `auto` every 2 minutes while the device is connected). It never copies while the game is saving, never into a running game, and never into an older game version than the one that wrote the save; when both sides changed, the newer save wins and the other is kept as a conflict copy. Every replaced save is backed up first in `~/Library/Application Support/MacNativeGames/TOTK/save-sync/backups`. See [save sync](docs/save-sync.md).
- **Memory:** the game uses close to 4 GB on an iPhone 14, near what the phone can give it. In busy areas iOS can close the game after several minutes of play when the phone runs out of memory (seen twice on an iPhone 14, about 8 minutes in). Reducing the game's memory on iOS is the next piece of work.

### Cheats (optional)

`--cheats` builds a cheat file into the app: an Atmosphère cheat file (the format Atmosphère, Yuzu and Ryujinx use) named after the selected version's build ID, or a folder that holds it. Base 1.0.0 is `082CE09B06E33A12.txt`. Only the file for the version you build is used; one for another version stops the build. The repository ships no cheat codes.

```sh
python3.14 build_app.py ~/Games/totk.xci -k ~/keys/prod.keys --cheats ~/Games/cheats/
```

The app then lists every cheat in the file, all off until you choose them, in a **Cheats** menu and in a panel over the game (key **6**, or both sticks pressed together on a controller; ↑↓ or the D-pad select, Space or A toggles, 6, Esc or B closes, and the game receives no input while it is open). Your choices are kept, and **Turn off all cheats** (last row of the panel, or the menu) switches everything off. Cheats that only change values in memory (arrows, rupees, moon jump) switch on and off immediately. Cheats that patch the game's code (for example infinite durability) take effect when the game restarts, and the panel says when a restart is needed: the code is never made writable while the game runs. A code patch is refused, and listed as not supported, when it would overwrite an instruction this project rewrites or jump out of the game's code. The first time you enable a cheat, your profile is copied to `profile-before-cheats`; cheats can corrupt saves on a real console too.

## Controls

On Mac, controllers recognized by macOS (such as DualSense and Xbox controllers) are mapped to a Switch Pro Controller. PlayStation and Xbox face buttons map by position, so each one sits where the game's prompt shows it: the right button is A, the bottom B, the top X and the left Y (Circle / Cross / Triangle / Square; Xbox B / A / Y / X). Nintendo controllers map by label. `TOTK_FACE_BUTTONS=label` maps every controller by label instead. Rumble and motion require the corresponding features to be exposed by the controller and macOS; not every controller has a gyroscope. Mac keyboard mappings use physical key positions corresponding to a US layout:

| Key | Switch | | Key | Switch |
|---|---|---|---|---|
| W A S D | left stick | | arrow keys | right stick |
| J | A | | K, Space | B |
| I | X (jump) | | U | Y (attack) |
| Q / E | L / R | | Option / Shift | ZL / ZR |
| F T H G | d-pad left / up / right / down | | Z / C | left / right stick press |
| Return | + (menu) | | Tab, Esc | − (map) |

Controller layout (Mac): by default the face buttons follow the Switch layout by position: the right button is A, the bottom B, the top X and the left Y on every controller (Nintendo controllers by their labels, which are the same; `TOTK_FACE_BUTTONS=label` maps PlayStation and Xbox controllers by label instead). To change any button, put lines `physical = Switch [mode]` in `~/Library/Application Support/MacNativeGames/TOTK/controls.txt`; lines not given keep the default, `#` starts a comment and `none` leaves a button unused. Physical buttons are named by position: `south` `east` `west` `north` (face buttons), `l1` `r1` `l2` `r2` `l3` `r3`, `menu` (Options / Menu), `view` (Create / View), `up` `down` `left` `right` (d-pad) and `touchpad` (the touchpad click of a DualSense or DualShock 4, unused by default: `touchpad = minus` opens the Purah Pad). Switch buttons: `a` `b` `x` `y` `l` `r` `zl` `zr` `plus` `minus` `lstick` `rstick` `up` `down` `left` `right`. The optional mode says how long the Switch button is held: `hold` (default) while the physical button is down, `tap` for 400 ms after each press, and `toggle` on and off at each press, cleared when the left stick returns to center after moving, and `aim <fire button>` while the button is down except while the fire button is also down: `l2 = zr aim r2` draws the bow with L2 and shoots with R2, and R2's own line applies only while L2 is up. The default is the same as this file, which maps by position:

```
east = a
south = b
north = x
west = y
```

To run without holding a button, two lines can drive the run button: `south = b tap` (tap the bottom button again and again, as in GTA) or `l3 = b toggle` (click the stick once and move; it stops when the stick returns to center). The Switch's B is contextual: while climbing, a held B lets go of the wall, so a toggle that is still on when you climb drops the player. Release the stick before climbing, or use `tap`.

The launcher checks the file at every start and names the first bad line in an alert. Delete the file for the default layout.

Settings and screen keys (Mac): **⌘,** or **F1** opens the in-game settings menu (a controller's **Home** button, or Options and Menu pressed together, does too), **F2** hides or shows the game's HUD. Keys **3** and **4** below predate the menu: *Settings › Display › Output resolution* and *Aspect ratio* do the same and more.

Mac-only extra keys: **1** cycles the on-screen overlays: all of them (FPS, shader count, system load and the shortcut reminder, together in one small box in the window's top-right corner; `TOTK_HUD=corners` puts the readings back in the picture's corners), then minimal (the FPS only), then none; **2** frame interpolation (off → 30+30 → half the refresh rate → unlimited), **3** cycle picture mode (MetalFX 2K → native 2K → off), **4** ultrawide 21:9 on / off (1.2.1 and 1.4.3: the camera sees more at the sides and the picture fills a wider window; `TOTK_ASPECT=32:9` or `3440x1440` picks another shape and starts with it on; the HUD, menus and map keep their shape in the middle 16:9 of the window, the map with black bars at the sides; markers that follow objects in the world are not moved out yet), **5** edge smoothing on / off (1.4.3: MetalFX temporal anti-aliasing of the 3D scene, off at start, `TOTK_TAA=1` starts with it on; it smooths the stair-stepped edges of the scene and works together with the picture modes of **3**; `TOTK_ANISO=16` also turns on 16x anisotropic filtering for the ground, walls and foliage), **6** (or L3+R3 on a controller) the cheat panel (when the app has cheats; see Cheats above). The shortcut reminder shows the current interpolation and picture modes and refreshes twice a second in both HUD layouts. MetalFX is off by default and only starts when you select its mode with **3**; resizing the window never enables it automatically. Text entry (naming a horse) uses the Mac keyboard. iPhone/iPad uses touch controls or a compatible game controller and does not show the Mac 1/2/3/4/5 shortcut prompts. The touch layout follows a Switch controller: L and ZL at the top left, R and ZR at the top right, − and + between them, the d-pad on the left and A, B, X, Y on the right. The d-pad sits above the left stick and A, B, X, Y above the right stick, and L3 and R3 (stick presses) are in the bottom corners. Touch the left half away from the buttons and drag for the left stick, the right half for the camera (right stick); each stick is centred where your finger goes down. With a game controller connected the touch controls are hidden: touch the screen to show and use them, and they hide again 3 seconds after you lift your fingers.

**Ultrawide layouts (optional, 1.4.3):** with key 4 the HUD and menus stay in the middle 16:9. To put the HUD in the corners of a 21:9 or 32:9 display instead, make layouts for its shape from your own game files and start with them: `.venv/bin/python scripts/make_romfs_patches.py --version v143 --aspect 3440x1440` (writes `extracted/v143/romfs-patches-3440x1440`; `--hud center` keeps the HUD in the middle) and `scripts/make_output_res_overlay.py 3440x1440 --version v143`, then `TOTK_OUTPUT_RES=3440x1440 TOTK_ASPECT=3440x1440 TOTK_UI_SQUEEZE=0 TOTK_ROMFS_PATCHES=extracted/v143/romfs-patches-3440x1440` (in the work folder). The layouts are read at start, so such a session stays wide; key 4 is not meant for it. Every edited pane is found by name and checked against the value the game ships before it is written.

The menu bar shows the live frame rate as the game's own menu, on the left after the app menu ("60 FPS", or "50+50 FPS" while frame interpolation adds generated frames), whatever key 1 shows in the window; in full screen it is hidden with the menu bar. `TOTK_MENUBAR_FPS=0` leaves the menu bar alone; `TOTK_TITLE_FPS=1` also ends the window's title in the same readout ("… Ver. 1.4.3 — 60 FPS").

Native 2K (key 3) changes the game's composition canvas and UI to 2560×1440. For any other size, up to your display's native pixels or 4K, and for the 3D scene's own resolution, use *Settings › Display* (Output resolution, 3D render scale). The manual MetalFX mode also works in smaller windows and scales to the actual output size, including 4K.

Language: the game and the launcher's own text (startup screen, shader warm-up progress, overlays, keyboard prompt) follow the Mac's language. The launcher text is Chinese on a Chinese Mac and English otherwise. In a 1.4.3 build, Brazilian Portuguese selects the game's official pt-BR text. To give only this game another language, set its preferred language, for example `defaults write org.macnativegames.totk AppleLanguages -array pt-BR` (`defaults delete org.macnativegames.totk AppleLanguages` removes it). To override, launch with `TOTK_LANG` for the game (for example `open --env TOTK_LANG=en-US "Tears of the Kingdom.app"`) or `TOTK_UI_LANG=zh` / `en` for the launcher text.

## Settings menu

The game has an in-game settings menu on Mac, iPad and iPhone, built like a recomp's: 35 settings across Display, Graphics,
Frame rate, Interface, Controls, Audio and World, plus a Mods tab. Nearly everything applies while you play; *Maximum
output*, *Button prompts* and mod changes apply at the next start, and on the Mac **Restart now** does that in one click.

- **Display:** output resolution (match the window, display native, 4K … 720p, custom width), 3D render scale (25–200 %),
  aspect ratio (wider and taller screens see more, nothing is stretched), maximum output.
- **Graphics:** field of view, anti-aliasing (the game's / off / FXAA / SMAA), sharpening (AMD CAS), anisotropic filtering,
  texture detail, frame interpolation, bloom, depth of field, lens flare, cave fog, shadow resolution, render distance.
- **Frame rate, Interface, Controls, Audio, World:** frame rate limit, performance readout (with average and 1 % low),
  shader counter (compiling / new / total), Download new shaders (Mac: the shared list, once a day), Hide HUD, touch controls (iOS), camera speed, face buttons, Button prompts
  (Switch / Xbox / PlayStation icons from a mod), rumble, motion, volume, time of day speed, Send saves (AirDrop).

On the Mac open it with **⌘,** or **F1** (or a controller's Home button); it is a sidebar inside the game window, so it
works in full screen and the picture stays in view while you change things. On iPad and iPhone use the gear at the top, a
three-finger tap or a controller's Home button. See [docs/settings.md](docs/settings.md), [mods](docs/mods.md) and
[save transfer](docs/save-sync.md).

## Where things are

- Settings: `~/Library/Application Support/MacNativeGames/TOTK/settings.txt` (the menu writes it; see [docs/settings.md](docs/settings.md))
- Save sync between the Mac and an iPhone / iPad: [docs/save-sync.md](docs/save-sync.md)
- Saves: `~/Library/Application Support/MacNativeGames/TOTK/profile`; saves replaced by an AirDrop import are kept in `save-backups` next to it (the last 10)
- Button prompts *Automatic* remembers the last controller in `last-controller.txt` next to `settings.txt`
- Shader cache: `~/Library/Application Support/MacNativeGames/TOTK/shader-cache` (delete it to translate everything again)
- Game data the app uses (RomFS image, prepared shaders): `~/Library/Application Support/MacNativeGames/TOTK/game-data` (`python3.14 build_app.py --prune-data` deletes what no app uses)
- Shaders the shared list does not have yet: `~/Library/Application Support/MacNativeGames/TOTK/shader-list-contribution.txt`
- Log: `~/Library/Logs/MacNativeGames/totk.log` (the run before: `totk.previous.log`)
- Mods: `~/Library/Application Support/MacNativeGames/TOTK/mods` (iPhone / iPad: the app's `mods` folder in Files). One folder per mod, as Yuzu / Ryujinx mods ship them: unzip the mod there so that its `romfs` folder ends up inside (`mods/<Mod>/romfs/...`; an extra wrapping folder from the archive is fine). `mods.txt` in the same folder sets the order (first line wins a file two mods both change) and turns a mod off with `-` in front; new folders are added at the end, on. Only game data (RomFS) is loaded: code patches (`exefs`), TKMM `.tkcl` packages and merging two mods that change the same file are not supported yet; a mod's own ResourceSizeTable is left out and the game's is adjusted for the mods' files instead. The log lists every mod, what it replaces or adds and what was left out. See [docs/mods.md](docs/mods.md). The settings menu's **Mods** tab switches mods on and off, orders and removes them, and browses and installs mods from GameBanana (Mac: zip, 7z and RAR; iPhone / iPad: zip): see [docs/mod-browser.md](docs/mod-browser.md). The older single folder `mods/<v0|v112|v121|v143>` with a `manifest.tsv` (`games/totk/scripts/make_mod_manifest.py`) still loads; its files are checked against their SHA-256 at start.
- Button icons: `python3.14 games/totk/scripts/make_button_icons.py <mods folder> --app <Tears of the Kingdom.app> --style keyboard` (or `--style xbox`) writes a mod (in the older single-folder layout: give it `mods/<version>`) that draws the Mac keyboard keys (J K I U for A B X Y, W A S D, the arrow keys and so on) or Xbox buttons (B A Y X in the Switch positions, LB/RB, LT/RT, Menu/View) in the game's button prompts (the settings menu's *Button prompts* picks among installed icon mods by name; see [docs/mods.md](docs/mods.md)). It redraws the icon font from your own RomFS (nothing of the game is in the project) and writes the manifest; files already in the folder are kept. Needs Python 3.14 and `python3.14 -m pip install fonttools`. If the folder already has a font archive (another mod's, or an earlier run) it stops; `--replace` overwrites it, which is also how to switch styles. The Xbox icons match the default face-button mapping (by position); with `TOTK_FACE_BUTTONS=label` or face buttons changed in `controls.txt` they do not. Remove the font file and run `make_mod_manifest.py` to go back. Prompts drawn as fixed images (some tutorial animations) keep the Switch icons.

## Known issues

- Supports base 1.0.0, or base plus the separate 1.1.2, 1.2.1 or 1.4.3 update NSP/NSZ. Other versions, DLC, combined packages, compressed base games (NSZ/XCZ) and personalized tickets remain unsupported. On iPhone and iPad only 1.4.3 has been tested; places no player has reported can show missing objects, and on an iPhone 14 iOS can close the game when memory runs out (see [iPhone and iPad](#iphone-and-ipad)).
- No online features, no amiibo.
- New shaders can cause compilation stutters. Cache reuse reduces these, but does not guarantee stutter-free gameplay.
- This fork: *Cave fog* changes the fog in caves and enclosed places, not the outdoor haze; *Render distance* only shows a difference below 100 % (the game already draws to 25 km); camera speed above 100 % only speeds up a partial stick push; Hide HUD also blanks menus while it is on; AirDrop save transfer and the iPad build have not been tried on devices end to end yet.
- The automated tests (101 Python tests and C tests for settings, mods, controller layouts, RomFS paths and serial sets) cover generated container/update fixtures, relocation formats, work-folder isolation, packaging, desktop migration, shader extraction (BNSH, agl `.sharcb` and game-module shaders), the shared shader list (with the Mac app's download: malformed lists, the native shader scan, the preparation) and the players' contribution file, first-import cache preservation, the Metal toolchain lookup and 27 native RomFS path cases. GitHub Actions is configured to run the source tests and the native path parser under the undefined-behavior sanitizer, without game data or keys. A complete Mac build also passed with a local XCI-derived NSP fixture and generated common ticket. An independent retail base NSP and gameplay from that input have not yet been tested.

## How it works

See [docs/how-it-works.md](docs/how-it-works.md).

Recorded performance experiments, remaining work and comparison methods: [Mac performance follow-ups](docs/performance-followups.md).

## Acknowledgements

- **[UltraCam](https://gamebanana.com/mods/480138)** and **[NX Optimizer](https://github.com/MaxLastBreath/nx-optimizer)** by **MaxLastBreath**: the ideas behind several settings (time of day speed, fog, depth of field, bloom, shadow resolution, render distance, camera speed, hiding the HUD). No UltraCam or NX Optimizer code or binaries are included; each setting is this port's own 1.4.3 implementation.
- **[d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile)** (MIT) developed this way of running Switch games natively (for Diablo II: Resurrected): the NSO rewriter, the Horizon host and the NVN-on-Metal layer here started as its generic parts.
- **[nxnative](https://github.com/seedds/nxnative)**: the one-script packaging and NSP common-ticket import follow its approach.
- **Ryujinx** (MIT): its shader translator (`vendor/ryujinx`) turns the game's Maxwell shaders into SPIR-V, and its system-service code is a reference. With this project's patches and service it is compiled ahead of time (.NET NativeAOT) into `games/totk/prebuilt/macos-arm64/totk_xlate`; `games/totk/tools/totk_xlate/publish_native.sh` rebuilds it (needs the .NET 10 SDK, only for developers).
- **[SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross)** (Apache-2.0) turns SPIR-V into Metal Shading Language; it is fetched at a pinned commit and compiled with clang during the build.
- **[TkSharp](https://github.com/TKMM-Team/TkSharp)** (MIT, TKMM Team): the ResourceSizeTable size estimate the mod loader uses. **[zstd](https://github.com/facebook/zstd)** (BSD): the decoder for the game's compressed files. **[SMAA](https://github.com/iryoku/smaa)** (MIT) and **[AMD FidelityFX CAS](https://github.com/GPUOpen-Effects/FidelityFX-CAS)** (MIT), ported to Metal: the SMAA anti-aliasing and sharpening settings (the FXAA option is this port's own) (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)). Mods are browsed and downloaded from **[GameBanana](https://gamebanana.com)**; each mod keeps its author's license and is never re-hosted.
- **deko3d** (zlib): texture layout sizes. **bcdec**: BC texture decoding. **libopus** and **libvpx** (BSD) from Homebrew: audio and the pre-rendered movies.

## License

MIT (see [LICENSE](LICENSE)). Third-party code keeps its own license: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
