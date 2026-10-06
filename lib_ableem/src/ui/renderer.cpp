#include "ableem/ui/renderer.h"
#include <SDL2/SDL_image.h>
#include "ableem/ui/canvas.h"
#include <mutex>
#include "ableem/ui/platform.h"
#include "ableem/ui/texture.h"
#include "perf_overlay.h"
#include "sdl_common.h"
#include <ableem/engine/ext_trace.h>
#include <ableem/engine/log.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace ableem {

//*******************************
// the canvas math (canvas.h)
//*******************************
bool isFourByThreeOutput(int outputW, int outputH) {
    // up to 1.5: 480p's 720x480 is 1.5 exactly, and no TV shows it at square pixels - it is 4:3 (or anamorphic 16:9,
    // which the launcher does not offer); 576p is 1.25, a 4:3 monitor 1.33. Wide: 16:10 (1.6), 16:9 (1.78)
    return outputW > 0 && outputH > 0 && outputW * 2 <= outputH * 3;
}

bool usesFrameTarget(int outputW, int outputH, int canvasW, int canvasH) {
    return isFourByThreeOutput(outputW, outputH) && canvasW > 0 && canvasH > 0 && canvasW * 3 > canvasH * 4;
}

CanvasMapping fitCanvas(int outputW, int outputH, int canvasW, int canvasH) {
    CanvasMapping m;
    if (canvasW <= 0 || canvasH <= 0)
        return m;
    // the canvas as big as fits the window, centred (what Renderer::recreate always did)
    const float sx = static_cast<float>(outputW) / canvasW;
    const float sy = static_cast<float>(outputH) / canvasH;
    m.scale = std::min(sx, sy);
    const int drawnW = static_cast<int>(std::lround(canvasW * m.scale));
    const int drawnH = static_cast<int>(std::lround(canvasH * m.scale));
    m.display = Rect((outputW - drawnW) / 2, (outputH - drawnH) / 2, drawnW, drawnH);
    m.scaleX = m.scaleY = m.scale;
    return m;
}

int clampSafeMargin(int percent) {
    return std::min(MaxSafeMargin, std::max(0, percent));
}

CanvasMapping mapCanvas(int outputW, int outputH, int canvasW, int canvasH, int marginPercent) {
    CanvasMapping m;
    if (canvasW <= 0 || canvasH <= 0)
        return m;
    if (!isFourByThreeOutput(outputW, outputH))
        return fitCanvas(outputW, outputH, canvasW, canvasH);
    m.fourByThree = true;
    m.scale = static_cast<float>(outputH) / FourByThreeCanvasH;
    m.frameW = static_cast<int>(std::lround(canvasW * m.scale));
    m.frameH = static_cast<int>(std::lround(canvasH * m.scale));
    // the safe area: the output inset by the margin, the same share of both sides (the pixel aspect stays)
    const int margin = clampSafeMargin(marginPercent);
    const int insetX = static_cast<int>(std::lround(outputW * margin / 100.0));
    const int insetY = static_cast<int>(std::lround(outputH * margin / 100.0));
    const int areaW = outputW - 2 * insetX, areaH = outputH - 2 * insetY;
    // the canvas at its own shape on a 4:3 screen: as tall as the area when it is no wider than 4:3, else as wide
    if (canvasW * 3 <= canvasH * 4) {
        const int w = static_cast<int>(std::lround(static_cast<double>(areaW) * canvasW * 3 / (canvasH * 4.0)));
        m.display = Rect(insetX + (areaW - w) / 2, insetY, w, areaH);
    } else {
        const int h = static_cast<int>(std::lround(static_cast<double>(areaH) * canvasH * 4 / (canvasW * 3.0)));
        m.display = Rect(insetX, insetY + (areaH - h) / 2, areaW, h);
    }
    m.scaleX = static_cast<float>(m.display.w) / canvasW;
    m.scaleY = static_cast<float>(m.display.h) / canvasH;
    return m;
}

Rect coverCrop(int textureW, int textureH, int canvasW, int canvasH) {
    const Rect whole(0, 0, textureW, textureH);
    if (textureW <= 0 || textureH <= 0 || canvasW <= 0 || canvasH <= 0)
        return whole;
    // the same shape within a pixel or so: the whole picture
    const long long a = static_cast<long long>(textureW) * canvasH, b = static_cast<long long>(textureH) * canvasW;
    if (std::llabs(a - b) * 100 <= std::max(a, b))
        return whole;
    if (a > b) { // wider than the canvas: the middle of it, full height
        const int w = static_cast<int>(std::lround(static_cast<double>(textureH) * canvasW / canvasH));
        return Rect((textureW - w) / 2, 0, w, textureH);
    }
    const int h = static_cast<int>(std::lround(static_cast<double>(textureW) * canvasH / canvasW));
    return Rect(0, (textureH - h) / 2, textureW, h);
}

namespace {
// A render target drawn from with linear filtering (the 4:3 frame, stretched to the output and blurred down for the CRT
// margin). The filter comes from the scale-quality hint at the texture's creation, never from SDL_SetTextureScaleMode:
// SDL 2.0.18 (the console's) calls the driver's SetTextureScaleMode on the texture it was given even when that is only
// SDL's stand-in for a native texture of another format - every RGBA8888 target on GLES2 - and
// GLES2_SetTextureScaleMode then reads the stand-in's missing driver data: a SIGSEGV (CRT 4:3 round 2, the launcher
// died on its first 4:3 frame)
Texture createLinearTarget(Renderer &renderer, int w, int h) {
    const char *quality = SDL_GetHint(SDL_HINT_RENDER_SCALE_QUALITY);
    const std::string previous = quality ? quality : "0"; // no hint is nearest - SDL_SetHint cannot unset one
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    Texture target = Texture::createTarget(renderer, w, h);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, previous.c_str());
    return target;
}

