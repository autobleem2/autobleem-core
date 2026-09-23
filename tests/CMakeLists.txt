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
ab_add_test(test_config core/test_config.cpp)
ab_add_test(test_disc_suffix core/test_disc_suffix.cpp)
ab_add_test(test_environment_setup core/test_environment_setup.cpp)
ab_add_test(test_platform_config core/test_platform_config.cpp)
ab_add_test(test_game_catalog core/test_game_catalog.cpp)
ab_add_test(test_game_database core/test_game_database.cpp)
ab_add_test(test_fat_dirty_flag core/test_fat_dirty_flag.cpp)
ab_add_test(test_filesystem core/test_filesystem.cpp)
ab_add_test(test_game_library core/test_game_library.cpp)
ab_add_test(test_game_query core/test_game_query.cpp)
ab_add_test(test_games_fingerprint core/test_games_fingerprint.cpp)
ab_add_test(test_game_set core/test_game_set.cpp)
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
ab_add_test(test_seven_zip core/test_seven_zip.cpp)
ab_add_test(test_xz_file core/test_xz_file.cpp)
ab_add_test(test_system_info core/test_system_info.cpp)
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

# the tools' suites live with the tools: autobleem-console-tools (pscbios, abflashkit) and
# autobleem-pc-tools (updateroms) run them on this harness; the installer job is here (ab_installer)

# the stick installer and the Windows product's data tree (ab_installer)
ab_add_test(test_installer_core installer/test_installer_core.cpp LIBS ab_installer)
ab_add_test(test_windows_install_job installer/test_windows_install_job.cpp LIBS ab_installer)
ab_add_test(test_flasher_job installer/test_flasher_job.cpp LIBS ab_installer)
