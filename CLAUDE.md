# AutoBleem-core — developer context

`autobleem-core` (`github.com/autobleem2/autobleem-core`) is the shared library the AutoBleem launcher and
the console tools build against: `lib_ableem` (the SDL-free `ableem_engine` and the SDL-owning `ableem` ui
library), `ab_core` (the launcher's own SDL-free model+services layer) and `ab_classic` (the game-agnostic
classic-UI screens and controls). It is checked out as a git submodule at `autobleem-core/` in the launcher
repo (`autobleem2/autobleem`) and in the console-tools/PC-tools repos; a source there is included as
`"core/..."`, `"gui/..."` the same way the launcher includes its own `ab_ui`/`ab_evoui` sources. This file
is what a developer needs when working inside `autobleem-core/` itself, or when tracing a class the
launcher's own `CLAUDE.md` points here for. The launcher's `CLAUDE.md` (`autobleem2/autobleem`) has the
project-wide picture, the build scripts, the runtime layout and the launcher's own (ab_ui/ab_evoui/
executable) source map; this file does not repeat that.

**Never add a `#include <SDL2/...>` to anything under `src/code/`** in the launcher; if new SDL
functionality is needed, add it to this library instead. Same for sqlite/json: extend
`GameDatabase`/`RetroArchPlaylist` instead of reaching for the vendored headers directly.

## lib_ableem

A portable library (`lib_ableem/`, namespace `ableem`) in two CMake targets, mirrored in `include/ableem/`
and `lib_ableem/src/`:
- **`ableem_engine`** (`include/ableem/engine/`, umbrella `<ableem/engine.h>`) - no SDL at all: filesystem,
  strings, ini/cfg files, the SQLite game database, cover dbs, disc images, the scanner, RetroArch playlists.
  Vendored code lives in `lib_ableem/third_party/` (sqlite, nlohmann json) and
  `src/engine/unecm.c`, all private to the library - the app includes none of them.
- **`ableem`** (`include/ableem/ui/`, links `ableem_engine`) - owns every SDL/SDL_image/SDL_mixer/SDL_ttf
  call: Platform, Renderer, Texture, Font, Audio, Input, GuiBase, GuiScreen, types.h.
- `include/ableem/ableem.h` - the umbrella over both.

Never add a `#include <SDL2/...>` to anything under `src/code/`; if you need new SDL functionality, add it to
the library. Same for sqlite/json: extend `GameDatabase`/`RetroArchPlaylist` instead.

### engine

The app imports the engine names into its global namespace once, in `src/code/core/main.h` (explicit `using`
declarations - not `using namespace ableem`, because the app's `GuiScreen` shares its name with
`ableem::GuiScreen`), so app code writes `DirEntry::exists(...)`, `IniFile`, `GameDatabase` unqualified.

- **`Environment`** (`engine/environment.h`) - every path the engine touches. The library has no
  `AB_DEBUG_HOST`, `/media`, `/gaadata` or `/usr/sony` literals: `main.cpp`'s `setupEnvironment()` decides
  the layout for the platform and calls the setters (`setUsbRoot/GamesDir/RegionalDbFile/InternalDbFile/
  WorkingPath/SonyDataPath/ThemesDir/CoversDbDir/InternalGamesDir`) once; everything else is derived
  (`getPathToMemCardsDir()` = games + `!MemCards`, `getPathToMemcardTemplateDir()` = working + `memcard`, ...).
  The app's `Env` (`src/code/core/services/environment.h`) derives from it and only adds `autobleemKernel`/`hiddenMenuEnabled`.
- **`DirEntry`/`sep`** (`engine/filesystem.h`) and the string helpers (`engine/strings.h`: in-place `trim/
  lcase/...` free functions, copying `Strings::trim/replaceAll/toInt/...`) are the old `DirEntry.h`, `main.h`
  and `Util` string parts, unchanged in API. The app reaches them as `Strings::`; the process helpers
  (`runAndWait`, `execUnixCommand`, `powerOff`, ...) are `System` (`core/services/system.h`).
- **`game_types.h`** - `ImageType`, `GAME_INI`/`EXT_*`, `SAVESTATES_DIR_NAME`/`MEMCARDS_DIR_NAME`.
- **`Lang`** (`engine/lang.h`) - the translation table: `load(langDir, name)`, `translate`, `listLanguages`,
  `dumpUntranslated`, and `setCurrent`/`ableem::translate()` for a global `_()`. Was the app's `Lang` singleton.
- **`MemcardImage`** (`engine/memcard_image.h`) - a 128 KB .mcd (or DexDrive) image and its 15 slots: block
  kinds and chains, product code / game id / Shift-JIS title per save, delete/undelete, export/import of a save
  between cards, and each icon frame as 16x16 RGBA pixels. The app's `CardEdit` (`evoui/card_edit.*`) is the
  texture wrapper over it.
- **`IniFile`** (`load/reload/mergeFrom/save`), **`ConfigFileEditor`** (pcsx.cfg / RetroArch cfg line editing:
  `getValue/replaceUsb/replaceInternal/replace/replaceInFile`), **`MemcardManager`** (`create/remove/rename/
  list/swapIn/swapOut/backup/restore/restoreAll/storeToRepo` over `<games>/!MemCards`).
- **`GameDatabase`** (`engine/game_database.h`) - the SQLite wrapper for regional.db, internal.db and the
  covers dbs, file-local RAII `Stmt` class inside; add queries the same way. Rows come back as
  `GameRecord`s (`engine/game_record.h`); the app's `PsGame : ableem::GameRecord` adds the launcher-only fields
  and `PsGame::fromRecords()` wraps `loadUsbGames()`/`loadInternalGames()` results. `reloadUsbGame(*game)`
  refreshes one. Schema notes in `lib_ableem/src/engine/database_tables.txt`.
