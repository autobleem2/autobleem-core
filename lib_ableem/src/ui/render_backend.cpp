// ABLEEM_RENDER_GL: the loadable renderer backend (render_backend.h) and, over it, the SDL_Renderer calls the drawing
// code makes (gl_shim.h). SDL_CreateRenderer tries the backends in order - a shared library ab_render_<name>.so /
// .dll next to the program (SDL_GetBasePath), then on the library path - and makes SDL's own renderer when none
// loads or none gets a GL context: every call is then SDL's, as in a build without ABLEEM_RENDER_GL.
//
// Context ownership: this side makes the GL context on the window (the versions the backend's window_needs lists, in
// order), makes it current, swaps the buffers and deletes it; the backend only draws, through SDL_GL_GetProcAddress.
// The window was made for it (windowFlags(), platform.cpp): SDL_WINDOW_OPENGL and the first choice's attributes.
#define ABLEEM_GL_SHIM_IMPL
#include "gl_shim.h"
#include "render_backend.h"
#include "sdl_common.h"

#include <ableem/engine/log.h>

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace ableem::gl {
namespace {

// a backend texture behind an SDL_Texture*: SDL's state of it kept here (format, access, the mods, the blend mode)
struct GlTexture {
    AbrTexture *tex = nullptr;
    int w = 0, h = 0;
    Uint32 format = SDL_PIXELFORMAT_RGBA32;
    int access = SDL_TEXTUREACCESS_STATIC;
    Uint8 r = 255, g = 255, b = 255, a = 255;
    SDL_BlendMode blend = SDL_BLENDMODE_NONE;
    std::vector<unsigned char> stream; // a streaming texture's pixels, in `format` (LockTexture)
    SDL_Rect locked{};
};

// the backend drawing for the one renderer, and SDL_Renderer's state that lives on this side
struct Backend {
    std::string name;
    const AbrApi *api = nullptr;
    AbrContext *ctx = nullptr;
    SDL_Window *window = nullptr;
    SDL_GLContext context = nullptr;
    SDL_Color draw{0, 0, 0, 255};
    SDL_BlendMode drawBlend = SDL_BLENDMODE_NONE;
    GlTexture *target = nullptr;
    bool hasViewport = false;
    SDL_Rect viewport{};
    bool screenClipOn = false, targetClipOn = false; // SDL keeps the screen's clip apart from a target's
    SDL_Rect screenClip{}, targetClip{};
    bool inFrame = false;
    int outW = 0, outH = 0;
    unsigned lastDrawCalls = 0; // the last presented frame's (lastFrameDrawCalls)
};

Backend *g = nullptr; // the backend while one draws; its address is the SDL_Renderer* handed out
std::string preference;
bool preferenceSet = false;

bool isGl(SDL_Renderer *r) {
    return g != nullptr && r == reinterpret_cast<SDL_Renderer *>(g);
}
GlTexture *glTex(SDL_Texture *t) {
    return reinterpret_cast<GlTexture *>(t);
}

std::vector<std::string> backendNames() {
    std::string list = preference;
    if (!preferenceSet) {
        const char *env = getenv("AB_RENDERER");
        list = env && *env ? env : "gles3"; // the platform default, the same everywhere for now
    }
    std::vector<std::string> names;
    std::stringstream in(list);
    std::string item;
    while (std::getline(in, item, ',')) {
        if (item == "sdl")
            break; // SDL_Renderer from here on
        if (!item.empty())
            names.push_back(item);
    }
    return names;
}

// a backend library's table, loaded once and kept for the program's life; nullptr (logged once) when it cannot be
const AbrApi *loadBackend(const std::string &name) {
    struct Loaded {
        std::string name;
        const AbrApi *api;
    };
    static std::vector<Loaded> loaded;
    for (const Loaded &l : loaded)
        if (l.name == name)
            return l.api;
#ifdef _WIN32
    const std::string file = "ab_render_" + name + ".dll";
#else
    const std::string file = "ab_render_" + name + ".so";
#endif
    std::string why;
    void *entry = nullptr;
    char *base = SDL_GetBasePath();
    std::vector<std::string> paths;
    if (base) {
        paths.push_back(std::string(base) + file);
        SDL_free(base);
    }
    paths.push_back(file); // the library path
    for (const std::string &path : paths) {
#ifdef _WIN32
        HMODULE lib = LoadLibraryA(path.c_str());
        if (!lib) {
            why = path + ": error " + std::to_string(GetLastError());
            continue;
        }
        entry = reinterpret_cast<void *>(GetProcAddress(lib, ABR_EXPORT_NAME));
#else
        void *lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!lib) {
            const char *e = dlerror();
            why = e ? e : path;
            continue;
        }
        entry = dlsym(lib, ABR_EXPORT_NAME);
#endif
        if (!entry)
            why = path + ": no " ABR_EXPORT_NAME;
        break;
    }
    const AbrApi *api = nullptr;
    if (entry) {
        api = reinterpret_cast<AbrEntry>(entry)();
        // the entries up to `flush` are version 1's: a smaller table is not one this build can use
        if (api && api->size < offsetof(AbrApi, flush) + sizeof(api->flush)) {
            why = file + ": its table is too small (" + std::to_string(api->size) + " bytes)";
            api = nullptr;
        }
    }
    if (!api)
        PLOG_WARNING << "Renderer backend " << name << " cannot be loaded: " << why;
    loaded.push_back({name, api});
    return api;
}

AbrBlend toAbr(SDL_BlendMode m) {
    if (m == SDL_BLENDMODE_NONE)
        return ABR_BLEND_NONE;
    if (m == SDL_BLENDMODE_ADD)
        return ABR_BLEND_ADD;
    if (m == SDL_BLENDMODE_MOD)
        return ABR_BLEND_MOD;
    if (m == premultipliedBlendMode())
        return ABR_BLEND_PREMULTIPLIED;
    if (m == maskBlendMode())
        return ABR_BLEND_MASK;
    return ABR_BLEND_BLEND;
}

AbrColor color(SDL_Color c) {
    return AbrColor{c.r, c.g, c.b, c.a};
}

AbrColor tint(const GlTexture *t) {
    return AbrColor{t->r, t->g, t->b, t->a};
}

AbrRect toRect(const SDL_Rect &r) {
    return AbrRect{r.x, r.y, r.w, r.h};
}

AbrFRect frect(const SDL_Rect &r) {
    return AbrFRect{static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.w),
                    static_cast<float>(r.h)};
}

