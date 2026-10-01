// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::Style: the one look every panel, menu and dialog shares - a dark sheet with a one-pixel edge over the
// dimmed screen it came from, a bold header ruled off from the rows, the selected row as a translucent band
// with a bar at its left edge, and the button hints in the footer - as data (the colour roles, the metrics)
// and the stateless primitives that draw it. It was AutoBleem's PanelStyle, which is now a thin adapter over
// it. The colours come from the program as a plain ColorRoles block (AutoBleem fills it from its theme.json).
//
#pragma once

#include <ableem/ui/renderer.h>
#include <ableem/ui/texture.h>
#include <ableem/ui/types.h>

#include <string>
#include <vector>

namespace abgui {

class Context;

// the fonts a style draws in, by what they are for - the program says which face and size each one is
enum class FontRole {
    Title,    // a panel's title (AutoBleem: its launcher pair's bold at 28)
    Row,      // a compact panel's row title; the footer's hints and counter at their largest (medium at 22)
    RowSmall, // the footer's hints a step down; the "/" and "+" between button icons (bold at 20)
    Small,    // descriptions; a button chip's name; the footer's hints at their smallest (bold at 15)
    Classic,  // the classic screens' list rows (AutoBleem: the theme's classic font)
};

// a colour the program may leave unset
struct OptionalColor {
    bool set = false;
    ableem::Color color;

    OptionalColor() = default;
    explicit OptionalColor(const ableem::Color &c) : set(true), color(c) {}
};

// a colour role as the program gives it: a colour, else the name of another colour of the block ("secondary",
// "row"), else unset - the role's fallback
struct RoleColor {
    OptionalColor color;
    std::string ref;
};

// The colours a Style is built from (Style::fromColors). The base palette - `text`, `secondary`, `hint`,
// `selection` - is plain colours; each role may be a colour or name another colour, a role included. Unset
// falls back: row/heading/description/edge -> secondary, rowSelected/footer/selectionBand -> text, value -> row,
// hint -> secondary, selection -> text.
struct ColorRoles {
    OptionalColor text, secondary, hint, selection;
    RoleColor row, rowSelected, heading, value, description, footer, selectionBand, edge;
};

// The `disabled` role (G5t): the colour and the alpha laid over a row that cannot be changed. A theme's own - it is
// not a Style member (Style's layout is the SDK's), the Context hands it out (Context::disabledVeil). Unset = the
// code's black at Style::disabledAlpha, and a disabled row's text keeps its usual colour; set, the veil is this and the
// row's text is drawn in the `description` role (Style::disabledColor).
struct DisabledVeil {
    bool set = false;
    ableem::Color color{0, 0, 0, 255};
    unsigned char alpha = 150;

    // the colour as it is filled, the alpha in place
    ableem::Color drawn() const { return ableem::Color(color.r, color.g, color.b, alpha); }
};

// The inactive-state alphas (G5r9): how faint a thing that is not active draws - the Resume icon when the game has no
// resume point, a tab that is not the current one, the track of a bubble's progress bar. A theme's own, like the
// DisabledVeil (not a Style member - Style's layout is the SDK's; the Context hands it out, Context::inactiveAlphas).
// A value the theme leaves out is Unset: the caller keeps today's alpha (`orToday`), so no theme = the same drawing.
struct InactiveAlphas {
    static constexpr int Unset = -1;
    int resume = Unset;   // the Resume icon and its picture without a resume point (today 120)
    int tab = Unset;      // a tab icon that is not the current one (today 120)
    int barTrack = Unset; // the track under a progress bar's fill (today Style::progressTrackAlpha, 120)

