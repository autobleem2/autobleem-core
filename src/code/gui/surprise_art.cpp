//
// SurpriseHud - see surprise_art.h.
//

#include "surprise_art.h"

#include <algorithm>
#include <cmath>

using ableem::Align;
using ableem::BlendMode;
using ableem::Color;
using ableem::Font;
using ableem::FRect;
using ableem::Point;
using ableem::Rect;
using ableem::Renderer;
using ableem::Size;
using ableem::Texture;

namespace {

struct Stop {
    float at;
    Color color;
};

// the README's gradients (make_surprise_art.py: CHROME, GOLD, ICE and the title's pink)
const std::vector<Stop> &stopsOf(SurpriseHud::Gradient gradient) {
    static const std::vector<Stop> chrome = {{0.0f, Color(255, 255, 255)},
                                             {0.45f, Color(150, 210, 255)},
                                             {0.5f, Color(30, 50, 120)},
                                             {0.62f, Color(255, 150, 60)},
                                             {1.0f, Color(255, 235, 140)}};
    static const std::vector<Stop> gold = {{0.0f, Color(255, 255, 220)},
                                           {0.5f, Color(255, 200, 60)},
                                           {0.52f, Color(200, 90, 20)},
                                           {1.0f, Color(255, 170, 60)}};
    static const std::vector<Stop> ice = {{0.0f, Color(255, 255, 255)},
                                          {0.5f, Color(140, 230, 255)},
                                          {0.52f, Color(40, 120, 200)},
                                          {1.0f, Color(120, 220, 255)}};
    static const std::vector<Stop> pink = {
        {0.0f, Color(255, 220, 250)}, {0.5f, Color(255, 60, 190)}, {1.0f, Color(150, 20, 120)}};
    switch (gradient) {
    case SurpriseHud::Gradient::Gold:
        return gold;
    case SurpriseHud::Gradient::Ice:
        return ice;
    case SurpriseHud::Gradient::Pink:
        return pink;
    case SurpriseHud::Gradient::Chrome:
    default:
        return chrome;
    }
}

Color colorAt(const std::vector<Stop> &stops, float u) {
    if (u <= stops.front().at)
        return stops.front().color;
    for (size_t i = 0; i + 1 < stops.size(); i++) {
        const Stop &a = stops[i];
        const Stop &b = stops[i + 1];
        if (u <= b.at) {
            float k = (u - a.at) / std::max(1e-6f, b.at - a.at);
            return Color(static_cast<unsigned char>(a.color.r + (b.color.r - a.color.r) * k),
                         static_cast<unsigned char>(a.color.g + (b.color.g - a.color.g) * k),
                         static_cast<unsigned char>(a.color.b + (b.color.b - a.color.b) * k));
        }
    }
    return stops.back().color;
}

// Oxanium's capital letters and digits stand between 10 % and 79 % of the line box (ascent 790, cap height 690, of
// a 1000-unit line): the gradient runs over that, the way the README's tool runs it over the glyphs' own bounds
const float CapTop = 0.10f, CapBottom = 0.79f;

// a glow layer's cache entry sits at its slot's number plus this, beside the lettering's own
const int GlowSlotOffset = 100;

const Color ShadowColor(0, 0, 0, 255);
const Color OutlineColor(8, 6, 20, 255);
const Color RimColor(150, 160, 200, 255);
const Color RimLightColor(230, 235, 255, 255);

int anchored(Align align, int x, int w) {
    return align == Align::Center ? x - w / 2 : align == Align::Right ? x - w : x;
}

void clearTarget(Renderer &renderer) {
    renderer.setBlendMode(BlendMode::None);
    renderer.setDrawColor(Color(0, 0, 0, 0));
    renderer.fillRect();
}

// the neon glow: the text drawn in four rings of twelve offsets, each ring fainter the further out
void drawGlow(Renderer &renderer, const Font &font, const std::string &text, Color glow, int padX, int padY) {
    const int radii[4] = {14, 11, 8, 5};
    const unsigned char alphas[4] = {18, 24, 34, 50};
    for (int i = 0; i < 4; i++) {
        for (int k = 0; k < 12; k++) {
            float angle = static_cast<float>(k) * 0.5235988f; // 30 degrees
            int dx = static_cast<int>(std::lround(radii[i] * std::cos(angle)));
            int dy = static_cast<int>(std::lround(radii[i] * std::sin(angle)));
            font.drawColor(renderer, padX + dx, padY + dy, Color(glow.r, glow.g, glow.b, alphas[i]), text);
        }
    }
}

// the glow alone, as a premultiplied texture the same way the lettering is (its own padding: no outline, no lean)
Texture buildGlow(Renderer &renderer, const Font &font, const std::string &text, Color glow, int &padX, int &padY,
                  int &textW) {
    const Size sz = font.textSize(text);
    textW = sz.w;
    padX = padY = 16 + 2;
    if (sz.w <= 0 || sz.h <= 0)
        return Texture();
    Texture layer = Texture::createTarget(renderer, sz.w + 2 * padX, sz.h + 2 * padY);
    if (!layer.valid())
        return Texture();
    renderer.pushTarget(&layer);
    clearTarget(renderer);
    renderer.setBlendMode(BlendMode::Blend);
    drawGlow(renderer, font, text, glow, padX, padY);
    renderer.popTarget();
    layer.setBlendMode(BlendMode::Premultiplied);
    return layer;
}

// where a texture of `dst` lands under an Fx: about its pivot, scaled (1.0 = the plain integer rect)
FRect fxRect(const Rect &dst, const SurpriseHud::Fx &fx) {
    return FRect(fx.pivotX + (dst.x - fx.pivotX) * fx.scale, fx.pivotY + (dst.y - fx.pivotY) * fx.scale,
                 dst.w * fx.scale, dst.h * fx.scale);
}

// a premultiplied texture faded to `alpha`: the colours follow the alpha, the tint is kept
void fade(Texture &tex, Color tint, unsigned char alpha) {
    auto scaled = [alpha](unsigned char c) { return static_cast<unsigned char>(c * alpha / 255); };
    tex.setColorMod(Color(scaled(tint.r), scaled(tint.g), scaled(tint.b)));
    tex.setAlphaMod(alpha);
}

// The lettering as a texture: the gradient fill (a target filled row by row, then cut to the text's shape with the
// Mask blend), over the dark outline and the glow, optionally leaned. Everything is composed into render targets, so
// what comes back holds premultiplied colours (drawn with BlendMode::Premultiplied).
Texture buildChrome(Renderer &renderer, const Font &font, const std::string &text, SurpriseHud::Gradient gradient,
                    int outline, Color glow, float skew, int &padX, int &padY, int &textW) {
    const Size sz = font.textSize(text);
    textW = sz.w;
    padY = outline + (glow.a ? 16 : 0) + 2;
    int skewPad = skew != 0.0f ? static_cast<int>(std::ceil(std::fabs(skew) * (sz.h + 2 * padY) / 2.0f)) + 1 : 0;
    padX = padY + skewPad;
    int w = sz.w + 2 * padX;
    int h = sz.h + 2 * padY;
    h += h & 1; // the lean copies two rows at a time, which is whole pixels at both output scales
    if (sz.w <= 0 || sz.h <= 0)
        return Texture();

    Texture mask = Texture::createTarget(renderer, w, h);
    Texture fill = Texture::createTarget(renderer, w, h);
    Texture layer = Texture::createTarget(renderer, w, h);
    if (!mask.valid() || !fill.valid() || !layer.valid())
        return Texture();

    // the text's shape, white on transparent
    renderer.pushTarget(&mask);
    clearTarget(renderer);
    renderer.setBlendMode(BlendMode::Blend);
    font.drawColor(renderer, padX, padY, Color(255, 255, 255, 255), text);
    renderer.popTarget();

    // the gradient, a row at a time over the capitals' height, then cut to the text
    const std::vector<Stop> &stops = stopsOf(gradient);
    const float top = padY + CapTop * sz.h;
    const float bottom = padY + CapBottom * sz.h;
    renderer.pushTarget(&fill);
    clearTarget(renderer);
    for (int y = 0; y < h; y++) {
        float u = std::max(0.0f, std::min(1.0f, (y - top) / std::max(1.0f, bottom - top)));
        renderer.setDrawColor(colorAt(stops, u));
        renderer.fillRect(Rect(0, y, w, 1));
    }
    renderer.popTarget();
    renderer.pushTarget(&fill);
    mask.setBlendMode(BlendMode::Mask);
    renderer.copy(mask, nullptr, nullptr);
    renderer.popTarget();
    fill.setBlendMode(BlendMode::Blend);

    // glow, outline, then the gradient text over them
    renderer.pushTarget(&layer);
    clearTarget(renderer);
    renderer.setBlendMode(BlendMode::Blend);
    if (glow.a)
        drawGlow(renderer, font, text, glow, padX, padY);
    if (outline > 0) {
        for (int dy = -outline; dy <= outline; dy++)
            for (int dx = -outline; dx <= outline; dx++)
                if (dx * dx + dy * dy <= outline * outline + outline)
                    font.drawColor(renderer, padX + dx, padY + dy, OutlineColor, text);
    }
    renderer.copy(fill, nullptr, nullptr);
    renderer.popTarget();
    layer.setBlendMode(BlendMode::Premultiplied);

    if (skew == 0.0f)
        return layer;

    // the lean: the rows shifted sideways by -skew x their distance from the middle, two rows at a time
    Texture leaned = Texture::createTarget(renderer, w, h);
    if (!leaned.valid())
        return layer;
    renderer.pushTarget(&leaned);
    clearTarget(renderer);
    for (int y = 0; y < h; y += 2) {
        float shift = -skew * (y + 1 - h / 2.0f);
        Rect src(0, y, w, 2);
        renderer.copy(layer, &src, FRect(shift, static_cast<float>(y), static_cast<float>(w), 2.0f));
    }
    renderer.popTarget();
    leaned.setBlendMode(BlendMode::Premultiplied);
    return leaned;
}

// the plate's spans: the polygon (c,0) (w,0) (w,h-c) (w-c,h) (0,h) (0,c), the top-left and bottom-right corners cut
void spanOf(int y, int w, int h, int c, int inset, int &left, int &right) {
    left = inset;
    right = w - inset;
    if (inset == 0) {
        if (y < c)
            left = c - y;
        if (y >= h - c)
            right = w + h - c - y;
    } else {
        // the cut edges moved in by the rim's width along the diagonal (3 px -> 4 px across)
        left = std::max(inset, c + inset + 1 - y);
        right = std::min(w - inset, w + h - c - inset - 1 - y);
    }
}

Texture buildPlate(Renderer &renderer, int w, int h) {
    Texture tex = Texture::createTarget(renderer, w, h);
    if (!tex.valid())
        return tex;
    const int c = 10;
    renderer.pushTarget(&tex);
    clearTarget(renderer);
    // the metal rim: the whole shape
    renderer.setDrawColor(RimColor);
    for (int y = 0; y < h; y++) {
        int l, r;
        spanOf(y, w, h, c, 0, l, r);
        renderer.fillRect(Rect(l, y, r - l, 1));
    }
    // the smoked glass inside it: a vertical gradient, about 80 % opaque
    for (int y = 3; y < h - 3; y++) {
        int l, r;
        spanOf(y, w, h, c, 3, l, r);
        float u = static_cast<float>(y) / static_cast<float>(h);
        renderer.setDrawColor(Color(static_cast<unsigned char>(30 - 18 * u), static_cast<unsigned char>(34 - 20 * u),
                                    static_cast<unsigned char>(70 - 40 * u), 205));
        if (r > l)
            renderer.fillRect(Rect(l, y, r - l, 1));
    }
    // the light top-left edge: the diagonal and the top, 2 px
    renderer.setDrawColor(RimLightColor);
    for (int y = 0; y < c; y++)
        renderer.fillRect(Rect(c - y, y, 2, 1));
    renderer.fillRect(Rect(c, 0, w - c, 2));
    // the cyan hairline inside the glass (cyan at about 55 % over the glass, written straight)
    renderer.setDrawColor(Color(32, 140, 165, 235));
    const Point inner[6] = {{c + 3, 5}, {w - 5, 5}, {w - 5, h - c - 2}, {w - c - 2, h - 5}, {5, h - 5}, {5, c + 3}};
    for (int i = 0; i < 6; i++)
        renderer.drawLine(inner[i], inner[(i + 1) % 6]);
    renderer.popTarget();
    renderer.setBlendMode(BlendMode::Blend);
    tex.setBlendMode(BlendMode::Blend);
    return tex;
}

} // namespace