// the size of what is drawn into: the target, or the screen's viewport (SDL's output size is the target's or the
// window's)
void currentSize(int &w, int &h) {
    if (g->target) {
        w = g->target->w;
        h = g->target->h;
    } else if (g->hasViewport) {
        w = g->viewport.w;
        h = g->viewport.h;
    } else {
        w = g->outW;
        h = g->outH;
    }
}

void applyClip() {
    const bool on = g->target ? g->targetClipOn : g->screenClipOn;
    const AbrRect r = toRect(g->target ? g->targetClip : g->screenClip);
    g->api->set_clip(g->ctx, on ? &r : nullptr);
}

// a frame starts with the first call after a present: the backend is told the drawable's size, and the state SDL
// keeps across frames (viewport, target, clip) is set again
void frame() {
    if (g->inFrame)
        return;
    SDL_GL_GetDrawableSize(g->window, &g->outW, &g->outH);
    g->api->begin_frame(g->ctx, g->outW, g->outH);
    const AbrRect v = toRect(g->viewport);
    g->api->set_viewport(g->ctx, g->hasViewport ? &v : nullptr);
    if (g->target)
        g->api->set_target(g->ctx, g->target->tex);
    applyClip();
    g->inFrame = true;
}

bool nearestHint() {
    const char *q = SDL_GetHint(SDL_HINT_RENDER_SCALE_QUALITY);
    return !q || !*q || strcmp(q, "0") == 0 || SDL_strcasecmp(q, "nearest") == 0;
}

// `w` x `h` pixels of `format` as RGBA bytes, rows tight
const void *toRgba(Uint32 format, const void *pixels, int pitch, int w, int h, std::vector<unsigned char> &buffer) {
    if (format == SDL_PIXELFORMAT_RGBA32 && pitch == w * 4)
        return pixels;
    buffer.resize(static_cast<size_t>(w) * h * 4);
    if (SDL_ConvertPixels(w, h, format, pixels, pitch, SDL_PIXELFORMAT_RGBA32, buffer.data(), w * 4) != 0)
        return nullptr;
    return buffer.data();
}

GlTexture *makeTexture(int w, int h, Uint32 format, int access, const void *rgba, uint32_t flags) {
    AbrTexture *tex = g->api->texture_create(g->ctx, w, h, rgba, w * 4, flags);
    if (!tex) {
        SDL_SetError("the renderer backend could not make a %dx%d texture", w, h);
        return nullptr;
    }
    auto *t = new GlTexture();
    t->tex = tex;
    t->w = w;
    t->h = h;
    t->format = format;
    t->access = access;
    return t;
}

