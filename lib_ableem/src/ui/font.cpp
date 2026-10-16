#include "ableem/ui/font.h"
#include "ableem/ui/renderer.h"
#include "font_atlas.h"
#include "sdl_common.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
#include <ableem/engine/log.h>

#ifdef ABLEEM_TEXT_RUST
#include "ab_text.h"
#endif

namespace ableem {

namespace {
void destroyFont(void *f) {
    if (f)
        FC_FreeFont(static_cast<FC_Font *>(f));
}
FC_AlignEnum toFC(Align a) {
    switch (a) {
    case Align::Center:
        return FC_ALIGN_CENTER;
    case Align::Right:
        return FC_ALIGN_RIGHT;
    case Align::Left:
    default:
        return FC_ALIGN_LEFT;
    }
}

#ifdef ABLEEM_TEXT_RUST
//*******************************
// the Rust engine
//*******************************
// Which engine draws is decided once per process: Rust, unless AB_TEXT=fc (a way back on a device without a
// new install, for one release).
bool rustText() {
    static const bool on = [] {
        const char *e = getenv("AB_TEXT");
        bool rust = !(e && std::strcmp(e, "fc") == 0);
#ifdef ABLEEM_RENDER_GL
        // SDL_FontCache draws through SDL itself, past the renderer backend
        if (!rust && gl::active()) {
            PLOG_WARNING << "AB_TEXT=fc is ignored: the renderer backend draws, which only the Rust engine draws through";
            rust = true;
        }
#endif
        PLOG_INFO << "Text engine: " << (rust ? "Rust (ab_text)" : "SDL_FontCache (AB_TEXT=fc)");
        return rust;
    }();
    return on;
}

void freeRustFont(void *f) {
    abt_font_free(static_cast<AbtFont *>(f));
}

const AbtFont *rs(const std::shared_ptr<void> &handle) {
    return static_cast<const AbtFont *>(handle.get());
}

// The atlas's pages as SDL textures, made on the renderer's first text and dropped with the renderer (or when
// its targets were lost: every page is uploaded again). One per process - there is one renderer at a time.
struct AtlasTextures {
    SDL_Renderer *owner = nullptr;
    unsigned long lostStamp = 0;
    std::vector<SDL_Texture *> pages;
    std::vector<uint8_t> scratch; // one page of RGBA, what a changed rectangle is copied through
    std::vector<AbtQuad> quads;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
#endif

    void drop(bool destroy) {
        if (destroy)
            for (SDL_Texture *t : pages)
                SDL_DestroyTexture(t);
        pages.clear();
        owner = nullptr;
    }