// The CRT's safe area: the margin around the frame is the frame's own edge mirrored outward (a strip of the frame
// flipped across the edge it touches, the corners flipped both ways), softened and a little dimmed - the colours go on
// smoothly past the safe rectangle instead of a black frame (`frame` is the softened copy: Renderer::mirrorMargin). Only on a side where the frame really sits on the safe
// rectangle (a letterboxed canvas keeps its black bars), and only with a margin. `display` is where the frame goes.
void copyMirroredMargin(SDL_Renderer *renderer, SDL_Texture *frame, const SDL_Rect &display, int outW, int outH,
                        int marginPercent) {
    if (marginPercent <= 0 || display.w <= 0 || display.h <= 0)
        return;
    int fw = 0, fh = 0;
    if (SDL_QueryTexture(frame, nullptr, nullptr, &fw, &fh) != 0 || fw <= 0 || fh <= 0)
        return;
    const int insetX = static_cast<int>(std::lround(outW * marginPercent / 100.0));
    const int insetY = static_cast<int>(std::lround(outH * marginPercent / 100.0));
    const bool left = std::abs(display.x - insetX) <= 1 && display.x > 0;
    const bool right = std::abs(display.x + display.w - (outW - insetX)) <= 1 && outW - (display.x + display.w) > 0;
    const bool top = std::abs(display.y - insetY) <= 1 && display.y > 0;
    const bool bottom = std::abs(display.y + display.h - (outH - insetY)) <= 1 && outH - (display.y + display.h) > 0;
    const double kx = static_cast<double>(fw) / display.w, ky = static_cast<double>(fh) / display.h;
    // a margin as frame pixels (at most the frame), and as the output pixels that many cover
    auto strip = [](int margin, double k, int full, int &src, int &dst) {
        src = std::min(full, static_cast<int>(std::ceil(margin * k)));
        dst = std::min(margin, static_cast<int>(std::lround(src / k)));
    };
    int sl = 0, dl = 0, sr = 0, dr = 0, st = 0, dt = 0, sb = 0, db = 0;
    if (left)
        strip(display.x, kx, fw, sl, dl);
    if (right)
        strip(outW - (display.x + display.w), kx, fw, sr, dr);
    if (top)
        strip(display.y, ky, fh, st, dt);
    if (bottom)
        strip(outH - (display.y + display.h), ky, fh, sb, db);
    const int x1 = display.x + display.w, y1 = display.y + display.h;
    SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_NONE);
    SDL_SetTextureColorMod(frame, 150, 150, 150); // a light dim: the margin reads as outside the picture
    auto put = [&](int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh, int flip) {
        if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
            return;
        const SDL_Rect src{sx, sy, sw, sh}, dst{dx, dy, dw, dh};
        SDL_RenderCopyEx(renderer, frame, &src, &dst, 0.0, nullptr, static_cast<SDL_RendererFlip>(flip));
    };
    const int H = SDL_FLIP_HORIZONTAL, V = SDL_FLIP_VERTICAL, HV = SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL;
    put(0, 0, sl, fh, display.x - dl, display.y, dl, display.h, H);
    put(fw - sr, 0, sr, fh, x1, display.y, dr, display.h, H);
    put(0, 0, fw, st, display.x, display.y - dt, display.w, dt, V);
    put(0, fh - sb, fw, sb, display.x, y1, display.w, db, V);
    put(0, 0, sl, st, display.x - dl, display.y - dt, dl, dt, HV);
    put(fw - sr, 0, sr, st, x1, display.y - dt, dr, dt, HV);
    put(0, fh - sb, sl, sb, display.x - dl, y1, dl, db, HV);
    put(fw - sr, fh - sb, sr, sb, x1, y1, dr, db, HV);
    SDL_SetTextureColorMod(frame, 255, 255, 255);
    SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_BLEND); // as the caller had it
}
} // namespace

namespace {
// A growable in-memory SDL_RWops, write-only: what IMG_SavePNG_RW encodes into for
// Renderer::encodeLastFramePng - no temp file needed just to hand a screenshot back over a socket.
struct MemWriter {
    std::vector<unsigned char> *out;
    size_t pos = 0;
};

Sint64 SDLCALL memWriterSize(SDL_RWops *ctx) {
    return static_cast<Sint64>(static_cast<MemWriter *>(ctx->hidden.unknown.data1)->out->size());
}

Sint64 SDLCALL memWriterSeek(SDL_RWops *ctx, Sint64 offset, int whence) {
    MemWriter *m = static_cast<MemWriter *>(ctx->hidden.unknown.data1);
    Sint64 base = whence == RW_SEEK_CUR   ? static_cast<Sint64>(m->pos)
                  : whence == RW_SEEK_END ? static_cast<Sint64>(m->out->size())
                                          : 0;
    Sint64 next = base + offset;
    if (next < 0)
        return -1;
    m->pos = static_cast<size_t>(next);
    return next;
}

size_t SDLCALL memWriterRead(SDL_RWops *, void *, size_t, size_t) {
    return 0; // write-only: the PNG encoder never reads back what it wrote
}

size_t SDLCALL memWriterWrite(SDL_RWops *ctx, const void *ptr, size_t size, size_t num) {
    MemWriter *m = static_cast<MemWriter *>(ctx->hidden.unknown.data1);
    size_t bytes = size * num;
    if (m->pos + bytes > m->out->size())
        m->out->resize(m->pos + bytes);
    memcpy(m->out->data() + m->pos, ptr, bytes);
    m->pos += bytes;
    return num;
}

int SDLCALL memWriterClose(SDL_RWops *ctx) {
    delete static_cast<MemWriter *>(ctx->hidden.unknown.data1);
    SDL_FreeRW(ctx);
    return 0;
}
} // namespace

namespace {
SDL_BlendMode toSDL(BlendMode m) {
    switch (m) {
    case BlendMode::None:
        return SDL_BLENDMODE_NONE;
    case BlendMode::Add:
        return SDL_BLENDMODE_ADD;
    case BlendMode::Mod:
        return SDL_BLENDMODE_MOD;
    case BlendMode::Premultiplied:
        return premultipliedBlendMode();
    case BlendMode::Mask:
        return maskBlendMode();
    case BlendMode::Blend:
    default:
        return SDL_BLENDMODE_BLEND;
    }
}
SDL_Rect toSDL(const Rect &r) {
    return SDL_Rect{r.x, r.y, r.w, r.h};
}
Rect scaleRect(const Rect &r, float k) {
    int x0 = static_cast<int>(std::lround(r.x * k));
    int y0 = static_cast<int>(std::lround(r.y * k));
    int x1 = static_cast<int>(std::lround((r.x + r.w) * k));
    int y1 = static_cast<int>(std::lround((r.y + r.h) * k));
    return Rect(x0, y0, x1 - x0, y1 - y0);
}
// the extension hand-off trap (BUG-31, ext_trace.h): where drawing goes, and whether a clear or present is outside
// the screen stack's frame
std::string traceTarget(SDL_Renderer *renderer) {
    return SDL_GetRenderTarget(renderer) == nullptr ? " target=screen" : " target=texture";
}

const char *traceOutside() {
    return ext_trace::inStackFrame() ? "" : " OUTSIDE-STACK";
}
} // namespace

struct Renderer::Impl {
    SDL_Renderer *renderer = nullptr;
    SDL_Window *window = nullptr; // the window the renderer was created for (the AB_TRACE_EXT trap reads its size)
    int width = 0, height = 0;    // the logical canvas
    float scale = 1.0f; // output pixels per logical pixel (on a 4:3 output: frame-target pixels, CanvasMapping)

    // the 4:3 output (canvas.h): every frame is drawn into frameTarget (`framing` from clear() to present()), which
    // present() stretches into `display`. The program's own canvas is baseWidth x baseHeight (Platform's logical
    // size); setCanvas() gives a frame another one, until present()
    bool fourByThree = false;
    int outputWidth = 0, outputHeight = 0;
    int baseWidth = 0, baseHeight = 0;
    int marginPercent = DefaultSafeMargin; // the CRT safe area (Renderer::setSafeMargin)
    bool marginSet = false;                // the program asked for one; until then only the 720x480 tube has a margin
    // the canvas every frame has unless it asks for another (setCanvas): the base one until setRestCanvas says
    int restWidth = 0, restHeight = 0;
    int wantRestWidth = 0, wantRestHeight = 0; // what setRestCanvas asked for: kept across a recreate()
    Rect display;
    Texture frameTarget;
    Texture marginSoft1, marginSoft2; // the frame blurred down for the CRT margin (Renderer::mirrorMargin)
    unsigned long marginSoftAt = ~0ul; // the targetsLost() they were made at
    bool framing = false;
    void useCanvas(int w, int h) {
        width = w;
        height = h;
        display = mapCanvas(outputWidth, outputHeight, w, h, marginPercent).display;
    }