    // `value` as an alpha, or `today` when unset
    static unsigned char orToday(int value, unsigned char today) {
        return value < 0 ? today : static_cast<unsigned char>(value > 255 ? 255 : value);
    }
};

// a colour of the style by its role, for the primitives that draw in "the text colour" or "the edge colour"
// with an alpha of the caller's choosing
enum class Tone {
    None,          // draw nothing (a box without a fill, or without an edge)
    Black,         // pure black
    White,         // pure white
    Text,          // Style::text
    Secondary,     // Style::secondary
    Edge,          // Style::edge
    SelectionBand, // Style::selectionBand
};

// a key cell's state (Style::key)
enum class KeyState {
    Normal,   // a shade of white over the sheet, an edge in the secondary colour
    Lit,      // Normal, lit up (Shift held on)
    Selected, // the cursor is on it: the text colour, filled and outlined
};

// a footer hint: one or more button icons ("X", "O", "T", "S", "Start", "Select", "L1", "R1", "L2", "R2",
// "Esc", "Enter", "Tab", "Up"...) and its label
struct HintItem {
    std::vector<std::string> icons;
    std::string label;
};

//********************
// Style
//********************
class Style {
public:
    // today's geometry, the metrics' defaults (AutoBleem's PanelStyle::HeaderHeight etc. are these)
    static constexpr int DefaultHeaderHeight = 74; // the title's band, the rule 8 px above its end
    static constexpr int DefaultFooterHeight = 54; // the hints' band
    static constexpr int DefaultRowHeight = 60;    // a title-and-description row
    static constexpr int DefaultRowInset = 24;     // the text from the panel's edge
    static constexpr int DefaultMargin = 40;       // the panel from the screen's edge
    static constexpr int DefaultSelectionBar = 5;  // the bar at the selected row's left edge
    // a button chip (a key without a picture): one size everywhere - 22 px tall, at least 28 wide, the name 7 px in;
    // a combination of keys with pictures (Left+Right) is one chip around them, PictureGap apart
    static constexpr int ChipHeight = 22;
    static constexpr int ChipMinWidth = 28;
    static constexpr int ChipPadding = 7;
    static constexpr int PictureGap = 2;
    // the footer's gap between hints at its largest, and between the hints and its status
    static constexpr int FooterGap = 36;
    static constexpr int FooterStatusGap = 36;

    // the colours resolved from the program's block: each role its own colour, or the colour it names, or its
    // fallback; a name nobody knows counts as unset, and a chain of names longer than the roles is a loop, cut
    // to `text`. Metrics and textShadow keep their defaults.
    static Style fromColors(const ColorRoles &roles);

    //*******************************
    // colours
    //*******************************
    ableem::Color text{255, 255, 255, 255};
    ableem::Color secondary{100, 100, 100, 255};
    ableem::Color hint{100, 100, 100, 255};
    bool textShadow = true; // the halo under every text on the panel (the program's text drawer applies it)
    // the roles (UIREV-29): every row, heading, value and description draws in one of these
    ableem::Color row{100, 100, 100, 255};           // an unselected row's text (secondary)
    ableem::Color rowSelected{255, 255, 255, 255};   // the selected row's text and value (text)
    ableem::Color heading{100, 100, 100, 255};       // the text on a heading band (secondary)
    ableem::Color value{100, 100, 100, 255};         // an unselected row's right-hand value (row)
    ableem::Color description{100, 100, 100, 255};   // second lines, subtitles, the strip, counters (secondary)
    ableem::Color footerText{255, 255, 255, 255};    // the footer's hint labels, role `footer` (text)
    ableem::Color selectionBand{255, 255, 255, 255}; // the selected row's band and bar (text)
    ableem::Color edge{100, 100, 100, 255};          // the sheet's edge, rules, the heading band (secondary)

    //*******************************
    // metrics
    //*******************************
    int headerHeight = DefaultHeaderHeight;
    int footerHeight = DefaultFooterHeight;
    int rowHeight = DefaultRowHeight;
    int rowInset = DefaultRowInset;
    int margin = DefaultMargin;
    int selectionBar = DefaultSelectionBar;
    int titleTop = 18;                 // the header's title below the panel's top
    int buttonHeight = 30;             // a footer hint's icon, and the height a chip is centred in
    int footerTop = 14;                // the footer's hints below the footer's top
    unsigned char dimAlpha = 110;      // the screen behind a panel, darkened to
    unsigned char sheetAlpha = 200;    // the sheet's black
    unsigned char edgeAlpha = 160;     // the sheet's edge and the rules
    unsigned char bandAlpha = 38;      // the selected row's band
    unsigned char labelAlpha = 70;     // a heading's band
    unsigned char disabledAlpha = 150; // the black over a row that cannot be changed
    // the keyboard's key cells and text field
    unsigned char keyAlpha = 18;         // a character key's white
    unsigned char keyFunctionAlpha = 8;  // a function key's white, a shade darker
    unsigned char keyLitAlpha = 50;      // a lit key's white
    unsigned char keySelectedAlpha = 60; // the selected key's fill (text colour)
    unsigned char keyEdgeAlpha = 110;    // an unselected key's edge (secondary colour)
    unsigned char fieldAlpha = 14;       // the text field's white
    unsigned char fieldEdgeAlpha = 160;  // the text field's edge (secondary colour)
    int caretWidth = 2;                  // the text caret
    // the busy spinner: dots on a ring, the brightest leading, each one behind it fading
    int spinnerDots = 12;
    int spinnerRadius = 30;
    int spinnerDot = 8;
    int spinnerFade = 19;
    // a progress bar's track and fill when the caller does not say otherwise
    unsigned char progressTrackAlpha = 120;
    int tabHeight = 3; // the active tab's underline

