# ab_text - the text engine in Rust (rust/ab_text): cargo builds it as a static library for the target of this
# CMake build, and `ableem` links it (ABLEEM_TEXT_RUST). Where there is no cargo - a bare toolchain, the
# make_psc.sh build on the Sony-toolchain server, a tool repository's own build - ABLEEM_TEXT_RUST is off and
# Font draws through SDL_FontCache as before.
#
# The Rust target comes from the C compiler's triple (the toolchain files name it), or ABLEEM_RUST_TARGET:
#   armv8-sony-linux-gnueabihf (the console), arm-linux-gnueabihf (Pi) -> thumbv7neon-unknown-linux-gnueabihf
#   aarch64-linux-gnu (Pi 64)                                          -> aarch64-unknown-linux-gnu
#   i686-linux-gnu (the PC stick: no SSE2, so i586's std)              -> i586-unknown-linux-gnu
#   x86_64-w64-mingw32 (Windows)                                       -> x86_64-pc-windows-gnu
#   a native build                                                     -> cargo's host target
# Crates: Cargo.lock is committed and cargo runs --locked; CARGO_HOME is the crates cache (docker/run.sh mounts
# one per host).

find_program(ABLEEM_CARGO cargo HINTS "$ENV{CARGO_HOME}/bin")
# a native MinGW build (MSYS2's UCRT64, make_win.sh) stays on SDL_FontCache unless asked: Rust's windows-gnu std
# is built against msvcrt, not the UCRT
if (ABLEEM_CARGO AND NOT (MINGW AND NOT CMAKE_CROSSCOMPILING))
    set(_abt_default ON)
else()
    set(_abt_default OFF)
endif()
option(ABLEEM_TEXT_RUST "Draw text with the Rust engine (rust/ab_text, needs cargo); OFF = SDL_FontCache" ${_abt_default})

if (ABLEEM_TEXT_RUST)
    if (NOT ABLEEM_CARGO)
        message(FATAL_ERROR "ABLEEM_TEXT_RUST is ON but no cargo was found (set CARGO_HOME or turn it OFF)")
    endif()
    if (NOT ABLEEM_RUST_TARGET AND CMAKE_CROSSCOMPILING)
        get_filename_component(_abt_cc "${CMAKE_C_COMPILER}" NAME)
        if (_abt_cc MATCHES "^(armv8-sony-linux-gnueabihf|arm-linux-gnueabihf)-")
            set(ABLEEM_RUST_TARGET thumbv7neon-unknown-linux-gnueabihf)
        elseif (_abt_cc MATCHES "^aarch64-linux-gnu-")
            set(ABLEEM_RUST_TARGET aarch64-unknown-linux-gnu)
        elseif (_abt_cc MATCHES "^i686-linux-gnu-")
            set(ABLEEM_RUST_TARGET i586-unknown-linux-gnu)
        elseif (_abt_cc MATCHES "^x86_64-w64-mingw32-")
            set(ABLEEM_RUST_TARGET x86_64-pc-windows-gnu)
        else()
            message(FATAL_ERROR "ab_text: no Rust target for the compiler ${_abt_cc} - set ABLEEM_RUST_TARGET")
        endif()
    endif()

    set(_abt_dir "${CMAKE_CURRENT_SOURCE_DIR}/rust/ab_text")
    set(_abt_out "${CMAKE_CURRENT_BINARY_DIR}/ab_text-target")
    set(_abt_args build --release --locked --manifest-path "${_abt_dir}/Cargo.toml")
    if (ABLEEM_RUST_TARGET)
        list(APPEND _abt_args --target ${ABLEEM_RUST_TARGET})
        set(_abt_lib "${_abt_out}/${ABLEEM_RUST_TARGET}/release/libab_text.a")
    else()
        set(_abt_lib "${_abt_out}/release/libab_text.a")
    endif()
    file(GLOB _abt_sources "${_abt_dir}/src/*.rs")
    add_custom_command(OUTPUT "${_abt_lib}"
            COMMAND ${CMAKE_COMMAND} -E env "CARGO_TARGET_DIR=${_abt_out}" "${ABLEEM_CARGO}" ${_abt_args}
            DEPENDS ${_abt_sources} "${_abt_dir}/Cargo.toml" "${_abt_dir}/Cargo.lock"
            COMMENT "cargo: ab_text ${ABLEEM_RUST_TARGET}"
            VERBATIM)
    add_custom_target(ab_text_cargo DEPENDS "${_abt_lib}")

    add_library(ab_text STATIC IMPORTED GLOBAL)
    set_target_properties(ab_text PROPERTIES IMPORTED_LOCATION "${_abt_lib}"
            INTERFACE_INCLUDE_DIRECTORIES "${_abt_dir}/include")
    add_dependencies(ab_text ab_text_cargo)
    # what Rust's std needs from the system (cargo's --print native-static-libs, the libc/libgcc part left to
    # the C++ driver)
    if (WIN32)
        set_property(TARGET ab_text PROPERTY INTERFACE_LINK_LIBRARIES ws2_32 userenv bcrypt ntdll)
    else()
        set_property(TARGET ab_text PROPERTY INTERFACE_LINK_LIBRARIES pthread dl rt util m)
    endif()
    message(STATUS "ab_text: Rust text engine ON (${ABLEEM_CARGO}, target ${ABLEEM_RUST_TARGET})")
else()
    message(STATUS "ab_text: Rust text engine OFF - SDL_FontCache draws text")
endif()
