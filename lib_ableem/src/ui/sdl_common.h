#pragma once
// Internal-only header. Never installed, never reachable from include/ableem/*.h.

#define ABLEEM_BUILDING
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_ttf.h>
#include "SDL_FontCache.h"

// Compile-time target selection. ABLEEM_EMBEDDED_TARGET is defined by CMake for the PlayStation Classic and
// Raspberry Pi cross/native builds; everything else (Linux/Windows/Mac dev machines) is a "dev host": no
// cursor grab, keyboard-as-pad translation on by default, no forking a real emulator process.
#ifndef ABLEEM_EMBEDDED_TARGET
#define ABLEEM_DEV_HOST 1
#endif

namespace ableem {
// The joystick device indices the program sees (input.cpp): SDL's own, or - with AB_INPUT_ISOLATED - only the
// virtual pads', numbered from 0. sdlJoystickIndex() turns one into SDL's (-1 when there is no such device).
int visibleJoystickCount();
int sdlJoystickIndex(int visibleIndex);
} // namespace ableem

// BlendMode::Premultiplied: colour = src + dst * (1 - srcA), alpha the same - "over" for premultiplied
// colours. SDL before 2.0.6 has no custom blend modes; plain blending is the nearest it offers.
inline SDL_BlendMode premultipliedBlendMode() {
#if SDL_VERSION_ATLEAST(2, 0, 6)
    static const SDL_BlendMode mode =
        SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                   SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    return mode;
#else
    return SDL_BLENDMODE_BLEND;
#endif
}

// BlendMode::Mask: colour = dst, alpha = dst alpha * src alpha. Before SDL 2.0.6 the nearest is SDL_BLENDMODE_MOD
// (dst colour * src colour - a white mask leaves the picture alone).
inline SDL_BlendMode maskBlendMode() {
#if SDL_VERSION_ATLEAST(2, 0, 6)
    static const SDL_BlendMode mode =
        SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
                                   SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    return mode;
#else
    return SDL_BLENDMODE_MOD;
#endif
}