// the window's GL context for one of the backend's versions, current; nullptr with every reason in `why`
SDL_GLContext makeContext(SDL_Window *window, const AbrWindowNeeds &needs, std::string &why) {
    for (int i = 0; i < needs.count && i < 4; i++) {
        const AbrGlVersion &v = needs.versions[i];
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, v.profile);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, v.major);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, v.minor);
        SDL_GLContext c = SDL_GL_CreateContext(window);
        if (c && SDL_GL_MakeCurrent(window, c) == 0)
            return c;
        why += std::string(v.profile == ABR_GL_ES ? " ES " : " GL ") + std::to_string(v.major) + "." +
               std::to_string(v.minor) + ": " + SDL_GetError() + ";";
        if (c)
            SDL_GL_DeleteContext(c);
    }
    return nullptr;
}

void *SDLCALL getProc(const char *name) {
    return SDL_GL_GetProcAddress(name);
}

} // namespace

void setPreference(const std::string &commaList) {
    preference = commaList;
    preferenceSet = true;
}

bool active() {
    return g != nullptr;
}

unsigned lastFrameDrawCalls() {
    return g != nullptr ? g->lastDrawCalls : 0;
}

Uint32 windowFlags() {
    for (const std::string &name : backendNames()) {
        const AbrApi *api = loadBackend(name);
        if (!api)
            continue;
        AbrWindowNeeds needs{};
        needs.size = sizeof(needs);
        api->window_needs(&needs);
        if (!needs.opengl || needs.count <= 0)
            return 0;
        const AbrGlVersion &v = needs.versions[0];
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, v.profile);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, v.major);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, v.minor);
        return SDL_WINDOW_OPENGL;
    }
    return 0;
}

SDL_Renderer *CreateRenderer(SDL_Window *window, int index, Uint32 flags) {
    const bool glWindow = (SDL_GetWindowFlags(window) & SDL_WINDOW_OPENGL) != 0;
    for (const std::string &name : backendNames()) {
        const AbrApi *api = loadBackend(name);
        if (!api)
            continue;
        AbrWindowNeeds needs{};
        needs.size = sizeof(needs);
        api->window_needs(&needs);
        if (needs.opengl && !glWindow) {
            PLOG_WARNING << "Renderer backend " << name << " needs a GL window, this one is not";
            continue;
        }
        std::string why;
        SDL_GLContext context = needs.opengl ? makeContext(window, needs, why) : nullptr;
        if (needs.opengl && !context) {
            PLOG_WARNING << "Renderer backend " << name << ": no GL context -" << why;
            continue;
        }
        char err[256] = "";
        AbrContext *ctx = api->create(getProc, err, sizeof(err));
        if (!ctx) {
            PLOG_WARNING << "Renderer backend " << name << " failed: " << err;
            if (context)
                SDL_GL_DeleteContext(context);
            continue;
        }
        if (flags & SDL_RENDERER_PRESENTVSYNC)
            SDL_GL_SetSwapInterval(1);
        g = new Backend();
        g->name = name;
        g->api = api;
        g->ctx = ctx;
        g->window = window;
        g->context = context;
        char line[1024] = "";
        api->describe(ctx, line, sizeof(line));
        PLOG_INFO << "Renderer backend: " << name << " (" << line << ")";
        return reinterpret_cast<SDL_Renderer *>(g);
    }
    // SDL's own renderer, as without ABLEEM_RENDER_GL: SDL sets the context attributes its GL renderers need itself;
    // the window's multisampling stays as asked for
    int buffers = 0, samples = 0;
    SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &buffers);
    SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &samples);
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, buffers);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, samples);
    if (!backendNames().empty())
        PLOG_WARNING << "No renderer backend could draw - SDL_Renderer draws";
    return SDL_CreateRenderer(window, index, flags);
}

void DestroyRenderer(SDL_Renderer *r) {
    if (!isGl(r))
        return SDL_DestroyRenderer(r);
    g->api->destroy(g->ctx);
    if (g->context)
        SDL_GL_DeleteContext(g->context);
    delete g;
    g = nullptr;
}

int GetRendererInfo(SDL_Renderer *r, SDL_RendererInfo *info) {
    if (!isGl(r))
        return SDL_GetRendererInfo(r, info);
    SDL_zerop(info);
    static std::string name;
    name = g->name;
    info->name = name.c_str();
    info->flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE;
    info->num_texture_formats = 1;
    info->texture_formats[0] = SDL_PIXELFORMAT_RGBA32;
    return 0;
}

