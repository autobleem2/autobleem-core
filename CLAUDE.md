# AutoBleem-core - developer context (short map)

`autobleem-core` is the shared library the launcher (`autobleem2/autobleem`) and the tool repos build against,
checked out as the `autobleem-core/` submodule there. This file is the **short map**; the full reference is
**`docs/developer-guide.md`** - read only the section you need (headings below). The launcher's `CLAUDE.md` has the
build, runtime and project picture.

## What is here

- **`lib_ableem`** (namespace `ableem`): `ableem_engine` (`include/ableem/engine/` - no SDL: filesystem, strings,
  ini/cfg, the SQLite `GameDatabase`, covers, disc images, the scanner, RetroArch playlists/cores/rdb, zip, md5,
  crc32, `Lang`, `ThemeSpec`) and `ableem` (`include/ableem/ui/` - owns every SDL call: Platform, Renderer,
  Texture, Font, Audio, Input, Joystick, GuiBase, GuiScreen, the DebugDriver).
- **`ab_gui`** (`ab_gui/`, namespace `abgui`, `AB_CORE_UI`): the User Interface Library - the look as data (`Style`,
  `Context`), panels, the screen stack and transitions, tweens, actions and the widgets (list, keyboard, confirm, ...). Links
  `ableem` only, **no SDL**. Plan: `docs/ab-gui-plan.md`; reference: developer guide "ab_gui".
- **`ab_core`** (`src/code/core/`): the launcher's SDL-free model + services (`model/`, `services/`: Env,
  PlatformConfig, Config, Theme, ScanService, LaunchService, GameSettingsService, MemcardService, ...).
- **`ab_classic`** (`src/code/app_base.*`, `src/code/gui/`): Gui, ThemeAssets, TextRenderer, PanelStyle, Fonts,
  AppAudio, the splash/confirm/keyboard/about/hardware-info screens and the list-menu framework (`gui/menus/`).
  Nothing in `ab_classic` includes `app.h`.

## Rules that bite

- **Never `#include <SDL2/...>` in `src/code/`**, never sqlite/json directly - extend the library
  (`GameDatabase`, `RetroArchPlaylist`, ...).
- The engine names reach the app through explicit `using`s in `src/code/core/main.h` (not `using namespace`).
- Paths come from `ableem::Environment` / `Env` - no `/media`, `/usr/sony` literals outside `EnvironmentSetup`;
  per-platform paths are data in `resources/platform/<platform>.ini`, never `#ifdef` in the services.
- **UI look**: every screen but the carousel frame uses `PanelStyle` (full vs compact panel, rows, right-aligned
  values, structured footers sorted Cross/Circle/Triangle/Square/Start/Select/L1-R1/L2-R2, "Back" vs "Cancel",
  sentence case, L2/R2 page and L1/R1 first/last); colours from the theme, fonts from `ThemeAssets` - never a
  hard-coded colour or ttf path. Full standard: developer guide "UI styling standards".
- **Every on-screen string is `_()`** and lands in all 16 language files in the same commit.
- Every service extracted from a screen ships with its tests in the same commit.

## Tests

doctest suites under `tests/`, run by ctest (`ci/build.sh native` or the launcher's `make_win.sh`). Any test
touching a path uses `tests/support/env_fixture.h`; scratch trees via `tests/support/temp_dir.h`; add a suite with
`ab_add_test(<name> core/<file>.cpp)`.

## The developer guide's sections (`docs/developer-guide.md`)

lib_ableem (engine: Environment, DirEntry, Lang, MemcardImage, IniFile/ConfigFileEditor/MemcardManager,
GameDatabase, MetadataLookup, ThumbnailLookup, DiscSuffix, the scanner, SerialScanner, RetroArchPlaylist,
CoreInfoTable, RetroArchScanner, ThemeSpec, ZipArchive/ZipWriter, Md5, Crc32; ui: Platform, output scale, MSAA,
Renderer, Texture, Font, Sound/Music/Audio, Joystick, Input, GuiBase/GuiScreen, CMake) - ab_gui (Style, Context,
Panel, ScreenStack, transitions, Tween, Action/ActionMap, Screen, the widgets, frames, icons, spinner, logo/mask) -
UI styling standards -
Source map: the core-owned files - Tests.