    // the one-off capture (see Renderer::captureNextFrame)
    bool captureRequested = false;
    // the capture is taken without touching the window (Renderer::captureNextFrameSilently): present() keeps the
    // frame as the capture and neither copies it to the window nor swaps the buffers (BUG-31)
    bool captureSilent = false;
    Texture capture;
    // the frame being captured is drawn into this target instead of the screen (clear() switches to it,
    // present() copies it to the screen): no read-back of the frame from the GPU, which on the console's
    // GLES took ~350 ms (SDL converts and flips the pixels on the CPU)
    Texture captureTarget;
    bool capturing = false;
    SDL_Texture *screenTarget() {
        if (capturing)
            return static_cast<SDL_Texture *>(captureTarget.native());
        return framing ? static_cast<SDL_Texture *>(frameTarget.native()) : nullptr;
    }

    // the frame cache (see Renderer::setFrameCache)
    struct FrameCache {
        std::mutex mutex;
        bool enabled = false;
        bool requested = false;            // copy the next frame (requestFrameCopy)
        std::vector<unsigned char> pixels; // ARGB8888, `pitch` bytes a row
        int w = 0, h = 0, pitch = 0;
        unsigned long frames = 0;
        unsigned long copied = 0; // the number of the frame in `pixels`
    } frame;

    // frame statistics (see Renderer::statsEnabled)
    struct Stats {
        const void *lastTexture = nullptr;
        long copies = 0, switches = 0; // this frame
        long frames = 0, slowFrames = 0, sumMs = 0, maxMs = 0, sumCopies = 0, sumSwitches = 0;
        unsigned int lastPresent = 0, lastReport = 0;
    } stats;

    PerfOverlay overlay; // see Renderer::setPerfOverlay

    // the render-target stack (see Renderer::pushTarget) and the count of target losses (targetsLost)
    std::vector<SDL_Texture *> targetStack;
    std::atomic<unsigned long> targetsLost{0};

    // the frame cap (see Renderer::present): when the next frame is due, in performance-counter ticks
    Uint64 nextFrameDue = 0;
    void capFrameRate();

    void noteCopy(const void *texture, int n) {
        stats.copies += n;
        if (texture != stats.lastTexture) {
            stats.switches++;
            stats.lastTexture = texture;
        }
    }
};

bool Renderer::statsEnabled() {
    static const bool enabled = getenv("AB_FRAME_STATS") != nullptr;
    return enabled;
}

void Renderer::countCopies(int n) {
    impl->stats.copies += n;
}

//*******************************
// the frame cap
//*******************************
// A display that waits for vsync paces present() by itself; one that does not (a VM's software GL, an offscreen
// sandbox, a driver with vsync off) would let every screen's loop spin as fast as it can draw. So present()
// never returns sooner than a frame after the previous one: 60 fps, AB_MAX_FPS for another rate (a sandbox
// runs at 20-30), 0 for no cap. The deadline advances by whole frames, so a frame that ran late does not make
// the next one short.
static int maxFps() {
    static const int fps = [] {
        const char *v = getenv("AB_MAX_FPS");
        if (!v || !*v)
            return 60;
        const int n = atoi(v);
        return n < 0 ? 60 : n;
    }();
    return fps;
}

void Renderer::Impl::capFrameRate() {
    const int fps = maxFps();
    if (fps <= 0)
        return;
    const Uint64 freq = SDL_GetPerformanceFrequency();
    const Uint64 frame = freq / static_cast<Uint64>(fps);
    Uint64 now = SDL_GetPerformanceCounter();
    if (nextFrameDue == 0 || now > nextFrameDue + frame) {
        nextFrameDue = now + frame; // the first frame, or one far behind: start the count again from here
        return;
    }
    if (now < nextFrameDue) {
        const Uint64 ms = (nextFrameDue - now) * 1000 / freq;
        if (ms > 0)
            SDL_Delay(static_cast<Uint32>(ms));
    }
    nextFrameDue += frame;
}

static bool perfOverlayForced() {
    static const bool forced = [] {
        const char *v = getenv("AB_PERF_OVERLAY");
        return v != nullptr && *v != '\0' && strcmp(v, "0") != 0;
    }();
    return forced;
}

void Renderer::setPerfOverlay(bool on) {
    impl->overlay.enabled = on || perfOverlayForced();
}

bool Renderer::perfOverlay() const {
    return impl->overlay.enabled;
}

// SDL says the targets' contents are gone (a D3D device lost, a GL context reset): count it, so that every
// cache drawn into a target (text runs, covers, layers) is made again. An event watch sees it whichever screen
// is polling, and before that screen draws again.
static int SDLCALL watchTargetsReset(void *userdata, SDL_Event *e) {
    if (e->type == SDL_RENDER_TARGETS_RESET || e->type == SDL_RENDER_DEVICE_RESET) {
        static_cast<std::atomic<unsigned long> *>(userdata)->fetch_add(1);
        PLOG_WARNING << "Render targets reset - the cached textures are drawn again";
    }
    return 1;
}

Renderer::Renderer(Platform &platform) : impl(new Impl()) {
    impl->overlay.enabled = perfOverlayForced();
    SDL_AddEventWatch(watchTargetsReset, &impl->targetsLost);
    recreate(platform);
}

void Renderer::release() {
    impl->overlay.release(); // its texture goes with the renderer
    impl->frameTarget = Texture();
    impl->marginSoft1 = Texture(); // the CRT margin's too: SDL_DestroyRenderer frees them, a handle kept would dangle
    impl->marginSoft2 = Texture();
    impl->framing = false;
    if (impl->renderer) {
        SDL_DestroyRenderer(impl->renderer);
        impl->renderer = nullptr;
        impl->window = nullptr;
    }
}

