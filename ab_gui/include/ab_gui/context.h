// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::Context: what ab_gui's drawing needs from the program that uses it - the renderer, the fonts by role,
// the button glyphs, the text drawing, the translator and the current Style - with no singletons. A program
// builds one and keeps it (AutoBleem's Gui owns its own); every primitive and, later, every widget takes it.
//
// Everything but the renderer is a provider, asked at draw time and never cached: a font or a texture is only
// good until the program drops it (a theme reload, the display handed to an emulator and taken back), so a
// Context never holds one itself. ab_gui knows nothing of AutoBleem and nothing of SDL: only lib_ableem's
// ableem types cross this interface.
//
#pragma once

#include <ab_gui/style.h>

#include <ableem/ui/font.h>
#include <ableem/ui/renderer.h>
#include <ableem/ui/texture.h>
#include <ableem/ui/types.h>

#include <functional>
#include <string>

namespace abgui {

//********************
// Context
//********************
class Context {
public:
    using FontProvider = std::function<const ableem::Font &(FontRole)>;
    // a button's image by its marker key ("X", "O", "T", "S", "Up", ...); an invalid texture = no image
    using GlyphProvider = std::function<ableem::Texture(const std::string &)>;
    // one run of text at (x, y) - the program's own text drawing (AutoBleem's has the halo and the run cache)
    using TextDrawer = std::function<void(const ableem::Font &, const std::string &, int, int, const ableem::Color &)>;
    using TextMeasurer = std::function<int(const ableem::Font &, const std::string &)>;
    using Translator = std::function<std::string(const std::string &)>;
    using StyleProvider = std::function<Style()>;

    explicit Context(ableem::Renderer &renderer) : renderer_(&renderer) {}

    ableem::Renderer &renderer() const { return *renderer_; }

    // What the program supplies. An unset provider falls back to something harmless (see the calls below).
    FontProvider fontProvider;
    GlyphProvider glyphProvider;
    // the dark outline drawn under a glyph (Style::outlineOf's texture), where it needs one to stay readable
    GlyphProvider glyphOutlineProvider;
    TextDrawer textDrawer;
    TextMeasurer textMeasurer;
    Translator translator;
    StyleProvider styleProvider;

    // What the drawing calls.
    // the font for `role`, or an invalid font when there is no provider
    const ableem::Font &font(FontRole role) const;
    // the glyph / its outline for `key`, or an invalid texture
    ableem::Texture glyph(const std::string &key) const;
    ableem::Texture glyphOutline(const std::string &key) const;
    // the text through the program's drawer, else the font's own plain drawing
    void drawText(const ableem::Font &font, const std::string &text, int x, int y, const ableem::Color &color) const;
    // the width of the text through the program's measurer, else the font's own
    int textWidth(const ableem::Font &font, const std::string &text) const;
    // the text through the translator, else unchanged
    std::string translate(const std::string &text) const;
    // the current look through the provider, else the defaults
    Style style() const;

private:
    ableem::Renderer *renderer_;
    ableem::Font none_; // what font() hands out without a provider
};

} // namespace abgui
