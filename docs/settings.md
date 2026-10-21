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

A setting marked *(after restart)*, or a change in the Mods tab, shows **Restart now** at the bottom of the menu (and on
the Mods tab's banner): the game closes the usual way (a save in progress finishes first) and the app opens again. Mac only;
on iPhone and iPad close the app from the app switcher and open it again.

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
| **Cave fog** | 0–100 % of the game's fog in caves and other enclosed places (the fog ramp the Env update writes from the area's palette): 50 % reaches it at twice the distance, 0 % has none. Outdoors on Great Sky Island it made no visible difference: the outdoor haze comes from the sky / atmosphere and is not adjustable yet. Version 1.4.3. |
| **Shadow resolution** | The sun's cascaded shadow map: *the game's own* (the game picks the side every frame: 400 to 800 per cascade docked, 800 on Great Sky Island), *2x* or *4x* that for sharper shadow edges, capped at 2048 (so 4x of 800 is 2048); the game's other views grow too. 4x adds about 190 MB of depth maps in all. Mac only; changes while you play (the next frame). Version 1.4.3. |
| **Anti-aliasing** | The game's own FXAA, off, FXAA or SMAA, on the 3D scene before the game draws its text and icons. |
| **Sharpening** | AMD FidelityFX CAS on the finished picture, under the text and icons. |
| **Anisotropic filtering** | The game's, or 2–16×: sharper ground and walls seen at an angle. |
| **Texture detail bias** | Below 0 picks sharper texture levels; too low shimmers. |
| **Depth of field** | On (the game's) or off. The game blurs the distance with one full-screen pass over the scene (it reads the depth and blends a blurred half-size copy of the picture over everything far away); off leaves that pass out, so far away stays as sharp as it was drawn. Version 1.4.3. |
| **Bloom** | 0–100 % of the game's glow around bright light. The tone map adds the bloom texture to the scene; below 100 % that texture is scaled first, so 0 % is no bloom at all. Version 1.4.3. |
| **Lens flare** | On (the game's) or off. With the sun in view the game draws about a dozen rings, ghosts and a glint over the scene before its tone map; off leaves those draws out (the sun, its glow and bloom stay). Live. Version 1.4.3. |
| **Render distance** | 50–300 % of the camera's far clip distance (the game's own is 100 %). The game's own is 25 km, more than the map is across, so below 100 % land in the far distance is cut off sooner and above 100 % mostly changes nothing you can see: it does not load more of the world or raise the detail of distant land, which the game decides separately. |

## Frame rate, interface, controls, audio

- **Frame rate limit**: *Auto (steady)*, 30, 40, 60, 120 fps or the display's refresh rate. When the display paces the
  game (a 120 Hz display) the limit holds each frame for whole refreshes (30 fps = 4 refreshes at 120 Hz); 40 needs a
  120 Hz display.
  *Auto (steady)* (`framerate.cap = auto`, `TOTK_UNCAPPED_HZ=auto`; the Mac default) picks the highest rate that divides
  the display's and that the game holds: 120 / 60 / 40 / 30 at 120 Hz, 60 / 30 at 60 Hz. A frame rate between those
  steps judders: at about 98 fps on a 120 Hz display one frame in five stays up for two refreshes, so motion alternates
  between 8.3 and 16.7 ms. Auto measures the game's own frame time without the limit's wait (the newest 2.5 s, leaving
  out shader builds, loading and light title / loading frames, and single frames over 100 ms). It goes down as soon as
  more than 1 frame in 10 misses the current rate, to the rate the 95th percentile fits with 8 % to spare. It goes up
  after 10 s in which the 95th percentile stayed under 85 % of the faster rate's frame time, or tries the faster rate
  after 20 s at a steady rate (a frame time measured at a lower rate misleads both ways: the GPU overlaps our wait,
  and it clocks down when idle); a try that fails within 5 s is remembered, and that rate is not tried again for
  5 minutes unless the scene gets lighter. Frames after a stall of 100 ms or more and after a size change are left
  out. Each change is logged (`[auto-cap]`). With frame interpolation at *30 + 30* or *Half the refresh rate* the interpolation paces the game and
  Auto steps aside (it resumes at its last choice when interpolation is turned off); with *Every frame* it limits the
  game frames as a fixed limit would. On a display the game does not wait for (the uncapped mode below 120 Hz) Auto
  moves the uncapped clock's ceiling instead (60 Hz: 60 or 30). The iPhone / iPad default stays the display rate.
- **Performance readout**, and its **Readout detail**: *Simple* (`FPS 60  2560x1440`) or *Detailed*, which adds the
  average and the 1 % low over the last 5 seconds (`FPS 60  avg 59  1% low 48  2560x1440`). The 1 % low is the frame
  rate of the slowest 1 % of the game's frames in that window, so a single long hitch shows there while the average
  barely moves.
- **Hide the HUD**: hides everything the game draws over the picture (hearts, map, prompts, quest text) for clean
  screenshots; **F2** on a Mac keyboard toggles it and the setting follows. It hides the game's menus too (the pause
  menu and the inventory are drawn the same way as the HUD: with it on, the pause menu is a black screen), so turn it
  off to use them. The performance readout is the port's own and stays.
- **Shader counter** (after restart), and on iPhone / iPad the **touch controls** (when they show, opacity, size) and
  the **settings button**.
- **Button prompts**: the button icons the game shows: *Automatic* (the last controller you connected: Xbox icons for an
  Xbox controller, PlayStation icons for a DualSense or DualShock, the game's own otherwise), *Switch*, *Xbox* or
  *PlayStation*. Xbox and PlayStation use a button-icon mod installed from the Mods tab (for Xbox: "Xbox UI for
  v1.4.3" on GameBanana); the setting turns exactly one of them on at start and the others off, so going back to the
  game's own icons is one choice, not uninstalling. Applies after a restart (the game loads its icons once at boot).
- **Camera speed, horizontal / vertical**: 25–300 % of the right stick's left-right and up-down push (controller and
  arrow keys alike), each axis on its own. The push is scaled before the game reads it and stays within the stick's
  range, so above 100 % a partial push turns the camera like a fuller one and the game's top speed (stick all the way,
  or an arrow key) stays the game's; below 100 % every push, the arrow keys included, turns it slower.
- **Face buttons** (PlayStation and Xbox by position or by label), **rumble** and **motion controls** (Mac).
- **Volume**.

## Credits

Several settings follow features first offered on Switch and emulators by MaxLastBreath's UltraCam
(https://gamebanana.com/mods/480138) and NX Optimizer (https://github.com/MaxLastBreath/nx-optimizer). This port
reimplements them independently for 1.4.3 and contains none of their code.
## World

| Setting | What it does |
|---|---|
| **Time of day speed** | *Paused*, 0.25×, 0.5×, *Normal* (the game's own), 2×, 4× or 8×: how fast the day passes, the clock, the sun, the sky and the shadows with it. *Paused* stops the time of day where it is. Events that set the time (sleeping, cutscenes) still do, and where the game holds the clock itself (the opening beneath Hyrule Castle stays at 7:00) the setting has nothing to speed up. |

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