void Renderer::recreate(Platform &platform) {
    release();
    impl->targetStack.clear();
    impl->targetsLost++; // a new renderer: nothing drawn into the old one's targets survives
    SDL_Window *window = static_cast<SDL_Window *>(platform.nativeWindow());
    impl->window = window;
    impl->renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!impl->renderer) {
        throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
    }
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(impl->renderer, &info) == 0) {
        PLOG_INFO << "Renderer: " << info.name << ", " << platform.multisampleSamples() << "x MSAA";
        // every screen draws through render targets; a GL renderer whose library SDL could not load (no
        // libGL.so.1 on a Pi) still gets created, without shaders or targets, and shows a black screen
        if (!(info.flags & SDL_RENDERER_TARGETTEXTURE)) {
            PLOG_ERROR << "Renderer " << info.name
                       << " has no render-target support - nothing will be drawn. On a Pi: is libgl1 "
                          "(libGL.so.1) installed? SDL_LOGGING=*=verbose shows what SDL failed to load.";
        }
    }
    int outputWidth = 0, outputHeight = 0;
    SDL_GetWindowSize(window, &outputWidth, &outputHeight);
    impl->outputWidth = outputWidth;
    impl->outputHeight = outputHeight;
    if (!impl->marginSet)
        impl->marginPercent = outputWidth == 720 && outputHeight == 480 ? DefaultSafeMargin : 0;
    impl->baseWidth = platform.logicalWidth();
    impl->baseHeight = platform.logicalHeight();
    impl->width = impl->baseWidth;
    impl->height = impl->baseHeight;
    impl->restWidth = impl->baseWidth;
    impl->restHeight = impl->baseHeight;
    // the frame target only for a wide canvas on a 4:3 output (the launcher's 1280x720 on 480p); a canvas of the
    // output's own shape (a test's 320x240 window) is drawn straight, as always
    const CanvasMapping mapping = usesFrameTarget(outputWidth, outputHeight, impl->width, impl->height)
                                      ? mapCanvas(outputWidth, outputHeight, impl->width, impl->height, impl->marginPercent)
                                      : fitCanvas(outputWidth, outputHeight, impl->width, impl->height);
    impl->fourByThree = mapping.fourByThree;
    impl->display = mapping.display;
    if (mapping.fourByThree && impl->wantRestWidth > 0 && impl->wantRestHeight > 0) {
        impl->restWidth = impl->wantRestWidth;
        impl->restHeight = impl->wantRestHeight;
        impl->useCanvas(impl->restWidth, impl->restHeight);
    }
    if (impl->width <= 0 || impl->height <= 0) {
        impl->scale = 1.0f; // no canvas (never in the program): as before, no scale
        return;
    }
    impl->scale = mapping.scale;
    if (mapping.fourByThree) {
        // no viewport: the frames go into the frame target, and present() places it (CanvasMapping)
        PLOG_INFO << "4:3 output " << outputWidth << "x" << outputHeight << ": frames at " << impl->scale << "x, a "
                  << impl->width << "x" << impl->height << " canvas shown " << mapping.display.w << "x"
                  << mapping.display.h << " at " << mapping.display.x << "," << mapping.display.y;
        return;
    }
    // the canvas as big as fits the window, centred: a window made outputScale times the canvas fits
    // exactly; a full-screen one on a desktop of another shape gets black bars (the viewport is the window
    // target's alone - SDL keeps a render target's viewport apart, so targets stay addressed from 0,0)
    const int drawnWidth = mapping.display.w, drawnHeight = mapping.display.h;
    if (drawnWidth != outputWidth || drawnHeight != outputHeight) {
        SDL_Rect viewport = toSDL(mapping.display);
        SDL_RenderSetViewport(impl->renderer, &viewport);
        PLOG_INFO << "Canvas " << drawnWidth << "x" << drawnHeight << " at " << viewport.x << "," << viewport.y
                  << " in a " << outputWidth << "x" << outputHeight << " window";
    }
}

// the margin of a 4:3 frame (see copyMirroredMargin): the frame is blurred down twice (a quarter of its size, then a
// quarter of that, linear filtering) and back up to a quarter, so the UI at the edges reads as soft colour, not as shapes in the margin
void Renderer::mirrorMargin(void *frameTexture, const Rect &displayRect) {
    if (impl->marginPercent <= 0)
        return;
    SDL_Texture *frame = static_cast<SDL_Texture *>(frameTexture);
    int fw = 0, fh = 0;
    if (SDL_QueryTexture(frame, nullptr, nullptr, &fw, &fh) != 0 || fw < 8 || fh < 8)
        return;
    const int w1 = fw / 4, h1 = fh / 4, w2 = std::max(1, w1 / 4), h2 = std::max(1, h1 / 4);
    const unsigned long lost = impl->targetsLost.load();
    auto ensure = [&](Texture &t, int w, int h) {
        const Size size = t.size();
        if (!t.valid() || size.w != w || size.h != h || impl->marginSoftAt != lost)
            t = createLinearTarget(*this, w, h); // linear: the passes between them blur (no SDL_SetTextureScaleMode)
    };
    ensure(impl->marginSoft1, w1, h1);
    ensure(impl->marginSoft2, w2, h2);
    impl->marginSoftAt = lost;
    if (!impl->marginSoft1.valid() || !impl->marginSoft2.valid())
        return;
    SDL_Texture *t1 = static_cast<SDL_Texture *>(impl->marginSoft1.native());
    SDL_Texture *t2 = static_cast<SDL_Texture *>(impl->marginSoft2.native());
    // the two filter linearly from their creation, the frame target too (clear()); a captured frame follows the
    // program's scale quality (the launcher's "best" is linear)
    SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_NONE);
    SDL_SetTextureBlendMode(t1, SDL_BLENDMODE_NONE);
    SDL_SetTextureBlendMode(t2, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(impl->renderer, t1);
    SDL_RenderCopy(impl->renderer, frame, nullptr, nullptr);
    SDL_SetRenderTarget(impl->renderer, t2);
    SDL_RenderCopy(impl->renderer, t1, nullptr, nullptr);
    SDL_SetRenderTarget(impl->renderer, t1); // and back up to a quarter, softened: the strips are cut from this one
    SDL_RenderCopy(impl->renderer, t2, nullptr, nullptr);
    SDL_SetRenderTarget(impl->renderer, nullptr);
    SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_BLEND); // as the caller had it
    SDL_Rect display = toSDL(displayRect);
    copyMirroredMargin(impl->renderer, t1, display, impl->outputWidth, impl->outputHeight, impl->marginPercent);
}

bool Renderer::setCanvas(int w, int h) {
    if (!impl->fourByThree || w <= 0 || h <= 0)
        return false;
    if (w != impl->width || h != impl->height)
        impl->useCanvas(w, h);
    return true;
}

void Renderer::setSafeMargin(int percent) {
    const int margin = clampSafeMargin(percent);
    const bool changed = margin != impl->marginPercent;
    impl->marginPercent = margin;
    impl->marginSet = true;
    if (impl->fourByThree) {
        impl->useCanvas(impl->width, impl->height); // the next present() places the frame inside the new margin
        // recreate() logged the mapping with the margin it had then (the default before the program's own is set)
        if (changed) {
            PLOG_INFO << "CRT margin " << margin << "%: a " << impl->width << "x" << impl->height << " canvas shown "
                      << impl->display.w << "x" << impl->display.h << " at " << impl->display.x << ","
                      << impl->display.y;
        }
    }
}

int Renderer::safeMargin() const {
    return impl->marginPercent;
}

bool Renderer::setRestCanvas(int w, int h) {
    if (!impl->fourByThree || w <= 0 || h <= 0)
        return false;
    impl->wantRestWidth = impl->restWidth = w;
    impl->wantRestHeight = impl->restHeight = h;
    if (!impl->framing && (w != impl->width || h != impl->height))
        impl->useCanvas(w, h);
    return true;
}

int Renderer::restCanvasWidth() const {
    return impl->restWidth;
}

int Renderer::restCanvasHeight() const {
    return impl->restHeight;
}

bool Renderer::fourByThreeOutput() const {
    return impl->fourByThree;
}

float Renderer::outputScale() const {
    return impl->scale;
}

std::string Renderer::driverName() const {
    SDL_RendererInfo info;
    if (!impl->renderer || SDL_GetRendererInfo(impl->renderer, &info) != 0)
        return "";
    return info.name;
}

