#include "ableem/ui/texture.h"
#include "ableem/ui/renderer.h"
#include "sdl_common.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <ableem/engine/log.h>

namespace ableem {

namespace {
void destroyTexture(void *t) {
    if (t)
        SDL_DestroyTexture(static_cast<SDL_Texture *>(t));
}
// RGBA8888 pixel format, allocated once and reused (matches the original engine/cardedit.cpp behavior).
SDL_PixelFormat *rgba8888Format() {
    static SDL_PixelFormat *fmt = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888);
    return fmt;
}
} // namespace

Texture::Texture() : handle(nullptr) {}

Texture::Texture(void *nativeTexture) : handle(nativeTexture, destroyTexture) {}

Texture Texture::loadFile(Renderer &renderer, const std::string &path) {
    // "no picture" is a legitimate answer callers pass straight on (a game with no resume point, no
    // snap): not an error, and not worth the trip through SDL_image - which on Windows took up to 200 ms
    // to fail on an empty name
    if (path.empty())
        return Texture();
    // with frame statistics on, a load slow enough to cost a frame says so - the launcher's scroll
    // stutter came from loads like these on its main thread
    struct SlowLoadReport {
        const std::string &path;
        unsigned int start = Renderer::statsEnabled() ? SDL_GetTicks() : 0;
        ~SlowLoadReport() {
            if (start != 0) {
                unsigned int ms = SDL_GetTicks() - start;
                if (ms >= 5) {
                    PLOG_INFO << "Texture::loadFile took " << ms << " ms: " << path;
                }
            }
        }
    } report{path};
    SDL_Texture *t = IMG_LoadTexture(static_cast<SDL_Renderer *>(renderer.native()), path.c_str());
    if (!t) {
        PLOG_ERROR << "Could not load texture: " << path << " (" << IMG_GetError() << ")";
    }
    return Texture(t);
}

Texture Texture::loadFile(Renderer &renderer, const std::string &path, float pixelScale) {
    Texture tex = loadFile(renderer, path);
    if (tex.valid() && pixelScale > 0.0f)
        tex.pixelScale_ = pixelScale;
    return tex;
}

