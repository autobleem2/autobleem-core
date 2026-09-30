# Unit tests for ab_core. Host builds only - the ARM toolchain file forces AB_BUILD_TESTS off, because
# nothing here ships to the console.
#
# doctest is vendored as a single header (tests/third_party/doctest/doctest.h). It is the one piece of
# vendored code outside lib_ableem/third_party, which is right: it ends up in no binary we ship.
#
# Run with:  ctest --test-dir build_win --output-on-failure    (make_win.sh does this after a build)

# doctest_main, ab_test_support and ab_add_test() - in harness.cmake so the tool repos that take this one as
# a submodule run their own suites (tests/apps/ there) on the same harness
include(${CMAKE_CURRENT_SOURCE_DIR}/harness.cmake)

ab_add_test(test_cd_image core/test_cd_image.cpp)
ab_add_test(test_clock core/test_clock.cpp)
ab_add_test(test_output_mode core/test_output_mode.cpp)
ab_add_test(test_process_stats core/test_process_stats.cpp)
ab_add_test(test_config core/test_config.cpp)
ab_add_test(test_disc_suffix core/test_disc_suffix.cpp)
ab_add_test(test_keyboard core/test_keyboard.cpp)
ab_add_test(test_pbp_image core/test_pbp_image.cpp)
ab_add_test(test_environment_setup core/test_environment_setup.cpp)
ab_add_test(test_platform_config core/test_platform_config.cpp)
ab_add_test(test_app_manifest core/test_app_manifest.cpp)
ab_add_test(test_content_installer core/test_content_installer.cpp)
ab_add_test(test_store_catalog core/test_store_catalog.cpp)
ab_add_test(test_lan_server core/test_lan_server.cpp LIBS ableem_lanserver)
ab_add_test(test_lan_client core/test_lan_client.cpp LIBS ableem_lanserver)
ab_add_test(test_disc_reader core/test_disc_reader.cpp LIBS ableem_lanserver)
ab_add_test(test_downloader core/test_downloader.cpp)
ab_add_test(test_extension_catalog core/test_extension_catalog.cpp)
if (AB_CORE_UI)
    # the runtime is ab_classic's (it knows Extension), tested with in-process fake plugins
    ab_add_test(test_extension_runtime classic/test_extension_runtime.cpp LIBS ab_classic)
    ab_add_test(test_hold_repeat classic/test_hold_repeat.cpp LIBS ab_gui)
    # GuiTextPage's line layout (splitItem is pure; the class is ab_classic's)
    ab_add_test(test_text_page classic/test_text_page.cpp LIBS ab_classic)
    # UIREV-2: doHome()/doEnd()'s skip-past-a-heading fix, driven through a local model of GuiMenuBase's
    # selection math - the real class needs a live AppBase/Gui over a theme/resources tree this repo does not
    # ship (see the file's opening comment), the same reason no other classic-menu test builds one
    ab_add_test(test_menu_base_navigation classic/test_menu_base_navigation.cpp)
    # UIREV-3: PanelStyle::footer()'s icons-only fallback once even FONT_15_BOLD is too wide, driven through
    # a local model of widthAt() - the real class needs a live Gui/ThemeAssets over a theme/resources tree
    # this repo does not ship (see the file's opening comment), the same reason as the suite above
    # (the label shortening is ab_gui's footer_shorten.h since G2 - the header only, the link is for its path)
    ab_add_test(test_panel_style_footer classic/test_panel_style_footer.cpp LIBS ab_gui)
    # UIREV-29: the theme's style roles as PanelStyle::fromTheme resolves them (static - no Gui needed)
    ab_add_test(test_panel_style_roles classic/test_panel_style_roles.cpp LIBS ab_classic)
    # ab_gui's Style on its own (G2): the defaults, the colour roles from a plain ColorRoles block, the hint
    # protocol - all static or plain data, no renderer needed
    ab_add_test(test_ab_gui_style gui/test_ab_gui_style.cpp LIBS ab_gui)
    # ab_gui's Context (G3a): the input, the clock and the UI sounds - through a real headless GuiBase (skips
    # itself without a renderer, like test_busy_input)
    ab_add_test(test_ab_gui_context gui/test_ab_gui_context.cpp LIBS ab_gui)
    # ab_gui's Panel (G3b): the classic panel's geometry against the old Gui formulas (pure), and the Context's
    # backdrop and panel rect (a headless GuiBase; those cases skip themselves without a renderer)
    ab_add_test(test_ab_gui_panel gui/test_ab_gui_panel.cpp LIBS ab_gui)
    # ab_gui's ScreenStack (G3c): clear -> draw -> present, one present a frame, nesting (pure, on a recording
    # display), and on a headless GuiBase the frame count, the DebugDriver's frame copy and the busy capture
    # (those cases skip themselves without a renderer)
    ab_add_test(test_ab_gui_screen_stack gui/test_ab_gui_screen_stack.cpp LIBS ab_gui)
    # ab_gui's Action and ActionMap (G3f): the default map against today's pad and keyboard mapping, the Confirm/Back
    # swap, and HoldRepeat's pace after its move into ab_gui (all pure)
    ab_add_test(test_ab_gui_actions gui/test_ab_gui_actions.cpp LIBS ab_gui)
    # ab_gui's Screen (G3g): its loop through the ActionMap reaches the same old hooks in the same order as
    # ableem::GuiScreen's loop - every button, d-pad direction and key, held repeats, releases - on a headless GuiBase
    # (skips itself without a renderer)
    ab_add_test(test_ab_gui_screen gui/test_ab_gui_screen.cpp LIBS ab_gui)
    # ab_gui's TextPage (G3h): the wrapping, the item layout and the scroll rule (pure), and the events through a
    # headless GuiBase (those cases skip themselves without a renderer)
    ab_add_test(test_ab_gui_text_page gui/test_ab_gui_text_page.cpp LIBS ab_gui)
    # ab_gui's FactsPage (G3i): the rows, the paging and refresh rules, the value column and the elision (pure), and the
    # events through a headless GuiBase (those cases skip themselves without a renderer)
    ab_add_test(test_ab_gui_facts_page gui/test_ab_gui_facts_page.cpp LIBS ab_gui)
    # the LAN-safety gate, the auth compare and the grab framing - pure, but the header pulls in gui_base.h,
    # so it links the ui library the way ab_classic's tests do
    ab_add_test(test_debug_driver classic/test_debug_driver.cpp LIBS ableem)
    # padsim's words for the driver's virtual pads - pure
    ab_add_test(test_pad_script classic/test_pad_script.cpp LIBS ableem)
    # the virtual pads and SIGTERM through a real GuiBase (skips itself without a renderer or on SDL < 2.24)
    ab_add_test(test_virtual_pads classic/test_virtual_pads.cpp LIBS ableem)
    # Image::pixel over a small hand-built PNG - no window/renderer needed, IMG_Load decodes from the file
    ab_add_test(test_image classic/test_image.cpp LIBS ableem)
    # Platform's AB_HEADLESS policy - pure, no display needed
    ab_add_test(test_headless classic/test_headless.cpp LIBS ableem)
    # the Display row's list from a display's modes - pure, no display needed
    ab_add_test(test_display_modes classic/test_display_modes.cpp LIBS ableem)
    # CONSOLE-11: Input::flushInputEvents() driven with real SDL events through a real GuiBase - needs a
    # working renderer (see the file's opening comment; it skips itself when this environment has none), so
    # it links SDL directly, the one exception to "nothing SDL-shaped in a consumer" (see lib_ableem's own
    # CMakeLists.txt) - this suite tests ableem's own Input class, not a consumer of it.
    ab_add_test(test_input_flush classic/test_input_flush.cpp LIBS ableem)
    # CONSOLE-13: the busy rule - nothing held and no hold-repeat carried over a busy job - the same way
    ab_add_test(test_busy_input classic/test_busy_input.cpp LIBS ableem ab_gui)
    # ab_add_test() links its target with the plain (non-keyword) signature, so this must match it.
    # lib_ableem/CMakeLists.txt's own SDL2 imported target is directory-scoped there, not visible here -
    # this repeats its discovery for these two test targets.
    if (MINGW)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(AB_TEST_SDL REQUIRED IMPORTED_TARGET sdl2)
        target_link_libraries(test_input_flush PkgConfig::AB_TEST_SDL)
        target_link_libraries(test_busy_input PkgConfig::AB_TEST_SDL)
    else()
        find_package(SDL2 REQUIRED)
        target_link_libraries(test_input_flush SDL2)
        target_link_libraries(test_busy_input SDL2)
    endif()