Rect Renderer::toOutput(const Rect &r) const {
    if (impl->scale == 1.0f)
        return r;
    int x0 = static_cast<int>(std::lround(r.x * impl->scale));
    int y0 = static_cast<int>(std::lround(r.y * impl->scale));
    int x1 = static_cast<int>(std::lround((r.x + r.w) * impl->scale));
    int y1 = static_cast<int>(std::lround((r.y + r.h) * impl->scale));
    return Rect(x0, y0, x1 - x0, y1 - y0);
}

Renderer::~Renderer() {
    SDL_DelEventWatch(watchTargetsReset, &impl->targetsLost);
    impl->overlay.release();
    if (impl->renderer) {
        SDL_DestroyRenderer(impl->renderer);
    }
    delete impl;
}

void Renderer::clear() {
    // a capture asked for: this frame (it starts here, on the screen) goes into a target of the canvas's size
    if (impl->captureRequested && !impl->capturing && SDL_GetRenderTarget(impl->renderer) == nullptr) {
        Texture t = Texture::createTarget(*this, impl->width, impl->height);
        if (t.valid() && SDL_SetRenderTarget(impl->renderer, static_cast<SDL_Texture *>(t.native())) == 0) {
            impl->captureTarget = t;
            impl->capturing = true;
        }
    }
    // a 4:3 output: a frame starting on the screen goes into the frame target of this frame's canvas (CanvasMapping)
    if (impl->fourByThree && !impl->capturing && !impl->framing && SDL_GetRenderTarget(impl->renderer) == nullptr) {
        const Size size = impl->frameTarget.size();
        if (!impl->frameTarget.valid() || size.w != impl->width || size.h != impl->height) {
            impl->frameTarget = Texture();
            // linear: stretched to the output, and the CRT margin's blur reads it (createLinearTarget)
            impl->frameTarget = createLinearTarget(*this, impl->width, impl->height);
        }
        if (impl->frameTarget.valid() &&
            SDL_SetRenderTarget(impl->renderer, static_cast<SDL_Texture *>(impl->frameTarget.native())) == 0)
            impl->framing = true;
    }
    if (ext_trace::active()) {
        Uint8 r = 0, g = 0, b = 0, a = 0;
        SDL_GetRenderDrawColor(impl->renderer, &r, &g, &b, &a);
        ext_trace::note("clear rgba=" + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + "," +
                        std::to_string(a) + traceTarget(impl->renderer) + traceOutside());
    }
    SDL_RenderClear(impl->renderer);
}
//*******************************
// debugShot
//*******************************
// AB_SHOT=<file.bmp> in the environment: what the renderer is about to present, saved every 3 s (a %d in
// the name numbers the frames) - a look at a display one cannot see, or at a PC whose screen is in use.
static void debugShot(SDL_Renderer *renderer) {
    static const char *path = nullptr;
    static bool checked = false;
    static Uint32 last = 0;
    static int count = 0;
    if (!checked) {
        path = std::getenv("AB_SHOT");
        checked = true;
    }
    if (!path)
        return;
    Uint32 now = SDL_GetTicks();
    if (last != 0 && now - last < 3000)
        return;
    last = now;
    int w = 0, h = 0;
    if (SDL_GetRendererOutputSize(renderer, &w, &h) != 0)
        return;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s)
        return;
    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0) {
        char name[512];
        snprintf(name, sizeof(name), path, count++);
        SDL_SaveBMP(s, name);
    }
    SDL_FreeSurface(s);
}

//*******************************
// the frame cache
//*******************************
void Renderer::setFrameCache(bool enabled) {
    std::lock_guard<std::mutex> lock(impl->frame.mutex);
    impl->frame.enabled = enabled;
}

unsigned long Renderer::frameCount() const {
    std::lock_guard<std::mutex> lock(impl->frame.mutex);
    return impl->frame.frames;
}

unsigned long Renderer::requestFrameCopy() {
    std::lock_guard<std::mutex> lock(impl->frame.mutex);
    impl->frame.requested = true;
    return impl->frame.frames;
}

unsigned long Renderer::copiedFrame() const {
    std::lock_guard<std::mutex> lock(impl->frame.mutex);
    return impl->frame.copied;
}

bool Renderer::saveLastFrame(const std::string &path) {
    std::lock_guard<std::mutex> lock(impl->frame.mutex);
    Impl::FrameCache &f = impl->frame;
    if (f.pixels.empty())
        return false;
    SDL_Surface *s =
        SDL_CreateRGBSurfaceWithFormatFrom(f.pixels.data(), f.w, f.h, 32, f.pitch, SDL_PIXELFORMAT_ARGB8888);
    if (!s)
        return false;
    int rc;
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".png") == 0)
        rc = IMG_SavePNG(s, path.c_str());
    else
        rc = SDL_SaveBMP(s, path.c_str());
    SDL_FreeSurface(s);
    return rc == 0;
}

bool Renderer::encodeLastFramePng(std::vector<unsigned char> &out) {
    std::lock_guard<std::mutex> lock(impl->frame.mutex);
    Impl::FrameCache &f = impl->frame;
    if (f.pixels.empty())
        return false;
    SDL_Surface *s =
        SDL_CreateRGBSurfaceWithFormatFrom(f.pixels.data(), f.w, f.h, 32, f.pitch, SDL_PIXELFORMAT_ARGB8888);
    if (!s)
        return false;
    out.clear();
    SDL_RWops *rw = SDL_AllocRW();
    if (!rw) {
        SDL_FreeSurface(s);
        return false;
    }
    rw->hidden.unknown.data1 = new MemWriter{&out};
    rw->size = memWriterSize;
    rw->seek = memWriterSeek;
    rw->read = memWriterRead;
    rw->write = memWriterWrite;
    rw->close = memWriterClose;
    int rc = IMG_SavePNG_RW(s, rw, 1); // freesrc = 1: memWriterClose frees the SDL_RWops itself
    SDL_FreeSurface(s);
    return rc == 0;
}

unsigned long Renderer::copyLastFrame(std::vector<unsigned char> &pixels, int &w, int &h, int &pitch) const {
    std::lock_guard<std::mutex> lock(impl->frame.mutex);
    const Impl::FrameCache &f = impl->frame;
    if (f.pixels.empty())
        return 0;
    pixels = f.pixels;
    w = f.w;
    h = f.h;
    pitch = f.pitch;
    return f.copied;
}

void Renderer::captureNextFrame() {
    impl->captureRequested = true;
    impl->captureSilent = false;
}

void Renderer::captureNextFrameSilently() {
    impl->captureRequested = true;
    impl->captureSilent = true;
}

bool Renderer::silentCapturePending() const {
    return impl->captureRequested && impl->captureSilent;
}

Texture Renderer::lastCapture() const {
    return impl->capture;
}