//*******************************
// SurpriseFonts::load
//*******************************
void SurpriseFonts::load(Renderer &renderer, const std::string &fontsDir, const std::string &cjkFont) {
    auto open = [&](const char *file, int size) {
        std::string path = cjkFont.empty() ? fontsDir + "/" + file : cjkFont;
        return Font::load(renderer, path, size);
    };
    label = open("Oxanium-ExtraBold.ttf", 18);
    number = open("Oxanium-ExtraBold.ttf", 30);
    semi20 = open("Oxanium-SemiBold.ttf", 20);
    bold20 = open("Oxanium-Bold.ttf", 20);
    title = open("Oxanium-ExtraBold.ttf", 112);
    subtitle = open("Oxanium-ExtraBold.ttf", 56);
    push = open("Oxanium-Bold.ttf", 30);
    credit = open("Oxanium-SemiBold.ttf", 14);
}

//*******************************
// SurpriseHud::shadowText
//*******************************
void SurpriseHud::shadowText(Renderer &renderer, const Font &wanted, const std::string &text, int x, int y, Align align,
                             Color color, bool shadow) const {
    const Font &font = face(wanted);
    if (!font.valid() || text.empty())
        return;
    int left = anchored(align, x, font.width(text));
    if (shadow)
        font.drawColor(renderer, left + 2, y + 2, ShadowColor, text);
    font.drawColor(renderer, left, y, color, text);
}