endif()
ab_add_test(test_game_catalog core/test_game_catalog.cpp)
ab_add_test(test_game_database core/test_game_database.cpp)
ab_add_test(test_fat_dirty_flag core/test_fat_dirty_flag.cpp)
ab_add_test(test_filesystem core/test_filesystem.cpp)
ab_add_test(test_game_library core/test_game_library.cpp)
ab_add_test(test_game_query core/test_game_query.cpp)
ab_add_test(test_games_fingerprint core/test_games_fingerprint.cpp)
ab_add_test(test_processor_output core/test_processor_output.cpp)
ab_add_test(test_processors core/test_processors.cpp)
ab_add_test(test_processor_runner core/test_processor_runner.cpp)
ab_add_test(test_game_set core/test_game_set.cpp)
ab_add_test(test_pad_assignment core/test_pad_assignment.cpp)
ab_add_test(test_game_settings core/test_game_settings.cpp)
ab_add_test(test_lang core/test_lang.cpp)
ab_add_test(test_launch core/test_launch.cpp)
ab_add_test(test_lightgun core/test_lightgun.cpp)
ab_add_test(test_memcard core/test_memcard.cpp)
ab_add_test(test_metadata_lookup core/test_metadata_lookup.cpp)
ab_add_test(test_memcard_image core/test_memcard_image.cpp)
ab_add_test(test_rdb_reader core/test_rdb_reader.cpp)
ab_add_test(test_resume_point core/test_resume_point.cpp)
ab_add_test(test_retroarch core/test_retroarch.cpp)
ab_add_test(test_online_assets core/test_online_assets.cpp)
if (AB_ONLINE_UPDATE)
    ab_add_test(test_update_service core/test_update_service.cpp)