void Renderer::present() {
    if (ext_trace::active()) {
        int ow = 0, oh = 0, ww = 0, wh = 0;
        SDL_GetRendererOutputSize(impl->renderer, &ow, &oh);
        if (impl->window)
            SDL_GetWindowSize(impl->window, &ww, &wh);
        ext_trace::note("present canvas=" + std::to_string(impl->width) + "x" + std::to_string(impl->height) +
                        " output=" + std::to_string(ow) + "x" + std::to_string(oh) + " window=" + std::to_string(ww) +
                        "x" + std::to_string(wh) + (impl->capturing ? " capturing" : "") +
                        (impl->captureRequested ? " capture-asked" : "") + traceTarget(impl->renderer) +
                        traceOutside());
    }
    // never present while a render target (other than the capture's own or the 4:3 frame's, handled below) is current:
    // the screen would show an unfilled frame
    if (!impl->capturing && SDL_GetRenderTarget(impl->renderer) != impl->screenTarget()) {
        PLOG_WARNING << "Renderer::present with a render target set - back to the screen first";
        SDL_SetRenderTarget(impl->renderer, impl->screenTarget());
        impl->targetStack.clear();
    }
    // a 4:3 output: the frame is not on the window until it is copied there below
    if (!impl->fourByThree)
        debugShot(impl->renderer);
    // the canvas a frame was given (setCanvas) lasts until this present, however it ends
    struct CanvasReset {
        Impl &impl;
        ~CanvasReset() {
            impl.framing = false;
            if (impl.width != impl.restWidth || impl.height != impl.restHeight)
                impl.useCanvas(impl.restWidth, impl.restHeight);
        }
    } canvasReset{*impl};
    // where the frame goes on the window: the whole viewport, or on a 4:3 output the frame's canvas at its shape
    SDL_Rect displayRect = toSDL(impl->display);
    const SDL_Rect *windowRect = impl->fourByThree ? &displayRect : nullptr;
    if (impl->capturing) {
        // the frame is in the target: it is the capture, and what the screen shows
        impl->capturing = false;
        impl->captureRequested = false;
        const bool silent = impl->captureSilent;
        impl->captureSilent = false;
        SDL_SetRenderTarget(impl->renderer, nullptr);
        SDL_Texture *frame = static_cast<SDL_Texture *>(impl->captureTarget.native());
        if (silent) {
            // a snapshot for a backdrop, not a frame to show: the window keeps what it shows (the screen the snapshot
            // is of is not what is on it - the System menu over the carousel), so there is no copy and no swap
            SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_NONE);
            impl->capture = impl->captureTarget;
            impl->captureTarget = Texture();
            if (ext_trace::active())
                ext_trace::note("capture taken " + std::to_string(impl->capture.size().w) + "x" +
                                std::to_string(impl->capture.size().h) + " (silent: no window copy, no present)");
            impl->stats.copies = 0; // per frame, for the overlay
            impl->stats.switches = 0;
            impl->stats.lastTexture = nullptr;
            return;
        }
        Uint8 r = 0, g = 0, b = 0, a = 0;
        SDL_GetRenderDrawColor(impl->renderer, &r, &g, &b, &a);
        SDL_SetRenderDrawColor(impl->renderer, 0, 0, 0, 255);
        SDL_RenderClear(impl->renderer);
        SDL_SetRenderDrawColor(impl->renderer, r, g, b, a);
        // the window copy blends over the opaque black clear, so a transparent pixel of the capture (the frame is
        // cleared to transparent black) reaches the window as opaque black - an alpha-0 pixel on a Wayland ARGB
        // surface shows what is behind the window (BUG-31)
        SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_BLEND);
        if (windowRect)
            mirrorMargin(frame, impl->display);
        SDL_RenderCopy(impl->renderer, frame, nullptr, windowRect);
        SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_NONE); // the capture stays opaque, as a read-back frame was
        impl->capture = impl->captureTarget;
        impl->captureTarget = Texture();
        if (ext_trace::active())
            ext_trace::note("capture taken " + std::to_string(impl->capture.size().w) + "x" +
                            std::to_string(impl->capture.size().h) + " (black clear + copy to the window)");
    } else if (impl->framing) {
        // a 4:3 output's frame: onto the window at the canvas's shape (stretched to the output's pixel aspect), over
        // opaque black for the reason the capture's copy above gives (BUG-31), the bars black
        SDL_SetRenderTarget(impl->renderer, nullptr);
        Uint8 r = 0, g = 0, b = 0, a = 0;
        SDL_GetRenderDrawColor(impl->renderer, &r, &g, &b, &a);
        SDL_SetRenderDrawColor(impl->renderer, 0, 0, 0, 255);
        SDL_RenderClear(impl->renderer);
        SDL_SetRenderDrawColor(impl->renderer, r, g, b, a);
        SDL_Texture *frame = static_cast<SDL_Texture *>(impl->frameTarget.native());
        SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_BLEND);
        if (windowRect)
            mirrorMargin(frame, impl->display);
        SDL_RenderCopy(impl->renderer, frame, nullptr, windowRect);
    } else if (impl->captureRequested) {
        // a frame that never called clear(): read it back
        impl->captureRequested = false;
        impl->captureSilent = false; // nothing to keep off the window: the frame is on it
        int w = 0, h = 0;
        if (SDL_GetRendererOutputSize(impl->renderer, &w, &h) == 0) {
            std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 4);
            if (SDL_RenderReadPixels(impl->renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels.data(), w * 4) == 0) {
                SDL_Texture *t =
                    SDL_CreateTexture(impl->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
                if (t) {
                    SDL_UpdateTexture(t, nullptr, pixels.data(), w * 4);
                    impl->capture = Texture(t);
                }
            }
        }
    }
    if (impl->fourByThree)
        debugShot(impl->renderer); // the window as it will show, the frame copied in
    // after the capture (a backdrop without it), before the frame cache (the DebugDriver's shots show it)
    impl->overlay.beforePresent(impl->renderer, impl->stats.copies, impl->scale);
    {
        // the frame cache: a copy of what is about to be shown, for saveLastFrame() - when one was asked for
        std::lock_guard<std::mutex> lock(impl->frame.mutex);
        Impl::FrameCache &f = impl->frame;
        f.frames++;
        int w = 0, h = 0;
        if (f.enabled && f.requested && SDL_GetRendererOutputSize(impl->renderer, &w, &h) == 0) {
            const int pitch = w * 4;
            f.pixels.resize(static_cast<size_t>(pitch) * h);
            f.w = w;
            f.h = h;
            f.pitch = pitch;
            SDL_RenderReadPixels(impl->renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, f.pixels.data(), pitch);
            f.requested = false;
            f.copied = f.frames;
        }
    }
    SDL_RenderPresent(impl->renderer);
    ext_trace::frameDone();
    impl->capFrameRate();
    impl->overlay.afterPresent();
    Impl::Stats &st = impl->stats;
    if (!statsEnabled()) {
        st.copies = 0; // per frame, for the overlay
        st.switches = 0;
        st.lastTexture = nullptr;
        return;
    }
    unsigned int now = SDL_GetTicks();
    if (st.lastPresent != 0) {
        long ms = static_cast<long>(now - st.lastPresent);
        st.frames++;
        st.sumMs += ms;
        st.maxMs = std::max(st.maxMs, ms);
        if (ms > 20)
            st.slowFrames++;
        st.sumCopies += st.copies;
        st.sumSwitches += st.switches;
    }
    st.lastPresent = now;
    st.copies = 0;
    st.switches = 0;
    st.lastTexture = nullptr;
    if (st.lastReport == 0)
        st.lastReport = now;
    if (now - st.lastReport >= 5000 && st.frames > 0) {
        PLOG_INFO << "Frames: " << st.frames << " in " << (now - st.lastReport) << " ms, avg " << (st.sumMs / st.frames)
                  << " ms, worst " << st.maxMs << " ms, " << st.slowFrames << " over 20 ms; per frame "
                  << (st.sumCopies / st.frames) << " copies, " << (st.sumSwitches / st.frames) << " texture switches";
        st.frames = st.slowFrames = st.sumMs = st.maxMs = st.sumCopies = st.sumSwitches = 0;
        st.lastReport = now;
    }
}

