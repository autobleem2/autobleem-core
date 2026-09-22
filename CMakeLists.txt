# autobleem-core: the AutoBleem C++ SDK - the portable lib_ableem, the SDL-free model+services (ab_core),
# and the classic-UI framework (ab_classic). Consumed as a git submodule by the launcher and the tools.
# This CMakeLists builds the three libraries + the unit tests standalone (a host/dev build); it is the split
# of the launcher's root CMakeLists ab_core/ab_classic definitions.
cmake_minimum_required(VERSION 3.12)
project(autobleem-core)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(AB_ENABLE_CHD "Enable CHD image support (builds the vendored libchdr)" ON)
option(AB_ONLINE_UPDATE "Build the online update check and download" ON)
option(AB_BUILD_TESTS "Build the ab_core unit tests" ON)
# OFF builds the SDL-free half only: ableem_engine + ab_core, no ableem (SDL) and no ab_classic. A consumer
# that needs the services but not the UI (the PC tools, cross-compiled with llvm-mingw and no UCRT SDL2)
# sets this OFF so no SDL is required.
option(AB_CORE_UI "Build the classic UI (ab_classic + the SDL ableem lib)" ON)

# a standalone core build is a development/host build
add_compile_definitions(AB_PLATFORM_DEV)
if (AB_ONLINE_UPDATE)
    add_compile_definitions(AB_ONLINE_UPDATE)
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
        src/code/core/model/game_set.h
        src/code/core/model/ps_game.cpp
        src/code/core/model/session.h
        src/code/core/model/timing.h
        src/code/core/services/config.cpp
        src/code/core/services/environment.cpp
        src/code/core/services/environment_setup.cpp
        src/code/core/services/windows_host.cpp
        src/code/core/services/clock.cpp
        src/code/core/services/game_catalog.cpp
        src/code/core/services/game_query.cpp
        src/code/core/services/game_settings.cpp
        src/code/core/services/lightgun.cpp
        src/code/core/services/launch.cpp
        src/code/core/services/memcard.cpp
        src/code/core/services/platform_config.cpp
        src/code/core/services/process_runner.cpp
        src/code/core/services/resume_point.cpp
        src/code/core/services/retroarch.cpp
        src/code/core/services/scan_service.cpp
        src/code/core/services/online_assets.cpp
        src/code/core/services/system.cpp
        src/code/core/services/system_info.cpp
        src/code/core/services/theme.cpp
        src/code/core/services/theme_converter.cpp
        src/code/core/services/theme_installer.cpp)
target_link_libraries(ab_core ableem_engine)
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
add_custom_target(ab_version
        COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DBINARY_DIR=${CMAKE_BINARY_DIR}
                -DVERSION_FALLBACK=${AB_VERSION_FALLBACK} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_version.cmake
        BYPRODUCTS ${CMAKE_BINARY_DIR}/generated/core/version.h
        COMMENT "core/version.h from git")
add_dependencies(ab_core ab_version)

# ---- ab_classic: the classic-UI framework (links ab_core + ableem) ---- (only with AB_CORE_UI)
if (AB_CORE_UI)
add_library(ab_classic STATIC
        src/code/app_base.cpp
        src/code/gui/app_audio.cpp
        src/code/gui/gui.cpp
        src/code/gui/gui_font.cpp
        src/code/gui/screens/gui_about.cpp
        src/code/gui/screens/gui_confirm.cpp
        src/code/gui/screens/gui_action_menu.cpp
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
target_link_libraries(ab_classic ab_core ableem)
target_include_directories(ab_classic PUBLIC src/code)
endif() # AB_CORE_UI

if (AB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