int GetRendererOutputSize(SDL_Renderer *r, int *w, int *h) {
    if (!isGl(r))
        return SDL_GetRendererOutputSize(r, w, h);
    int ow = 0, oh = 0;
    if (g->target) {
        ow = g->target->w;
        oh = g->target->h;
    } else {
        SDL_GL_GetDrawableSize(g->window, &ow, &oh);
    }
    if (w)
        *w = ow;
    if (h)
        *h = oh;
    return 0;
}

void RenderPresent(SDL_Renderer *r) {
    if (!isGl(r))
        return SDL_RenderPresent(r);
    frame();
    g->api->flush(g->ctx);
    g->lastDrawCalls = g->api->draw_calls(g->ctx);
    SDL_GL_SwapWindow(g->window);
    g->api->end_frame(g->ctx);
    g->inFrame = false;
    // AB_GL_STATS=1: the backend's line, once a second (a table from before `stats` has none)
    if (g->api->size >= offsetof(AbrApi, stats) + sizeof(g->api->stats)) {
        char line[1024];
        if (g->api->stats(g->ctx, line, sizeof(line)) > 0)
            PLOG_INFO << line;
    }
}

int RenderClear(SDL_Renderer *r) {
    if (!isGl(r))
        return SDL_RenderClear(r);
    frame();
    g->api->clear(g->ctx, color(g->draw));
    return 0;
}

int SetRenderDrawColor(SDL_Renderer *r, Uint8 red, Uint8 green, Uint8 blue, Uint8 a) {
    if (!isGl(r))
        return SDL_SetRenderDrawColor(r, red, green, blue, a);
    g->draw = SDL_Color{red, green, blue, a};
    return 0;
}

int GetRenderDrawColor(SDL_Renderer *r, Uint8 *red, Uint8 *green, Uint8 *blue, Uint8 *a) {
    if (!isGl(r))
        return SDL_GetRenderDrawColor(r, red, green, blue, a);
    if (red)
        *red = g->draw.r;
    if (green)
        *green = g->draw.g;
    if (blue)
        *blue = g->draw.b;
    if (a)
        *a = g->draw.a;
    return 0;
}

int SetRenderDrawBlendMode(SDL_Renderer *r, SDL_BlendMode mode) {
    if (!isGl(r))
        return SDL_SetRenderDrawBlendMode(r, mode);
    g->drawBlend = mode;
    return 0;
}

int SetRenderTarget(SDL_Renderer *r, SDL_Texture *t) {
    if (!isGl(r))
        return SDL_SetRenderTarget(r, t);
    GlTexture *target = glTex(t);
    if (target && target->access != SDL_TEXTUREACCESS_TARGET)
        return SDL_SetError("Texture not created with SDL_TEXTUREACCESS_TARGET");
    frame();
    if (!g->api->set_target(g->ctx, target ? target->tex : nullptr))
        return SDL_SetError("the renderer backend could not draw into the texture");
    g->target = target;
    g->targetClipOn = false; // a target starts unclipped; the screen gets its own clip back
    applyClip();
    return 0;
}

SDL_Texture *GetRenderTarget(SDL_Renderer *r) {
    if (!isGl(r))
        return SDL_GetRenderTarget(r);
    return reinterpret_cast<SDL_Texture *>(g->target);
}

int RenderSetViewport(SDL_Renderer *r, const SDL_Rect *rect) {
    if (!isGl(r))
        return SDL_RenderSetViewport(r, rect);
    if (g->target)
        return 0; // a target is always drawn whole (the Renderer sets the screen's viewport only)
    g->hasViewport = rect != nullptr;
    if (rect)
        g->viewport = *rect;
    if (g->inFrame) {
        const AbrRect v = rect ? AbrRect{rect->x, rect->y, rect->w, rect->h} : AbrRect{};
        g->api->set_viewport(g->ctx, rect ? &v : nullptr);
        applyClip();
    }
    return 0;
}

void RenderGetViewport(SDL_Renderer *r, SDL_Rect *rect) {
    if (!isGl(r))
        return SDL_RenderGetViewport(r, rect);
    if (!rect)
        return;
    if (g->target)
        *rect = SDL_Rect{0, 0, g->target->w, g->target->h};
    else if (g->hasViewport)
        *rect = SDL_Rect{0, 0, g->viewport.w, g->viewport.h}; // SDL answers the size, from 0,0 as drawing sees it
    else {
        int w = 0, h = 0;
        SDL_GL_GetDrawableSize(g->window, &w, &h);
        *rect = SDL_Rect{0, 0, w, h};
    }
}

