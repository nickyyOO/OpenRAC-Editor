# Behaviour presets

An official mod of OpenRAC Editor: it plays the simple behaviours given to mobys in the editor's
Inspector (spin, bob, patrol, orbit, face the hero, follow the hero), in OpenRAC's port. The
editor packs them with the level (`level_NN/behaviours.json`); the mod reads them when the level
loads, drives each moby named there and poses it every tick. The format is in
[docs/BEHAVIOUR_FORMAT.md](../../docs/BEHAVIOUR_FORMAT.md).

It uses only the port's mod interface (1.15; SDK 1.15.1), so the game's own logic is not changed:
a moby with a behaviour just does not run its own update.

## Build

```sh
python3 tools/fetch_sdk.py          # the SDK sdk.lock pins, into .sdk/
cmake -S . -B build -G Ninja        # or -DOPENRAC_SDK=<an OpenRAC checkout>/sdk
cmake --build build
python3 .sdk/openrac-mod-sdk-*/tools/package_mod.py build/package/behaviour_presets
```

## Test

The presets' sums and the JSON reader are in `src/presets.hpp` and `src/json.hpp`, apart from the
port, with their tests:

```sh
cmake -S tests -B build/tests -G Ninja && cmake --build build/tests && ctest --test-dir build/tests
```

## Try it

`examples/` is a pack for Rilgar (level 5): the vendor by the landing spot spins, a taxi bobs.

```sh
openrac-rac1-ntsc --mods examples --plugins <folder>/plugins.json --level 5
```

with `plugins.json` as `{"format": 1, "mods": [{"id": "behaviour-presets", "enabled": true}]}` and
the packaged mod in `<folder>/behaviour-presets`.
