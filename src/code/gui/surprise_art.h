//
// SurpriseHud: how "BleemStrike: Reloaded" draws its lettering and plates (UIREV-39, autobleem-design
// launcher/surprise/README.md) - the Oxanium faces, the chrome-gradient numbers with their dark outline (and the
// title's glow and lean), the smoked-glass HUD plates with their cut corners, the power-up timer's slanted segments.
// All of it is built once into render-target textures and kept until the text changes or the targets are lost.
//
#pragma once

#include <ableem/ui/font.h>
#include <ableem/ui/renderer.h>
#include <ableem/ui/texture.h>

#include <map>
#include <string>
#include <vector>

//******************
// SurpriseFonts
//******************
// The HUD's faces, each a static instance of Oxanium (resources/fonts); a language the theme's fonts cannot draw
// (Chinese) passes its CJK font, which then stands in for all of them. A face that cannot be opened is invalid -
// the caller (SurpriseHud) draws with the fallback it is given instead.
struct SurpriseFonts {
    ableem::Font label;    // 18, ExtraBold
    ableem::Font number;   // 30, ExtraBold
    ableem::Font semi20;   // 20, SemiBold
    ableem::Font bold20;   // 20, Bold
    ableem::Font title;    // 112, ExtraBold
    ableem::Font subtitle; // 56, ExtraBold
    ableem::Font push;     // 30, Bold
    ableem::Font credit;   // 14, SemiBold

    void load(ableem::Renderer &renderer, const std::string &fontsDir, const std::string &cjkFont);
};

//******************
// SurpriseFx
//******************
// How a lettering or a plate is put on the screen when it fades or pops: `alpha` over everything (the colours follow
// it, the lettering being premultiplied), `scale` about (pivotX, pivotY) - 1.0 is the plain integer copy.
struct SurpriseFx {
    unsigned char alpha = 255;
    float scale = 1.0f;
    int pivotX = 0, pivotY = 0;
};

//******************
// SurpriseHud
//******************
class SurpriseHud {
public:
    // the multi-stop vertical gradients of the README (stops in surprise_art.cpp)
    enum class Gradient { Chrome, Gold, Ice, Pink };
    // one cached lettering per place on screen
    enum class Slot {
        Score,
        HiScore,
        Wave,
        Lives,
        TitleMain,
        TitleSub,
        TitlePush,
        LifeLost,
        GameOver,
        FinalScore,
        FinalHi,
        Push,
        Count
    };

    using Fx = SurpriseFx;

    SurpriseFonts fonts;

    // a face that cannot be opened is replaced by this one (set once, the launcher's own font)
    ableem::Font fallback;

    // plain text with the 2 px dark shadow the HUD labels have; x is the left, centre or right edge by `align`
    void shadowText(ableem::Renderer &renderer, const ableem::Font &font, const std::string &text, int x, int y,
                    ableem::Align align, ableem::Color color, bool shadow = true) const;

    // the chrome lettering: gradient fill, dark `outline` px edge, an optional glow (alpha 0 = none) and a lean
    // (`skew`, 0 = upright; the README's -0.18 leans the title). `tint` multiplies the result (the dimmed hi-score).
    // Returns the width of the text itself.
    int chrome(ableem::Renderer &renderer, Slot slot, const ableem::Font &font, const std::string &text,
               Gradient gradient, int outline, int x, int y, ableem::Align align,
               ableem::Color glow = ableem::Color(0, 0, 0, 0), float skew = 0.0f,
               ableem::Color tint = ableem::Color(255, 255, 255, 255), const Fx &fx = Fx());

    // only the glow of a lettering (the colour, built and cached on its own under `slot`), drawn at `alpha` - what a
    // pulsing glow needs while the lettering itself (drawn by chrome() with no glow) stays steady
    void glowLayer(ableem::Renderer &renderer, Slot slot, const ableem::Font &font, const std::string &text,
                   ableem::Color glow, int x, int y, ableem::Align align, unsigned char alpha);

    // a smoked-glass plate of the README: a 3 px metal rim, a light top-left edge, a cyan hairline, cut corners
    void plate(ableem::Renderer &renderer, int x, int y, int w, int h, const Fx &fx = Fx());

    // the power-up timer: `total` slanted 12x8 segments every 18 px from (x, y), the first `lit` in `on`, the rest
    // `off`
    void segments(ableem::Renderer &renderer, int x, int y, int total, int lit, ableem::Color on, ableem::Color off);

    // the face to draw with: the asked one, else the fallback
    const ableem::Font &face(const ableem::Font &wanted) const { return wanted.valid() ? wanted : fallback; }

private:
    struct Lettering {
        std::string key;
        ableem::Texture tex;
        int padX = 0, padY = 0, textW = 0;
        unsigned long lost = 0;
    };
    struct PlateTexture {
        int w = 0, h = 0;
        ableem::Texture tex;
        unsigned long lost = 0;
    };

    std::map<int, Lettering> letterings_;
    std::vector<PlateTexture> plates_;
    ableem::Texture segment_;
    unsigned long segmentLost_ = 0;
    bool segmentBuilt_ = false;
};
