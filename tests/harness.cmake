# The unit-test harness, shared with the repos that consume this one as a submodule (the console tools, the
# PC tools): include(<core>/tests/harness.cmake) after add_subdirectory(<core>), then ab_add_test(...). Every
# path is resolved against this repo, not the includer's CMAKE_SOURCE_DIR.
#
#   doctest_main     doctest's main() (tests/doctest_main.cpp)
#   ab_test_support  EnvFixture (Environment's setters are global), TempDir, makeFakeGame, the tar/zip/rdb
#                    builders - include them as "support/<name>.h"
#   ab_add_test(<name> <sources...> [LIBS <targets...>]) - one executable per suite, each its own ctest test
#                    so a crash in one does not hide the others; links ab_core plus LIBS (a tool's *_core)
include_guard(GLOBAL)

set(AB_CORE_TESTS_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "")
get_filename_component(AB_CORE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(AB_CORE_ROOT "${AB_CORE_ROOT}" CACHE INTERNAL "")

add_library(doctest_main STATIC ${AB_CORE_TESTS_DIR}/doctest_main.cpp)
target_link_libraries(doctest_main ableem_engine)
target_include_directories(doctest_main PUBLIC ${AB_CORE_TESTS_DIR}/third_party)

add_library(ab_test_support STATIC ${AB_CORE_TESTS_DIR}/support/temp_dir.cpp ${AB_CORE_TESTS_DIR}/support/fake_game.cpp
        ${AB_CORE_TESTS_DIR}/support/tree_snapshot.cpp)
target_include_directories(ab_test_support PUBLIC ${AB_CORE_TESTS_DIR}
        # support/tar_builder.h gzips with the engine's own miniz (a private header of ableem_engine)
        ${AB_CORE_ROOT}/lib_ableem/third_party/miniz)
target_link_libraries(ab_test_support ableem_engine)

# ab_test_path_define(TARGET MACRO ABS_DIR [TAIL]) - MACRO expands to the run-time path of ABS_DIR[/TAIL]
# (support/ab_test_path.h: relative to the program's folder, which is the target's binary folder). The absolute
# path is not on the command line, so the compiler cache (sccache) keys the same in every clone.
function(ab_test_path_define target macro absdir)
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_BINARY_DIR}" "${absdir}")
    if (rel STREQUAL "")
        set(rel ".")
    endif()
    if (ARGC GREATER 3)
        set(rel "${rel}/${ARGV3}")
    endif()
    target_compile_definitions(${target} PRIVATE "${macro}=ab_test_path(\"${rel}\")")
endfunction()

function(ab_add_test name)
    cmake_parse_arguments(AB_TEST "" "" "LIBS" ${ARGN})
    add_executable(${name} ${AB_TEST_UNPARSED_ARGUMENTS})
    # tests include app headers as "core/services/config.h"
    target_include_directories(${name} PRIVATE ${AB_CORE_ROOT}/src/code)
    # tests/data: checked-in fixtures (the test disc images, see tests/data/generate_test_files.sh)
    ab_test_path_define(${name} AB_TEST_DATA_DIR "${AB_CORE_TESTS_DIR}/data")
    ab_test_path_define(${name} AB_RESOURCES_DIR "${AB_CORE_ROOT}/src/resources")
    target_compile_options(${name} PRIVATE "SHELL:-include support/ab_test_path.h")
    target_link_libraries(${name} doctest_main ab_test_support ab_core ${AB_TEST_LIBS})
    add_test(NAME ${name} COMMAND ${name})
endfunction()