//*******************************
// SurpriseHud::chrome
//*******************************
int SurpriseHud::chrome(Renderer &renderer, Slot slot, const Font &wanted, const std::string &text, Gradient gradient,
                        int outline, int x, int y, Align align, Color glow, float skew, Color tint, const Fx &fx) {
    const Font &font = face(wanted);
    if (!font.valid() || text.empty())
        return 0;
    Lettering &l = letterings_[static_cast<int>(slot)];
    const std::string key = text + "|" + std::to_string(static_cast<int>(gradient)) + "|" + std::to_string(outline) +
                            "|" + std::to_string(static_cast<int>(glow.a)) + "|" + std::to_string(font.lineHeight());
    if (!l.tex.valid() || l.key != key || l.lost != renderer.targetsLost()) {
        l.tex = buildChrome(renderer, font, text, gradient, outline, glow, skew, l.padX, l.padY, l.textW);
        l.key = key;
        l.lost = renderer.targetsLost();
        renderer.setBlendMode(BlendMode::Blend);
    }
    if (!l.tex.valid())
        return 0;
    fade(l.tex, tint, fx.alpha);
    const Size size = l.tex.size();
    Rect dst(anchored(align, x, l.textW) - l.padX, y - l.padY, size.w, size.h);
    if (fx.scale == 1.0f)
        renderer.copy(l.tex, nullptr, &dst);
    else
        renderer.copy(l.tex, nullptr, fxRect(dst, fx));
    return l.textW;
}

