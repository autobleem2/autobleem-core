// ABLEEM_RENDER_GL: the SDL_Renderer calls of the drawing code (renderer.cpp, texture.cpp, font.cpp, perf_overlay.cpp)
// go through render_backend.cpp, which draws through a loadable renderer backend (render_backend.h, ab_render_gles3)
// when one was made, and hands every call to SDL as before when not (the fallback: no backend library, no GL context).
// So the Renderer's logic - the 4:3 frame target, captures, the frame cache, the CRT margin - stays one piece of code
// for both, and a build without ABLEEM_RENDER_GL is exactly what it was: this header is not included there.
//
// With a backend the SDL_Renderer* is the backend's stand-in and every SDL_Texture* one of its textures: nothing else
// may hand them to SDL. The macros below rename the calls; render_backend.cpp defines ABLEEM_GL_SHIM_IMPL to reach
// SDL's own.
#pragma once

#ifdef ABLEEM_RENDER_GL

#include <SDL2/SDL.h>
#include <string>

namespace ableem::gl {

// before the window is made: the flags it needs (SDL_WINDOW_OPENGL for a GL backend), with the GL context attributes
// set for the backend's first choice; 0 when the SDL_Renderer is to be used
Uint32 windowFlags();
// a backend draws (the SDL_Renderer was not made)
bool active();
// the backend's draw calls in the last presented frame (the performance overlay); 0 without a backend
unsigned lastFrameDrawCalls();
// the backends to try, in order: setPreference's list, else AB_RENDERER, else the platform's ("gles3"); "sdl" ends it
void setPreference(const std::string &commaList);

SDL_Renderer *CreateRenderer(SDL_Window *window, int index, Uint32 flags);
void DestroyRenderer(SDL_Renderer *r);
int GetRendererInfo(SDL_Renderer *r, SDL_RendererInfo *info);
int GetRendererOutputSize(SDL_Renderer *r, int *w, int *h);
void RenderPresent(SDL_Renderer *r);
int RenderClear(SDL_Renderer *r);
int SetRenderDrawColor(SDL_Renderer *r, Uint8 red, Uint8 green, Uint8 blue, Uint8 a);
int GetRenderDrawColor(SDL_Renderer *r, Uint8 *red, Uint8 *green, Uint8 *blue, Uint8 *a);
int SetRenderDrawBlendMode(SDL_Renderer *r, SDL_BlendMode mode);
int SetRenderTarget(SDL_Renderer *r, SDL_Texture *t);
SDL_Texture *GetRenderTarget(SDL_Renderer *r);
int RenderSetViewport(SDL_Renderer *r, const SDL_Rect *rect);
void RenderGetViewport(SDL_Renderer *r, SDL_Rect *rect);
int RenderSetClipRect(SDL_Renderer *r, const SDL_Rect *rect);
void RenderGetClipRect(SDL_Renderer *r, SDL_Rect *rect);
SDL_bool RenderIsClipEnabled(SDL_Renderer *r);
int RenderFillRect(SDL_Renderer *r, const SDL_Rect *rect);
int RenderFillRects(SDL_Renderer *r, const SDL_Rect *rects, int count);
int RenderDrawRect(SDL_Renderer *r, const SDL_Rect *rect);
int RenderDrawLine(SDL_Renderer *r, int x1, int y1, int x2, int y2);
int RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_Rect *dst);
int RenderCopyF(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_FRect *dst);
int RenderCopyEx(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_Rect *dst, double angle,
                 const SDL_Point *center, SDL_RendererFlip flip);
int RenderCopyExF(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_FRect *dst, double angle,
                  const SDL_FPoint *center, SDL_RendererFlip flip);
int RenderGeometry(SDL_Renderer *r, SDL_Texture *t, const SDL_Vertex *vertices, int count, const int *indices,
                   int indexCount);
// SDL 2.0.18 (the console's) takes the colours as ints, 2.0.20 on as SDL_Colors - the same four bytes each
#if SDL_VERSION_ATLEAST(2, 0, 20)
using GeometryColor = SDL_Color;
#else
using GeometryColor = int;
#endif
int RenderGeometryRaw(SDL_Renderer *r, SDL_Texture *t, const float *xy, int xyStride, const GeometryColor *color,
                      int colorStride, const float *uv, int uvStride, int count, const void *indices, int indexCount,
                      int indexSize);
int RenderReadPixels(SDL_Renderer *r, const SDL_Rect *rect, Uint32 format, void *pixels, int pitch);