void Renderer::setDrawColor(Color c) {
    SDL_SetRenderDrawColor(impl->renderer, c.r, c.g, c.b, c.a);
}

Color Renderer::drawColor() const {
    Uint8 r, g, b, a;
    SDL_GetRenderDrawColor(impl->renderer, &r, &g, &b, &a);
    return Color(r, g, b, a);
}

void Renderer::setBlendMode(BlendMode mode) {
    SDL_SetRenderDrawBlendMode(impl->renderer, toSDL(mode));
}

void Renderer::fillRect(const Rect &r) {
    impl->noteCopy(nullptr, 1);
    SDL_Rect sr = toSDL(toOutput(r));
    SDL_RenderFillRect(impl->renderer, &sr);
}

void Renderer::fillRect() {
    SDL_RenderFillRect(impl->renderer, nullptr);
}

void Renderer::fillRects(const Rect *rects, int count) {
    if (count <= 0)
        return;
    impl->noteCopy(nullptr, count);
    // thread_local so repeated calls (once per frame, per color bucket) don't reallocate
    thread_local std::vector<SDL_Rect> buffer;
    buffer.resize(count);
    for (int i = 0; i < count; i++)
        buffer[i] = toSDL(toOutput(rects[i]));
    SDL_RenderFillRects(impl->renderer, buffer.data(), count);
}

void Renderer::drawRect(const Rect &r) {
    SDL_Rect sr = toSDL(toOutput(r));
    SDL_RenderDrawRect(impl->renderer, &sr);
}

void Renderer::drawLine(Point a, Point b) {
    float k = impl->scale;
    SDL_RenderDrawLine(impl->renderer, static_cast<int>(std::lround(a.x * k)), static_cast<int>(std::lround(a.y * k)),
                       static_cast<int>(std::lround(b.x * k)), static_cast<int>(std::lround(b.y * k)));
}

void Renderer::copy(const Texture &tex, const Rect *src, const Rect *dst) {
    SDL_Rect ssrc, sdst;
    SDL_Rect *psrc = nullptr, *pdst = nullptr;
    if (src) {
        // a render target is addressed in logical pixels like the screen; a loaded image in its own
        ssrc = toSDL(tex.pixelScale() == 1.0f ? *src : scaleRect(*src, tex.pixelScale()));
        psrc = &ssrc;
    } else if (!dst && impl->fourByThree && SDL_GetRenderTarget(impl->renderer) == impl->screenTarget()) {
        // a 4:3 output: a whole picture over the whole canvas at its own shape - a frame of the other canvas (the 4:3
        // launcher's snapshot under a 16:9 menu) shows its middle, not squeezed (coverCrop); the same shape, all of it
        const Size size = tex.size();
        const Rect crop = coverCrop(size.w, size.h, impl->width, impl->height);
        if (crop.w != size.w || crop.h != size.h) {
            ssrc = toSDL(tex.pixelScale() == 1.0f ? crop : scaleRect(crop, tex.pixelScale()));
            psrc = &ssrc;
        }
    }
    if (dst) {
        sdst = toSDL(toOutput(*dst));
        pdst = &sdst;
    }
    impl->noteCopy(tex.native(), 1);
    SDL_RenderCopy(impl->renderer, static_cast<SDL_Texture *>(tex.native()), psrc, pdst);
}

void Renderer::copy(const Texture &tex, const Rect *src, const FRect &dst) {
    SDL_Rect ssrc;
    SDL_Rect *psrc = nullptr;
    if (src) {
        ssrc = toSDL(tex.pixelScale() == 1.0f ? *src : scaleRect(*src, tex.pixelScale()));
        psrc = &ssrc;
    }
    const float k = impl->scale;
    impl->noteCopy(tex.native(), 1);
#if SDL_VERSION_ATLEAST(2, 0, 10)
    SDL_FRect fdst{dst.x * k, dst.y * k, dst.w * k, dst.h * k};
    SDL_RenderCopyF(impl->renderer, static_cast<SDL_Texture *>(tex.native()), psrc, &fdst);
#else
    SDL_Rect sdst = toSDL(toOutput(Rect(static_cast<int>(std::lround(dst.x)), static_cast<int>(std::lround(dst.y)),
                                        static_cast<int>(std::lround(dst.w)), static_cast<int>(std::lround(dst.h)))));
    SDL_RenderCopy(impl->renderer, static_cast<SDL_Texture *>(tex.native()), psrc, &sdst);
#endif
}

void Renderer::copyTrapezoid(const Texture &tex, const Rect *src, VerticalEdge left, VerticalEdge right, Color tint) {
    copyTrapezoidFaded(tex, src, left, right, tint, tint, false);
}