//*******************************
// SurpriseHud::glowLayer
//*******************************
void SurpriseHud::glowLayer(Renderer &renderer, Slot slot, const Font &wanted, const std::string &text, Color glow,
                            int x, int y, Align align, unsigned char alpha) {
    const Font &font = face(wanted);
    if (!font.valid() || text.empty() || alpha == 0)
        return;
    Lettering &l = letterings_[static_cast<int>(slot) + GlowSlotOffset];
    const std::string key = text + "|" + std::to_string(static_cast<int>(glow.r)) + "," +
                            std::to_string(static_cast<int>(glow.g)) + "," + std::to_string(static_cast<int>(glow.b)) +
                            "|" + std::to_string(font.lineHeight());
    if (!l.tex.valid() || l.key != key || l.lost != renderer.targetsLost()) {
        l.tex = buildGlow(renderer, font, text, glow, l.padX, l.padY, l.textW);
        l.key = key;
        l.lost = renderer.targetsLost();
        renderer.setBlendMode(BlendMode::Blend);
    }
    if (!l.tex.valid())
        return;
    fade(l.tex, Color(255, 255, 255), alpha);
    const Size size = l.tex.size();
    Rect dst(anchored(align, x, l.textW) - l.padX, y - l.padY, size.w, size.h);
    renderer.copy(l.tex, nullptr, &dst);
}