    // a row's text / its value, selected or not
    const ableem::Color &rowColor(bool selected) const { return selected ? rowSelected : row; }
    const ableem::Color &valueColor(bool selected) const { return selected ? rowSelected : value; }
    // a colour of the style by its name as a theme writes it - "text", "secondary", "hint", "row", "rowSelected",
    // "heading", "value", "description", "footer", "selectionBand", "edge"; false (out untouched) for any other
    bool colorByName(const std::string &name, ableem::Color &out) const;

    //*******************************
    // frames (G4, frame.h)
    //*******************************
    // the frame `name` of the Context (Context::frame) drawn into `box`, tinted by the style's colour its tint names
    // (an unknown name: its own colours); false, and nothing drawn, when the Context has no such frame - the caller
    // then draws its code-drawn shape. The primitives' Context overloads ask for theirs: sheet() "panel" (G4a)
    bool drawFrame(Context &ctx, const std::string &name, const ableem::Rect &box) const;
    // the same at `alpha` (255: as drawn above) - a frame that fades (G5a; the cover glow's, G5k)
    bool drawFrame(Context &ctx, const std::string &name, const ableem::Rect &box, unsigned char alpha) const;
    // the same with the frame's slices and bleed scaled by `scale` (scaledFrame): the cover glow follows its cover
    // (size / 222, G5k); a scale of 1 draws as above
    bool drawFrame(Context &ctx, const std::string &name, const ableem::Rect &box, unsigned char alpha,
                   float scale) const;
    // the first of `names` the Context has a frame for, drawn into `box` (the toast's: "toast", then "panel"); false,
    // and nothing drawn, when it has none of them (G5a)
    bool drawFirstFrame(Context &ctx, const std::vector<std::string> &names, const ableem::Rect &box) const;

    //*******************************
    // the primitives
    //*******************************
    // the screen behind the panel, darkened
    void dim(ableem::Renderer &renderer) const;
    void dim(Context &ctx) const;
    // the sheet and its edge; through the Context, the "panel" frame instead when there is one
    void sheet(ableem::Renderer &renderer, const ableem::Rect &panel) const;
    void sheet(Context &ctx, const ableem::Rect &panel) const;
    // a notification bubble's panel (G5f): the "toast" frame when the Context has one, else the "panel" frame (what
    // sheet() draws since G4b), else the code-drawn sheet and edge - so a theme without a toast frame is unchanged
    void toast(Context &ctx, const ableem::Rect &panel) const;
    // a one-pixel rule across the panel, inset, at y
    void rule(ableem::Renderer &renderer, const ableem::Rect &panel, int y) const;
    void rule(Context &ctx, const ableem::Rect &panel, int y) const;
    // the bold title at the top of the panel and the rule under it; returns the y the rows start at
    int header(Context &ctx, const ableem::Rect &panel, const std::string &title) const;
    // the selected row: the band and the bar, `rect` being the row's full extent
    void selection(ableem::Renderer &renderer, const ableem::Rect &rect) const;
    // through the Context, the "selection" frame (G4c) drawn into `rect` instead of the band and the bar when there
    // is one - then it must be drawn UNDER the row's text (selectionFramed), the code-drawn band goes over it
    void selection(Context &ctx, const ableem::Rect &rect) const;
    // whether the Context has a "selection" frame: the callers then draw the selection before the rows, not after
    bool selectionFramed(Context &ctx) const;
    // a row that cannot be changed (a locked setting): drawn over the row once it is drawn, the sheet's black
    // laid over it again so label, value and switch all fall back behind the rows around it
    void disabled(ableem::Renderer &renderer, const ableem::Rect &rect) const;
    // through the Context, the theme's `disabled` role (G5t: its colour and alpha) when it has one, else the above
    void disabled(Context &ctx, const ableem::Rect &rect) const;
    // the colour a disabled row's text is drawn in: `normal` (what the row would have) - or, when the theme has a
    // `disabled` role, the `description` role (G5t)
    const ableem::Color &disabledColor(Context &ctx, const ableem::Color &normal) const;
    // a heading row (a label between the rows): a faint band in the edge colour
    void label(ableem::Renderer &renderer, const ableem::Rect &rect) const;
    // the theme's `heading` frame (G4d) in the box when it has one, else the faint band
    void label(Context &ctx, const ableem::Rect &rect) const;
    // a small triangle at (cx, cy) pointing up (direction -1) or down (1): more rows that way
    void scrollMarker(ableem::Renderer &renderer, int cx, int cy, int direction) const;
    void scrollMarker(Context &ctx, int cx, int cy, int direction) const;

