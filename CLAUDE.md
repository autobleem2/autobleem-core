# AutoBleem-core - developer context (short map)

`autobleem-core` is the AutoBleem C++ SDK: the portable `lib_ableem` plus the launcher's SDL-free model and services
and the classic-UI framework. The launcher (`autobleem2/autobleem`), the extensions (plugins the launcher loads) and
the tool repos build against it as the `autobleem-core/` git submodule. This file is the short map; the full
reference is `docs/developer-guide.md` - read only the section you need. The launcher's `CLAUDE.md` and docs hold
the runtime, the resources and the project picture (see the last section).

## Layout

- `lib_ableem/` (namespace `ableem`): `ableem_engine` (`include/ableem/engine/`, no SDL: filesystem, strings, ini,
  `GameDatabase`, scanner, RetroArch playlists/cores, zip, md5, `Lang`, `ThemeSpec`, `StoreCatalog`, `log.h`);
  `ableem` (`include/ableem/ui/`, owns every SDL call: Platform, Renderer, Texture, Font, Audio, Input, GuiBase,
  GuiScreen, DebugDriver); `ableem_lanserver` (`include/ableem/lanserver/`); vendored code in `third_party/`
  (plog, sqlite, miniz, nlohmann, stb, libchdr, lzma-7z).
- `src/code/core/` = **`ab_core`**: `model/` and `services/` (Env, PlatformConfig, Config, Theme, ScanService,
  LaunchService, PackageService, UpdateService, ...), SDL-free. `main.h` carries the explicit `using ableem::...`
  lines (no `using namespace`); `version.h.in` becomes `core/version.h`.
- `src/code/app_base.*`, `src/code/gui/` = **`ab_classic`**: AppBase, Gui, ThemeAssets, TextRenderer, PanelStyle,
  the extension host/runtime (`gui/extension.h`), basic screens (`gui/screens/`), the list-menu base (`gui/menus/`).
- `src/code/installer/` = **`ab_installer`**: stick installer and flasher jobs, SDL-free, over `ab_core`.
- `ab_gui/` (`AB_CORE_UI`, namespace `abgui`): the UI library - `Style`, `Context`, panels, `ScreenStack`, widgets.
  Links `ableem`, no ab_core.
- `cmake/`: `ab_extension.cmake` (`ab_add_extension()`, how a plugin is built) and `generate_version.cmake`.
- `tests/`: doctest suites - `core/`, `classic/`, `gui/`, `installer/`; `support/` (fixtures), `data/` (checked-in
  test files), `third_party/doctest`, `harness.cmake`.
- `docs/`: `developer-guide.md`, `ab-gui-plan.md`, `ab-gui-frames-spec.md`, `ab-gui-evoui-art-spec.md`.
- There is no `src/resources/` here (see "Strings"). Targets are listed in the root `CMakeLists.txt`.

## Build and test