//*******************************
// SurpriseHud::plate
//*******************************
void SurpriseHud::plate(Renderer &renderer, int x, int y, int w, int h, const Fx &fx) {
    PlateTexture *found = nullptr;
    for (PlateTexture &p : plates_)
        if (p.w == w && p.h == h)
            found = &p;
    if (!found) {
        plates_.push_back(PlateTexture());
        found = &plates_.back();
        found->w = w;
        found->h = h;
    }
    if (!found->tex.valid() || found->lost != renderer.targetsLost()) {
        found->tex = buildPlate(renderer, w, h);
        found->lost = renderer.targetsLost();
    }
    if (found->tex.valid()) {
        found->tex.setAlphaMod(fx.alpha);
        Rect dst(x, y, w, h);
        if (fx.scale == 1.0f)
            renderer.copy(found->tex, nullptr, &dst);
        else
            renderer.copy(found->tex, nullptr, fxRect(dst, fx));
    }
}

//*******************************
// SurpriseHud::segments
//*******************************
void SurpriseHud::segments(Renderer &renderer, int x, int y, int total, int lit, Color on, Color off) {
    const int SegW = 16, SegH = 8; // a 12 px wide parallelogram leaning 4 px, in a 16x8 box
    if (!segmentBuilt_ || !segment_.valid() || segmentLost_ != renderer.targetsLost()) {
        segment_ = Texture::createTarget(renderer, SegW, SegH);
        if (segment_.valid()) {
            renderer.pushTarget(&segment_);
            clearTarget(renderer);
            renderer.setDrawColor(Color(255, 255, 255, 255));
            for (int r = 0; r < SegH; r++) {
                int left = static_cast<int>(std::lround(4.0f * (1.0f - (r + 0.5f) / SegH)));
                renderer.fillRect(Rect(left, r, 12, 1));
            }
            renderer.popTarget();
            renderer.setBlendMode(BlendMode::Blend);
            segment_.setBlendMode(BlendMode::Blend);
        }
        segmentBuilt_ = true;
        segmentLost_ = renderer.targetsLost();
    }
    if (!segment_.valid())
        return;
    for (int i = 0; i < total; i++) {
        segment_.setColorMod(i < lit ? on : off);
        Rect dst(x + i * 18, y, SegW, SegH);
        renderer.copy(segment_, nullptr, &dst);
    }
}