SDL_Texture *CreateTexture(SDL_Renderer *r, Uint32 format, int access, int w, int h);
SDL_Texture *CreateTextureFromSurface(SDL_Renderer *r, SDL_Surface *surface);
void DestroyTexture(SDL_Texture *t);
int QueryTexture(SDL_Texture *t, Uint32 *format, int *access, int *w, int *h);
int UpdateTexture(SDL_Texture *t, const SDL_Rect *rect, const void *pixels, int pitch);
int LockTexture(SDL_Texture *t, const SDL_Rect *rect, void **pixels, int *pitch);
void UnlockTexture(SDL_Texture *t);
int SetTextureBlendMode(SDL_Texture *t, SDL_BlendMode mode);
int SetTextureColorMod(SDL_Texture *t, Uint8 red, Uint8 green, Uint8 blue);
int GetTextureColorMod(SDL_Texture *t, Uint8 *red, Uint8 *green, Uint8 *blue);
int SetTextureAlphaMod(SDL_Texture *t, Uint8 a);
int GetTextureAlphaMod(SDL_Texture *t, Uint8 *a);
// SDL_image's: decoded by SDL_image, uploaded through CreateTextureFromSurface
SDL_Texture *LoadTexture(SDL_Renderer *r, const char *file);
SDL_Texture *LoadTexture_RW(SDL_Renderer *r, SDL_RWops *src, int freesrc);

} // namespace ableem::gl

#ifndef ABLEEM_GL_SHIM_IMPL
#define SDL_CreateRenderer ableem::gl::CreateRenderer
#define SDL_DestroyRenderer ableem::gl::DestroyRenderer
#define SDL_GetRendererInfo ableem::gl::GetRendererInfo
#define SDL_GetRendererOutputSize ableem::gl::GetRendererOutputSize
#define SDL_RenderPresent ableem::gl::RenderPresent
#define SDL_RenderClear ableem::gl::RenderClear
#define SDL_SetRenderDrawColor ableem::gl::SetRenderDrawColor
#define SDL_GetRenderDrawColor ableem::gl::GetRenderDrawColor
#define SDL_SetRenderDrawBlendMode ableem::gl::SetRenderDrawBlendMode
#define SDL_SetRenderTarget ableem::gl::SetRenderTarget
#define SDL_GetRenderTarget ableem::gl::GetRenderTarget
#define SDL_RenderSetViewport ableem::gl::RenderSetViewport
#define SDL_RenderGetViewport ableem::gl::RenderGetViewport
#define SDL_RenderSetClipRect ableem::gl::RenderSetClipRect
#define SDL_RenderGetClipRect ableem::gl::RenderGetClipRect
#define SDL_RenderIsClipEnabled ableem::gl::RenderIsClipEnabled
#define SDL_RenderFillRect ableem::gl::RenderFillRect
#define SDL_RenderFillRects ableem::gl::RenderFillRects
#define SDL_RenderDrawRect ableem::gl::RenderDrawRect
#define SDL_RenderDrawLine ableem::gl::RenderDrawLine
#define SDL_RenderCopy ableem::gl::RenderCopy
#define SDL_RenderCopyF ableem::gl::RenderCopyF
#define SDL_RenderCopyEx ableem::gl::RenderCopyEx
#define SDL_RenderCopyExF ableem::gl::RenderCopyExF
#define SDL_RenderGeometry ableem::gl::RenderGeometry
#define SDL_RenderGeometryRaw ableem::gl::RenderGeometryRaw
#define SDL_RenderReadPixels ableem::gl::RenderReadPixels
#define SDL_CreateTexture ableem::gl::CreateTexture
#define SDL_CreateTextureFromSurface ableem::gl::CreateTextureFromSurface
#define SDL_DestroyTexture ableem::gl::DestroyTexture
#define SDL_QueryTexture ableem::gl::QueryTexture
#define SDL_UpdateTexture ableem::gl::UpdateTexture
#define SDL_LockTexture ableem::gl::LockTexture
#define SDL_UnlockTexture ableem::gl::UnlockTexture
#define SDL_SetTextureBlendMode ableem::gl::SetTextureBlendMode
#define SDL_SetTextureColorMod ableem::gl::SetTextureColorMod
#define SDL_GetTextureColorMod ableem::gl::GetTextureColorMod
#define SDL_SetTextureAlphaMod ableem::gl::SetTextureAlphaMod
#define SDL_GetTextureAlphaMod ableem::gl::GetTextureAlphaMod
#define IMG_LoadTexture ableem::gl::LoadTexture
#define IMG_LoadTexture_RW ableem::gl::LoadTexture_RW
#endif

#endif // ABLEEM_RENDER_GL
