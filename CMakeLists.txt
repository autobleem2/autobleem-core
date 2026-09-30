# autobleem-core: the AutoBleem C++ SDK - the portable lib_ableem, the SDL-free model+services (ab_core),
# and the classic-UI framework (ab_classic). Consumed as a git submodule by the launcher and the tools.
# This CMakeLists builds the three libraries + the unit tests standalone (a host/dev build); it is the split
# of the launcher's root CMakeLists ab_core/ab_classic definitions.
cmake_minimum_required(VERSION 3.12)
project(autobleem-core)

# ab_add_extension(): how an extension (a plugin of the launcher) is built - docs/extensions-plan.md
include(${CMAKE_CURRENT_LIST_DIR}/cmake/ab_extension.cmake)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(AB_ENABLE_CHD "Enable CHD image support (builds the vendored libchdr)" ON)
option(AB_ONLINE_UPDATE "Build the online update check and download" ON)
option(AB_BUILD_TESTS "Build the ab_core unit tests" ON)
# OFF builds the SDL-free half only: ableem_engine + ab_core, no ableem (SDL) and no ab_classic. A consumer
# that needs the services but not the UI (the PC tools, cross-compiled with llvm-mingw and no UCRT SDL2)
# sets this OFF so no SDL is required.
option(AB_CORE_UI "Build the classic UI (ab_classic + the SDL ableem lib)" ON)

# AB_TARGET: the same switch the launcher's own root CMakeLists.txt has (psc | rpi | pcusb | win | dev),
# ported here 2026-09-23 so a cross build works through this submodule too - it had always been hardcoded
# to AB_PLATFORM_DEV, which is correct for a standalone host build but wrong the moment a CONSUMER cross-
# compiles against this submodule (autobleem-console-tools targeting psc). A CACHE variable, not a plain
# one, precisely because directory scope does NOT flow sideways in CMake: a consumer that add_subdirectory()s
# this submodule AND its own targets (both as siblings of its own top level) needs BOTH sides to agree on
# the platform, and a CACHE variable is the one thing genuinely global across that split - the consumer sets
# it (or passes -DAB_TARGET=psc on the command line) before add_subdirectory(autobleem-core), and its own
# top-level CMakeLists.txt reads the SAME variable for its own add_compile_definitions()/CPU-flags calls
# (see autobleem-console-tools/CMakeLists.txt). Empty (the default): psc when cross-compiling for ARM/
# AArch64, dev otherwise - exactly root's own auto-detect.
set(AB_TARGET "" CACHE STRING "psc | rpi | pcusb | win | dev (empty: psc on ARM, dev elsewhere)")
if (AB_TARGET STREQUAL "")
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^arm" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        set(AB_TARGET "psc")
    else()
        set(AB_TARGET "dev")
    endif()
endif()
set(_ab_core_targets psc rpi pcusb win dev)
if (NOT AB_TARGET IN_LIST _ab_core_targets)
    message(FATAL_ERROR "AB_TARGET must be one of: ${_ab_core_targets} (got '${AB_TARGET}')")
endif()
string(TOUPPER "${AB_TARGET}" _ab_core_target_upper)
message(STATUS "autobleem-core: building for target ${AB_TARGET} (AB_PLATFORM_${_ab_core_target_upper})")
add_compile_definitions(AB_PLATFORM_${_ab_core_target_upper})

# the console too since 2026-09-23: it checks only when it has a network (the AutoBleem kernel's WiFi)
if (AB_ONLINE_UPDATE)
    add_compile_definitions(AB_ONLINE_UPDATE)
endif()
if (AB_TARGET STREQUAL "psc" OR AB_TARGET STREQUAL "rpi" OR AB_TARGET STREQUAL "pcusb")
    set(ABLEEM_EMBEDDED_TARGET ON CACHE BOOL "" FORCE)
endif()