Rect Texture::opaqueBounds(const std::string &path) {
    SDL_Surface *loaded = IMG_Load(path.c_str());
    if (!loaded)
        return Rect();
    SDL_Surface *s = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(loaded);
    if (!s)
        return Rect();
    // converted to ARGB, an image without alpha is opaque throughout: the whole of it
    Rect bounds(0, 0, s->w, s->h);
    {
        int minX = s->w, minY = s->h, maxX = -1, maxY = -1;
        const unsigned char *pixels = static_cast<const unsigned char *>(s->pixels);
        for (int y = 0; y < s->h; y++) {
            const uint32_t *row = reinterpret_cast<const uint32_t *>(pixels + y * s->pitch);
            for (int x = 0; x < s->w; x++) {
                if ((row[x] >> 24) != 0) {
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
            }
        }
        if (maxX >= 0)
            bounds = Rect(minX, minY, maxX - minX + 1, maxY - minY + 1);
    }
    SDL_FreeSurface(s);
    return bounds;
}

Texture Texture::loadMemory(Renderer &renderer, const void *data, unsigned int size) {
    SDL_RWops *rw = SDL_RWFromConstMem(data, static_cast<int>(size));
    // freesrc=1: IMG_LoadTexture_RW closes/frees the RWops for us
    SDL_Texture *t = IMG_LoadTexture_RW(static_cast<SDL_Renderer *>(renderer.native()), rw, 1);
    if (!t) {
        PLOG_ERROR << "Could not load texture from memory (" << IMG_GetError() << ")";
    }
    return Texture(t);
}

Image Image::loadFile(const std::string &path) {
    Image image;
    if (path.empty())
        return image;
    SDL_Surface *s = IMG_Load(path.c_str());
    if (!s) {
        PLOG_ERROR << "Could not load image: " << path << " (" << IMG_GetError() << ")";
        return image;
    }
    image.surface = std::shared_ptr<void>(s, [](void *p) { SDL_FreeSurface(static_cast<SDL_Surface *>(p)); });
    return image;
}

Size Image::size() const {
    Size s;
    if (surface) {
        s.w = static_cast<SDL_Surface *>(surface.get())->w;
        s.h = static_cast<SDL_Surface *>(surface.get())->h;
    }
    return s;
}

Color Image::pixel(int x, int y) const {
    auto *s = static_cast<SDL_Surface *>(surface.get());
    if (!s || x < 0 || y < 0 || x >= s->w || y >= s->h)
        return Color(0, 0, 0, 0);
    const bool lock = SDL_MUSTLOCK(s);
    if (lock && SDL_LockSurface(s) != 0)
        return Color(0, 0, 0, 0);
    const int bpp = s->format->BytesPerPixel;
    const Uint8 *p = static_cast<const Uint8 *>(s->pixels) + y * s->pitch + x * bpp;
    Uint32 raw = 0;
    switch (bpp) {
    case 1:
        raw = *p;
        break;
    case 2:
        raw = *reinterpret_cast<const Uint16 *>(p);
        break;
    case 3:
        raw = SDL_BYTEORDER == SDL_BIG_ENDIAN ? (p[0] << 16 | p[1] << 8 | p[2]) : (p[0] | p[1] << 8 | p[2] << 16);
        break;
    default:
        raw = *reinterpret_cast<const Uint32 *>(p);
        break;
    }
    if (lock)
        SDL_UnlockSurface(s);
    Uint8 r, g, b, a;
    SDL_GetRGBA(raw, s->format, &r, &g, &b, &a);
    return Color(r, g, b, a);
}

Texture Texture::fromImage(Renderer &renderer, const Image &image) {
    if (!image.valid())
        return Texture();
    SDL_Texture *t = SDL_CreateTextureFromSurface(static_cast<SDL_Renderer *>(renderer.native()),
                                                  static_cast<SDL_Surface *>(image.surface.get()));
    if (!t) {
        PLOG_ERROR << "Could not upload image (" << SDL_GetError() << ")";
    }
    return Texture(t);
}

Texture Texture::createTarget(Renderer &renderer, int w, int h) {
    // allocated in output pixels, so that what is composed into it at a scale above 1 keeps its sharpness
    float k = renderer.outputScale();
    SDL_Texture *t = SDL_CreateTexture(static_cast<SDL_Renderer *>(renderer.native()), SDL_PIXELFORMAT_RGBA8888,
                                       SDL_TEXTUREACCESS_TARGET, static_cast<int>(std::lround(w * k)),
                                       static_cast<int>(std::lround(h * k)));
    Texture tex(t);
    tex.pixelScale_ = k;
    return tex;
}

Texture Texture::createStreaming(Renderer &renderer, int w, int h) {
    SDL_Texture *t = SDL_CreateTexture(static_cast<SDL_Renderer *>(renderer.native()), SDL_PIXELFORMAT_RGBA8888,
                                       SDL_TEXTUREACCESS_STREAMING, w, h);
    return Texture(t);
}

bool Texture::valid() const {
    return handle != nullptr;
}

Size Texture::size() const {
    Size s;
    if (handle) {
        SDL_QueryTexture(static_cast<SDL_Texture *>(handle.get()), nullptr, nullptr, &s.w, &s.h);
        if (pixelScale_ != 1.0f) {
            s.w = static_cast<int>(std::lround(s.w / pixelScale_));
            s.h = static_cast<int>(std::lround(s.h / pixelScale_));
        }
    }
    return s;
}

void Texture::setBlendMode(BlendMode mode) {
    if (!handle)
        return;
    SDL_BlendMode m = SDL_BLENDMODE_BLEND;
    switch (mode) {
    case BlendMode::None:
        m = SDL_BLENDMODE_NONE;
        break;
    case BlendMode::Add:
        m = SDL_BLENDMODE_ADD;
        break;
    case BlendMode::Mod:
        m = SDL_BLENDMODE_MOD;
        break;
    case BlendMode::Premultiplied:
        m = premultipliedBlendMode();
        break;
    case BlendMode::Mask:
        m = maskBlendMode();
        break;
    case BlendMode::Blend:
    default:
        m = SDL_BLENDMODE_BLEND;
        break;
    }
    SDL_SetTextureBlendMode(static_cast<SDL_Texture *>(handle.get()), m);
}

void Texture::setColorMod(Color c) {
    if (!handle)
        return;
    SDL_SetTextureColorMod(static_cast<SDL_Texture *>(handle.get()), c.r, c.g, c.b);
}

void Texture::setAlphaMod(unsigned char a) {
    if (!handle)
        return;
    SDL_SetTextureAlphaMod(static_cast<SDL_Texture *>(handle.get()), a);
}

PixelLock Texture::lock() {
    int w = 0, h = 0; // texture pixels, whatever the scale (only streaming textures are locked, at scale 1)
    if (handle)
        SDL_QueryTexture(static_cast<SDL_Texture *>(handle.get()), nullptr, nullptr, &w, &h);
    return PixelLock(handle.get(), w, h);
}

//******************
// PixelLock
//******************
PixelLock::PixelLock(void *texture, int _w, int _h) : tex(texture), w(_w), h(_h) {
    void *raw = nullptr;
    int pitchBytes = 0;
    if (SDL_LockTexture(static_cast<SDL_Texture *>(tex), nullptr, &raw, &pitchBytes) == 0) {
        pixels = static_cast<unsigned int *>(raw);
        pitchPixels = pitchBytes / 4;
    }
}

PixelLock::PixelLock(PixelLock &&other) noexcept
    : tex(other.tex), pixels(other.pixels), pitchPixels(other.pitchPixels), w(other.w), h(other.h) {
    other.tex = nullptr;
    other.pixels = nullptr;
}

PixelLock::~PixelLock() {
    if (tex) {
        SDL_UnlockTexture(static_cast<SDL_Texture *>(tex));
    }
}

Color PixelLock::get(int x, int y) const {
    if (!pixels)
        return Color(0, 0, 0, 0);
    Uint32 raw = pixels[y * pitchPixels + x];
    Uint8 r, g, b, a;
    SDL_GetRGBA(raw, rgba8888Format(), &r, &g, &b, &a);
    return Color(r, g, b, a);
}

void PixelLock::set(int x, int y, Color c) {
    if (!pixels)
        return;
    pixels[y * pitchPixels + x] = SDL_MapRGBA(rgba8888Format(), c.r, c.g, c.b, c.a);
}

} // namespace ableem