    // an alpha argument of OwnAlpha keeps the colour's own alpha (a theme colour is opaque unless it says otherwise)
    static constexpr int OwnAlpha = -1;
    // ... and StyleAlpha the style's own metric for that primitive (progressTrackAlpha, edgeAlpha)
    static constexpr int StyleAlpha = -2;
    // the role's colour with `alpha` (OwnAlpha: its own); Tone::None gives a transparent black
    ableem::Color tone(Tone role, int alpha = OwnAlpha) const;

    // a box: the fill in `fill` at `fillAlpha` under a one-pixel edge in `edgeTone` at `edgeAlpha`; Tone::None
    // leaves that part out. The defaults are the plain frame: an edge in the edge colour, no fill
    void box(ableem::Renderer &renderer, const ableem::Rect &rect, Tone fill = Tone::None, int fillAlpha = OwnAlpha,
             Tone edgeTone = Tone::Edge, int edgeAlpha = OwnAlpha) const;
    void box(Context &ctx, const ableem::Rect &rect, Tone fill = Tone::None, int fillAlpha = OwnAlpha,
             Tone edgeTone = Tone::Edge, int edgeAlpha = OwnAlpha) const;
    // a flat translucent plate in the caller's own colour (alpha included) - a theme's status bar, whose colour
    // and rect the theme gives
    void plate(ableem::Renderer &renderer, const ableem::Rect &rect, const ableem::Color &color) const;
    void plate(Context &ctx, const ableem::Rect &rect, const ableem::Color &color) const;
    // a key cell of the on-screen keyboard: a function key is a shade darker than a character key
    void key(ableem::Renderer &renderer, const ableem::Rect &rect, KeyState state = KeyState::Normal,
             bool function = false) const;
    void key(Context &ctx, const ableem::Rect &rect, KeyState state = KeyState::Normal, bool function = false) const;
    // the keyboard's text field, and the caret in it: `x`, `y` and `height` the caret's own place
    void field(ableem::Renderer &renderer, const ableem::Rect &rect) const;
    void field(Context &ctx, const ableem::Rect &rect) const;
    void caret(ableem::Renderer &renderer, int x, int y, int height) const;
    void caret(Context &ctx, int x, int y, int height) const;
    // a progress bar: the `track` in trackTone at trackAlpha, and over it from its left the share done/total in
    // fillTone at fillAlpha (nothing when total is 0; done is clamped to total)
    void progress(ableem::Renderer &renderer, const ableem::Rect &track, unsigned long long done,
                  unsigned long long total, Tone trackTone = Tone::Secondary, int trackAlpha = StyleAlpha,
                  Tone fillTone = Tone::Text, int fillAlpha = OwnAlpha) const;
    // through the Context (G5g): the theme's `progressTrack` frame is drawn into the whole bar and its `progressFill`
    // frame into the share done (from the left; nothing while that is 0 px wide) - each one independently, so a theme
    // with one of them keeps the code-drawn other; the tones and alphas then belong to the frames (their tint) and are
    // unused. No frame = the Renderer overload's two fills (the track alpha: the theme's `barTrack`, G5r9)
    void progress(Context &ctx, const ableem::Rect &track, unsigned long long done, unsigned long long total,
                  Tone trackTone = Tone::Secondary, int trackAlpha = StyleAlpha, Tone fillTone = Tone::Text,
                  int fillAlpha = OwnAlpha) const;
    // the width of a progress bar's fill: trackWidth * done / total (done clamped to total), 0 when total is 0
    static int progressFillWidth(int trackWidth, unsigned long long done, unsigned long long total);
    // an outlined progress bar, the Software Update prompt's: a one-pixel outline in the edge colour at
    // ProgressBoxEdgeAlpha, and inside it a fill in the text colour ProgressBoxInset px in from the outline, `fraction`
    // (0..1) of its width. It draws no blend mode of its own (the caller's, as the prompt always did)
    static constexpr int ProgressBoxInset = 2;
    static constexpr int ProgressBoxEdgeAlpha = 120;
    void progressBox(ableem::Renderer &renderer, const ableem::Rect &bar, double fraction) const;
    // through the Context (G5g): the `progressTrack` frame in the whole bar and the `progressFill` frame in `fraction`
    // of it (the outline's inset is for the code-drawn fill only), each one falling back to the code drawing alone
    void progressBox(Context &ctx, const ableem::Rect &bar, double fraction) const;
    // the rect of progressBox's fill: inside the outline (`framed` false - what the prompt always drew: the width is
    // the inner width times the fraction, cut to whole pixels) or from the bar's left edge over its whole height
    // (`framed` true, for a frame; the fraction clamped to 0..1)
    static ableem::Rect progressBoxFillRect(const ableem::Rect &bar, double fraction, bool framed);
    // the busy spinner: spinnerDots dots on a ring of `radius` around (cx, cy), `dot` px squares, dot `lead`
    // the brightest and every one behind it spinnerFade alpha dimmer; the program turns `lead` with its clock
    void spinner(ableem::Renderer &renderer, int cx, int cy, int radius, int dot, int lead) const;
    // through the Context, the theme's frame strip (G5p, spinner.h) instead when it has one: its current frame, drawn
    // centred on (cx, cy) at its own size - radius, dot and lead are then unused, the frame is (Context::ticks() * fps
    // / 1000) mod frames
    void spinner(Context &ctx, int cx, int cy, int radius, int dot, int lead) const;
    // the same fitted into `box`: the metrics' radius and dot, no bigger than the box allows (a strip: centred in it,
    // at its own size)
    void spinner(ableem::Renderer &renderer, const ableem::Rect &box, int lead) const;
    void spinner(Context &ctx, const ableem::Rect &box, int lead) const;
    // the strip's frame `elapsedMs` into the animation drawn centred on (cx, cy); false, and nothing drawn, when the
    // Context has no strip - the caller then draws its ring (Busy plays it from the job's start)
    bool spinnerStrip(Context &ctx, int cx, int cy, unsigned long long elapsedMs) const;
    // the active tab's underline: tabHeight tall, `w` wide, from (x, y), in the selection band's colour
    void tab(ableem::Renderer &renderer, int x, int y, int w) const;
    void tab(Context &ctx, int x, int y, int w) const;
    // the current tab of a tab strip (the set picker's), `cell` its whole box: the theme's `tab` frame (G5h), else the
    // cell filled with the selection band's colour at bandAlpha and a selectionBar-tall bar of it along the cell's
    // bottom. Drawn under the tab's icon and label; the other tabs draw nothing. The Renderer overload is the old look.
    void tabCell(ableem::Renderer &renderer, const ableem::Rect &cell) const;
    void tabCell(Context &ctx, const ableem::Rect &cell) const;
    // a vertical one-pixel rule from (x, y), `h` tall, in the edge colour; alpha: StyleAlpha is edgeAlpha, OwnAlpha is
    // the colour's own
    void vrule(ableem::Renderer &renderer, int x, int y, int h, int alpha = StyleAlpha) const;
    void vrule(Context &ctx, int x, int y, int h, int alpha = StyleAlpha) const;