- **`MetadataLookup(coversDir, rdbFile)`** (`engine/metadata_lookup.h`, 2026-09-18) - where a game's title,
  publisher, year and players come from: RetroArch's `Sony - PlayStation.rdb` (`Environment::
  getPathToPlayStationRdbFile()`, `<retroarch>/database/rdb/` on both targets' standard tree) through
  **`RdbReader`** (`engine/rdb_reader.h`: the whole `.rdb` in memory, rmsgpack, indexed by serial and name,
  `findBySerial` also takes a suffixed serial like `SLUS-01251GH`; since 2026-09-19 also `crc`, `size`,
  `rom_name` with `findByCrc`/`findByRomName` for the other systems' databases), else the three covers dbs through
  **`CoverDatabase`** - and the covers db is still asked for its PNG when the rdb answered, so a stick with
  no thumbnails keeps its art. `GameMetadata::recordName` is the rdb's name (`"Crash Bandicoot (USA)"`),
  `title` has the trailing tags stripped, `lastRegion` ("U"/"P"/"J", what pcsx.cfg's region is set from)
  comes from the rdb's region, PAL countries included. `GameLibrary::metadata()` owns the launcher's; the
  scan worker has its own (sqlite handles are per thread). Ported from AutoBleem-NG.
- **`ThumbnailLookup`** (`engine/thumbnail_lookup.h`, 2026-09-18) - where a cover/title screen/snap is in
  `<retroarch>/thumbnails/<db name>/Named_Boxarts|Titles|Snaps/`, and the user's own screenshots and save-state
  pictures: the rdb's record name first, then the title, each with trailing ` (...)` tags peeled one at a
  time, then a fuzzy `"<bare name> ("` match scored by shared tags and region. Listings are cached per
  instance (`DirEntry::listNames` - no stat per entry, Named_Boxarts is ~9000 files) - the scan makes its
  own, `App::thumbnails()` is the launcher's, cleared when a game returns. `escapeName()` is the one file
  name rule (`RetroArchService::escapeName` delegates). The scanner resolves every game's cover and snap
  and caches them in Game.ini (`Thumbnail_record_name`, `Cached_cover_path`, `Cached_snap_path`, read back
  onto `GameRecord::recordName/coverPath/snapPath`); the carousel's PS1 chain is the PNG next to the game
  -> the cached path while its file exists -> a fresh lookup (internal games) -> `default.png` drawn at
  draw time. **No `default.png` is copied next to a game any more** and a cover is not a verify()
  requirement; a placeholder byte-identical to `default.png` is removed by the scan once a thumbnail
  exists (`DirEntry::filesAreIdentical`). Titles of *unlocked* games lose their region tag on a rescan
  with an rdb around (`"Persona (USA)"` -> `"Persona"`).
- **`DiscSuffix::parse`** (`engine/disc_suffix.h`, 2026-09-18) - `"Game (Disc 2)"` / `"(Disk 2)"` / `"(CD 2)"` /
  `"(CD2)"` / `"Game - Disc 2"` -> `{base, disc}`. **`GameScanner::mergeMultiDiscFolders(gamesDir)`** runs in
  the scan worker before the tree is read: sibling folders whose names differ only by that marker become one
  `<base>` folder - the lowest disc's folder is renamed, the other discs' images (chd/pbp/cue/bin/img) are moved
  in, **their folders are deleted with their Game.ini/pcsx.cfg/save states** (the owner accepted the fork's
  behaviour), disc 1's Game.ini keeps its settings but loses its `Discs=` key (the scan rebuilds it) and a
  folder-derived `(Disc 1)` title. A group is skipped when `<base>` already exists as something else or a
  file would be overwritten. `ScanStage::MergingDiscs` is the status line. pcsx-ab gets the first disc's
  `.cue` as before (its frontend cannot open an `.m3u`; its disc picker lists the folder); RetroArch gets
  the `.m3u`. Ported from AutoBleem-NG's `mergeMultiDiscGames`, moved ahead of the scan because our
  `onGameVerified` events would otherwise have announced the per-disc folders first.
- **`UsbGame`/`GamesHierarchy`/`GameScanner`** - the scan. `GameScanner::scanGamesDirectory(hierarchy,
  metadata)` then `writeRegionalDatabase(hierarchy, db)`; progress is reported to a `ScanProgressListener`
  (`ScanStage::Scanning/Game/DecompressingEcm/UpdatingDatabase/GameFailedVerify`). The app's listener is
  `SplashScanProgress` (`gui/scan_progress.*`): `Gui::splash(_(...))` per stage, and the 3 s pause after a
  failed verify. `AutoBleem::rescan` constructs the `GameScanner` with one.
  `UsbGame::verify()` reasons are plain English: the scanner keeps them in `failedGames`, regional.db's
  `FAILED_GAMES` holds them, and the Game Manager lists and translates them (`gamesThatFailedVerifyCheck.txt`
  is gone - "The quiet stick").
- **`SerialScanner`** (`readSerial/readSerialFromImage/readSerialByWorkaround/serialFromMd5/normalizeSerial/
  serialToRegion`), **`IsoDirectoryReader::read`**, **`EcmDecoder::decode`** (+ `setProgressHandler`, which
  is how unecm.c's percentage messages reach the splash). Private: `cd_image_reader.h` (`CdImageReader`,
  `ChdImageReader` behind `ABLEEM_ENABLE_CHD`), `binary_reader.h`, `md5.*` (replaces `head|md5sum`).
- **`RetroArchPlaylist`** - `.lpl` files: `load/loadJson/loadSixLine/save` over `RetroArchPlaylistEntry`,
  plus the `RetroArchPlaylistHeader` (2026-09-18): every top-level field but `items`, as opaque JSON text
  in file order, so a playlist RetroArch 1.22 wrote (version 1.5, `sort_mode`, `scan_content_dir`, ...)
  survives a rewrite; `save()` puts `version` first. `GameLibrary::exportToRetroArchPlaylist`,
  `RetroArchService` and the scanner are the callers.
- **`CoreInfoTable`** (`engine/retroarch_cores.h`, 2026-09-18) - `<retroarch>/info/*.info` for the cores
  whose `.so` is installed (`CoreInfo`: display name, extensions, databases, `block_extract`), each database
  mapped to the first installed core listing it (most extensions first) unless the cores.cfg given to
  `load()` overrides it. Was `RetroArchService::loadCores`; the service holds one, the scan worker builds
  its own.
- **`RetroArchScanner`** (`engine/retroarch_scanner.h`, 2026-09-18) - the ROM scan:
  `scan(Options{romsDir, playlistsDir, targetRomsDir}, systemsFrom(cores))` walks each `roms/<system>/`
  folder that has a core, one entry per game (a `.cue` hides its bins, an `.m3u` its discs, a `.ccd` its
  image; a `.zip` for a core that does not read archives itself - `zip` among its extensions, or
  `block_extract` - is opened: one ROM inside is `zip#rom` named after the zip with the ROM's CRC from the
  central directory, several are one entry each, none is skipped; an arcade core gets the zip whole),
  label = the file's stem, and merges into the existing playlist (`Options::folderAliases` sends a folder
  named otherwise to its database's playlist): entries
  outside the folder stay, an entry whose file is still there is kept exactly (RetroArch's own label/CRC),
  the vanished go, the new join, sorted by label; `.tmp` + `DirEntry::replaceFile`, only when something
  changed. `targetRomsDir` is what the playlists name (the console's `/media/RetroArch/roms` when a PC writes them -
  the plan's step 5); `""` = `romsDir`. `AutoBleem`, `Applications` and `content_*` are never written.
  With `Options::rdbDir` (2026-09-19) each folder's `ScannedRoms` go through `identify()` first: the
  system's `.rdb` names a zip member by CRC, a loose file by `Crc32::ofFile` (up to `maxCrcBytes`), an
  arcade set (`wholeArchive`) by `rom_name`; the label becomes the record's name and, in the merge, an
  identified entry replaces an existing one for the same ROM whose label differs. **A rescan is cheap**
  (2026-09-21): with `Options::stateFile` a digest per folder (file names + sizes, the playlist's size,
  the rdb's size, target path, core - no mtimes) lets a folder nothing changed in be skipped outright
  (`RetroArchScanResult::systemsSkipped`; its playlist is only read for the counts), and
  `seedCrcsFromPlaylist` gives a loose ROM the CRC its existing playlist entry carries, so `identify()`
  hashes only files new to the playlist. `UpdateRoms` passes no state file yet (the target's state dir
  differs per platform); the digest is portable, so it could.
- **`ThemeSpec`** (`engine/theme_spec.h`) - a theme as a typed struct (`music`, `classic`, `launcher`, `sounds`)
  with `load/save` of theme.json (never throws), `mergeOver(base)` for a partial theme over the default, and
  `resolveFiles()` (the theme's file if it exists, else the default's). `fileFields()` is the one list every
  file loop uses. Scalars a theme may omit are `Opt<T>`; colours are `ThemeColor` (`#rrggbb`).
  The app's `ThemeConverter` (`core/services/theme_converter.*`) is the only writer besides tests.
- **`ZipArchive`** (`engine/zip_archive.h`) - `list/extract` of a .zip over vendored miniz (`third_party/miniz/`,
  built with `MINIZ_NO_TIME`). Entry names are checked before anything is written: no
  `..`, no absolute paths, no backslashes. Themes dropped as zips are its only caller. **`ZipWriter`**
  (`engine/zip_writer.h`, 2026-09-18) is the write side: `open/addFile(path, name)/addBytes/close`, files
  streamed through miniz's read callback with 64-bit offsets, so a partition image of any size goes in
  without being read into memory - for abflashkit's `LBOOT.EPB`. The entry size is given up front on
  purpose: it is what keeps the archive plain zip (no zip64), which is what the console's recovery reads.
- **`Md5`** (`engine/md5.h`, public since 2026-09-18) - RFC 1321: `ofBytes/ofString/ofFile` (streamed) and
  the incremental `update/hexDigest`. `SerialScanner::serialFromMd5` and abflashkit's kernel check use it.
- **`Crc32`** (`engine/crc32.h`, 2026-09-19) - `ofFile(path, crc, maxBytes)` streamed over miniz's
  `mz_crc32` (false, no read at all, over the cap), `ofBytes`, and `playlistText()` (`"%08X|crc"`). What
  the ROM scanner identifies a loose file by.

### ui

- **`Platform`** - owns SDL_Init/window/TTF_Init/Mix_Init (created by `GuiBase`). `isDevHost()` replaces the
  app's old per-call `AB_DEBUG_HOST` checks for cursor grab; `setPowerOffHandler()` is how the app supplies
  what "power off" means (main.cpp wires it once to `gui->drawText(...); System::powerOff();`) - `Input::poll()`
  calls it automatically on the console power button or Esc, so screens never check for that themselves.
  `Platform::shutdownSDL()` must be registered with `atexit()` before the first `GuiBase`/`Gui` is constructed
  (done once, in `main.cpp`) - it runs SDL_Quit() after everything else is destroyed.
- **Output scale** (2026-09-18): the app draws on a logical 1280x720 canvas; the window may be bigger by
  `Renderer::outputScale()` (`GuiBase(title, w, h, outputScale)`), and every `Renderer` call maps logical to
  output pixels (`toOutput()`, edges rounded so neighbours tile; identity at 1). `Texture::createTarget`
  allocates output pixels and carries `pixelScale()`, which `copy()` applies to a source rect, so a target is
  addressed like the screen; `Font::load` loads the face `scale` times bigger, draws in output pixels and
  measures in logical ones. Nothing in the app knows. `Gui::outputScale()` is the policy: a Pi on a >= 1080p
  display gets 1.5 (`Platform::desktopDisplaySize()`), a dev host reads `AB_OUTPUT_SCALE`, the console is 1.
  The Pi installer boots in 1920x1080 by default now (`--hdmi-mode`), the plymouth script scales the logo up.
- **MSAA** (2026-09-18): `GuiBase(..., multisampleSamples)` asks for a multisampled GL context before the
  window exists (`Platform::createWindow`: `SDL_GL_MULTISAMPLESAMPLES`, `SDL_WINDOW_OPENGL`, Windows also
  forced onto the "opengl" driver - direct3d would ignore it; a driver that refuses gets a plain window
  and `Platform::multisampleSamples()` says 0). SDL's GL renderer then rasterises every quad with it, the
  carousel's cover strips included, which now sit at fractional positions (`SDL_RenderCopyF`, SDL >= 2.0.10;
  the console's 2.0.4 headers keep the integer path). `Gui::multisampleSamples()`: 4 on a Pi and a dev host,
  `AB_MSAA` overrides (0 off), the console 0. Was costly on the Pi at 1080p (idle CPU 17% -> 60%) when
  a frame was ~3200 copies; the launcher performance work of 2026-09-18 (its plan, `docs/perf-plan.md`,
  was removed once done - the git log has it and the numbers) took that to 85:
  `AB_FRAME_STATS=1` logs frame times and copies every 5 s (`Renderer::present`) and slow
  `Texture::loadFile`s; the launcher defers the snap/resume-picture loads to the frame the carousel
  settles in and keeps `Carousel::Lookahead` covers past each end decoded; every animation is
  `easeOutCubic` (`core/model/timing.h`), a held stick chains steps without a pause and a tap during a
  scroll is queued; `TextRenderer` caches every run as a texture (`clearTextCache()` on font reload and
  display release); `copyTrapezoid` is one `SDL_RenderGeometry` call on SDL >= 2.0.18 with the tint in
  the vertex colours. Step 7 measured MSAA on the Pi 400 at 1080p: **even 2x drops to 30 fps for
  stretches**, so a Pi runs with 0 (`Gui::multisampleSamples`; a dev host keeps 4) and the covers' edges
  are smoothed by `CoverMargin` - each cover composed 2 px inset into a transparent-black texture, which the
  linear filter blends the edge into. 60 fps solid there, ~25% of a core idle.
- **`Renderer`** - the one SDL_Renderer, `clear/present/setDrawColor/fillRect/drawRect/drawLine/copy/setTarget`,
  and `copyTrapezoid(tex, src, VerticalEdge left, VerticalEdge right)` (2026-09-18): pseudo-3D for the
  carousel - a texture drawn into a trapezoid with vertical sides, one `SDL_RenderCopy` strip per screen
  column, the columns spread perspective-correctly with each side's height as its depth. Nothing newer than
  SDL 2.0.4 (`SDL_RenderGeometry` is 2.0.18, which the console's own `autobleem_sdl` now is since 2026-09-29 -
  was 2.0.14; the carousel still targets 2.0.4 unguarded).
- **`Texture`** - shared handle (copy freely) with `loadFile/loadMemory/createTarget/createStreaming`, plus
  `PixelLock` (RAII `lock()`) for per-pixel `get/set` - replaces the old manual `SDL_LockTexture` +
  `SDL_AllocFormat`/`SDL_MapRGBA` dance (see `engine/cardedit.cpp`, the memory card icon renderer).
- **`Font`** - shared handle over SDL_FontCache: `textSize/width/lineHeight/draw/drawAlign/drawColor`. The
  app's own `Fonts`/`FontEnum` (`gui/gui_font.*`) is unchanged in spirit - it still maps FONT_15_BOLD etc to a
  themed .ttf path, just building `ableem::Font`s now instead of `FC_Font_Shared`s.
- **`Sound`/`Music`/`Audio`** - `Sound::play()` replaces `Mix_PlayChannel(-1, chunk, 0)`; `Audio::close()` is
  the old "close until `Mix_QuerySpec` fails" loop, now one call (`gui->audio().close()`).
- **`Joystick`** (`ui/joystick.h`, 2026-09-18) - one device by index opened *raw*, for a pad-mapping wizard:
  `count/nameForIndex/guidForIndex/isGameControllerAtIndex`, `open(i)`, `update()` into `state()` (every
  axis, button and hat as SDL's joystick API reports them, hats as `HatUp|...` masks) and `controllerState()`
  (the 15 standard buttons and 6 axes through the mapping, when it has one). `Input::addMapping(line)`,
  `mappingForDeviceIndex(i)` and `currentMappingPath()` are its companions; `Input::setPowerKeyAsKey(true)`
  makes the power button/Esc arrive as `Key::Sleep` instead of calling the power-off handler (a wizard uses
  it as "cancel"); `Key::Reset`/`Key::Open` are the console's other front buttons (AUDIOPLAY/EJECT scancodes).
- **`Input`** - one `poll(Event&)` replaces `SDL_PollEvent` + `PadMapper` + `gui/abl.c`'s PSC event filter
  (still there, moved to `lib_ableem/src/ui/psc_event_filter.c`, wired up by `Input`'s constructor). `Event::Type`
  is `Quit/ButtonDown/ButtonUp/DpadDown/DpadUp/KeyDown/KeyUp/TextInput/PadAdded/PadRemoved/RenderReset`;
  `Button`/`Key` replace `SDL_BTN_*`/`SDLK_*`. `dpadUp()/Down()/Left()/Right()/Centered()` are the old
  `PadMapper::isUp()` etc (state, not just "this event's direction" - screens read them right after `poll()`
  returns a Dpad event, same priority order as before: up, down, right, left, center).
  `setKeyboardAsPad(true)` (the default on **every** platform since 2026-09-26) turns keys into pad events:
  the PC-style map (`ui/keyboard_map.h`, see "The keyboard" under Build) everywhere, and on a dev host also
  the letter map `tools/win_drive.ps1` drives the app with - X/O/S/T = cross/circle/square/triangle,
  I/J/K/L = d-pad, Space/B = Start/Select, Q/E/1/2 = L1/R1/L2/R2. `keyboardPresent()` says whether a
  keyboard is connected (`engine/keyboard_presence.h`).
- **`GuiBase`/`GuiScreen`** - `GuiBase` owns Platform+Renderer+Input+Audio in that order. The app's `Gui`
  (`gui/gui.h`) derives from it and adds theme/config/database/carousel state - lib_ableem has no idea what a
  theme or a database is. The app's own `gui/gui_screen.h` is now a thin shim: `class GuiScreen :
  public ableem::GuiScreen` that also carries `std::shared_ptr<Gui> gui` and `ableem::Renderer &renderer` as
  members, so every existing screen file keeps writing `gui->cursor.play()` / `renderer.copy(...)` unchanged -
  only the SDL-specific calls inside each screen needed converting, not every constructor caller. Screens are
  constructed with a `GuiBase&`, in practice always `*gui` (e.g. `GuiConfirm confirm(*gui);`).
- **CMake**: `add_subdirectory(lib_ableem)` from the root file; `ABLEEM_EMBEDDED_TARGET` is forced on for both
  ARM builds (no cursor grab, keyboard-as-pad off); the non-MinGW branch does one `find_package(SDL2)` and links
  the bare names `SDL2 SDL2_image SDL2_mixer SDL2_ttf`, which is why each cross toolchain ships its own
  `cmake/FindSDL2.cmake` defining those four imported targets; `ABLEEM_ENABLE_CHD` follows the root `AB_ENABLE_CHD`
  (libchdr is linked by `ableem_engine`); `lib_ableem/examples/demo.cpp` (`ableem_demo` target) is a
  from-scratch smoke test of the ui library alone - texture + font + sound + input, no AutoBleem code involved.

## UI styling standards (2026-09-21, the `feature/ui-fixes` pass)

Every screen but the launcher's own carousel frame draws in **one look**, and new screens must too:

- **`PanelStyle`** (`gui/panel_style.*`) is the look: the screen behind dimmed (`dim`, black 110), a sheet
  (black 200) with a 1 px edge in the launcher theme's *secondary* colour, a **header** (`header`: the title in
  `FONT_28_BOLD` at `RowInset` (24) + 18 from the top, a rule 8 px above the header's 74 px end), rows,
  and a **footer** band (`FooterHeight` 54). Colours come from `launcher.colors` (`text`, `secondary`,
  `hint`) - never hard-coded. `Gui::panelStyle()` resolves it for the current theme.
- **Style roles** (UIREV-29, the owner's "like CSS": one block, change it once and every window follows).
  Every row, heading, value and description draws in a `PanelStyle` role resolved from `launcher.colors`:
  `row` (an unselected row), `rowSelected` (the selected row, label and value), `heading` (text on a
  heading band), `value` (an unselected row's right-hand value), `description` (second lines, subtitles,
  the strip, the footer counter), `footer` (member `footerText`: the footer's hint labels), `selectionBand`
  (the band and bar), `edge` (the sheet's edge, rules, the heading band). A role is `#rrggbb` or the name
  of another colour in the block (`"row": "secondary"`), resolved after the merge over the default theme;
  unset falls back (row/heading/description/edge -> secondary, rowSelected/footer/selectionBand -> text,
  value -> row). The look: unselected rows dim, the selected row bright (the Quick menu's). Compact panels
  use `style.rowColor(selected)`/`valueColor(selected)`/`description`; the classic rows get it through
  `TextRenderer::setRowRole` (`Row`/`Selected`/`Heading`/`FactRow`, `RowRoleScope`) - `GuiMenuBase::renderLines`
  sets it per row for every menu built on it, a screen with its own row loop sets it itself, and `Plain`
  (the font's own colour) is what everything else keeps. A facts page (no cursor) draws labels in `row`,
  values in `rowSelected`. The table is the launcher's `docs/theme-format.md`.
- **Two panel shapes.** A *full* panel (the classic screens: Options, the editors, Game Manager, Memory
  Cards, Hardware Information, the keyboard, pages): `Gui::renderTextBar()` + `renderHeader(title)` +
  rows + `renderStatus(hints)`; its rect is the theme's `classic.menuPanel` down to the status line
  (`Gui::classicPanel()`), rows live in `classicContent()`, the footer in `classicFooter()`. A *compact*
  panel centred on the screen (the system menu, the set picker, the update prompt, Confirm): 800 wide,
  as tall as its rows, `PanelStyle::Margin` (40) from the edges, the launcher's captured frame under it
  (`renderer.captureNextFrame(); render(); background = renderer.lastCapture()`). A dialog with one
  question is compact, never full.
- **Rows.** Text at `RowInset + 8` (32 px) from the panel's edge - the header's text x. The classic
  screens' rows use the theme's classic font at its own line height, one under the other, **as many as
  fit** (`Gui::classicRowsThatFit(font)`), scrolling a row at a time with **markers**
  (`Gui::renderScrollMarkers` - triangles at the content's right edge). The selected row is
  `renderSelectionBox`: a band in the `selectionBand` colour at alpha 38 with a 5 px bar at the panel's left edge
  (`PanelStyle::selection`); a heading between rows is `renderLabelBox` (a faint band). A row that cannot be changed is drawn, then greyed over
  with `renderDisabledBox` (`PanelStyle::disabled`, black at alpha 150) - still selectable, so the cursor
  can pass it. Compact panels
  use `PanelStyle::RowHeight` (60: `FONT_22_MED` title + `FONT_15_BOLD` description) or 44 for a
  single-line row.
- **Values right-aligned.** An option row is its label at the left and its value at the row's right
  edge: a boolean's switch (`renderTextLineOptions`, the theme's on/off image with its transparent margin
  measured so the art meets the edge) or text (`renderRowValue`). A screen with a pane on the right
  passes the pane's `rowsRight` as the edge.
- **The detail pane** (`gui/game_detail_pane.*`, 360 wide) is the right side of any screen about one
  game: the cover on a plate, a screenshot when there is one, then facts as `FONT_15_BOLD` label over
  `FONT_20_BOLD` value, a rule to its left.
- **Footers are structured** and drawn by `PanelStyle::footer` from the `"|@X| Label  |@O| Label"`
  protocol (`parseHints`): the hints **sorted** Cross, Circle, Triangle, Square, Start, Select, L1/R1,
  L2/R2, keyboard keys; icons 30 px (the launcher's hint images for X/O/T, the theme's buttons for the
  rest); labels in the largest launcher font that fits; a counter ("Game 3/21") at the right edge in the
  secondary colour. **Labels**: Circle is "Back" wherever leaving loses nothing, "Cancel" only where
  Cross commits; Cross names its action; sentence case ("Delete game"). **Paging is L2/R2 everywhere**,
  L1/R1 go to the first/last row (or switch tabs where there are tabs).
- **Fonts.** Titles/labels: the launcher pair (`themeFonts[FONT_28_BOLD/22_MED/20_BOLD/15_BOLD]`, Open
  Sans); classic rows: the theme's classic font (`assets().themeFont`, Saira / Selawik); never a
  hard-coded ttf path - the shipped ones are `Env::getPathToFontsDir()`'s.
- **Waiting.** A long job on the main thread runs inside `Gui::beginBusy(message, redraw)` /
  `endBusy()` with `Gui::tickBusy()` in its loops (the spinner over the dimmed screen); a blocking call
  with no loop goes through `Gui::drawText(message)` (background, logo, spinner). Background work
  reports in the launcher's `NotificationBubble` (top-right, slides in and out), never in a status line.
- **Input across a busy job (the busy rule, CONSOLE-13).** While a spinner shows every pad and key input is
  ignored, and when the job ends the input starts clean - nothing held, no hold-repeat carried over. It is
  done once, in `Input` (`Gui::endBusy()` -> `Input::flushInputEvents()`, and `poll()`), never per screen:
  a press made before the job's end and not yet handed out is dropped (CONSOLE-11); every press a screen was
  handed and not the release of is released at the job's end (a `ButtonUp`/`DpadUp`/`KeyUp`, the first
  things `poll()` hands out, `padEventPending()` true until read, the d-pad state centred), whether or not the
  player let go (CONSOLE-12's hold stops on it); and `poll()` never hands out a release of a press it did not
  hand out, nor a key's repeat or its text while no screen holds that key - so the player's own later release,
  or a press made during the job and held past it, never reaches a screen. A screen or an extension gets it
  for free by ending a hold on any one of: its release event (`GuiScreen`'s loop, the launcher's L1/R1),
  `padEventPending()` (`fastForwardUntilAnotherEvent`, the list menus), or the live d-pad state read once a
  frame (`HoldRepeat` + a `holdTick` as Options and the game editor do). A hold that can outlive a screen
  opened over it must use the live state: that screen may read the release, the one under it never sees it
  (the launcher's carousel, e57dfa4). Never act on a `...Up` event as if it were a press. Tests:
  `tests/classic/test_busy_input.cpp` (and `test_input_flush.cpp`).
- **Every string on screen is `_()`** and lands in all 16 language files in the same commit
  (`tools/lang_tools.py extract`/`update`, then translate); no `=` in a key.
- **Testing a screen** is `tools/ab_drive.py` (`start --show`, `run "menu 6; wait_screen GuiOptions; shot
  a.png"`, `sheet`, `stop`); every screen's class name is what `wait_screen` takes.


## Source map: the core-owned files (`src/code/core/`, `app_base.*`, ab_classic `gui/`)

These are the rows of the launcher's own "Source map" table that describe files living in this repository
(`ab_core` and `ab_classic`); the launcher's `CLAUDE.md` keeps the rows for its own `ab_ui`/`ab_evoui`/
executable/abpad files and points here for the rest.

| Area | Files | Notes |
|---|---|---|
| Version | `core/version.h` (generated) | `Version::VERSION` (the last git tag, else `AB_VERSION_FALLBACK` in CMakeLists - was `config.ini`'s `Version=` key, dropped on load now), `GIT_HASH`, `GIT_BRANCH`, `GIT_DIRTY`, `BUILD_TIMESTAMP`, `FULL_VERSION` (`v2.0.0-pre0 (master@a83777b*)`). Written by `cmake/generate_version.cmake` into `<build>/generated/core/` on every build (`ab_version` target; the header only changes when the facts do - `BUILD_TIMESTAMP` is kept from the existing header while tag, hash, branch and dirty flag are the same (2026-09-21), so it is when *this version* was first built, and a no-change ninja run is a no-op). The splash, About and the log's first line use it. Include as `"core/version.h"`. |
| `core/services/environment_setup.*` | `EnvironmentSetup` | The layouts a program can be started with (2026-09-18, was `main.cpp`'s `setupEnvironment()`): `fromRoot(root)` (everything under one root - the console's `/media`, the Pi's data partition, the 1-arg debug mode: `Games/`, `System/Databases/`, `Autobleem/bin/autobleem` as the resources dir, `Autobleem/bin/db`, `themes/`; the Sony data tree is the console's own or `<resources>/sony` under `AB_ROOT_RELATIVE_LAYOUT`), `fromDbAndGames()` (the 2-arg debug layout), `fromArguments()` (autobleem-gui's command line) and `forTool(argc, argv, name)` for a console tool in `Apps/<tool>` (optional root, `/media` by default on the console; pins `Env::getAppDir()` - the tool's own folder, `getPathToAppLangDir()` its `lang/` - before anything can chdir). Every one applies `PlatformConfig`. The only place besides `Env::platformName()` that spells `/media` or `/usr/sony`. Tested in `tests/core/test_environment_setup.cpp`. |
| `app_base.*` | `AppBase` | The model of any program drawn with the classic UI: `Config`, `Lang`, `Theme`, `Clock`, the `Gui` singleton (whose window title it sets - `Gui::setWindowTitle` before the first `getInstance()`) and `AppAudio`. Top of `ab_classic`; every `GuiScreen`'s `app` member is one. `AppBase::get()` for the non-screens (Gui, Theme, AppAudio, Fonts). |
| `core/model/session.h` | `Session` | Where we are across one run: `menuOption` (`MENU_OPTION_IDLE`/`RETRO`/`START`/`UPDATE`/`POWEROFF` - the classic-UI values are gone), the game being started (`runningGame`, `EmuMode`, `resumePoint`), and `launcher`, the carousel's `GameSetSelection`. |
| `core/services/online_assets.*` | `OnlineAssets` | The scan's online side (2026-09-19): `probe()` (one request per instance), `fetch(url, file)` through the platform's `download_command` (`%u`/`%o`, `std::system`, a `.part` renamed on success), `ensureDatabases(rdbDir)` (the 40 MB `database-rdb.zip` unpacked when there is no `.rdb`), `fetchBoxArt(thumbnailsDir, db, label)` -> Fetched / AlreadyThere / Missing (remembered in `Named_Boxarts/.autobleem-missing.txt` after a re-probe) / Failed (the network went). `boxArtUrl()`/`urlEncode()` spell the libretro-thumbnails URL. `CommandRunner` is the test seam. Made per scan cycle by `ScanService` from what `setOnline()` was given (`App::applyOnlineSetting()`: config.ini `online` + `Env::downloadCommand()`). |
| `core/services/scan_service.*` | `ScanService` | The background scan: one worker thread (lowest OS priority - `System::lowerCurrentThreadPriority()`) does the filesystem work (`GamesFingerprint`, `GameScanner`, its own `CoverDatabase`, and - with RetroArch detected, `romScanEnabled()` - `ableem::RetroArchScanner` over the ROM folders with its own `CoreInfoTable`) and queues `WorkerEvent`s; `poll()`, called once a frame from `GuiLauncher::loop()`, applies every regional.db write on the main thread, has `RetroArchService` reload rewritten playlists, and returns a `ScanUpdate` (added/updated/removed games, `playlistsWritten`, progress, finished with the game and ROM counts). `requestScan()`/`scanning()`/`setWatching()`; `checkForChanges()` is the watcher's debounce over both `games.fingerprint` and `roms.fingerprint`, checked every `ScanWatchInterval` when nothing was requested directly; `fingerprintsMatchDisk()` is the startup check. **A moved game keeps its row** (2026-09-21): the rows whose folder is not where the database says are kept aside at `ScanStarted` (`VanishedGame`: id, folder name, disc names), a verified game at a new path with the same folder name and disc file names claims one (`claimMovedGame` -> `GameDatabase::updateGamePath`, reported in `updatedGames`, so id/history/last_played and the carousel's selection survive a drag into a sub-folder), and the unclaimed are deleted at `Finished`; a *renamed* folder is a new game. The ROM pass gets `<state>/roms.scanstate` (`romScanStateFilePath()`) as the scanner's per-folder state, so a rescan skips every ROM folder nothing changed in. Owned by `App` (`app.scans()`, constructed with `&retroArch_`). |
| `core/main.h` | | The `using` declarations that bring the lib_ableem engine names (`DirEntry`, `sep`, `ImageType`, `GAME_INI`, `trim`/`lcase`, `IniFile`, `GameDatabase`, ...) into the app's global namespace. |
| `core/services/environment.*` | `Env` | `struct Environment : ableem::Environment` + the two app flags, the `AB_DEBUG_HOST` macro, `platformName()` (`"psc"`/`"rpi"`/`"pc"` - the one place the build macros decide a path), `retroArchInstalled()`, `padMappingFiles()` (the `gamecontrollerdb.txt` list `Gui`'s constructor hands `Input::loadMappings()` - the kernel's `/etc/autobleem` one on the console, then the shipped one in the resources dir; **loaded since 2026-09-18** - until then nothing called `loadMappings` and the pscbios wizard's output was never read), and **`clockIsSet()`** (2026-09-26: on the console only, true if the network has set the clock - the marker file at `clockSetMarkerFile()` (`/run/autobleem/clock-set`), touched by the dhcpcd `70-autobleem-time` hook; off-console always true - `LaunchService::recordLastPlayed()` uses it to avoid overwriting valid times with 2018-09-01). The shipped `src/resources/gamecontrollerdb.txt` is the community SDL_GameControllerDB at a pinned commit, cleaned for the console's SDL 2.0.14 (2026-09-29: the console's own SDL is now `autobleem_sdl` 2.0.18, ABI-compatible - the file needed no re-clean), with our own `#Magnus RC`/`#AutoBleem` sections last (they win): refresh it with **`tools/update_gamecontrollerdb.py`** (2026-09-25, 755 Linux mappings - the 6.1 pad drivers' GUIDs included). All path getters live in the library (`getPathToKernelConfigDir()` is `""` off the console); extend `ableem::Environment` instead of adding new literal paths. |
| `core/services/platform_config.*` | `PlatformConfig` | **What differs per target about where things are, as data**: `resources/platform/<platform>.ini` (`psc.ini`, `rpi.ini`, `pcusb.ini`, `pc.ini`; `win.ini` to come) - `retroarch_dir` (relative to the USB root), `retroarch_core` (the PS1 core the exported playlist names, relative to that dir), `retroarch_binary` (`;`-separated candidates; "RetroArch" in the system menu and Square on a game are offered when one exists), `retroarch_roms_dir` (the other systems' ROM folders the scan writes playlists for, relative to the USB root; 2026-09-18), `download_command` (how the platform fetches a URL to a file, `%u`/`%o`, with its own timeout; empty = never online - the console; 2026-09-19, `Env::downloadCommand()`). `main.cpp` loads and `apply()`s it after the roots are set; a missing file means the console's layout. `retroarch_catalog`, `launch_mode`, `core_extension`, `pcsx_dir` (2026-09-20, see "The platform model"). Add per-platform paths here, never as `#ifdef AB_PLATFORM_*` in the services. **`<platformName>.cores.cfg`** next to it (2026-09-18) is which core plays which RetroArch playlist on that platform (`<database name>=<part of a core display name>`, `#` comments), read by `RetroArchService` ahead of its `.info` mapping - was the one `coreOverride.cfg` for every platform; the Pi's prefers Genesis Plus GX (picodrive's Cyclone core segfaulted on the Pi 400), plain Snes9x and blueMSX. Tested in `tests/core/test_platform_config.cpp`. |
| `core/services/system.*` | `System` | The process/console helpers: `execUnixCommand` (popen, returns "" on failure), **`runAndWait(exe, args)`** - the only fork/exec in the code base, `powerOff`, `getAvailableSpace`, `getRandom*`. The string helpers are `Strings::` (`ableem::Strings`, via `main.h`). |
| `core/main.h` | `_()` | The app's `_("...")` is `ableem::translate()`, which goes through the `ableem::Lang` the `App` owns and registered (`app.lang()`); `resources/lang/<Language>.txt` is `English text=Translated text` lines under a `#` header (since 2026-09-18; the old pairs-of-lines layout is still read when the first line is not a comment). **`tools/lang_tools.py`** keeps them in step: `extract` (English.txt from every `_("...")`), `update [--remove-obsolete]`, `validate` (run by `make_win.sh`), `compare <Lang>`, `convert`, `merge <dir>`. A key cannot contain `=` - decorate at render time (`".-= " + _("Testing") + " =-."`). Emoji markers like `\|@X\|` in strings are replaced by button textures by `TextRenderer`. |
| `core/services/clock.*` | `Clock` | The "last played" time as text: `displayTime(t)` in config.ini's `datetimeformat`, "" for a time the console could not have known (before 2020 - no battery clock). Owned by `App` (`app.clock()`). |
| `core/services/config.*` | `Config` | `config.ini` on top of `ableem::IniFile`: app defaults (`language`, `aspect`, ...; keys are lower-cased on load, e.g. `values["theme"]`) and a few obsolete keys dropped on load, `ui` (the classic UI is gone) among them. Owned by `App`; read as `app.config().inifile.values["..."]`. |
| `core/services/theme.*` | `Theme` | The current theme's `theme.json` merged over `themes/default/theme.json` (so every key has a value), every file resolved to the theme's own or the default's. `load()` converts an old-layout folder first (`ThemeConverter`). Owned by `App`; read as `app.theme().classic().menuPanel.x`, `app.theme().launcher().footer`, `app.theme().sounds().cursor`. No platform `#ifdef`s - the paths come from `Env`. |
| `core/services/theme_installer.*` | `ThemeInstaller` | `<themes>/<name>.zip` -> `<themes>/<name>/` (root files or one folder inside; replaces an existing folder; a non-theme becomes `.zip.bad`). Run by `Theme::load()` and the Options theme list before they look at folders. |
| `core/services/theme_converter.*` | `ThemeConverter` | `theme.ini` + the PSC data tree -> `theme.json` + role-named files, in place: json first, then the renames, then the deletes. `needsConversion(dir)` is also what makes an old folder count as a theme in the Options menu. `tools/theme_convert` wraps it. |
| `gui/app_audio.*` | `AppAudio` | The background music track and the five UI sounds (`cursor`, `cancel`, `home_up`, `home_down`, `resume`), plus which track to play (theme's or the user's from `resources/music`) at which sample rate. Owned by `App`: `app.audio().cursor.play()`. Sits on `gui->audio()`, which is only lib_ableem's mixer device. |
| `gui/gui.*` | `Gui` singleton | The screen only: SDL window/renderer (via `ableem::GuiBase`), `assets()`, `text()`, and the background/logo/status drawing that combines them. `display(resume)` (re)inits and shows the splash (`resume=false`, boot only) or sets `session().resumingGui` for the launcher to pick up (`resume=true`, after a game exits). |
| `gui/screens/gui_splash.*` | `GuiSplash` | Fades in, holds at full brightness for `SplashHoldDuration` (2s), fades back out, then returns - `Gui::display(false)` is its only caller, once at boot. |
| `gui/theme_assets.*` | `ThemeAssets` | The current theme's textures (background, logo, jewel case, the `|@X|` button markers) and fonts (`themeFont` at the theme's size, plus the `themeFonts`/`sonyFonts` sets). `load()` re-reads theme.json (`Theme::load()`) and reloads everything from the resolved paths. Screens use `gui->assets()`. |
| `gui/text_renderer.*` | `TextRenderer` | The classic UI's text drawing: `|@X|` button markers laid out inline with text, `renderTextLine/ToColumns/Options`, selection and label boxes, the theme's menu-panel/status-bar rects, `toColor()`. Holds references to `Gui`'s theme font and button textures; screens use `gui->text()`. |
| `gui/gui_screen.h` | `GuiScreen` | Base for every screen: `init/render/loop` + virtual `doCross_Pressed()`-style handlers; `show()` runs them. Set `menuVisible=false` to exit. Carries `gui`, `renderer` and `app` (an `AppBase &` - see `app_base.*`). |
| `gui/menus/gui_*` | `GuiMenuBase`, `GuiOptionsMenuBase`, ... | Header-only templated list menus (string, two-column, playlist, game dir) and concrete Options / Memory Cards / Game Manager / Game Editor menus. |
| `gui/screens/gui_*` | | The rest of the classic screens, shown from the launcher's L2+R2 system menu or its sub-screens: About (`credits` settable by the caller, AutoBleem's by default - a tool shows its own), Confirm dialog, on-screen Keyboard (`GuiKeyboard`, rebuilt 2026-09-24 as ABI 3: pages of letters, symbols - `/ \ : ? & = % @ #` and the rest a URL, path or password needs - and two of accented letters, a function row with Shift/caps lock, the page key, Space, Backspace and Done; L1 Shift, R1 the next page, L2/R2 move the cursor; a USB keyboard types alongside the pad, Esc cancels, and while it shows a dev host's keyboard-as-pad is off; UTF-8 by whole characters; keys and field drawn as plain text, never parsed for `|@X|` markers), memcard select, `GuiTextPage` (a titled page of static `lines`, Circle back - a tool's instructions). `gui/starfx.*` is the star field the About screen draws. (`GuiScrollWin`/`GuiPadTest` were deleted on 2026-09-18 - nothing had shown them since the classic menu went.) |
| `gui/screens/gui_facts_page.*`, `gui_action_menu.*` | `GuiFactsPage`, `GuiActionMenu` | Two reusable classic screens (2026-09-21, for the console tools): a facts page - sections with a heading band and label/value rows, scrolling, re-read every `refreshInterval`, a subclass gives `title()`/`collect()` and takes its own buttons through `onButton()`/`extraHints()` (Hardware Information and PSC-Bios's opening screen) - and an action menu in the system menu's look (rows of a name over a description, Cross picks into `result`, Circle leaves; ABFlashKit's screen). |
| `gui/screens/gui_hardware_info.*` | `GuiHardwareInfo` | The Hardware Information screen (2026-09-26): a `GuiFactsPage` built in on every platform with `SystemInfoService`'s sections - system (os, hostname, uptime, load), hardware (model, CPU cores, clock, thermal zone, RAM), storage (the data root and every block filesystem), network (IPv4 adapters, time zone from timedatectl), display (render driver + MSAA, video driver, display mode, canvas/scale, audio driver, SDL version), pads (by name with their mapping file in use - `Env::padMappingFiles()`'s first file found). Rows paged like Options, re-read every second. `autobleem-gui <root> --sysinfo` prints the sections to stdout and exits (minus display) - for bug reports and checking the Linux branch over ssh. The System menu's Hardware Information item (2026-09-26) opens this screen on every platform; the Network & Controllers item (when an installed extension provides the `network` entry - see below) opens that extension at its network entry through `Extension::runEntry("network")` (ABI 4), which on the console and a Pi/PC stick opens PSC-Bios's hub for Wi-Fi settings, Bluetooth pairing, DualShock 3 pairing and controller mapping. `GuiHardwareInfo` serves as fallback where Network & Controllers is not provided. |
| `core/services/system_info.*` | `SystemInfoService` | What that screen shows, SDL-free: `collect()` = `system()` (os-release/uname, hostname, uptime, load; the registry on Windows), `hardware()` (device-tree model, cpuinfo, cpufreq, thermal_zone0, meminfo), `storage()` (the data root first, then every block filesystem in `/proc/mounts` - or the fixed/removable drives - with `statvfs`/`GetDiskFreeSpaceEx`), `network()` (IPv4 per interface, `getifaddrs`/`GetAdaptersAddresses` - ab_core links `iphlpapi ws2_32` on Windows), `software()` (version, build, platform, roots, RetroArch). The parsers and formatters are static and tested (`tests/core/test_system_info.cpp`). |
| `gui/gui_font.*` | `Fonts`, `FontEnum` | Theme/Sony SST font loader built on `ableem::Font` (SDL_FontCache itself is now in lib_ableem). |
| `core/model/ps_game.*` | `PsGame : ableem::GameRecord` | Game as seen by the UI (from DB via `PsGame::fromRecords`, or playlist). `PsGamePtr = shared_ptr<PsGame>`. Adds the RetroArch/App fields. A plain data record - the resume points are `ResumePointService`'s, the memcard `MemcardService`'s. |
| `core/services/game_catalog.*` | `GameCatalogService` | The writes: play history ranking, game delete, cover flush. Owned by `App` (`app.gameCatalog()`). |
| `core/services/resume_point.*` | `ResumePointService` | The save-state slots in a game's `!SaveStates` folder, and the prepare/save around a PCSX launch. Owned by `App` (`app.resumePoints()`); non-screens reach it via `App::get()`. |
| `core/services/memcard.*` | `MemcardService` | The `!MemCards` sets and a game's chosen card; the swap in/out around a launch. Owned by `App` (`app.memcards()`). |
| `core/services/game_settings.*` | `GameSettingsService` | The game editor's model: a game's Game.ini flags and pcsx.cfg values, read with `open()` and written one setter per option. A game with its own config (`PcsxConfig`) is read-only until `unlock()`. Owned by `App` (`app.gameSettings()`). The editor's Display rows are pcsx-abnxt's in-game Picture rows with its keys (2026-09-29): `gpu_neon.enhancement_enable` (Resolution 1x/2x) and `enhancement_no_seams`, `dithering2` (0/1/2 - not the classic `gpu_peops.iUseDither`), `soft_filter`, `plat_target.hwfilter` (0..6, pcsx-abnxt's `ab_filter_names`), `scanlines` (0..3), `scanline_level`. What a platform offers is data here, keyed by `Env::platformName()`: `filtersFor` (all seven everywhere today), `smoothingsFor` (no HQ2x/HQ3x on `psc`), `neonGpuFor` (`psc`, `rpi`). |
| `core/services/game_query.*` | `GameQueryService` | Which games a set shows and in what order - `gamesFor(selection)` is the whole of the old `switchSet` query. Owned by `App` (`app.gameQuery()`); RetroArch arrives through the `RetroArchGames` interface. |
| `core/services/retroarch.*` | `RetroArchService` | RetroArch's playlists as sets of foreign `PsGame`s: `.lpl` parsing (both formats via `ableem::RetroArchPlaylist`), the core for an entry from its `ableem::CoreInfoTable` (`info/*.info` + `platform/<platform>.cores.cfg`, `coresCfgPath()`), Favorites/History, `reloadPlaylists()` after the scan rewrote them, and `ensureMetadata()` - publisher/year/players from `<rdb dir>/<playlist>.rdb` by label, read once per playlist on first use and dropped again (Favorites/History copy from the source playlist). Implements `RetroArchGames`. Owned by `App` (`app.retroArch()`). |
| `core/services/launch.*`, `process_runner.*` | `LaunchService`, `ProcessRunner` | A game launch start to finish: argv for `rc/launch.sh` (PCSX) / `rc/launch_rb.sh` (RetroArch) / an App's `startup`, the memcard and resume-point work around it, the RetroArch config transfer, `writeSelectionScript()`. `recordLastPlayed()` writes the last-played time only when `Env::clockIsSet()` (2026-09-26: the console has no battery clock). Runs through a `ProcessRunner`. Owned by `App` (`app.launcher()`). |

## Tests

The core test harness (doctest, `ctest`), used by the launcher's `make_win.sh` and by any suite added here:

- **Tests**: `tests/` builds two doctest executables against `ab_core` and runs under `ctest`
  (`ctest --test-dir build_win --output-on-failure`; `make_win.sh` does it for you). `AB_BUILD_TESTS=OFF`
  skips them, and both cross toolchain files force that. Every service extracted from a screen from here on
  ships with its tests in the same commit (the refactor plan's rule, kept after the plan itself was done).
  - `tests/support/env_fixture.h` - **use it in any test that touches a path.** `ableem::Environment`'s
    setters are static, so without it tests inherit each other's roots and pass or fail by run order.
  - `tests/support/temp_dir.h` - a scratch tree that deletes itself; `makeSubDir`/`writeFile`/`readFile`. Named
    with the pid and a counter, so suites run in parallel (`ctest -j`, the CI) cannot touch each other.
  - Add a suite with `ab_add_test(<name> core/<file>.cpp)` in `tests/CMakeLists.txt`. Tests include app
    headers from `src/code`, e.g. `#include "core/services/config.h"`.
  - The test exes need `C:\msys64\ucrt64\bin` on PATH to run directly (ctest inherits it from the
    MSYS2 login shell; running one from another shell exits 127 without it).