int RenderSetClipRect(SDL_Renderer *r, const SDL_Rect *rect) {
    if (!isGl(r))
        return SDL_RenderSetClipRect(r, rect);
    bool &on = g->target ? g->targetClipOn : g->screenClipOn;
    SDL_Rect &clip = g->target ? g->targetClip : g->screenClip;
    on = rect != nullptr;
    clip = rect ? *rect : SDL_Rect{};
    if (g->inFrame)
        applyClip();
    return 0;
}

void RenderGetClipRect(SDL_Renderer *r, SDL_Rect *rect) {
    if (!isGl(r))
        return SDL_RenderGetClipRect(r, rect);
    if (rect)
        *rect = (g->target ? g->targetClipOn : g->screenClipOn) ? (g->target ? g->targetClip : g->screenClip)
                                                                 : SDL_Rect{};
}

SDL_bool RenderIsClipEnabled(SDL_Renderer *r) {
    if (!isGl(r))
        return SDL_RenderIsClipEnabled(r);
    return (g->target ? g->targetClipOn : g->screenClipOn) ? SDL_TRUE : SDL_FALSE;
}

int RenderFillRect(SDL_Renderer *r, const SDL_Rect *rect) {
    if (!isGl(r))
        return SDL_RenderFillRect(r, rect);
    frame();
    int w = 0, h = 0;
    currentSize(w, h);
    const AbrFRect f = rect ? frect(*rect) : AbrFRect{0, 0, static_cast<float>(w), static_cast<float>(h)};
    g->api->fill_rects(g->ctx, &f, 1, color(g->draw), toAbr(g->drawBlend));
    return 0;
}

int RenderFillRects(SDL_Renderer *r, const SDL_Rect *rects, int count) {
    if (!isGl(r))
        return SDL_RenderFillRects(r, rects, count);
    frame();
    std::vector<AbrFRect> f(static_cast<size_t>(count > 0 ? count : 0));
    for (int i = 0; i < count; i++)
        f[i] = frect(rects[i]);
    g->api->fill_rects(g->ctx, f.data(), count, color(g->draw), toAbr(g->drawBlend));
    return 0;
}

int RenderDrawRect(SDL_Renderer *r, const SDL_Rect *rect) {
    if (!isGl(r))
        return SDL_RenderDrawRect(r, rect);
    frame();
    int w = 0, h = 0;
    currentSize(w, h);
    const SDL_Rect o = rect ? *rect : SDL_Rect{0, 0, w, h};
    if (o.w <= 0 || o.h <= 0)
        return 0;
    // SDL's outline: the rect's outermost pixels, each once
    std::vector<AbrFRect> edges{{float(o.x), float(o.y), float(o.w), 1.0f}};
    if (o.h > 1)
        edges.push_back({float(o.x), float(o.y + o.h - 1), float(o.w), 1.0f});
    if (o.h > 2) {
        edges.push_back({float(o.x), float(o.y + 1), 1.0f, float(o.h - 2)});
        if (o.w > 1)
            edges.push_back({float(o.x + o.w - 1), float(o.y + 1), 1.0f, float(o.h - 2)});
    }
    g->api->fill_rects(g->ctx, edges.data(), static_cast<int32_t>(edges.size()), color(g->draw),
                       toAbr(g->drawBlend));
    return 0;
}

int RenderDrawLine(SDL_Renderer *r, int x1, int y1, int x2, int y2) {
    if (!isGl(r))
        return SDL_RenderDrawLine(r, x1, y1, x2, y2);
    frame();
    // pixels: the backend draws through their centres, both ends included
    const AbrPoint points[2] = {{float(x1), float(y1)}, {float(x2), float(y2)}};
    g->api->draw_lines(g->ctx, points, 1, color(g->draw), toAbr(g->drawBlend));
    return 0;
}

int RenderCopyExF(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_FRect *dst, double angle,
                  const SDL_FPoint *center, SDL_RendererFlip flip) {
    if (!isGl(r))
        return SDL_RenderCopyExF(r, t, src, dst, angle, center, flip);
    GlTexture *tex = glTex(t);
    if (!tex)
        return SDL_SetError("no texture");
    frame();
    const AbrFRect s = src ? frect(*src) : AbrFRect{};
    const AbrFRect d = dst ? AbrFRect{dst->x, dst->y, dst->w, dst->h} : AbrFRect{};
    const AbrPoint c = center ? AbrPoint{center->x, center->y} : AbrPoint{};
    g->api->copy(g->ctx, tex->tex, src ? &s : nullptr, dst ? &d : nullptr, tint(tex), toAbr(tex->blend), angle,
                 center ? &c : nullptr, static_cast<int32_t>(flip));
    return 0;
}