    // the status-line protocol every screen writes - "Card 1/12   |@L1+R1| Page  |@X| Rename  |@O| Go back |"
    // - taken apart: the text before the first marker is the status (a counter, drawn at the footer's right
    // edge), each marker and the text up to the next one is a hint, a marker whose text is empty or a
    // separator ("/", "|") joins the next hint's icons
    static std::vector<HintItem> parseHints(const std::string &line, std::string &status);
    // the footer: the rule along the top of `footer` (footerHeight tall, the panel's width), the hints from the
    // left inset in the largest of the Row / RowSmall / Small fonts they fit in (then their labels shortened,
    // then icons only), the status at the right edge. The hints are drawn in the one order every screen
    // shares, whatever order they were given in: Cross, Circle, Triangle, Square, the d-pad, Start, Select, L1/R1,
    // L2/R2, then the keyboard's keys. G5r8: a theme's optional `footer` frame is drawn over `footer` (the band) instead
    // of the rule, and only when `withRule` is set; no frame = the rule, call for call
    void footer(Context &ctx, const ableem::Rect &footer, const std::vector<HintItem> &hints,
                const std::string &status = "", bool withRule = true) const;
    // the same from the protocol string
    void footer(Context &ctx, const ableem::Rect &footer, const std::string &line, bool withRule = true) const;
    // the footer's shared order (G5r3): where a hint's first key ranks - X, O, T, S, then the d-pad (Left, Right,
    // Up, Down), Start, Select, L1, R1, L2, R2, Enter, Esc, Tab; any other key last (100) - and the hints sorted by
    // it (stable)
    static int hintRank(const std::string &icon);
    static std::vector<HintItem> sortedHints(std::vector<HintItem> hints);
    // what a hint's keys take in the footer, each with the 6 px after it: `buttonWidth` per key (the theme's glyph
    // when there is one, else the chip) - the width the footer's fit is decided on
    int hintIconsWidth(Context &ctx, const HintItem &hint, int height = 30) const;
    // the width a footer of these hints and status needs in ONE row at the Row font with the largest gaps (the
    // rowInset each side included) - what Panel::compactWidth widens a compact panel to
    int footerWidth(Context &ctx, const std::vector<HintItem> &hints, const std::string &status) const;
    int footerWidth(Context &ctx, const std::string &line) const;