endif()
ab_add_test(test_retroarch_scanner core/test_retroarch_scanner.cpp)
ab_add_test(test_scan_service core/test_scan_service.cpp)
ab_add_test(test_quiet_stick core/test_quiet_stick.cpp)
ab_add_test(test_seven_zip core/test_seven_zip.cpp)
ab_add_test(test_xz_file core/test_xz_file.cpp)
ab_add_test(test_system_info core/test_system_info.cpp)
ab_add_test(test_pad_battery core/test_pad_battery.cpp)
ab_add_test(test_pad_battery_match core/test_pad_battery_match.cpp)
ab_add_test(test_system core/test_system.cpp)
ab_add_test(test_theme core/test_theme.cpp)
ab_add_test(test_thumbnail_lookup core/test_thumbnail_lookup.cpp)
ab_add_test(test_theme_converter core/test_theme_converter.cpp)
ab_add_test(test_theme_installer core/test_theme_installer.cpp)
ab_add_test(test_theme_spec core/test_theme_spec.cpp)
ab_add_test(test_zip_writer core/test_zip_writer.cpp)
ab_add_test(test_tar_archive core/test_tar_archive.cpp)
ab_add_test(test_update_catalog_packs core/test_update_catalog_packs.cpp)
ab_add_test(test_harness core/test_env_fixture.cpp)

# proc_helper: a scanner processor the tests script (support/proc_helper.cpp); the suites that run one get
# its path as AB_PROC_HELPER
add_executable(proc_helper support/proc_helper.cpp)
foreach(suite test_system test_scan_service)
    target_compile_definitions(${suite} PRIVATE AB_PROC_HELPER="$<TARGET_FILE:proc_helper>")
    add_dependencies(${suite} proc_helper)
endforeach()

# the tools' suites live with the tools: autobleem-console-tools (pscbios, abflashkit) and
# autobleem-pc-tools (updateroms) run them on this harness; the installer job is here (ab_installer)

# the stick installer and the Windows product's data tree (ab_installer)
ab_add_test(test_installer_core installer/test_installer_core.cpp LIBS ab_installer)
ab_add_test(test_windows_install_job installer/test_windows_install_job.cpp LIBS ab_installer)
ab_add_test(test_flasher_job installer/test_flasher_job.cpp LIBS ab_installer)
ab_add_test(test_command_downloader installer/test_command_downloader.cpp LIBS ab_installer)
