# Third party libraries

Please keep categories (`##` level) listed alphabetically and matching their
respective folder names. Use two empty lines to separate categories for
readability.


## amd-fsr

- Upstream: https://github.com/GPUOpen-Effects/FidelityFX-FSR
- Version: 1.0.2 (a21ffb8f6c13233ba336352bdff293894c706575, 2021)
- License: MIT

Files extracted from upstream source:

- `ffx_a.h` and `ffx_fsr1.h` from `ffx-fsr`
- `license.txt`


## amd-fsr2

- Upstream: https://github.com/GPUOpen-Effects/FidelityFX-FSR2
- Version: 2.2.1 (1680d1edd5c034f88ebbbb793d8b88f8842cf804, 2023)
- License: MIT

Files extracted from upstream source:

- `ffx_*.cpp` and `ffx_*.h` from `src/ffx-fsr2-api`
- `shaders` folder from `src/ffx-fsr2-api` with `ffx_*.hlsl` files excluded
- `LICENSE.txt`

Patches:

- `0001-build-fixes.patch` ([GH-81197](https://github.com/godotengine/godot/pull/81197))
- `0002-godot-fsr2-options.patch` ([GH-81197](https://github.com/godotengine/godot/pull/81197))


## brotli

- Upstream: https://github.com/google/brotli
- Version: 1.2.0 (028fb5a23661f123017c060daa546b55cf4bde29, 2025)
- License: MIT

Files extracted from upstream source:

- `common/`, `dec/` and `include/` folders from `c/`,
  minus the `dictionary.bin*` files
- `LICENSE`


## clipper2

- Upstream: https://github.com/AngusJohnson/Clipper2
- Version: 1.5.4 (ef88ee97c0e759792e43a2b2d8072def6c9244e8, 2025)
- License: BSL 1.0

Files extracted from upstream source:

- `CPP/Clipper2Lib/` folder (in root)
- `LICENSE`

Patches:

- `0001-disable-exceptions.patch` ([GH-80796](https://github.com/godotengine/godot/pull/80796))
- `0002-llvm-21-header.patch` ([GH-113850](https://github.com/godotengine/godot/pull/113850))


## fonts

- `DroidSans*.woff2`:
  * Upstream: https://android.googlesource.com/platform/frameworks/base/+/master/data/fonts/
  * Version: ? (pre-2014 commit when DroidSansJapanese.ttf was obsoleted)
  * License: Apache 2.0
- `Inter*.woff2`:
  * Upstream: https://github.com/rsms/inter
  * Version: v4.1 (e3a3d4c57d5ecc01453a575621882a384c1995a3, 2024)
  * License: OFL-1.1
- `JetBrainsMono_Regular.woff2`:
  * Upstream: https://github.com/JetBrains/JetBrainsMono
  * Version: 2.304 (cd5227bd1f61dff3bbd6c814ceaf7ffd95e947d9, 2023)
  * License: OFL-1.1
- `NotoSansBengali*.woff2`:
  * Upstream: https://github.com/notofonts/bengali
  * Version: 3.011 (85d80394cbbbb798ca0a41c983902e6cf77be3a3, 2026)
  * License: OFL-1.1
- `NotoSansDevanagari*.woff2`:
  * Upstream: https://github.com/notofonts/devanagari
  * Version: 2.006 (bb8d2566a1708ef2dcc6396ee2eb261a18967f76, 2024)
  * License: OFL-1.1
- `NotoSansGeorgian*.woff2`:
  * Upstream: https://github.com/notofonts/georgian
  * Version: 2.005 (c02e5483c2dd63c5cf223845010ebd6e6dc56aec, 2024)
  * License: OFL-1.1
- `NotoSansHebrew*.woff2`:
  * Upstream: https://github.com/notofonts/hebrew
  * Version: 3.001 (caa7ab0614fb5b37cc003d9bf3d7d3e765331110, 2024)
  * License: OFL-1.1
- `NotoSansMalayalam*.woff2`:
  * Upstream: https://github.com/notofonts/malayalam
  * Version: 2.104 (0fd65e553a6af3dc1c09ed39dfe8933e01c17b32, 2023)
  * License: OFL-1.1
- `NotoSansOriya*.woff2`:
  * Upstream: https://github.com/notofonts/oriya
  * Version: 2.006 (97abab82ec512f8a4a98c389352f194a03385ce2, 2024)
  * License: OFL-1.1
- `NotoSansSinhala*.woff2`:
  * Upstream: https://github.com/notofonts/sinhala
  * Version: 3.000 (032355e96de5bac83fd996535af3d13b1fbfeccf, 2025)
  * License: OFL-1.1
- `NotoSansTamil*.woff2`:
  * Upstream: https://github.com/notofonts/tamil
  * Version: 2.004 (f34a08d1ae3fa810581f63410296d971bdcd62dc, 2023)
  * License: OFL-1.1
- `NotoSansTelugu*.woff2`:
  * Upstream: https://github.com/notofonts/telugu
  * Version: 2.005 (e97c3409a8347d68cccd06a82a68b418c315ee0c, 2023)
  * License: OFL-1.1
- `NotoSansThai*.woff2`:
  * Upstream: https://github.com/notofonts/thai
  * Version: 2.002 (f8b482c158650260bba5d5edba9da3e8bb7185b4, 2023)
  * License: OFL-1.1
- `OpenSans_SemiBold.woff2`:
  * Upstream: https://github.com/googlefonts/opensans
  * Version: git (bd7e37632246368c60fdcbd374dbf9bad11969b6, 2023)
  * License: OFL-1.1
- `Vazirmatn*.woff2`:
  * Upstream: https://github.com/rastikerdar/vazirmatn
  * Version: 33.003 (83629f877e8f084cc07b47030b5d3a0ff06c76ec, 2022)
  * License: OFL-1.1

All fonts are converted from the unhinted `.ttf` sources using the
`https://github.com/google/woff2` tool.

Use UI font variant if available, because it has tight vertical metrics and good
for UI.


## freetype

- Upstream: https://gitlab.freedesktop.org/freetype/freetype
- Version: 2.14.3 (0a0221a1347e2f1e07c395263540026e9a0aa7c7, 2026)
- License: FreeType License (BSD-like)

Files extracted from upstream source:

- `src/` folder, minus the `dlg` and `tools` subfolders
  * These files can be removed: `.dat`, `.diff`, `.mk`, `.rc`, `README*`
  * In `src/gzip/`, keep only `ftgzip.c`
- `include/` folder, minus the `dlg` subfolder
- `LICENSE.TXT` and `docs/FTL.TXT`


## gamepadmotionhelpers

- Upstream: https://github.com/JibbSmart/GamepadMotionHelpers
- Version: 39b578aacf34c3a1c584d8f7f194adc776f88055, 2023
- License: MIT

Files extracted from upstream source:

- `GamepadMotion.hpp`
- `LICENSE.TXT`

Patches:

- `0001-fix-warnings.patch` ([GH-111679](https://github.com/godotengine/godot/pull/111679))

## glslang

- Upstream: https://github.com/KhronosGroup/glslang
- Version: vulkan-sdk-1.4.335.0 (b5782e52ee2f7b3e40bb9c80d15b47016e008bc9, 2025)
- License: glslang

Version should be kept in sync with the one of the used Vulkan SDK (see `vulkan`
section).

Files extracted from upstream source:

- `glslang/` folder (except the `glslang/HLSL` and `glslang/ExtensionHeaders`
  subfolders), `SPIRV/` folder
  * Remove C interface code: `CInterface/` folders, files matching `"*_c[_\.]*"`
  * Remove `glslang/stub.cpp`
  * Remove `SPIRV/spirv.hpp11` (should use copy from `thirdparty/spirv-headers`)
- Run `cmake . && make` and copy generated `include/glslang/build_info.h`
  to `glslang/build_info.h`
- `LICENSE.txt`
- Unnecessary files like `CMakeLists.txt` or `updateGrammar` removed

Patches:

- `0001-apple-disable-absolute-paths.patch` ([GH-92010](https://github.com/godotengine/godot/pull/92010))
- `0002-apple-m1-msaa-fix.patch` ([GH-115893](https://github.com/godotengine/godot/issues/115893))


## grisu2

- Upstream: https://github.com/simdjson/simdjson/blob/master/src/to_chars.cpp
- Version: git (667d0ed3c77f55cbda2082b034168d69898d1f88, 2025)
- License: Apache and MIT

Files extracted from upstream source:

- The `src/to_chars.cpp` file renamed to `grisu2.h` and slightly modified.

Patches:

- `0001-godot-changes.patch` ([GH-98750](https://github.com/godotengine/godot/pull/98750))


## libbacktrace

- Upstream: https://github.com/ianlancetaylor/libbacktrace
- Version: git (4d2dd0b172f2c9192f83ba93425f868f2a13c553, 2022)
- License: BSD-3-Clause

Files extracted from upstream source:

- `*.{c,h}` files for Windows platform, i.e. remove the following:
  * `allocfail.c`, `instrumented_alloc.c`, `*test*.{c,h}`
  * `elf.c`, `macho.c`, `mmap.c`, `mmapio.c`, `nounwind.c`, `unknown.c`, `xcoff.c`
- `LICENSE`

Patches:

- `0001-big-files-support.patch` ([GH-100281](https://github.com/godotengine/godot/pull/100281))


## libpng

- Upstream: http://libpng.org/pub/png/libpng.html
- Version: 1.6.58 (3061454d980de7d53608f594194cfac722721d2a, 2026)
- License: libpng/zlib

Files extracted from upstream source:

- All `.c` and `.h` files of the main directory, apart from `example.c` and `pngtest.c`
- `arm/`, `intel/`, `loongarch/`, and `powerpc/` folders, except `arm/filter_neon.S` and `.editorconfig` files
- `scripts/pnglibconf.h.prebuilt` as `pnglibconf.h`
- `LICENSE`


## linuxbsd_headers

See `linuxbsd_headers/README.md`.


## mbedtls

- Upstream: https://github.com/Mbed-TLS/mbedtls
- Version: 3.6.7 (068ff080b369adfac81509f9b57b2afabaf82dc5, 2026)
- License: Apache 2.0

File extracted from upstream release tarball:

- All `.h` from `include/mbedtls/` to `thirdparty/mbedtls/include/mbedtls/`
  and all `.h` from `include/psa/` to `thirdparty/mbedtls/include/psa/`
- From `library/` to `thirdparty/mbedtls/library/`:
  - All `.c` and `.h` files
  - Except `bignum_mod.c`, `block_cipher.c`, `ecp_curves_new.c`, `lmots.c`,
    `lms.c`
- The `LICENSE` file (edited to keep only the Apache 2.0 variant)
- Added 2 files `godot_core_mbedtls_platform.c` and `godot_core_mbedtls_config.h`
  providing configuration for light bundling with core
- Added 2 files `godot_module_mbedtls_config.h` and `threading_alt.h`
  to customize the build configuration when bundling the full library

Patches:

- `0001-msvc-2019-psa-redeclaration.patch` ([GH-90535](https://github.com/godotengine/godot/pull/90535))


## metal-cpp

- Upstream: https://developer.apple.com/metal/cpp/
- Version: 26.0 (2025)
- License: Apache 2.0

Update instructions:

- Download latest metal-cpp ZIP from https://developer.apple.com/metal/cpp/:
- Run `update-metal-cpp.sh <path to the downloaded zip>` to extract the relevant files and apply patches.


## meshoptimizer

- Upstream: https://github.com/zeux/meshoptimizer
- Version: 1.1.1 (b22872835dbabc56a6e4a366ea9917f62b7daf1a, 2026)
- License: MIT

Files extracted from upstream repository:

- All files in `src/`
- `LICENSE.md`


## mingw-std-threads

- Upstream: https://github.com/meganz/mingw-std-threads
- Version: git (c931bac289dd431f1dd30fc4a5d1a7be36668073, 2023)
- License: BSD-2-clause

Files extracted from upstream repository:

- `LICENSE`
- `mingw.condition_variable.h`
- `mingw.invoke.h`
- `mingw.mutex.h`
- `mingw.shared_mutex.h`
- `mingw.thread.h`

Patches:

- `0001-disable-exceptions.patch` ([GH-85039](https://github.com/godotengine/godot/pull/85039))
- `0002-clang-std-replacements-leak.patch` ([GH-85208](https://github.com/godotengine/godot/pull/85208))
- `0003-explicit-includes.patch` ([GH-111557](https://github.com/godotengine/godot/pull/111557))


## minizip

- Upstream: https://github.com/madler/zlib
- Version: 1.3.2 (da607da739fa6047df13e66a2af6b8bec7c2a498, 2026)
- License: zlib

Files extracted from the upstream source:

- From `contrib/minizip`:
  `{crypt.h,ints.h,ioapi.{c,h},skipset.h,unzip.{c,h},zip.{c,h}}`
  `MiniZip64_info.txt`

Patches:

- `0001-godot-seek.patch` ([GH-10428](https://github.com/godotengine/godot/pull/10428))


## misc

Collection of single-file libraries used in Godot components.

- `bcdec.h`
  * Upstream: https://github.com/iOrange/bcdec
  * Version: git (3b29f8f44466c7d59852670f82f53905cf627d48, 2024)
  * License: MIT
- `cubemap_coeffs.h`
  * Upstream: https://research.activision.com/publications/archives/fast-filtering-of-reflection-probes
    File coeffs_const_8.txt (retrieved April 2020)
  * License: MIT
- `fastlz.{c,h}`
  * Upstream: https://github.com/ariya/FastLZ
  * Version: 0.5.0 (4f20f54d46f5a6dd4fae4def134933369b7602d2, 2020)
  * License: MIT
- `FastNoiseLite.h`
  * Upstream: https://github.com/Auburn/FastNoiseLite
  * Version: 1.1.0 (f7af54b56518aa659e1cf9fb103c0b6e36a833d9, 2023)
  * License: MIT
  * Patches:
    - `FastNoiseLite-0001-namespace-warnings.patch` ([GH-88526](https://github.com/godotengine/godot/pull/88526))
- `ifaddrs-android.{cc,h}`
  * Upstream: https://chromium.googlesource.com/external/webrtc/stable/talk/+/master/base/ifaddrs-android.h
  * Version: git (5976650443d68ccfadf1dea24999ee459dd2819d, 2013)
  * License: BSD-3-Clause
  * Patches:
    - `ifaddrs-android-0001-complete-struct.patch` ([GH-34101](https://github.com/godotengine/godot/pull/34101))
- `mikktspace.{c,h}`
  * Upstream: https://archive.blender.org/wiki/index.php/Dev:Shading/Tangent_Space_Normal_Maps/
  * Version: 1.0 (2011)
  * License: zlib
- `nvapi_minimal.h`
  * Upstream: http://download.nvidia.com/XFree86/nvapi-open-source-sdk
  * Version: R525
  * License: MIT
  * Modifications: Created from upstream `nvapi.h` by removing unnecessary code.
- `ok_color.h`
  * Upstream: https://github.com/bottosson/bottosson.github.io/blob/master/misc/ok_color.h
  * Version: git (d69831edb90ffdcd08b7e64da3c5405acd48ad2c, 2022)
  * License: MIT
  * Modifications: License included in header.
- `ok_color_shader.h`
  * https://www.shadertoy.com/view/7sK3D1
  * Version: 2021-09-13
  * License: MIT
- `pcg.{cpp,h}`
  * Upstream: http://www.pcg-random.org
  * Version: minimal C implementation, http://www.pcg-random.org/download.html
  * License: Apache 2.0
- `polypartition.{cpp,h}`
  * Upstream: https://github.com/ivanfratric/polypartition (`src/polypartition.{cpp,h}`)
  * Version: git (7bdffb428b2b19ad1c43aa44c714dcc104177e84, 2021)
  * License: MIT
  * Patches:
    - `polypartition-0001-godot-types.patch` (2185c018f)
    - `polypartition-0002-shadow-warning.patch` ([GH-66808](https://github.com/godotengine/godot/pull/66808))
- `qoa.{c,h}`
  * Upstream: https://github.com/phoboslab/qoa
  * Version: git (ae07b57deb98127a5b40916cb57775823d7437d2, 2025)
  * License: MIT
  * Modifications: Added implementation through `qoa.c`.
- `r128.{c,h}`
  * Upstream: https://github.com/fahickman/r128
  * Version: git (6fc177671c47640d5bb69af10cf4ee91050015a1, 2023)
  * License: Public Domain or Unlicense
- `smaz.{c,h}`
  * Upstream: https://github.com/antirez/smaz
  * Version: git (2f625846a775501fb69456567409a8b12f10ea25, 2012)
  * License: BSD-3-Clause
  * Modifications: License included in header.
  * Patches:
    - `smaz-0001-write-string-warning.patch` ([GH-8572](https://github.com/godotengine/godot/pull/8572))
- `smolv.{cpp,h}`
  * Upstream: https://github.com/aras-p/smol-v
  * Version: git (9dd54c379ac29fa148cb1b829bb939ba7381d8f4, 2024)
  * License: Public Domain or MIT
- `stb_rect_pack.h`
  * Upstream: https://github.com/nothings/stb
  * Version: 1.01 (af1a5bc352164740c1cc1354942b1c6b72eacb8a, 2021)
  * License: Public Domain or Unlicense or MIT
- `yuv2rgb.h`
  * Upstream: http://wss.co.uk/pinknoise/yuv2rgb/ (to check)
  * Version: ?
  * License: BSD


## pcre2

- Upstream: http://www.pcre.org
- Version: 10.47 (f454e231fe5006dd7ff8f4693fd2b8eb94333429, 2025)
- License: BSD-3-Clause

Files extracted from upstream source:

- Files listed in the file `NON-AUTOTOOLS-BUILD` steps 1-4
- All `.h` files in `src/` apart from `pcre2posix.h`, `pcre2_printint_inc.h`, `pcre2test_inc.h`
- `src/pcre2_compile_cgroup.c`
- `src/pcre2_match_next.c`
- `src/pcre2_{jit_char,jit_match,jit_misc,jit_simd,ucptables}_inc.h`
- `deps/sljit/sljit_src`
- `AUTHORS.md` and `LICENCE.md`


## re-spirv

- Upstream: https://github.com/renderbag/re-spirv
- Version: git (29a77fca357567d00aa37b8ffde19c19cfe477c4, 2026)
- License: MIT

Files extracted from upstream source:

- `re-spirv.cpp`
- `re-spirv.h`
- `LICENSE`


## smaa

- Upstream: https://github.com/iryoku/smaa
- Version: git (71c806a838bdd7d517df19192a20f0c61b3ca29d, 2013)
- License: MIT

Files extracted from upstream source:

- `LICENSE`
- Textures generated using the Python scripts in the `Scripts` folder


## spirv-cross

- Upstream: https://github.com/KhronosGroup/SPIRV-Cross
- Version: git (fb0c1a307cca4b4a9d891837bf4c44d17fe2d324, 2025)
- License: Apache 2.0

Files extracted from upstream source:

- All `.cpp`, `.hpp` and `.h` files, minus `main.cpp`, `spirv.h*`, `spirv_cross_c.*`, `spirv_hlsl.*`, `spirv_cpp.*`
- `include/` folder
- `LICENSE` and `LICENSES/` folder, minus `CC-BY-4.0.txt`

Versions of this SDK do not have to match the `vulkan` section, as this SDK is required
to generate Metal source from Vulkan SPIR-V.


## spirv-headers

- Upstream: https://github.com/KhronosGroup/SPIRV-Headers
- Version: vulkan-sdk-1.4.335.0 (b824a462d4256d720bebb40e78b9eb8f78bbb305, 2025)
- License: MIT

Files extracted from upstream source:

- `include/spirv/unified1/spirv.{h,hpp,hpp11}` with the same folder structure
- `LICENSE` (edited to keep only relevant license)


## spirv-reflect

- Upstream: https://github.com/KhronosGroup/SPIRV-Reflect
- Version: vulkan-sdk-1.4.335.0 (ef913b3ab3da1becca3cf46b15a10667c67bebe5, 2025)
- License: Apache 2.0

Version should be kept in sync with the one of the used Vulkan SDK (see `vulkan`
section).

Files extracted from upstream source:

- `spirv_reflect.h`, `spirv_reflect.c`
- `LICENSE`

Patches:

- `0001-zero-size-for-sc-sized-arrays.patch` ([GH-94985](https://github.com/godotengine/godot/pull/94985))
- `0002-spirv-headers.patch` ([GH-111452](https://github.com/godotengine/godot/pull/111452))


## thorvg

- Upstream: https://github.com/thorvg/thorvg
- Version: 1.0.3 (d114cd9e3c32d7f77bc9b324ae5c71d7775cb7ae, 2026)
- License: MIT

Files extracted from upstream source:

- See `thorvg/update-thorvg.sh` for extraction instructions.
  Set the version number and run the script.

Patches:

- `0001-let-delete-be-delete.patch` ([GH-116024](https://github.com/godotengine/godot/pull/116024))


## volk

- Upstream: https://github.com/zeux/volk
- Version: vulkan-sdk-1.4.335.0 (4f3bcee79618a9abe79f4c717c50379197c77512, 2025)
- License: MIT

Version should be kept in sync with the one of the used Vulkan SDK (see `vulkan`
section).

Files extracted from upstream source:

- `volk.h`, `volk.c`
- `LICENSE.md`


## vulkan

- Upstream: https://github.com/KhronosGroup/Vulkan-Headers
- Version: vulkan-sdk-1.4.335.0 (2fa203425eb4af9dfc6b03f97ef72b0b5bcb8350, 2025)
- License: Apache 2.0

Unless there is a specific reason to package a more recent version, please stick
to tagged SDK releases. All Vulkan libraries and headers should be kept in sync so:

- Update Vulkan SDK components to the matching tag (see "vulkan")
- Update volk (see "volk")
- Update glslang (see "glslang")
- Update spirv-headers (see "spriv-headers")
- Update spirv-cross (see "spirv-cross")
- Update spirv-reflect (see "spirv-reflect")

Files extracted from upstream source:

- `include/`
- `LICENSE.md`

`vk_enum_string_helper.h` is taken from the matching `Vulkan-Utility-Libraries`
SDK release: https://github.com/KhronosGroup/Vulkan-Utility-Libraries/blob/main/include/vulkan/vk_enum_string_helper.h

`vk_mem_alloc.h` is taken from https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
Version: 3.3.0 (1d8f600fd424278486eade7ed3e877c99f0846b1, 2025)
`vk_mem_alloc.cpp` is a Godot file and should be preserved on updates.

Patches:

- `0001-VKEnumStringHelper-godot-vulkan.patch` ([GH-97510](https://github.com/godotengine/godot/pull/97510))
- `0002-VMA-godot-vulkan.patch` ([GH-97510](https://github.com/godotengine/godot/pull/97510))
- `0003-VMA-add-vmaCalculateLazilyAllocatedBytes.patch` ([GH-99257](https://github.com/godotengine/godot/pull/99257))


## wayland

- Upstream: https://gitlab.freedesktop.org/wayland/wayland
- Version: 1.24.0 (736d12ac67c20c60dc406dc49bb06be878501f86, 2025)
- License: MIT

Files extracted from upstream source:

- `protocol/wayland.xml`
- `COPYING`


# wayland-protocols

- Upstream: https://gitlab.freedesktop.org/wayland/wayland-protocols
- Version: 1.47 (88223018d1b578d0d8869866da66d9608e05f928, 2025)
- License: MIT

Files extracted from upstream source:

- `stable/tablet/README`
- `stable/tablet/tablet-unstable-v2.xml`
- `stable/viewporter/README`
- `stable/viewporter/viewporter.xml`
- `stable/xdg-shell/README`
- `stable/xdg-shell/xdg-shell.xml`
- `staging/color-management/README.md`
- `staging/color-management/color-management-v1.xml`
- `staging/fractional-scale/README`
- `staging/fractional-scale/fractional-scale-v1.xml`
- `staging/xdg-activation/README`
- `staging/xdg-activation/xdg-activation-v1.xml`
- `staging/xdg-system-bell/xdg-system-bell-v1.xml`
- `staging/pointer-warp/pointer-warp-v1.xml`
- `staging/pointer-warp/README`
- `unstable/idle-inhibit/README`
- `unstable/idle-inhibit/idle-inhibit-unstable-v1.xml`
- `unstable/pointer-constraints/README`
- `unstable/pointer-constraints/pointer-constraints-unstable-v1.xml`
- `unstable/pointer-gestures/README`
- `unstable/pointer-gestures/pointer-gestures-unstable-v1.xml`
- `unstable/primary-selection/README`
- `unstable/primary-selection/primary-selection-unstable-v1.xml`
- `unstable/relative-pointer/README`
- `unstable/relative-pointer/relative-pointer-unstable-v1.xml`
- `unstable/text-input/README`
- `unstable/text-input/text-input-unstable-v3.xml`
- `unstable/xdg-decoration/README`
- `unstable/xdg-decoration/xdg-decoration-unstable-v1.xml`
- `unstable/xdg-foreign/README`
- `unstable/xdg-foreign/xdg-foreign-unstable-v1.xml`
- `COPYING`

The following files are extracted from thirdparty sources:

- `mesa/wayland-drm.xml`: https://gitlab.freedesktop.org/mesa/mesa/-/blob/mesa-25.3.0/src/egl/wayland/wayland-drm/wayland-drm.xml


## zlib

- Upstream: https://github.com/madler/zlib
- Version: 1.3.2 (da607da739fa6047df13e66a2af6b8bec7c2a498, 2026)
- License: zlib

Files extracted from upstream source:

- All `.c` and `.h` files, except `gz*.c` and `infback.c`
- `LICENSE`


## zstd

- Upstream: https://github.com/facebook/zstd
- Version: 1.5.7 (f8745da6ff1ad1e7bab384bd1f9d742439278e99, 2025)
- License: BSD-3-Clause

Files extracted from upstream source:

- `lib/{common/,compress/,decompress/,zstd.h,zstd_errors.h}`
- `LICENSE`