int RenderCopyEx(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_Rect *dst, double angle,
                 const SDL_Point *center, SDL_RendererFlip flip) {
    if (!isGl(r))
        return SDL_RenderCopyEx(r, t, src, dst, angle, center, flip);
    const SDL_FRect d = dst ? SDL_FRect{float(dst->x), float(dst->y), float(dst->w), float(dst->h)} : SDL_FRect{};
    const SDL_FPoint c = center ? SDL_FPoint{float(center->x), float(center->y)} : SDL_FPoint{};
    return RenderCopyExF(r, t, src, dst ? &d : nullptr, angle, center ? &c : nullptr, flip);
}

int RenderCopyF(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_FRect *dst) {
    if (!isGl(r))
        return SDL_RenderCopyF(r, t, src, dst);
    return RenderCopyExF(r, t, src, dst, 0.0, nullptr, SDL_FLIP_NONE);
}

int RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_Rect *dst) {
    if (!isGl(r))
        return SDL_RenderCopy(r, t, src, dst);
    return RenderCopyEx(r, t, src, dst, 0.0, nullptr, SDL_FLIP_NONE);
}

int RenderGeometryRaw(SDL_Renderer *r, SDL_Texture *t, const float *xy, int xyStride, const GeometryColor *color,
                      int colorStride, const float *uv, int uvStride, int count, const void *indices, int indexCount,
                      int indexSize) {
    if (!isGl(r))
        return SDL_RenderGeometryRaw(r, t, xy, xyStride, color, colorStride, uv, uvStride, count, indices, indexCount,
                                     indexSize);
    GlTexture *tex = glTex(t);
    if (count <= 0)
        return 0;
    frame();
    // the texture's colour and alpha mod multiply the vertices' colours, as SDL does
    const int mr = tex ? tex->r : 255, mg = tex ? tex->g : 255, mb = tex ? tex->b : 255, ma = tex ? tex->a : 255;
    std::vector<AbrVertex> v(static_cast<size_t>(count));
    const char *pxy = reinterpret_cast<const char *>(xy);
    const char *pc = reinterpret_cast<const char *>(color);
    const char *puv = reinterpret_cast<const char *>(uv);
    for (int i = 0; i < count; i++) {
        const float *p = reinterpret_cast<const float *>(pxy + static_cast<size_t>(i) * xyStride);
        const SDL_Color c = *reinterpret_cast<const SDL_Color *>(pc + static_cast<size_t>(i) * colorStride);
        v[i].x = p[0];
        v[i].y = p[1];
        v[i].color = AbrColor{static_cast<uint8_t>(c.r * mr / 255), static_cast<uint8_t>(c.g * mg / 255),
                              static_cast<uint8_t>(c.b * mb / 255), static_cast<uint8_t>(c.a * ma / 255)};
        if (tex && uv) {
            const float *q = reinterpret_cast<const float *>(puv + static_cast<size_t>(i) * uvStride);
            v[i].u = q[0];
            v[i].v = q[1];
        } else {
            v[i].u = v[i].v = 0.0f;
        }
    }
    std::vector<int32_t> idx;
    if (indices && indexCount > 0) {
        idx.resize(static_cast<size_t>(indexCount));
        for (int i = 0; i < indexCount; i++) {
            if (indexSize == 1)
                idx[i] = static_cast<const Uint8 *>(indices)[i];
            else if (indexSize == 2)
                idx[i] = static_cast<const Uint16 *>(indices)[i];
            else
                idx[i] = static_cast<const int32_t *>(indices)[i];
        }
    }
    g->api->geometry(g->ctx, tex ? tex->tex : nullptr, v.data(), count, idx.empty() ? nullptr : idx.data(),
                     static_cast<int32_t>(idx.size()), toAbr(tex ? tex->blend : g->drawBlend));
    return 0;
}

int RenderGeometry(SDL_Renderer *r, SDL_Texture *t, const SDL_Vertex *vertices, int count, const int *indices,
                   int indexCount) {
    if (!isGl(r))
        return SDL_RenderGeometry(r, t, vertices, count, indices, indexCount);
    return RenderGeometryRaw(r, t, &vertices->position.x, sizeof(SDL_Vertex),
                             reinterpret_cast<const GeometryColor *>(&vertices->color), sizeof(SDL_Vertex),
                             &vertices->tex_coord.x, sizeof(SDL_Vertex), count, indices, indexCount, 4);
}

