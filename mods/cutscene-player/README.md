# Cutscene player

An official mod of OpenRAC Editor: it plays the cutscenes made in the editor, in OpenRAC's port.
The editor packs each cutscene with its level (`level_NN/cutscenes/<name>.cutscene`); the player
reads them when the level loads and starts each one when its trigger happens. The format is in
[docs/CUTSCENE_FORMAT.md](../../docs/CUTSCENE_FORMAT.md).

It uses only the port's mod interface (1.14 with `pack_folder` and `game_language`), so the game's
own logic is not changed. Start skips a cutscene that allows it.

## Build

```sh
python3 tools/fetch_sdk.py          # the SDK sdk.lock pins, into .sdk/
cmake -S . -B build -G Ninja        # or -DOPENRAC_SDK=<an OpenRAC checkout>/sdk
cmake --build build
python3 .sdk/openrac-mod-sdk-*/tools/package_mod.py build/package/cutscene_player
```

## Test

The timeline's sums (tracks, paths, the camera, subtitles, fades, the letterbox, volumes) and the
JSON reader are in `src/timeline.hpp` and `src/json.hpp`, apart from the port, with their tests:

```sh
cmake -S tests -B build/tests -G Ninja && cmake --build build/tests && ctest --test-dir build/tests
```

## Try it

Run the port with the editor's pack and the mod, for example:

```sh
openrac-rac1-ntsc --mods <pack folder> --plugins <folder>/plugins.json --level 3
```

with `plugins.json` as `{"format": 1, "mods": [{"id": "cutscene-player", "enabled": true}]}` and
the packaged mod in `<folder>/cutscene-player`.

`examples/` is a pack with one cutscene for Kerwan (level 3), right where Ratchet lands: a camera
move, a cut, a second Ratchet walking, two subtitles. Pass `--mods examples`.
