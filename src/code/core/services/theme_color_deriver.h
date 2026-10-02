//
// ThemeColorDeriver: the 2.0 colour roles of a 1.0 theme, derived from its background picture and from whatever
// colour data its theme.ini / colors.ini carried. Pure and SDL-free: the input is a plain pixel buffer.
//
#pragma once

#include <string>
#include <vector>

//******************
// ThemeRgb
//******************
struct ThemeRgb {
    int r = 0;
    int g = 0;
    int b = 0;

    ThemeRgb() = default;
    ThemeRgb(int r_, int g_, int b_) : r(r_), g(g_), b(b_) {}

    bool operator==(const ThemeRgb &o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const ThemeRgb &o) const { return !(*this == o); }

    // "#rrggbb", lower case
    std::string hex() const;
};

//******************
// ThemeColorInput
//******************
// What a 1.0 theme gives. `pixels` is the background, rows packed, 3 (RGB) or 4 (RGBA, composited on black)
// bytes per pixel; no picture (null pixels or a zero size) gives the fixed neutral sheet and no palette.
// The optional colours are the 1.0 data: `text` = colors.ini fg (else theme.ini Text_fg), `secondary` =
// colors.ini sec, `mainBg` = theme.ini Main_bg. Label_bg is not read (a role nothing in 2.0 draws).
struct ThemeColorInput {
    const unsigned char *pixels = nullptr;
    int width = 0;
    int height = 0;
    int channels = 3;
    bool hasText = false;
    ThemeRgb text;
    bool hasSecondary = false;
    ThemeRgb secondary;
    bool hasMainBg = false;
    ThemeRgb mainBg;
};

//******************
// ThemeColorRoles
//******************
// The derived block. Every text role is at least TextMinContrast against `sheet`, edge/selectionBand/accent at
// least EdgeMinContrast (by construction, checked by the invariant test); `disabled` and the sheet's alpha are
// the veil values. `fallbacks` says why a role took the default theme's value, `notes` where each source came from.
struct ThemeColorRoles {
    ThemeRgb sheet;
    int sheetAlpha = 200;
    ThemeRgb accent; // = edge = selectionBand
    ThemeRgb edge;
    ThemeRgb selectionBand;
    ThemeRgb text;
    ThemeRgb rowSelected;
    ThemeRgb footer;
    ThemeRgb row;
    ThemeRgb value;
    ThemeRgb description;
    ThemeRgb secondary;
    ThemeRgb hint;
    ThemeRgb heading;
    ThemeRgb disabled; // the veil's colour (= the sheet's)
    int disabledAlpha = 120;
    bool monochrome = false; // no chromatic accent anywhere: the grey fallback accent is in use
    std::vector<std::string> notes;
    std::vector<std::string> fallbacks;
};

//******************
// Thresholds (named constants; the prototype's, frozen by the plan's G6b1)
//******************
namespace themecolor {
constexpr double SheetLMin = 12.0; // L* of the panel sheet
constexpr double SheetLMax = 18.0;
constexpr double SheetChromaMax = 18.0; // the sheet is a tint, not a colour
constexpr int SheetAlpha = 200;
constexpr int DisabledAlpha = 120;
constexpr int StockBlackMax = 12;          // a Main_bg whose brightest channel is at most this is "not chosen"
constexpr double DominantShare = 0.06;     // a palette cluster counts as dominant
constexpr double AccentShare = 0.03;       // ... as an accent candidate
constexpr double AccentMinChroma = 9.0;    // LCH chroma below this is grey
constexpr double AccentBoostChroma = 32.0; // a muted accent is pushed to at least this chroma (hue kept)
constexpr double AccentLMin = 20.0;        // near-black and near-white clusters are ignored
constexpr double AccentLMax = 92.0;
constexpr double OwnChromaMin = 20.0;   // a 1.0 colour with less chroma is neutral
constexpr double TextMinContrast = 4.5; // WCAG AA, every text role vs the sheet
constexpr double EdgeMinContrast = 3.0; // non-text (edge, band) vs the sheet
constexpr double TextTint = 0.12;       // near-white tinted toward the accent
constexpr double RowMix = 0.70;         // row = 70% of the way sheet -> text
constexpr double DescMix = 0.55;        // description: greyer, further toward the sheet
constexpr double LiftStep = 1.0;        // L* step of the lift
constexpr double LiftMaxL = 97.0;       // a lift past this gives up
constexpr int PaletteColors = 12;
constexpr int PaletteWidth = 160; // the downscale the palette is read from
constexpr int PaletteHeight = 90;
// the default theme's block, the fall-back of a role that cannot be reached
constexpr int DefaultText = 0xffffff;
constexpr int DefaultSecondary = 0x646464;
constexpr int MonochromeHeading = 170; // the grey heading of a monochrome theme (before it is lifted)
} // namespace themecolor

//******************
// ThemeColorDeriver
//******************
// derive() is the whole job; the pieces below it are public for the tests and the comparison tool.
//
// The palette is Pillow's: a Lanczos downscale to 160x90 and a median cut to 12 clusters, ported from Pillow's
// quantiser (the same box choice, the same split, the same nearest-entry remap) so the clusters match the Python
// prototype's. The resize and the RGBA compositing are ported with Pillow's fixed-point arithmetic; on the 51
// pictures of the community pack the palettes and every role came out identical to the prototype's.
class ThemeColorDeriver {
public:
    struct PaletteEntry {
        ThemeRgb color;
        double share = 0.0;
    };

    static ThemeColorRoles derive(const ThemeColorInput &in);

    // the roles from a palette already read (biggest share first) - derive() minus the picture; `in` supplies only
    // the optional 1.0 colours. `haveBackground` false adds the "no readable background" note.
    static ThemeColorRoles fromPalette(const std::vector<PaletteEntry> &pal, bool haveBackground,
                                       const ThemeColorInput &in);

    // the clusters of a picture, biggest share first (ties keep palette order)
    static std::vector<PaletteEntry> palette(const unsigned char *pixels, int width, int height, int channels);

    // an RGB/RGBA picture (RGBA composited on black) resized with Pillow's Lanczos to outW x outH, RGB, packed
    static std::vector<unsigned char> downscale(const unsigned char *pixels, int width, int height, int channels,
                                                int outW, int outH);

    // WCAG relative luminance and contrast ratio (>= 1)
    static double luminance(const ThemeRgb &c);
    static double contrast(const ThemeRgb &a, const ThemeRgb &b);

    // sRGB <-> CIELAB (D65); fromLab is false when the colour is outside the sRGB gamut (unless clip)
    static void toLab(const ThemeRgb &c, double &L, double &a, double &b);
    static bool fromLab(double L, double a, double b, bool clip, ThemeRgb &out);
    static double chroma(const ThemeRgb &c);
    // the same hue at a new L*, chroma (optionally capped) reduced until it fits the gamut
    static ThemeRgb withLightness(const ThemeRgb &c, double L, double chromaCap = -1.0);
    // 0 -> a, 1 -> b, in sRGB, rounded half to even
    static ThemeRgb mix(const ThemeRgb &a, const ThemeRgb &b, double t);
    // raises L* (same hue) until the contrast vs the sheet reaches `minimum`; false when it never does
    static bool liftUntil(const ThemeRgb &c, const ThemeRgb &sheet, double minimum, ThemeRgb &out);
};