int RenderReadPixels(SDL_Renderer *r, const SDL_Rect *rect, Uint32 format, void *pixels, int pitch) {
    if (!isGl(r))
        return SDL_RenderReadPixels(r, rect, format, pixels, pitch);
    frame();
    int w = 0, h = 0;
    currentSize(w, h);
    const SDL_Rect area = rect ? *rect : SDL_Rect{0, 0, w, h};
    if (area.w <= 0 || area.h <= 0)
        return SDL_SetError("nothing to read");
    std::vector<unsigned char> rgba(static_cast<size_t>(area.w) * area.h * 4);
    // on the screen SDL reads within the viewport; the backend reads the whole screen
    AbrRect a = toRect(area);
    if (!g->target && g->hasViewport) {
        a.x += g->viewport.x;
        a.y += g->viewport.y;
    }
    if (!g->api->read_pixels(g->ctx, &a, rgba.data(), area.w * 4))
        return SDL_SetError("the renderer backend could not read the pixels back");
    if (format == 0)
        format = SDL_PIXELFORMAT_RGBA32;
    return SDL_ConvertPixels(area.w, area.h, SDL_PIXELFORMAT_RGBA32, rgba.data(), area.w * 4, format, pixels, pitch);
}

SDL_Texture *CreateTexture(SDL_Renderer *r, Uint32 format, int access, int w, int h) {
    if (!isGl(r))
        return SDL_CreateTexture(r, format, access, w, h);
    if (SDL_BYTESPERPIXEL(format) != 4) {
        SDL_SetError("the renderer backend takes 32-bit texture formats only");
        return nullptr;
    }
    uint32_t flags = nearestHint() ? ABR_TEX_NEAREST : 0;
    if (access == SDL_TEXTUREACCESS_TARGET)
        flags |= ABR_TEX_TARGET;
    GlTexture *t = makeTexture(w, h, format, access, nullptr, flags);
    if (!t)
        return nullptr;
    t->blend = SDL_ISPIXELFORMAT_ALPHA(format) ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE; // SDL's default
    if (access == SDL_TEXTUREACCESS_STREAMING)
        t->stream.assign(static_cast<size_t>(w) * h * 4, 0);
    return reinterpret_cast<SDL_Texture *>(t);
}

SDL_Texture *CreateTextureFromSurface(SDL_Renderer *r, SDL_Surface *surface) {
    if (!isGl(r))
        return SDL_CreateTextureFromSurface(r, surface);
    if (!surface) {
        SDL_SetError("no surface");
        return nullptr;
    }
    SDL_Surface *rgba = surface->format->format == SDL_PIXELFORMAT_RGBA32
                            ? surface
                            : SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
    if (!rgba)
        return nullptr;
    std::vector<unsigned char> tight;
    if (SDL_MUSTLOCK(rgba))
        SDL_LockSurface(rgba);
    const void *px = toRgba(SDL_PIXELFORMAT_RGBA32, rgba->pixels, rgba->pitch, rgba->w, rgba->h, tight);
    // a picture: mipmaps when it is drawn shrunk (covers, scaled art), never made for one drawn 1:1
    const uint32_t flags = nearestHint() ? ABR_TEX_NEAREST : ABR_TEX_AUTO_MIPMAPS;
    GlTexture *t = px ? makeTexture(rgba->w, rgba->h, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, px, flags)
                      : nullptr;
    if (SDL_MUSTLOCK(rgba))
        SDL_UnlockSurface(rgba);
    if (rgba != surface)
        SDL_FreeSurface(rgba);
    if (!t)
        return nullptr;
    t->blend = surface->format->Amask || SDL_HasColorKey(surface) ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE;
    return reinterpret_cast<SDL_Texture *>(t);
}

void DestroyTexture(SDL_Texture *t) {
    if (!g)
        return SDL_DestroyTexture(t);
    GlTexture *tex = glTex(t);
    if (!tex)
        return;
    if (g->target == tex) {
        g->target = nullptr; // as SDL does: drawing goes back to the screen
        if (g->inFrame) {
            g->api->set_target(g->ctx, nullptr);
            applyClip();
        }
    }
    g->api->texture_destroy(g->ctx, tex->tex);
    delete tex;
}

int QueryTexture(SDL_Texture *t, Uint32 *format, int *access, int *w, int *h) {
    if (!g)
        return SDL_QueryTexture(t, format, access, w, h);
    const GlTexture *tex = glTex(t);
    if (!tex)
        return SDL_SetError("no texture");
    if (format)
        *format = tex->format;
    if (access)
        *access = tex->access;
    if (w)
        *w = tex->w;
    if (h)
        *h = tex->h;
    return 0;
}

