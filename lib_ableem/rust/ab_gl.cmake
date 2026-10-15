# ab_gl - the launcher's GLES 3.0 renderer in Rust (rust/ab_gl, on glow) as a loadable renderer backend: cargo builds
# it as a shared library that is copied next to the programs as ab_render_gles3.so / .dll, and `ableem`'s Renderer
# loads it at start (src/ui/render_backend.cpp) with SDL_Renderer as the fallback (ABLEEM_RENDER_GL).
#
# OFF (the default): the Renderer is SDL_Renderer only, built exactly as before. ON needs the Rust text engine
# (ABLEEM_TEXT_RUST, whose atlas pages become backend textures) and takes its Rust target from rust/ab_text.cmake.
# Crates: Cargo.lock is committed and cargo runs --locked, as for ab_text.

option(ABLEEM_RENDER_GL "Draw through the loadable GLES 3 renderer (rust/ab_gl) with SDL_Renderer as the fallback" OFF)

if (ABLEEM_RENDER_GL)
    if (NOT ABLEEM_TEXT_RUST)
        message(FATAL_ERROR "ABLEEM_RENDER_GL needs the Rust text engine (ABLEEM_TEXT_RUST)")
    endif()

    set(_abg_dir "${CMAKE_CURRENT_SOURCE_DIR}/rust/ab_gl")
    set(_abg_out "${CMAKE_CURRENT_BINARY_DIR}/ab_gl-target")
    set(_abg_args build --release --locked --manifest-path "${_abg_dir}/Cargo.toml")
    set(_abg_env "CARGO_TARGET_DIR=${_abg_out}")
    if (ABLEEM_RUST_TARGET)
        list(APPEND _abg_args --target ${ABLEEM_RUST_TARGET})
        set(_abg_rel "${_abg_out}/${ABLEEM_RUST_TARGET}/release")
        # a shared library is linked by cargo (ab_text's static one never is): with this build's C compiler, as
        # the host's cc knows nothing of the target
        string(TOUPPER "${ABLEEM_RUST_TARGET}" _abg_triple)
        string(REPLACE "-" "_" _abg_triple "${_abg_triple}")
        list(APPEND _abg_env "CARGO_TARGET_${_abg_triple}_LINKER=${CMAKE_C_COMPILER}")
        if (CMAKE_SYSROOT)
            list(APPEND _abg_env "CARGO_TARGET_${_abg_triple}_RUSTFLAGS=-C link-arg=--sysroot=${CMAKE_SYSROOT}")
        endif()
    else()
        set(_abg_rel "${_abg_out}/release")
    endif()
    if (WIN32)
        set(_abg_built "${_abg_rel}/ab_gl.dll")
        set(_abg_name ab_render_gles3.dll)
    else()
        set(_abg_built "${_abg_rel}/libab_gl.so")
        set(_abg_name ab_render_gles3.so)
    endif()
    # next to the programs (where the Renderer looks first: SDL_GetBasePath), and the path for an installer step
    if (CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(_abg_dest "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    else()
        set(_abg_dest "${CMAKE_BINARY_DIR}")
    endif()
    set(ABLEEM_RENDER_GL_LIBRARY "${_abg_dest}/${_abg_name}" CACHE INTERNAL "the built GLES 3 renderer backend")

    file(GLOB _abg_sources "${_abg_dir}/src/*.rs")
    add_custom_command(OUTPUT "${ABLEEM_RENDER_GL_LIBRARY}"
            COMMAND ${CMAKE_COMMAND} -E env ${_abg_env} "${ABLEEM_CARGO}" ${_abg_args}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_abg_dest}"
            COMMAND ${CMAKE_COMMAND} -E copy "${_abg_built}" "${ABLEEM_RENDER_GL_LIBRARY}"
            DEPENDS ${_abg_sources} "${_abg_dir}/Cargo.toml" "${_abg_dir}/Cargo.lock"
            COMMENT "cargo: ab_gl ${ABLEEM_RUST_TARGET} -> ${_abg_name}"
            VERBATIM)
    add_custom_target(ab_gl_cargo ALL DEPENDS "${ABLEEM_RENDER_GL_LIBRARY}")
    message(STATUS "ab_gl: loadable GLES 3 renderer ON (${_abg_name}, target ${ABLEEM_RUST_TARGET})")
else()
    message(STATUS "ab_gl: loadable GLES 3 renderer OFF - SDL_Renderer only")
endif()