# MSYS2 UCRT64 packaging regression (seen from mingw-w64-ucrt-x86_64-gcc 16.2.0-3, 2026-09): the gcc driver's
# own specs only add -D_UCRT when -mcrtdll=ucrt is passed explicitly (`gcc -dumpspecs`'s `*cpp:` rule) - there
# is no `self_spec` defaulting that in for the ucrt64 environment any more, so mingw-w64's <cstdlib> (which
# gates at_quick_exit/quick_exit on _UCRT) fails to declare them on a from-scratch build: "'at_quick_exit' has
# not been declared in '::'". An already-configured build dir keeps its object files and never retriggers this,
# which is why it looked intermittent. Scoped to exactly that toolchain - MinGW GNU gcc whose binary resolves
# under a ucrt64 tree - so llvm-mingw (Clang, no `MSYS2`/`ucrt64` path) and Debian's mingw-w64 gcc-posix cross
# compiler (no ucrt64 path either) are untouched; harmless again once MSYS2 fixes the packaging, since the
# compiler would then define it on its own. add_compile_definitions() (not CMAKE_CXX_FLAGS) because the root
# launcher CMakeLists' Debug branch overwrites CMAKE_CXX_FLAGS wholesale further down its own file.
if (MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    get_filename_component(_ab_core_cxx_compiler_real "${CMAKE_CXX_COMPILER}" REALPATH)
    if (_ab_core_cxx_compiler_real MATCHES "/ucrt64/" OR "$ENV{MSYSTEM}" STREQUAL "UCRT64")
        message(STATUS "autobleem-core: MSYS2 UCRT64 gcc without a default -D_UCRT detected - defining it (see CMakeLists.txt comment)")
        add_compile_definitions(_UCRT)
    endif()
endif()

find_package(Threads REQUIRED)

set(ABLEEM_ENABLE_CHD ${AB_ENABLE_CHD} CACHE BOOL "" FORCE)
# AB_CORE_UI OFF -> lib_ableem builds engine only (no SDL); the demo is never part of a core build
set(ABLEEM_BUILD_UI ${AB_CORE_UI} CACHE BOOL "" FORCE)
set(ABLEEM_BUILD_DEMO OFF CACHE BOOL "" FORCE)
add_subdirectory(lib_ableem)
if (NOT AB_ENABLE_CHD)
    add_compile_definitions(AB_NO_CHD)
endif()

# ---- ab_core: the SDL-free model + services ----
add_library(ab_core STATIC
        src/code/core/main.h
        src/code/core/model/cover_light.h
        src/code/core/model/game_set.h
        src/code/core/model/ps_game.cpp
        src/code/core/model/session.h
        src/code/core/model/timing.h
        src/code/core/services/app_manifest.cpp
        src/code/core/services/content_installer.cpp
        src/code/core/services/disc_reader.cpp
        src/code/core/services/downloader.cpp
        src/code/core/services/extension_catalog.cpp
        src/code/core/services/processor_catalog.cpp
        src/code/core/services/processor_runner.cpp
        src/code/core/services/processor_sequences.cpp
        src/code/core/services/processor_state.cpp
        src/code/core/services/plugin_loader.cpp
        src/code/core/services/config.cpp
        src/code/core/services/cover_aspect.cpp
        src/code/core/services/environment.cpp
        src/code/core/services/environment_setup.cpp
        src/code/core/services/windows_host.cpp
        src/code/core/services/clock.cpp
        src/code/core/services/output_mode.cpp
        src/code/core/services/game_catalog.cpp
        src/code/core/services/game_query.cpp
        src/code/core/services/game_settings.cpp
        src/code/core/services/pcsx_config.cpp
        src/code/core/services/lightgun.cpp
        src/code/core/services/launch.cpp
        src/code/core/services/memcard.cpp
        src/code/core/services/platform_config.cpp
        src/code/core/services/process_runner.cpp
        src/code/core/services/resume_point.cpp
        src/code/core/services/retroarch.cpp
        src/code/core/services/scan_service.cpp
        src/code/core/services/online_assets.cpp
        src/code/core/services/pad_battery.cpp
        src/code/core/services/system.cpp
        src/code/core/services/system_info.cpp
        src/code/core/services/theme.cpp
        src/code/core/services/theme_converter.cpp
        src/code/core/services/theme_installer.cpp)
target_link_libraries(ab_core ableem_engine ${CMAKE_DL_LIBS}) # dlopen: PluginLoader
target_include_directories(ab_core PUBLIC src/code ${CMAKE_BINARY_DIR}/generated)
if (AB_ONLINE_UPDATE)
    target_sources(ab_core PRIVATE src/code/core/services/update_service.cpp)
endif()
if (WIN32)
    target_link_libraries(ab_core iphlpapi ws2_32 shell32 ole32 advapi32)
endif()

# core/version.h, regenerated from git on every build
set(AB_VERSION_FALLBACK "v2.0.0-pre0")
# CMAKE_CURRENT_SOURCE_DIR, not CMAKE_SOURCE_DIR, so this works whether core is the top-level project or a
# submodule of a consumer (launcher/tools): the generator script and version.h.in are core's own.
# the version is the consumer's: git describe of the top-level project (the launcher, a tool repo), or of
# this repository when it is built on its own
set(AB_VERSION_SOURCE_DIR "${CMAKE_SOURCE_DIR}" CACHE PATH "the repository whose git facts are the version")
add_custom_target(ab_version
        COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DBINARY_DIR=${CMAKE_BINARY_DIR}
                -DGIT_SOURCE_DIR=${AB_VERSION_SOURCE_DIR}
                -DVERSION_FALLBACK=${AB_VERSION_FALLBACK} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_version.cmake
        BYPRODUCTS ${CMAKE_BINARY_DIR}/generated/core/version.h
        COMMENT "core/version.h from git")
add_dependencies(ab_core ab_version)

# ab_installer: AutoBleem onto a PlayStation Classic stick (InstallerJob - unpack the stick package keeping
# everything of the user's, the legacy-layout conversion, the ROM folders, the site's packs) and the Windows
# product's data tree (WindowsInstallJob). SDL-free, ab_core only, with the download behind a Downloader the
# program supplies - WinINet in autobleem-pc-tools' installers, a shell command on the console's own updater
# (the launcher). Moved here from autobleem-pc-tools' installer_core (2026-09-23) so both use one copy.
# FlasherJob too: the PC stick's image onto a disk the program supplies (autobleem-pc-tools' flasher).
add_library(ab_installer STATIC
        src/code/installer/install_job_base.cpp
        src/code/installer/installer_job.cpp
        src/code/installer/legacy_layout.cpp
        src/code/installer/windows_install_job.cpp
        src/code/installer/flasher_job.cpp
        src/code/installer/command_downloader.cpp)
target_link_libraries(ab_installer ab_core)

# ---- ab_gui: the AutoBleem User Interface Library (docs/ab-gui-plan.md) ---- (only with AB_CORE_UI)
# The look and its primitives (abgui::Style), the classic panel (abgui::Panel), what they draw with (abgui::Context),
# where a frame is presented (abgui::ScreenStack) -
# no AutoBleem code (no ab_core/ab_classic) and no SDL: it links the ableem ui library only. Headers as <ab_gui/...>.
if (AB_CORE_UI)
add_library(ab_gui STATIC
        ab_gui/include/ab_gui/actions.h
        ab_gui/include/ab_gui/action_menu.h
        ab_gui/include/ab_gui/busy.h
        ab_gui/include/ab_gui/confirm.h
        ab_gui/include/ab_gui/context.h
        ab_gui/include/ab_gui/facts_page.h
        ab_gui/include/ab_gui/footer_shorten.h
        ab_gui/include/ab_gui/frame.h
        ab_gui/include/ab_gui/hint_bar.h
        ab_gui/include/ab_gui/hold_repeat.h
        ab_gui/include/ab_gui/icon.h
        ab_gui/include/ab_gui/list.h
        ab_gui/include/ab_gui/keyboard.h
        ab_gui/include/ab_gui/list_model.h
        ab_gui/include/ab_gui/panel.h
        ab_gui/include/ab_gui/screen.h
        ab_gui/include/ab_gui/screen_stack.h
        ab_gui/include/ab_gui/spinner.h
        ab_gui/include/ab_gui/style.h
        ab_gui/include/ab_gui/text_page.h
        ab_gui/src/actions.cpp
        ab_gui/src/action_menu.cpp
        ab_gui/src/backdrop.cpp
        ab_gui/src/busy.cpp
        ab_gui/src/confirm.cpp
        ab_gui/src/context.cpp
        ab_gui/src/facts_page.cpp
        ab_gui/src/frame.cpp
        ab_gui/src/hint_bar.cpp
        ab_gui/src/icon.cpp
        ab_gui/src/list.cpp
        ab_gui/src/keyboard.cpp
        ab_gui/src/panel.cpp
        ab_gui/src/screen.cpp
        ab_gui/src/screen_stack.cpp
        ab_gui/src/spinner.cpp
        ab_gui/src/style.cpp
        ab_gui/src/text_page.cpp)
target_link_libraries(ab_gui ableem)
target_include_directories(ab_gui PUBLIC ab_gui/include)

# ---- ab_classic: the classic-UI framework (links ab_core + ableem + ab_gui) ---- (only with AB_CORE_UI)
add_library(ab_classic STATIC
        src/code/app_base.cpp
        src/code/gui/app_audio.cpp
        src/code/gui/extension_host_base.cpp
        src/code/gui/extension_runtime.cpp
        src/code/gui/gui.cpp
        src/code/gui/gui_font.cpp
        src/code/gui/screens/gui_about.cpp
        src/code/gui/screens/gui_facts_page.cpp
        src/code/gui/screens/gui_hardware_info.cpp
        src/code/gui/screens/gui_keyboard.cpp
        src/code/gui/screens/gui_splash.cpp
        src/code/gui/screens/gui_text_page.cpp
        src/code/gui/starfx.cpp
        src/code/gui/surprise_game.cpp
        src/code/gui/panel_style.cpp
        src/code/gui/text_renderer.cpp
        src/code/gui/theme_assets.cpp)
target_link_libraries(ab_classic ab_core ableem ab_gui)
target_include_directories(ab_classic PUBLIC src/code)
endif() # AB_CORE_UI

if (AB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