    // a dark outline/halo texture from an image's own alpha shape: the shape drawn in black at alpha 150 at
    // each of the eight 1 px offsets and once more 2 px down-right. The texture is the image's size plus 5 in
    // each dimension, the image's own shape sitting at (2, 2) in it; draw it at the icon's rect expanded by
    // (-2, -2, +5, +5). An invalid image (or one the renderer cannot back) gives an invalid texture back.
    static ableem::Texture outlineOf(ableem::Renderer &renderer, const ableem::Image &image);

    // one button at (x, y), `height` tall: a key with a glyph (Context::glyph - the face buttons, the d-pad)
    // as its image, with its outline under it when there is one; every other key (Start, Select, L1..R2, Esc,
    // or any word such as RESET) as a chip - a small dark box with a light edge and the name in Small bold
    // capitals; a combination ("L2+R2", "Select+Start": every "A+B" name is one chip) is one chip with the whole
    // name, or - when every part has a picture ("Left+Right") - one chip around the pictures. Every button is
    // centred on the line y + height / 2. Returns the width drawn.
    int button(Context &ctx, const std::string &key, int x, int y, int height = 30) const;
    // a marker string as a button guide writes it - "|@L2+Select|", "|@X| / |@O|", "RESET" - drawn as
    // icons, chips and the text between them; returns the width
    int buttons(Context &ctx, const std::string &markers, int x, int y, int height = 30) const;
    // the width the two above would draw, without drawing - for laying a row out first
    int buttonWidth(Context &ctx, const std::string &key, int height = 30) const;
    int buttonsWidth(Context &ctx, const std::string &markers, int height = 30) const;

private:
    struct PictureChip;
    bool pictureChip(Context &ctx, const std::string &key, int height, PictureChip &out) const;
    int layoutButtons(Context &ctx, const std::string &markers, int x, int y, int height, bool draw) const;
};

} // namespace abgui