void Renderer::copyTrapezoidFaded(const Texture &tex, const Rect *src, VerticalEdge left, VerticalEdge right,
                                  Color topTint, Color bottomTint, bool flipVertically) {
    Rect s;
    if (src) {
        s = tex.pixelScale() == 1.0f ? *src : scaleRect(*src, tex.pixelScale());
    } else {
        Size size = tex.size();
        s = scaleRect(Rect(0, 0, size.w, size.h), tex.pixelScale());
    }
    // the strips are output columns: the edges go to output pixels first
    const float k = impl->scale;
    left = VerticalEdge(left.x * k, left.top * k, left.bottom * k);
    right = VerticalEdge(right.x * k, right.top * k, right.bottom * k);
    bool mirrored = false;
    if (left.x > right.x) {
        std::swap(left, right);
        mirrored = true;
    }
    float width = right.x - left.x;
    float leftHeight = left.bottom - left.top, rightHeight = right.bottom - right.top;
    if (width < 1.0f || s.w <= 0 || s.h <= 0 || leftHeight <= 0.0f || rightHeight <= 0.0f)
        return;

    // perspective-correct texture mapping: 1/depth is linear across the screen, and each side's height is
    // proportional to 1/depth, so the source column for screen fraction t is t*hR / lerp(hL, hR, t)
    auto sourceAt = [&](float t) {
        float u = t * rightHeight / (leftHeight + (rightHeight - leftHeight) * t);
        return mirrored ? 1.0f - u : u;
    };
    auto *native = static_cast<SDL_Texture *>(tex.native());
    int xFirst = static_cast<int>(std::floor(left.x));
    int xLast = static_cast<int>(std::ceil(right.x));

#if SDL_VERSION_ATLEAST(2, 0, 18)
    // one triangle mesh: a column of vertices at the left side, at every whole output column between, and at
    // the right side, each with its own perspective-correct texture column; the tint rides in the vertex
    // colours, so no texture state changes and one draw call per trapezoid. The sloping top and bottom edges
    // get a skirt one output pixel wide that fades to nothing (the edge row of the texture at alpha 0 on its
    // outer side): without MSAA a GPU draws a triangle's edge hard, and a turned cover's top and bottom are
    // the slopes that showed as steps - the texture's own transparent margin is under a pixel on a small cover
    int texW = 0, texH = 0;
    SDL_QueryTexture(native, nullptr, nullptr, &texW, &texH);
    if (texW <= 0 || texH <= 0)
        return;
    thread_local std::vector<float> xy, uv;
    thread_local std::vector<SDL_Color> colors;
    thread_local std::vector<int> indices;
    xy.clear();
    uv.clear();
    colors.clear();
    indices.clear();
    float v0 = static_cast<float>(s.y) / texH, v1 = static_cast<float>(s.y + s.h) / texH;
    if (flipVertically)
        std::swap(v0, v1);
    const SDL_Color topColor{topTint.r, topTint.g, topTint.b, topTint.a};
    const SDL_Color bottomColor{bottomTint.r, bottomTint.g, bottomTint.b, bottomTint.a};
    const SDL_Color topClear{topTint.r, topTint.g, topTint.b, 0};
    const SDL_Color bottomClear{bottomTint.r, bottomTint.g, bottomTint.b, 0};
    const float skirt = 1.0f;
    auto addColumn = [&](float x) {
        float t = std::min(1.0f, std::max(0.0f, (x - left.x) / width));
        float top = left.top + (right.top - left.top) * t;
        float bottom = left.bottom + (right.bottom - left.bottom) * t;
        // clamped: SDL 2.0.18 (the console's) refuses the whole call for a u a rounding error past 1
        float u = std::min(1.0f, std::max(0.0f, (s.x + sourceAt(t) * s.w) / texW));
        const float ys[4] = {top - skirt, top, bottom, bottom + skirt};
        const float vs[4] = {v0, v0, v1, v1};
        const SDL_Color cs[4] = {topClear, topColor, bottomColor, bottomClear};
        for (int r = 0; r < 4; r++) {
            xy.push_back(x);
            xy.push_back(ys[r]);
            uv.push_back(u);
            uv.push_back(vs[r]);
            colors.push_back(cs[r]);
        }
    };
    addColumn(left.x);
    for (int x = xFirst + 1; x < xLast; x++) {
        if (x > left.x && x < right.x)
            addColumn(static_cast<float>(x));
    }
    addColumn(right.x);
    const int columns = static_cast<int>(colors.size() / 4);
    for (int i = 0; i + 1 < columns; i++) {
        const int a = 4 * i, b = a + 4;
        for (int r = 0; r < 3; r++) { // the top skirt, the face, the bottom skirt
            indices.push_back(a + r);
            indices.push_back(a + r + 1);
            indices.push_back(b + r);
            indices.push_back(a + r + 1);
            indices.push_back(b + r + 1);
            indices.push_back(b + r);
        }
    }
    impl->noteCopy(native, 1);
    SDL_SetTextureColorMod(native, 255, 255, 255); // the tint is in the vertices; a mod left on it would double up
#if SDL_VERSION_ATLEAST(2, 0, 20)
    const SDL_Color *vertexColors = colors.data();
#else
    // 2.0.18 (the console's) takes the colours as ints - the same four bytes each
    const int *vertexColors = reinterpret_cast<const int *>(colors.data());
#endif
    if (SDL_RenderGeometryRaw(impl->renderer, native, xy.data(), 2 * sizeof(float), vertexColors, sizeof(SDL_Color),
                              uv.data(), 2 * sizeof(float), static_cast<int>(colors.size()), indices.data(),
                              static_cast<int>(indices.size()), sizeof(int)) == 0)
        return;
    static bool reported = false; // then the strips below draw it
    if (!reported) {
        reported = true;
        PLOG_WARNING << "SDL_RenderGeometryRaw failed, drawing strips instead: " << SDL_GetError();
    }
#endif

    impl->noteCopy(native, xLast - xFirst);
    const Color tint((topTint.r + bottomTint.r) / 2, (topTint.g + bottomTint.g) / 2, (topTint.b + bottomTint.b) / 2,
                     (topTint.a + bottomTint.a) / 2);
    Uint8 modR = 255, modG = 255, modB = 255, modA = 255; // the tint goes on as the texture's mods for the strips
    SDL_GetTextureColorMod(native, &modR, &modG, &modB);
    SDL_GetTextureAlphaMod(native, &modA);
    SDL_SetTextureColorMod(native, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(native, tint.a);
    for (int x = xFirst; x < xLast; x++) {
        float t0 = std::min(1.0f, std::max(0.0f, (x - left.x) / width));
        float t1 = std::min(1.0f, std::max(0.0f, (x + 1 - left.x) / width));
        if (t1 <= t0)
            continue;
        float u0 = sourceAt(t0), u1 = sourceAt(t1);
        if (u0 > u1)
            std::swap(u0, u1);
        // the strip of source columns this screen column shows; at least one column wide
        int c0 = std::min(s.w - 1, static_cast<int>(u0 * s.w));
        int c1 = std::min(s.w, std::max(c0 + 1, static_cast<int>(std::ceil(u1 * s.w))));
        float tm = (t0 + t1) * 0.5f;
        float top = left.top + (right.top - left.top) * tm;
        float bottom = left.bottom + (right.bottom - left.bottom) * tm;
        SDL_Rect sr{s.x + c0, s.y, c1 - c0, s.h};
#if SDL_VERSION_ATLEAST(2, 0, 10)
        // the ends placed to a fraction of a pixel, so that a multisampled context can smooth the slope
        SDL_FRect dr{static_cast<float>(x), top, 1.0f, std::max(1.0f, bottom - top)};
        SDL_RenderCopyExF(impl->renderer, native, &sr, &dr, 0.0, nullptr,
                          flipVertically ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE);
#else
        int topPixel = static_cast<int>(std::lround(top)), bottomPixel = static_cast<int>(std::lround(bottom));
        SDL_Rect dr{x, topPixel, 1, std::max(1, bottomPixel - topPixel)};
        SDL_RenderCopyEx(impl->renderer, native, &sr, &dr, 0.0, nullptr,
                         flipVertically ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE);
#endif
    }
    SDL_SetTextureColorMod(native, modR, modG, modB);
    SDL_SetTextureAlphaMod(native, modA);
}

void Renderer::setTarget(Texture *target) {
    // "the screen" is the capture's target while a frame is being captured
    if (ext_trace::active())
        ext_trace::note(target ? "setTarget texture" : "setTarget screen");
    SDL_SetRenderTarget(impl->renderer, target ? static_cast<SDL_Texture *>(target->native()) : impl->screenTarget());
}

void Renderer::pushTarget(Texture *target) {
    impl->targetStack.push_back(SDL_GetRenderTarget(impl->renderer));
    setTarget(target);
}

void Renderer::popTarget() {
    SDL_Texture *previous = nullptr;
    if (!impl->targetStack.empty()) {
        previous = impl->targetStack.back();
        impl->targetStack.pop_back();
    } else {
        PLOG_WARNING << "Renderer::popTarget without a pushTarget - back to the screen";
    }
    if (ext_trace::active())
        ext_trace::note(previous ? "popTarget texture" : "popTarget screen");
    SDL_SetRenderTarget(impl->renderer, previous ? previous : impl->screenTarget());
}

unsigned long Renderer::targetsLost() const {
    return impl->targetsLost.load();
}

int Renderer::width() const {
    return impl->width;
}
int Renderer::height() const {
    return impl->height;
}

void *Renderer::native() const {
    return impl->renderer;
}

} // namespace ableem
