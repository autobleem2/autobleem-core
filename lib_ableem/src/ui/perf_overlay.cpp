#include "perf_overlay.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace ableem {

namespace {
//*******************************
// the font
//*******************************
// 5x7, a row per byte, bit 4 the leftmost column; upper case only (the text is upper-cased), anything
// without a glyph is a blank
struct Glyph {
    char c;
    unsigned char rows[7];
};
const Glyph Font5x7[] = {
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}}, {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}}, {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}}, {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}}, {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}}, {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}, {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}}, {'D', {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}}, {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}}, {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}}, {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}}, {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}}, {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}}, {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}}, {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}}, {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}}, {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'%', {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}}, {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}}, {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}}, {'/', {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00}},
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
};
const int GlyphW = 5, GlyphH = 7, CellW = 6, CellH = 9, Pad = 3;
const Uint32 Ink = 0xFFFFFFFF, Paper = 0xFF000000; // ARGB8888

const unsigned char *glyphFor(char c) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const Glyph &g : Font5x7)
        if (g.c == c)
            return g.rows;
    return nullptr;
}

double msBetween(Uint64 a, Uint64 b) {
    return static_cast<double>(b - a) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
}
} // namespace

//*******************************
// PerfOverlay::beforePresent
//*******************************
void PerfOverlay::beforePresent(SDL_Renderer *renderer, long copies, float outputScale) {
    if (!enabled) {
        lastPresent = windowStart = 0; // measured afresh when it is switched on
        lines.clear();
        havePrevious = false;
        return;
    }
    const Uint64 now = SDL_GetPerformanceCounter();
    if (lastPresent != 0) {
        const double frameMs = msBetween(lastPresent, now);
        const double workMs = msBetween(workStart, now);
        frames++;
        sumFrameMs += frameMs;
        maxFrameMs = std::max(maxFrameMs, frameMs);
        sumWorkMs += workMs;
        maxWorkMs = std::max(maxWorkMs, workMs);
        sumCopies += copies;
    }
    lastPresent = now;
    if (windowStart == 0)
        windowStart = now;
    if (msBetween(windowStart, now) >= 1000.0 || lines.empty()) {
        rebuildText(renderer);
        windowStart = now;
    }
    if (textDirty)
        rebuildTexture(renderer);
    if (!texture || SDL_GetRenderTarget(renderer) != nullptr)
        return; // only ever onto the window

    SDL_Rect viewport;
    SDL_RenderGetViewport(renderer, &viewport);
    const int k = std::max(1, static_cast<int>(std::lround(2.0f * outputScale)));
    const int margin = k * 2;
    SDL_Rect dst{margin, viewport.h - texH * k - margin, texW * k, texH * k};
    SDL_Rect clip;
    const SDL_bool clipped = SDL_RenderIsClipEnabled(renderer);
    SDL_RenderGetClipRect(renderer, &clip);
    SDL_RenderSetClipRect(renderer, nullptr);
    SDL_RenderCopy(renderer, texture, nullptr, &dst);
    if (clipped)
        SDL_RenderSetClipRect(renderer, &clip);
}

void PerfOverlay::afterPresent() {
    workStart = SDL_GetPerformanceCounter();
}

void PerfOverlay::release() {
    if (texture)
        SDL_DestroyTexture(texture);
    texture = nullptr;
    textDirty = true;
    driver.clear();
}

//*******************************
// PerfOverlay::rebuildText
//*******************************
void PerfOverlay::rebuildText(SDL_Renderer *renderer) {
    if (driver.empty()) {
        SDL_RendererInfo info;
        if (SDL_GetRendererInfo(renderer, &info) == 0)
            driver = info.name;
    }
    const int cores = SDL_GetCPUCount();
    const ProcessSample now = ProcessStats::sample();
    ProcessStats::Load load;
    if (havePrevious)
        load = ProcessStats::cpuLoad(previous, now, cores);
    previous = now;
    havePrevious = true;

    char a[160], b[160];
    const double seconds = frames > 0 ? sumFrameMs / 1000.0 : 0;
    const double fps = seconds > 0 ? frames / seconds : 0;
    if (frames > 0)
        snprintf(a, sizeof(a), "FPS %.0f  FRAME %.1f MS (MAX %.1f)  WORK %.1f MS (MAX %.1f)  %ld COPIES", fps,
                 sumFrameMs / frames, maxFrameMs, sumWorkMs / frames, maxWorkMs, sumCopies / frames);
    else
        snprintf(a, sizeof(a), "FPS -");
    std::string second;
    char part[64];
    if (load.process >= 0) {
        snprintf(part, sizeof(part), "CPU %.0f%%", load.process);
        second += part;
    } else {
        second += "CPU -";
    }
    if (load.system >= 0) {
        snprintf(part, sizeof(part), " (SYSTEM %.0f%%)", load.system);
        second += part;
    }
    snprintf(part, sizeof(part), "  CORES %d", cores);
    second += part;
    if (now.threads >= 0) {
        snprintf(part, sizeof(part), "  THREADS %d", now.threads);
        second += part;
    }
    if (now.rssBytes >= 0) {
        snprintf(part, sizeof(part), "  MEM %lld MB", static_cast<long long>(now.rssBytes / (1024 * 1024)));
        second += part;
    }
    if (now.temperatureMilliC >= 0) {
        snprintf(part, sizeof(part), "  TEMP %dC", now.temperatureMilliC / 1000);
        second += part;
    }
    snprintf(b, sizeof(b), "%s  %s", second.c_str(), driver.c_str());

    lines = {a, b};
    textDirty = true;
    frames = 0;
    sumFrameMs = maxFrameMs = sumWorkMs = maxWorkMs = 0;
    sumCopies = 0;
}

//*******************************
// PerfOverlay::rebuildTexture
//*******************************
void PerfOverlay::rebuildTexture(SDL_Renderer *renderer) {
    textDirty = false;
    size_t longest = 0;
    for (const std::string &l : lines)
        longest = std::max(longest, l.size());
    const int w = Pad * 2 + static_cast<int>(longest) * CellW - 1;
    const int h = Pad * 2 + static_cast<int>(lines.size()) * CellH - 2;
    std::vector<Uint32> pixels(static_cast<size_t>(w) * h, Paper);
    for (size_t li = 0; li < lines.size(); li++) {
        const int y0 = Pad + static_cast<int>(li) * CellH;
        for (size_t ci = 0; ci < lines[li].size(); ci++) {
            const unsigned char *rows = glyphFor(lines[li][ci]);
            if (!rows)
                continue;
            const int x0 = Pad + static_cast<int>(ci) * CellW;
            for (int y = 0; y < GlyphH; y++)
                for (int x = 0; x < GlyphW; x++)
                    if (rows[y] & (0x10 >> x))
                        pixels[static_cast<size_t>(y0 + y) * w + x0 + x] = Ink;
        }
    }
    if (!texture || w != texW || h != texH) {
        if (texture)
            SDL_DestroyTexture(texture);
        // whole-pixel scaling: nearest, whatever the app asked for its own textures
        const char *quality = SDL_GetHint(SDL_HINT_RENDER_SCALE_QUALITY);
        const std::string previousQuality = quality ? quality : "";
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, previousQuality.empty() ? nullptr : previousQuality.c_str());
        texW = w;
        texH = h;
    }
    if (texture)
        SDL_UpdateTexture(texture, nullptr, pixels.data(), w * static_cast<int>(sizeof(Uint32)));
}

} // namespace ableem