    // the textures matching the engine's pages, with every changed rectangle uploaded
    bool sync(Renderer &renderer) {
        auto *r = static_cast<SDL_Renderer *>(renderer.native());
        if (owner != r || lostStamp != renderer.targetsLost()) {
            drop(owner == r); // another renderer: the old one took its textures with it
            owner = r;
            lostStamp = renderer.targetsLost();
            abt_atlas_all_dirty();
        }
        const int size = abt_atlas_page_size();
        const int pitch = size * 4;
        while (static_cast<int>(pages.size()) < abt_atlas_pages()) {
            SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, size, size);
            if (!t) {
                PLOG_ERROR << "Cannot make a glyph atlas page: " << SDL_GetError();
                return false;
            }
            // (no scale mode set: glyphs land on whole pixels at 1:1, so any filter samples texel centres - and the
            // console's SDL 2.0.18 crashes in SDL_SetTextureScaleMode on GLES2, test_crt_frame keeps it out)
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            pages.push_back(t);
        }
        if (scratch.size() < static_cast<size_t>(pitch) * size)
            scratch.resize(static_cast<size_t>(pitch) * size);
        for (size_t p = 0; p < pages.size(); p++) {
            AbtRect rc;
            if (abt_atlas_take_dirty(static_cast<int32_t>(p), &rc, scratch.data(), pitch)) {
                SDL_Rect area{rc.x, rc.y, rc.w, rc.h};
                SDL_UpdateTexture(pages[p], &area, scratch.data() + static_cast<size_t>(rc.y) * pitch + rc.x * 4,
                                  pitch);
            }
        }
        return true;
    }
};

AtlasTextures &atlas() {
    // never destroyed: the atlas is first made on the first text, after the Gui static was built, so a static object
    // would be destroyed before ~Renderer (in ~Gui) calls fontAtlasRelease() into it
    static AtlasTextures *a = new AtlasTextures;
    return *a;
}

// draws the text with its top-left at (x, y) output pixels; returns the number of glyphs drawn
int drawRust(const AbtFont *font, Renderer &renderer, int x, int y, int wrapWidth, int align, Color color,
             const std::string &text) {
    AtlasTextures &a = atlas();
    size_t n = abt_quads(font, text.data(), text.size(), wrapWidth, align, a.quads.data(), a.quads.size());
    if (n > a.quads.size()) {
        a.quads.resize(n + n / 2);
        n = abt_quads(font, text.data(), text.size(), wrapWidth, align, a.quads.data(), a.quads.size());
    }
    if (n == 0 || !a.sync(renderer))
        return 0;
    auto *r = static_cast<SDL_Renderer *>(renderer.native());
    const float inv = 1.0f / static_cast<float>(abt_atlas_page_size());
    for (size_t page = 0; page < a.pages.size(); page++) {
#if SDL_VERSION_ATLEAST(2, 0, 18)
        // one call per page: two triangles a glyph, the colour in the vertices
        a.vertices.clear();
        a.indices.clear();
        const SDL_Color c{color.r, color.g, color.b, color.a};
        for (size_t i = 0; i < n; i++) {
            const AbtQuad &q = a.quads[i];
            if (q.page != static_cast<int32_t>(page))
                continue;
            const float x0 = static_cast<float>(x + q.x), y0 = static_cast<float>(y + q.y);
            const float x1 = x0 + q.w, y1 = y0 + q.h;
            const float u0 = q.u * inv, v0 = q.v * inv, u1 = (q.u + q.w) * inv, v1 = (q.v + q.h) * inv;
            const int b = static_cast<int>(a.vertices.size());
            a.vertices.push_back({{x0, y0}, c, {u0, v0}});
            a.vertices.push_back({{x1, y0}, c, {u1, v0}});
            a.vertices.push_back({{x1, y1}, c, {u1, v1}});
            a.vertices.push_back({{x0, y1}, c, {u0, v1}});
            for (int k : {0, 1, 2, 0, 2, 3})
                a.indices.push_back(b + k);
        }
        if (a.vertices.empty())
            continue;
        SDL_SetTextureColorMod(a.pages[page], 255, 255, 255);
        SDL_SetTextureAlphaMod(a.pages[page], 255);
        if (SDL_RenderGeometry(r, a.pages[page], a.vertices.data(), static_cast<int>(a.vertices.size()),
                               a.indices.data(), static_cast<int>(a.indices.size())) == 0)
            continue;
        static bool reported = false; // then the copies below draw it
        if (!reported) {
            reported = true;
            PLOG_WARNING << "SDL_RenderGeometry failed, drawing glyph by glyph: " << SDL_GetError();
        }
#endif
        SDL_SetTextureColorMod(a.pages[page], color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(a.pages[page], color.a);
        for (size_t i = 0; i < n; i++) {
            const AbtQuad &q = a.quads[i];
            if (q.page != static_cast<int32_t>(page))
                continue;
            SDL_Rect src{q.u, q.v, q.w, q.h}, dst{x + q.x, y + q.y, q.w, q.h};
            SDL_RenderCopy(r, a.pages[page], &src, &dst);
        }
    }
    return static_cast<int>(n);
}
#endif // ABLEEM_TEXT_RUST
} // namespace

void fontAtlasRelease() {
#ifdef ABLEEM_TEXT_RUST
    atlas().drop(true);
#endif
}

Font::Font() : handle(nullptr) {}

Font::Font(void *fcFont) : handle(fcFont, destroyFont) {}

Font Font::load(Renderer &renderer, const std::string &ttfPath, int pointSize) {
    float scale = renderer.outputScale();
    int outputPointSize = static_cast<int>(std::lround(pointSize * scale));
#ifdef ABLEEM_TEXT_RUST
    if (rustText()) {
        AbtFont *f = abt_font_load(ttfPath.c_str(), static_cast<float>(outputPointSize));
        if (!f) {
            PLOG_ERROR << "FAILURE opening font " << ttfPath << " of size " << pointSize;
            return Font();
        }
        PLOG_DEBUG << "Opened font " << ttfPath << " of size " << pointSize;
        Font font;
        font.handle = std::shared_ptr<void>(f, freeRustFont);
        font.scale_ = scale;
        return font;
    }
#endif
    FC_Font *fc = FC_CreateFont();
    Uint8 ok = FC_LoadFont(fc, static_cast<SDL_Renderer *>(renderer.native()), ttfPath.c_str(), outputPointSize,
                           FC_MakeColor(255, 255, 255, 255), TTF_STYLE_NORMAL);
    if (!ok) {
        // an invalid Font, so a caller's valid() check can fall back (a 0-byte or broken .ttf); the cache
        // FC_CreateFont made is freed here, nothing else holds it
        PLOG_ERROR << "FAILURE opening font " << ttfPath << " of size " << pointSize;
        FC_FreeFont(fc);
        return Font();
    }
    PLOG_DEBUG << "Opened font " << ttfPath << " of size " << pointSize;
    Font font(fc);
    font.scale_ = scale;
    return font;
}

void Font::addFallback(const std::string &ttfPath) {
#ifdef ABLEEM_TEXT_RUST
    if (rustText() && abt_add_fallback(ttfPath.c_str()) < 0)
        PLOG_WARNING << "Cannot add the fallback font " << ttfPath;
#else
    (void)ttfPath;
#endif
}

std::vector<size_t> Font::lineBreaks(const std::string &text) {
    std::vector<size_t> breaks;
#ifdef ABLEEM_TEXT_RUST
    if (rustText()) {
        std::vector<uint32_t> at(text.size() / 2 + 8);
        size_t n = abt_line_breaks(text.data(), text.size(), at.data(), at.size());
        if (n > at.size()) {
            at.resize(n);
            n = abt_line_breaks(text.data(), text.size(), at.data(), at.size());
        }
        breaks.assign(at.begin(), at.begin() + static_cast<std::ptrdiff_t>(n));
        return breaks;
    }
#endif
    for (size_t i = 1; i < text.size(); i++) {
        const bool blank = text[i - 1] == ' ' || text[i - 1] == '\t';
        if (blank && text[i] != ' ' && text[i] != '\t')
            breaks.push_back(i);
    }
    return breaks;
}

int Font::logical(int outputPixels) const {
    return scale_ == 1.0f ? outputPixels : static_cast<int>(std::lround(outputPixels / scale_));
}

bool Font::valid() const {
    return handle != nullptr;
}

Size Font::textSize(const std::string &text) const {
    Size s;
    if (!handle)
        return s;
    s.w = width(text);
    s.h = lineHeight();
    return s;
}

int Font::lineHeight() const {
    if (!handle)
        return 0;
#ifdef ABLEEM_TEXT_RUST
    if (rustText())
        return logical(abt_font_line_height(rs(handle)));
#endif
    return logical(FC_GetLineHeight(static_cast<FC_Font *>(handle.get())));
}

int Font::width(const std::string &text) const {
    if (!handle)
        return 0;
#ifdef ABLEEM_TEXT_RUST
    if (rustText()) {
        int32_t w = 0;
        abt_measure(rs(handle), text.data(), text.size(), 0, &w, nullptr);
        return logical(w);
    }
#endif
    // "%s" avoids treating the text itself as a printf format string
    return logical(FC_GetWidth(static_cast<FC_Font *>(handle.get()), "%s", text.c_str()));
}

void Font::draw(Renderer &renderer, int x, int y, const std::string &text) const {
    drawColor(renderer, x, y, Color(255, 255, 255, 255), text);
}

void Font::drawAlign(Renderer &renderer, int x, int y, Align align, const std::string &text) const {
    if (!handle)
        return;
#ifdef ABLEEM_TEXT_RUST
    if (rustText()) {
        // (x, y) is the anchor: the left end, the middle or the right end of the widest line, as SDL_FontCache
        int32_t w = 0;
        abt_measure(rs(handle), text.data(), text.size(), 0, &w, nullptr);
        int ox = static_cast<int>(std::lround(x * scale_));
        int how = ABT_ALIGN_LEFT;
        if (align == Align::Center) {
            ox -= w / 2;
            how = ABT_ALIGN_CENTER;
        } else if (align == Align::Right) {
            ox -= w;
            how = ABT_ALIGN_RIGHT;
        }
        renderer.countCopies(drawRust(rs(handle), renderer, ox, static_cast<int>(std::lround(y * scale_)), 0, how,
                                      Color(255, 255, 255, 255), text));
        return;
    }
#endif
    renderer.countCopies(static_cast<int>(text.size()));
    FC_DrawAlign(static_cast<FC_Font *>(handle.get()), static_cast<SDL_Renderer *>(renderer.native()), x * scale_,
                 y * scale_, toFC(align), "%s", text.c_str());
}

void Font::drawColor(Renderer &renderer, int x, int y, Color color, const std::string &text) const {
    if (!handle)
        return;
#ifdef ABLEEM_TEXT_RUST
    if (rustText()) {
        renderer.countCopies(drawRust(rs(handle), renderer, static_cast<int>(std::lround(x * scale_)),
                                      static_cast<int>(std::lround(y * scale_)), 0, ABT_ALIGN_LEFT, color, text));
        return;
    }
#endif
    renderer.countCopies(static_cast<int>(text.size()));
    SDL_Color c{color.r, color.g, color.b, color.a};
    FC_DrawColor(static_cast<FC_Font *>(handle.get()), static_cast<SDL_Renderer *>(renderer.native()), x * scale_,
                 y * scale_, c, "%s", text.c_str());
}

std::string Font::wrappedText(const std::string &text, int maxWidth) const {
    if (!handle)
        return text;
#ifdef ABLEEM_TEXT_RUST
    if (rustText()) {
        const int32_t wrap = static_cast<int32_t>(std::lround(maxWidth * scale_));
        std::string out(text.size() + 64, '\0');
        size_t len = abt_wrap(rs(handle), text.data(), text.size(), wrap, &out[0], out.size());
        if (len > out.size()) {
            out.resize(len);
            len = abt_wrap(rs(handle), text.data(), text.size(), wrap, &out[0], out.size());
        }
        out.resize(len);
        return out;
    }
#endif
    std::vector<char> buffer(text.size() + 256);
    int len = FC_GetWrappedText(static_cast<FC_Font *>(handle.get()), buffer.data(), static_cast<int>(buffer.size()),
                                static_cast<Uint16>(maxWidth * scale_), "%s", text.c_str());
    return std::string(buffer.data(), len > 0 ? static_cast<size_t>(len) : 0);
}

int Font::columnHeight(const std::string &text, int width) const {
    if (!handle)
        return 0;
#ifdef ABLEEM_TEXT_RUST
    if (rustText()) {
        int32_t lines = 0;
        abt_measure(rs(handle), text.data(), text.size(), static_cast<int32_t>(std::lround(width * scale_)), nullptr,
                    &lines);
        return logical(lines * abt_font_line_height(rs(handle)));
    }
#endif
    return logical(FC_GetColumnHeight(static_cast<FC_Font *>(handle.get()), static_cast<Uint16>(width * scale_), "%s",
                                      text.c_str()));
}

int Font::drawColumn(Renderer &renderer, int x, int y, int width, Color color, const std::string &text) const {
    if (!handle)
        return 0;
#ifdef ABLEEM_TEXT_RUST
    if (rustText()) {
        const int32_t wrap = static_cast<int32_t>(std::lround(width * scale_));
        renderer.countCopies(drawRust(rs(handle), renderer, static_cast<int>(std::lround(x * scale_)),
                                      static_cast<int>(std::lround(y * scale_)), wrap, ABT_ALIGN_LEFT, color, text));
        return columnHeight(text, width);
    }
#endif
    renderer.countCopies(static_cast<int>(text.size()));
    SDL_Color c{color.r, color.g, color.b, color.a};
    FC_Rect r = FC_DrawColumnColor(static_cast<FC_Font *>(handle.get()), static_cast<SDL_Renderer *>(renderer.native()),
                                   x * scale_, y * scale_, static_cast<Uint16>(width * scale_), c, "%s", text.c_str());
    return logical(static_cast<int>(r.h));
}

void Font::resetAfterRendererReset(Renderer &renderer, bool deviceLost) {
    if (!handle)
        return;
#ifdef ABLEEM_TEXT_RUST
    if (rustText())
        return; // the atlas sees Renderer::targetsLost() move and uploads every page again
#endif
    Uint32 evType = deviceLost ? SDL_RENDER_DEVICE_RESET : SDL_RENDER_TARGETS_RESET;
    FC_ResetFontFromRendererReset(static_cast<FC_Font *>(handle.get()), static_cast<SDL_Renderer *>(renderer.native()),
                                  evType);
}

} // namespace ableem