CI (`.github/workflows/build.yml`) runs in the `ghcr.io/autobleem2/autobleem-build:latest` image
(Ubuntu 24.04), gated by the repo variable `AB_CI_ENABLED`:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure -j4
```

- Options: `AB_CORE_UI=OFF` builds the SDL-free half only (ableem_engine + ab_core + ab_installer, no SDL needed);
  `AB_ENABLE_CHD`, `AB_ONLINE_UPDATE`, `AB_BUILD_TESTS` (all ON). C++14.
- Tests are host-only. The ARM toolchain file of the consumer forces `AB_BUILD_TESTS` off (`tests/CMakeLists.txt`).
- Add a suite: `ab_add_test(<name> core/<file>.cpp [LIBS ...])` in `tests/CMakeLists.txt`. Tests touching paths use
  `tests/support/env_fixture.h`; scratch trees use `tests/support/temp_dir.h`. Consumer repos reuse `harness.cmake`.
- The case that needs the launcher's `src/resources` skips itself when the folder is missing (as in CI).
- A new service ships with its test in the same commit.

## SDK ABI guard

Extensions are plugins compiled against these headers and loaded by the launcher; `AB_SDK_ABI` (`src/code/gui/extension.h`,
currently 11) is checked at load, so an extension built for another number is refused. `test_sdk_layout`
(`tests/classic/test_sdk_layout.cpp`, table in `tests/classic/sdk_layout_table.h`) compares `sizeof`/`offsetof` of
what plugins reach through inline code - `AppBase` (and its `cfg_`, `lang_`, `theme_`, `clock_`, `gui_`, `audio_`),
`Theme`, `ThemeSpec`, `Gui`, `Config`, `Lang`, `ThemeAssets`, `StoreItem`, `Event`, `DownloadRequest`, ... A growing `ThemeSpec`
or `Theme` moves `clock_`, `gui_` and `audio_` in `AppBase`, and a plugin built earlier crashes (BUG-55).
When it fails: bump `AB_SDK_ABI`, add a history line in `extension.h`, then copy the printed numbers into the
table. Never edit the table alone. The test only runs on x86_64 Linux with libstdc++'s C++11 ABI.
Append new members last. A changed virtual or function signature an extension calls also needs a bump.

## Logging

plog, vendored and wrapped by `ableem/engine/log.h`: `Log::initConsoleOnly()` first thing in `main()`, then
`Log::addFile()` once the logs folder is known. Use `PLOG_DEBUG/INFO/WARNING/ERROR` (format
`HH:MM:SS LEVEL [function:line] message`, no date). A `PLOG_*` before init is dropped. Default level is `debug`, `info` under
`NDEBUG` (`ABLEEM_LOG_LEVEL`). Extensions log through plog instance 1 (`PLOG_DEFAULT_INSTANCE_ID=1`, set by
`ab_add_extension`). `log.h` states it is the only logging; do not add `cout`.

## Platforms

`AB_TARGET` (cache variable): `psc` (PlayStation Classic), `rpi`, `pcusb`, `win`, `dev` (host). Empty means `psc`
on ARM/AArch64 and `dev` elsewhere. It defines `AB_PLATFORM_<NAME>`. `rpi64` is not an `AB_TARGET`: it is `rpi` built
for aarch64 (`Env::buildTargetKey()`, `ab_extension_platform_key()`), the key under which apps and extensions ship
`bin/<key>/` and the update package is named. `Env::platformName()` gives `psc|rpi|pcusb|win|pc`.
Keep `#ifdef AB_PLATFORM_*` out of services where possible: per-platform paths are data in
`<resources>/platform/<platform>.ini` (`PlatformConfig`); only `EnvironmentSetup` spells `/media` or `/usr/sony`.

## Rules that bite

- No `#include <SDL2/...>` in `src/code/` or `ab_gui/` (none exists today); SDL stays in `lib_ableem/ui`.
  Extend `GameDatabase`, `RetroArchPlaylist`, ... rather than using sqlite/json directly.
- Paths come from `ableem::Environment` / `Env`.
- UI look: `PanelStyle` and theme colours/fonts via `ThemeAssets`; standard in the developer guide "UI styling standards".

## Strings

Every on-screen string is `_("English text")` (`ableem::translate()` through `main.h`). The translation files
(`resources/lang/<Language>.txt`) and `tools/lang_tools.py` are in the launcher, not here; the rule there is that a new or
changed string lands in all language files in the same commit. A core change that adds a `_()` string needs
that launcher-side commit. Tests in `tests/core/test_lang.cpp` use their own fixture files.

## What lives in the launcher (autobleem2/autobleem)

Its `CLAUDE.md` and docs: the application and screens, `src/resources` (lang, platform ini, themes), the
`make_win.sh` and image builds, the release flow, `docs/extensions-plan.md`, `docs/packages.md`, and this repository
as a submodule pointer (bump it after merging here). The full ab_core/lib_ableem reference stays in `docs/developer-guide.md`.