int UpdateTexture(SDL_Texture *t, const SDL_Rect *rect, const void *pixels, int pitch) {
    if (!g)
        return SDL_UpdateTexture(t, rect, pixels, pitch);
    GlTexture *tex = glTex(t);
    if (!tex || !pixels)
        return SDL_SetError("no texture or pixels");
    const SDL_Rect area = rect ? *rect : SDL_Rect{0, 0, tex->w, tex->h};
    if (area.w <= 0 || area.h <= 0)
        return 0;
    if (!tex->stream.empty()) // a streaming texture keeps its pixels for the next lock
        for (int y = 0; y < area.h; y++)
            memcpy(&tex->stream[(static_cast<size_t>(area.y + y) * tex->w + area.x) * 4],
                   static_cast<const unsigned char *>(pixels) + static_cast<size_t>(y) * pitch,
                   static_cast<size_t>(area.w) * 4);
    std::vector<unsigned char> buffer;
    const void *px = toRgba(tex->format, pixels, pitch, area.w, area.h, buffer);
    if (!px)
        return -1;
    const AbrRect a = toRect(area);
    g->api->texture_update(g->ctx, tex->tex, &a, px, area.w * 4);
    return 0;
}

int LockTexture(SDL_Texture *t, const SDL_Rect *rect, void **pixels, int *pitch) {
    if (!g)
        return SDL_LockTexture(t, rect, pixels, pitch);
    GlTexture *tex = glTex(t);
    if (!tex || tex->stream.empty())
        return SDL_SetError("SDL_LockTexture(): texture must be streaming");
    tex->locked = rect ? *rect : SDL_Rect{0, 0, tex->w, tex->h};
    *pixels = &tex->stream[(static_cast<size_t>(tex->locked.y) * tex->w + tex->locked.x) * 4];
    *pitch = tex->w * 4;
    return 0;
}

void UnlockTexture(SDL_Texture *t) {
    if (!g)
        return SDL_UnlockTexture(t);
    GlTexture *tex = glTex(t);
    if (!tex || tex->stream.empty() || tex->locked.w <= 0 || tex->locked.h <= 0)
        return;
    const SDL_Rect &area = tex->locked;
    std::vector<unsigned char> buffer;
    const void *px = toRgba(tex->format, &tex->stream[(static_cast<size_t>(area.y) * tex->w + area.x) * 4],
                            tex->w * 4, area.w, area.h, buffer);
    if (px) {
        const AbrRect a = toRect(area);
        g->api->texture_update(g->ctx, tex->tex, &a, px, area.w * 4);
    }
    tex->locked = SDL_Rect{};
}

int SetTextureBlendMode(SDL_Texture *t, SDL_BlendMode mode) {
    if (!g)
        return SDL_SetTextureBlendMode(t, mode);
    if (GlTexture *tex = glTex(t))
        tex->blend = mode;
    return 0;
}

int SetTextureColorMod(SDL_Texture *t, Uint8 red, Uint8 green, Uint8 blue) {
    if (!g)
        return SDL_SetTextureColorMod(t, red, green, blue);
    if (GlTexture *tex = glTex(t)) {
        tex->r = red;
        tex->g = green;
        tex->b = blue;
    }
    return 0;
}

int GetTextureColorMod(SDL_Texture *t, Uint8 *red, Uint8 *green, Uint8 *blue) {
    if (!g)
        return SDL_GetTextureColorMod(t, red, green, blue);
    const GlTexture *tex = glTex(t);
    if (!tex)
        return -1;
    if (red)
        *red = tex->r;
    if (green)
        *green = tex->g;
    if (blue)
        *blue = tex->b;
    return 0;
}

int SetTextureAlphaMod(SDL_Texture *t, Uint8 a) {
    if (!g)
        return SDL_SetTextureAlphaMod(t, a);
    if (GlTexture *tex = glTex(t))
        tex->a = a;
    return 0;
}

SDL_Texture *LoadTexture(SDL_Renderer *r, const char *file) {
    if (!isGl(r))
        return IMG_LoadTexture(r, file);
    SDL_Surface *s = IMG_Load(file);
    if (!s)
        return nullptr;
    SDL_Texture *t = CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    return t;
}

SDL_Texture *LoadTexture_RW(SDL_Renderer *r, SDL_RWops *src, int freesrc) {
    if (!isGl(r))
        return IMG_LoadTexture_RW(r, src, freesrc);
    SDL_Surface *s = IMG_Load_RW(src, freesrc);
    if (!s)
        return nullptr;
    SDL_Texture *t = CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    return t;
}

int GetTextureAlphaMod(SDL_Texture *t, Uint8 *a) {
    if (!g)
        return SDL_GetTextureAlphaMod(t, a);
    const GlTexture *tex = glTex(t);
    if (!tex)
        return -1;
    if (a)
        *a = tex->a;
    return 0;
}

} // namespace ableem::gl
