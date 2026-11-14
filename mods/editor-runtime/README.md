# editor-runtime

The official mod of OpenRAC Editor: it plays, in OpenRAC's port, what the editor packs with a
level beside its placements. A level made in the editor needs it on; the port says so when a level
has editor parts and the mod is off.

- **Cutscenes** (`src/cutscenes.cpp`): the editor packs each cutscene with its level
  (`level_NN/cutscenes/<name>.cutscene`); the mod reads them when the level loads and starts each
  one when its trigger happens. Start skips a cutscene that allows it. Format:
  [docs/CUTSCENE_FORMAT.md](../../docs/CUTSCENE_FORMAT.md).
- **Behaviours** (`src/behaviours.cpp`): simple movements set in a moby's Inspector (spin, bob,
  patrol, orbit, face the hero, follow the hero), from `level_NN/behaviours.json`. Each moby named
  there is driven (its own update stops) and posed every tick. Format:
  [docs/BEHAVIOUR_FORMAT.md](../../docs/BEHAVIOUR_FORMAT.md).

It uses only the port's mod interface (1.15; SDK 1.15.1 for the field of view, the hidden HUD, the
music, joint layers, joints as targets, sounds on actors, the air, slow motion, hidden ties,
animated props and feet on the ground, which it skips on an older port), so the game's own logic
is not changed.

## Build

```sh
python3 tools/fetch_sdk.py          # the SDK sdk.lock pins, into .sdk/
cmake -S . -B build -G Ninja        # or -DOPENRAC_SDK=<an OpenRAC checkout>/sdk
cmake --build build
python3 .sdk/openrac-mod-sdk-*/tools/package_mod.py build/package/editor_runtime
```

## Test

The sums are apart from the port, with their tests: the cutscene timeline (tracks, paths, the
camera and its field of view, look-at, joint layers, the music, subtitles and their pictures,
fades, the letterbox, volumes, the legs' IK) in `src/timeline.hpp`, the behaviour presets in
`src/presets.hpp`, and the JSON reader in `src/json.hpp`:

```sh
cmake -S tests -B build/tests -G Ninja && cmake --build build/tests && ctest --test-dir build/tests
```

## Try it

Run the port with the editor's pack and the mod, for example:

```sh
openrac-rac1-ntsc --mods examples --plugins <folder>/plugins.json --level 3
```

with `plugins.json` as `{"format": 1, "mods": [{"id": "editor-runtime", "enabled": true}]}` and
the packaged mod in `<folder>/editor-runtime`.

`examples/` is a pack with:

- one cutscene for Kerwan (level 3), right where Ratchet lands: a camera move, a cut, a second
  Ratchet walking, two subtitles;
- two behaviours for Rilgar (level 5): the vendor by the landing spot spins, a taxi bobs.
