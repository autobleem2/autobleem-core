#pragma once

#include <memory>
#include <string>
#include <vector>
#include "types.h"

namespace ableem {

class Renderer;

//******************
// Font
//******************
// Cheap shared handle over a face at a size. Copy freely. Two engines draw it: the Rust one (rust/ab_text -
// shaping, kerning, font fallback, Unicode line breaking, one glyph atlas for every font) where the build has
// it, and SDL_FontCache otherwise or with AB_TEXT=fc in the environment. Metrics are SDL_ttf's in both.
class ABLEEM_API Font {
public:
    Font();
    Font(const Font &) = default;
    Font &operator=(const Font &) = default;

    // pointSize is in logical pixels: the face is loaded renderer.outputScale() times bigger and drawn at
    // that size in output pixels, so text is as sharp as the window; every measurement here comes back in
    // logical pixels
    static Font load(Renderer &renderer, const std::string &ttfPath, int pointSize);

    // adds a font file to the fallback chain: a character the font in use lacks is drawn from the first file
    // here that has it (a Chinese or Cyrillic game title in any language). Loaded once, mapped, not read into
    // memory. The Rust engine only; SDL_FontCache has no fallback.
    static void addFallback(const std::string &ttfPath);

    // the byte offsets where a line of the text may break, its end left out: Unicode line breaking with the Rust
    // engine (after a space, between two Chinese characters but not before "。"), else after each run of spaces
    // and tabs. abgui::wrapText cuts its rows there.
    static std::vector<size_t> lineBreaks(const std::string &text);

    bool valid() const;

    Size textSize(const std::string &text) const;
    int lineHeight() const;
    int width(const std::string &text) const;

    void draw(Renderer &renderer, int x, int y, const std::string &text) const;
    void drawAlign(Renderer &renderer, int x, int y, Align align, const std::string &text) const;
    void drawColor(Renderer &renderer, int x, int y, Color color, const std::string &text) const;

    // wraps text to fit maxWidth pixels, returning it with '\n' inserted (the Rust engine: Unicode line breaking,
    // so Chinese breaks between characters, and a word wider than the line between letters)
    std::string wrappedText(const std::string &text, int maxWidth) const;
    // the height the text takes when wrapped to width pixels
    int columnHeight(const std::string &text, int width) const;
    // draws the text wrapped to width pixels, top-left at (x, y); returns the height drawn
    int drawColumn(Renderer &renderer, int x, int y, int width, Color color, const std::string &text) const;

    // must be called after the SDL renderer target or device is reset, which SDL_FontCache needs to know
    // about to keep its glyph cache valid. deviceLost distinguishes a full device reset from a target reset.
    // (The Rust engine's atlas notices a reset by itself, through Renderer::targetsLost().)
    void resetAfterRendererReset(Renderer &renderer, bool deviceLost);

private:
    explicit Font(void *fcFont);
    float scale_ = 1.0f; // output pixels per logical pixel the face was loaded for
    int logical(int outputPixels) const;
    std::shared_ptr<void> handle;

public:
    void *native() const { return handle.get(); }
};

} // namespace ableem
