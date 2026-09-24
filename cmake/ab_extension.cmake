# ab_add_extension(<name> HOST <executable target> SOURCES <files...> [INI <extension.ini>] [ICON <png>]
#                  [LANG <dir>])
#
# An AutoBleem extension (docs/extensions-plan.md in the launcher): a plugin the launcher loads into its own
# process. It is built with the SDK's headers and definitions (ab_classic, ab_core, lib_ableem) but none of
# their code - the symbols are the launcher's, bound when it is loaded; linking the SDK in would give the
# plugin a second Gui, Env and Lang. On Windows it links the launcher's import library (the executable is
# built with ENABLE_EXPORTS); on Linux it links nothing of ours and dlopen binds it.
#
# Its logging goes through plog instance 1 (PLOG_DEFAULT_INSTANCE_ID=1), chained into the launcher's by
# AB_EXTENSION - see gui/extension.h.
#
# The result is staged as the folder the stick takes: <build>/extensions/<name>/ with extension.ini, the icon,
# lang/ and bin/<key>/<name>(.so|.dll), <key> being this build's platform key (Env::buildTargetKey()).
include_guard(GLOBAL)

# this build's platform key, as Env::buildTargetKey() spells it
function(ab_extension_platform_key out)
    set(key "${AB_TARGET}")
    if (key STREQUAL "rpi" AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(key "rpi64")
    endif()
    if (key STREQUAL "")
        set(key "dev")
    endif()
    set(${out} "${key}" PARENT_SCOPE)
endfunction()

function(ab_add_extension name)
    cmake_parse_arguments(EXT "" "HOST;INI;ICON;LANG" "SOURCES" ${ARGN})
    if (NOT EXT_HOST)
        message(FATAL_ERROR "ab_add_extension(${name}): HOST <the launcher's executable target> is required")
    endif()
    ab_extension_platform_key(key)
    set(stage "${CMAKE_BINARY_DIR}/extensions/${name}")

    add_library(${name} MODULE ${EXT_SOURCES})
    set_target_properties(${name} PROPERTIES
            PREFIX ""
            OUTPUT_NAME "${name}"
            LIBRARY_OUTPUT_DIRECTORY "${stage}/bin/${key}"
            RUNTIME_OUTPUT_DIRECTORY "${stage}/bin/${key}")
    foreach (sdk ab_classic ab_core ableem ableem_engine)
        target_include_directories(${name} PRIVATE $<TARGET_PROPERTY:${sdk},INTERFACE_INCLUDE_DIRECTORIES>)
        target_compile_definitions(${name} PRIVATE $<TARGET_PROPERTY:${sdk},INTERFACE_COMPILE_DEFINITIONS>)
    endforeach ()
    target_compile_definitions(${name} PRIVATE PLOG_DEFAULT_INSTANCE_ID=1)
    # Nothing of the plugin is visible to the others but the two AB_EXTENSION entry points (marked visible
    # there). Otherwise the dynamic linker merges what two plugins both define - a static in an inline or
    # template function is a GNU "unique" symbol, one per process - and plog's instance-1 logger was one
    # logger for every plugin, each chaining its tagged appender into it: every line logged once per
    # extension, under each one's tag. Hidden, each plugin keeps its own, as a Windows DLL always does.
    set_target_properties(${name} PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            C_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON)
    add_dependencies(${name} ${EXT_HOST})
    if (WIN32)
        target_link_libraries(${name} PRIVATE $<TARGET_LINKER_FILE:${EXT_HOST}>)
    endif ()

    if (EXT_INI)
        add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXT_INI}" "${stage}/extension.ini")
    endif ()
    if (EXT_ICON)
        get_filename_component(icon_name "${EXT_ICON}" NAME)
        add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXT_ICON}" "${stage}/${icon_name}")
    endif ()
    if (EXT_LANG)
        add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${EXT_LANG}" "${stage}/lang")
    endif ()
endfunction()
