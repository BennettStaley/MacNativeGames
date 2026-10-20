# The settings menu

The game has its own settings menu, the same settings on Mac, iPad and iPhone. Changes apply while you play: the picture
changes within a frame or two, except the few settings marked *after restart*. Settings are saved to `settings.txt`.

## Opening it

| | |
|---|---|
| Mac | **⌘,**, **F1**, the app menu's **Settings…**, a controller's **Home** button, or **Options and Menu** together |
| iPad / iPhone | the **gear** at the top of the screen, a **three-finger tap**, a controller's **Home** button, **− and +** held together, or **Esc** / **⌘,** on a keyboard |

On the Mac the menu is a sidebar inside the game window, so it works in full screen and the picture stays in view beside
it while you change things. Navigate with the mouse, the keyboard (↑ ↓ choose, ← → change, Return toggles, Q / E or Tab
switch tabs, Esc closes) or a controller (D-pad or left stick, A, B, the shoulder buttons switch tabs). While the menu is
open the game receives no input.

## Display

| Setting | What it does |
|---|---|
| **Output resolution** | The picture the game composes, its text and icons included: *Match the window*, *Display native* (your screen's panel pixels, 3456 wide on a 16" MacBook Pro, whatever scaling macOS uses), 4K, 1440p, 1080p (the game's own), 900p, 720p or a *Custom width* (height at 16:9). Live, up to the maximum output. Mac only: iPhone and iPad show 720p. |
| **3D render scale** | The 3D scene against the output: *The game's own* (about 83 %), or 25–200 %. 50 % of a 3456-wide output renders 1728×972; above 100 % renders more pixels than the output (supersampling). The scene is capped at 2560×1440 (see below). |
| **Aspect ratio** | *16:9* (the game's own) or *Fill the screen*, 16:10, 21:9, 32:9. A wider shape shows more at the sides; a taller one (16:10, a MacBook in full screen) keeps the game's view across and shows more above and below. Nothing is stretched: the HUD keeps its shape in the middle 16:9 of the screen. Mac only. |
| **Maximum output** | The biggest output you can switch to while playing; a bigger one sets more memory aside. *Auto* is 4K on a Mac with 16 GB or more, else 1440p. Applies after a restart. |

The status line at the top of the menu shows what the game was last asked for (`Output 3456 x 1944, 3D scene 1728 x 968`).

Limits and why:

- The 3D scene stops at 2560×1440. The game keeps its render targets in one fixed texture heap sized at start; a 4K
  scene would need a 1.5 GB heap, above the 1 GB a single zero-copy GPU buffer may take here.
- An output change rebuilds the game's canvas, and for a moment both sizes are held. The menu therefore applies a new
  output once the choice has stayed the same for 0.4 s, so clicking through several sizes rebuilds once.
- The runtime stops the game if GPU memory passes its budget (5000 MB, 6500 MB with the 4K maximum output; never above
  40 % of the Mac's memory): earlier kernel panics on this hardware happened near 15 GB of resident memory.

## Graphics

| Setting | What it does |
|---|---|
| **Frame interpolation** | MetalFX makes a frame between two game frames: off, 30 + 30, half the refresh rate, every frame (Mac and iPad). |
| **Field of view** | 70–140 % of the game's camera angle. Aiming, first person and cutscenes keep their own changes, scaled. |
| **Anti-aliasing** | The game's own FXAA, off, FXAA or SMAA, on the 3D scene before the game draws its text and icons. |
| **Sharpening** | AMD FidelityFX CAS on the finished picture, under the text and icons. |
| **Anisotropic filtering** | The game's, or 2–16×: sharper ground and walls seen at an angle. |
| **Texture detail bias** | Below 0 picks sharper texture levels; too low shimmers. |

## Frame rate, interface, controls, audio

- **Frame rate limit**: 30, 40, 60, 120 fps or the display's refresh rate. When the display paces the game (a 120 Hz
  display) the limit holds each frame for whole refreshes (30 fps = 4 refreshes at 120 Hz); 40 needs a 120 Hz display.
- **Performance readout**, **shader counter** (after restart), and on iPhone / iPad the **touch controls** (when they
  show, opacity, size) and the **settings button**.
- **Face buttons** (PlayStation and Xbox by position or by label), **rumble** and **motion controls** (Mac).
- **Volume**.

## The file

`settings.txt` — Mac: `~/Library/Application Support/MacNativeGames/TOTK/settings.txt`; iPhone / iPad: the app's
Documents folder (Files › Tears of the Kingdom). One `key = value` line per setting, `#` starts a comment; unknown keys
and bad values are ignored, so a file from a newer build still starts an older one. An environment variable that is
already set wins over the file (`open --env TOTK_FOV=120 "Tears of the Kingdom.app"`), so a test never has to touch the
player's settings. The table of settings is `games/totk/runtime/settings_schema.h`, shared by the Mac launcher, the
runtime, the iOS app and `tests/test_settings.c`.

## For testing

Test runs drive the menu's own code path through `TOTK_INPUT_FILE`: `set <key> <value>` changes a setting as the menu
does (`set display.scale 50`, `set graphics.aa smaa`), `menu` opens or closes the Mac menu, `menukey <Mac key code>`
presses a key in it, and `settings-shot <png>` saves the Mac menu as a picture.
