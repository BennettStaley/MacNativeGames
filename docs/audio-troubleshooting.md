# Streamed audio: diagnosis, fix and validation

## Missing dialogue and intermittent background music

The observed failure affected long audio streams: subtitles appeared without
speech, and background music could play a short fragment and then fall silent.
Short effects could still work. These symptoms alone are not sufficient to
identify the cause; device routing and other runtime faults can also cause
audio problems.

In the reproduced case, the game constructed requests such as:

```text
content://Sound/Resource/Stream/Dm_OP_0001.bwav
```

The old RomFS parser removed exactly one separator after the mount name. Its
relative-path validator then rejected the remaining leading slash, returning
file-not-found even though the resource was present. Prefetched audio could
play while the remaining stream buffers contained zeros.

The fix in `games/totk/runtime/romfs_path.h` skips repeated separators immediately
after the mount prefix before validating the relative resource path. Mount
names still have to match. Empty resources, `.` and `..` components,
backslashes, and empty interior components remain rejected. Resource bytes,
shader code and save formats are unchanged.

## Rebuild and verify

1. Rebuild with the normal `build_app.py` command and your own original inputs.
   The same work directory can be reused. A rebuild to the same app path replaces
   the runtime; the profile and shader cache are stored outside the app.
2. Run the game-data-free parser regression (27 cases):

   ```sh
   python3 -m unittest discover -s tests -p test_romfs_paths.py -v
   ```

3. With Python 3.14 and the build requirements installed, run the existing
   container/update tests together with the parser regression:

   ```sh
   python3.14 -m unittest discover -s tests -v
   ```

4. For local integration testing, the rebuilt runtime supports `probe-fs` and
   `probe-audio`. Run from its resource directory, for example:

   ```sh
   cd "/path/to/Tears of the Kingdom.app/Contents/Resources/totk"
   ./build/totk-native rewritten/v0 probe-fs
   ./build/totk-native rewritten/v0 probe-audio
   ```

   `probe-fs` checks that normal, double-slash and triple-slash paths read the
   same opening-audio bytes and still reject traversal. `probe-audio` checks
   constructed mixer/decoder state without opening an audio device; it does not
   prove that in-game streaming or the selected speaker/headphones are audible.
   For a 1.1.2 app, use `rewritten/v112` instead. The added resource integration
   checks have been exercised locally on 1.0.0, not on 1.1.2.

5. Play the opening scene and confirm both dialogue and background music through
   the intended output device. This listening check confirmed the 1.0.0 fix on
   an Apple M5 Mac running macOS 26.5.2.

## If sound is still missing

- Check the current macOS output device and volume. Test a known system sound;
  if that also fails, inspect the device connection first.
- Turn on the log (`defaults write org.macnativegames.totk TOTK_DEBUG_LOG -bool YES`,
  then start the game again) and check `~/Library/Logs/MacNativeGames/totk.log` for CoreAudio setup failures,
  decode errors and output-level reports. A nonzero mixed level does not by
  itself prove the selected physical output is audible.
- For local file-open diagnostics, quit the game normally and launch with:

  ```sh
  open --env TOTK_FS_LOG=1 "/path/to/Tears of the Kingdom.app"
  ```

  A logged request is not proof of a successful open. Compare it with the
  parser regression and the rebuilt runtime's `probe-fs` results.
- First-use shader compilation can interrupt playback under load. That is a
  separate issue from repeated root separators and is not fixed by this patch.

Do not upload game packages, extracted resources, keys, saves, generated runtime
tables or complete logs when sharing a report. Describe the game version, the
scene, the output device, and relevant redacted error messages instead.

## Audio effects and output buffering (2026-10-18)

The renderer runs every known effect type (1-9): besides the auxiliary send/return, 3D reverb and limiter it already had,
delay, plain reverb (its early reflections chosen by the early mode), compressor, effect-level biquads, buffer mix and
capture. It also fixes the revision decoding, the six-channel mapping, compressor statistics, PCM24 input, the circular
sink and a fast Stop -> Start queue, and keeps the mixing scratch state per renderer instance. Output starts once 20 ms of
audio is queued, also after an underrun; `TOTK_AUDIO_PREFILL_MS=<0..500>` changes that, rounded up to 5 ms render blocks
(0 / 20 / 40 ms ran without underruns in the tests behind the 20 ms default). This work came from the original port and
was merged with this repository's REV15 float biquads for 1.4.3.

After the output device stalls (a Bluetooth headset reconnecting, a CoreAudio hiccup) the mixer keeps at most the prefill
plus 30 ms queued and drops older audio, so the delay does not stay (#64); the `output level` line counts these as
`backlog trims N (M frames)`.

Checks, without game data:

- `./build/totk-native-v143 rewritten/v143 probe-audio`: 46,587 production-mixer checks, REV15 voices and splitters
  included; with `TOTK_AUDIO_STOP_RELEASE=0` or `TOTK_AUDIO_REV15_NEGATIVE=1` it must fail.
- `make test-audio-effects RYUJINX_AUDIO_REF=<folder with Ryujinx.Audio.dll> DOTNET=<dotnet>`: 24 groups, 1,872,000
  samples against an unmodified Ryujinx.Audio (largest difference 4.47e-08), then 1,885,461 checks of the production
  effect graph, routing, capture / circular buffers and output prefill / recovery / stop.

## Audio lost through splitter submix chains (2026-10-22)

The submix ordering previously followed only `dest_mix_id`, even when the mixer
actually routed through `dest_splitter_id`. A receiving submix could therefore
be processed before its source. Samples arriving later were never propagated
to the final mix and were cleared on the next frame.

Ordering now follows every used splitter destination with an active receiving
mix. Depths are memoized per frame, with a recursion guard for malformed cycles.
The game-data-free regression exercises direct routing, one splitter, two
splitters, and a branching path that rejoins a downstream submix:

```sh
python3 -m unittest discover -s tests -p test_audio_routing.py -v
```

It compares production-mixer stereo output against independently calculated
PCM values under UBSan. The two-splitter and branching cases output zeros before
the fix; all four cases pass afterwards. This verifies routing, while an actual
in-game listening check is still needed to establish the cause of a particular
missing-sound report.

