# Contributing

Thanks for helping. This page says how the project works and what a pull request needs. The easiest way to help needs no code at all: play, and [send your shaders](https://github.com/ly05010419/MacNativeGames/issues/new?template=shader-list.yml) (see the [shared shader list](docs/shader-list.md)).

## Legal: no game content, ever

- Never commit game content: no game program or code, RomFS files, art, text, audio, translated shaders or libraries, keys or saves. The shader list holds shader keys and bind states only.
- The repository is MIT. Write your own code. Do not copy GPL code or code from other mods (UltraCam, Dynamic FPS, TOTK-Ultrawide and so on); reading them for ideas is fine. Third-party code you bring in keeps its license, and its source and license go into [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- We do not help anyone obtain keys or game files.

## How the port works, and what it must not become

- **The game's own ARM64 code runs natively on Apple silicon.** It is rewritten ahead of time, loaded and relocated, host calls go through gates, system services are answered by a small host (HLE), and NVN graphics are rebuilt on Metal. See [how it works](docs/how-it-works.md).
- **No CPU emulation.** No interpreter, JIT, dynamic binary translation or CPU emulator (Unicorn, QEMU, the CPU backends of Ryujinx or yuzu). Do not copy Horizon services, IPC or schedulers from an emulator. An emulator may be used outside the port as a reference (to compare a picture or confirm a behavior); it is not a result of the port.
- **Fix the platform, not the symptom.** ABI, TLS, loading, memory, threads, input, files and saves, audio, NVN on Metal and the app life cycle come first. A platform bug is fixed in the platform layer, not hidden behind another patch in game code.
- **Game patches for features are welcome** (frame rate, aspect ratio, output resolution). Every patch checks the original instruction at its address first and stops with an error when it differs; it never writes to an unchecked place.
- **The game is only ever rebuilt from the original files.** `build_app.py` starts from the unmodified ExeFS/NSO, applies the rules declared in the repository, and checks the input hashes (`expected-*.json`, `game_version.h`). Never patch an already rewritten binary or edit bytes by hand. When you add or change a build step, record what it used and produced (tool and rule fingerprints, options, output hashes) in the existing checks rather than in a new mechanism.
- **Change game logic as little as possible.** Do not change the player's or the world's state to make something pass. Gameplay changes are separate, optional features that can be switched off.

## Repository layout

- Tears of the Kingdom and Breath of the Wild share `games/totk`: one runtime, with switches for each game.
- Every other game lives in `games/<name>/` and carries its own copy of the runtime, graphics layer, rewriter and scripts. A change for one game never edits another game's files.
- The NVN layer maps NVN semantics one by one (resources, shaders, state, draws, synchronization, presentation) and contains no camera, scene or gameplay decisions. Game-specific handling sits apart, says why it exists, is behind a switch, and is never needed for the game to run.

## Picture quality and performance

- **The picture must be correct before it is fast.** Missing objects, black blocks, wrong geometry or changed gameplay are never an acceptable price for frames.
- **Mac: no loss of picture quality or effects.** A performance change must leave the picture as it was; only floating-point differences of the kind the original already has are acceptable.
- **iPhone and iPad: a slight loss of picture quality is acceptable for performance.**
  - Resolution is never lowered (neither the render nor the output resolution).
  - Lower precision and reasonable floating-point differences are fine; pixel-exact output is not required.
  - An expensive effect may be reduced or removed when the picture as a whole only gets slightly worse. A clearly worse picture needs the maintainer's agreement first.
  - Keep iOS-only trade-offs behind a platform condition or an iOS switch, so the Mac picture does not change.
  - Say what was given up and what it bought, with before and after screenshots.
- **Measure.** Decide what to optimize from measurements of these games, not from another project's list of optimizations. Use existing tools first: Instruments (Metal System Trace, Time Profiler, Game Performance), Xcode GPU frame capture, the Metal HUD (`MTL_HUD_ENABLED=1`) and the repository's own diagnostic switches. Write your own timer only for what the tools cannot show, and say what it measures.
- **An optimization without an immediate frame-rate gain can still be right.** Explain when it applies, what work it removes (computation, memory traffic, synchronization, allocations) and what it adds. Keep it when the mechanism is clear and nothing regresses, even if another bottleneck hides the gain for now. Report the saving in that work, the added cost and the whole-frame change separately; do not call a local saving a frame-rate gain.
- Unfinished or faulty experiments stay switched off by default until they are fixed.

## Pull requests

- **One topic per pull request.** Unrelated features, debug code and reformatting go elsewhere. Do not change existing keys, defaults or verified version tables (`expected-*.json`, `game_version.h`) unless that is the point of the pull request.
- **Build what you touched.** Build the app for every game version you changed. Code shared with iOS (the runtime, `d2r_gfx`) must build for iOS too (`build_app.py --platform ios`); files used only by Breath of the Wild are excluded from the iOS project in `games/totk/ios/project.yml`. If you could not build one of them, say so.
- **Run the tests.** `python -m unittest discover -s tests` (what GitHub Actions runs), plus the probes and tests for the part you changed (`probe-*` targets in `games/totk/Makefile`, `tests/`).
- **Say how you tested, per platform:** built only, played on a Mac, or played on an iPhone or iPad. Name the game version and where in the game. Tests and probes passing do not replace playing the game.
- **Diagnostics are off by default,** switched on by an environment variable or an input-file command.
- **Commit messages:** `<type>: <description>`, with type one of feat, fix, refactor, docs, test, chore, perf, ci.
