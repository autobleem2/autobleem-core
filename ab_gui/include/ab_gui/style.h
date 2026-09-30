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

    // a row's text / its value, selected or not
    const ableem::Color &rowColor(bool selected) const { return selected ? rowSelected : row; }
    const ableem::Color &valueColor(bool selected) const { return selected ? rowSelected : value; }

    //*******************************
    // the primitives
    //*******************************
    // the screen behind the panel, darkened
    void dim(ableem::Renderer &renderer) const;
    void dim(Context &ctx) const;
    // the sheet and its edge
    void sheet(ableem::Renderer &renderer, const ableem::Rect &panel) const;
    void sheet(Context &ctx, const ableem::Rect &panel) const;
    // a one-pixel rule across the panel, inset, at y
    void rule(ableem::Renderer &renderer, const ableem::Rect &panel, int y) const;
    void rule(Context &ctx, const ableem::Rect &panel, int y) const;
    // the bold title at the top of the panel and the rule under it; returns the y the rows start at
    int header(Context &ctx, const ableem::Rect &panel, const std::string &title) const;
    // the selected row: the band and the bar, `rect` being the row's full extent
    void selection(ableem::Renderer &renderer, const ableem::Rect &rect) const;
    void selection(Context &ctx, const ableem::Rect &rect) const;
    // a row that cannot be changed (a locked setting): drawn over the row once it is drawn, the sheet's black
    // laid over it again so label, value and switch all fall back behind the rows around it
    void disabled(ableem::Renderer &renderer, const ableem::Rect &rect) const;
    void disabled(Context &ctx, const ableem::Rect &rect) const;
    // a heading row (a label between the rows): a faint band in the edge colour
    void label(ableem::Renderer &renderer, const ableem::Rect &rect) const;
    void label(Context &ctx, const ableem::Rect &rect) const;
    // a small triangle at (cx, cy) pointing up (direction -1) or down (1): more rows that way
    void scrollMarker(ableem::Renderer &renderer, int cx, int cy, int direction) const;
    void scrollMarker(Context &ctx, int cx, int cy, int direction) const;

    // the status-line protocol every screen writes - "Card 1/12   |@L1|/|@R1| Page  |@X| Rename  |@O| Go back |"
    // - taken apart: the text before the first marker is the status (a counter, drawn at the footer's right
    // edge), each marker and the text up to the next one is a hint, a marker whose text is empty or a
    // separator ("/", "|") joins the next hint's icons
    static std::vector<HintItem> parseHints(const std::string &line, std::string &status);
    // the footer: the rule along the top of `footer` (footerHeight tall, the panel's width), the hints from the
    // left inset in the largest of the Row / RowSmall / Small fonts they fit in (then their labels shortened,
    // then icons only), the status at the right edge. The hints are drawn in the one order every screen
    // shares, whatever order they were given in: Cross, Circle, Triangle, Square, Start, Select, L1/R1, L2/R2,
    // then the keyboard's keys
    void footer(Context &ctx, const ableem::Rect &footer, const std::vector<HintItem> &hints,
                const std::string &status = "", bool withRule = true) const;
    // the same from the protocol string
    void footer(Context &ctx, const ableem::Rect &footer, const std::string &line, bool withRule = true) const;

    // a dark outline/halo texture from an image's own alpha shape: the shape drawn in black at alpha 150 at
    // each of the eight 1 px offsets and once more 2 px down-right. The texture is the image's size plus 5 in
    // each dimension, the image's own shape sitting at (2, 2) in it; draw it at the icon's rect expanded by
    // (-2, -2, +5, +5). An invalid image (or one the renderer cannot back) gives an invalid texture back.
    static ableem::Texture outlineOf(ableem::Renderer &renderer, const ableem::Image &image);

    // one button at (x, y), `height` tall: a key with a glyph (Context::glyph - the face buttons, the d-pad)
    // as its image, with its outline under it when there is one; every other key (Start, Select, L1..R2, Esc,
    // or any word such as RESET) as a chip - a small dark box with a light edge and the name in Small bold
    // capitals. Returns the width drawn.
    int button(Context &ctx, const std::string &key, int x, int y, int height = 30) const;
    // a marker string as a button guide writes it - "|@L2| + |@Select|", "|@X| / |@O|", "RESET" - drawn as
    // icons, chips and the text between them; returns the width
    int buttons(Context &ctx, const std::string &markers, int x, int y, int height = 30) const;
    // the width the two above would draw, without drawing - for laying a row out first
    int buttonWidth(Context &ctx, const std::string &key, int height = 30) const;
    int buttonsWidth(Context &ctx, const std::string &markers, int height = 30) const;

private:
    int layoutButtons(Context &ctx, const std::string &markers, int x, int y, int height, bool draw) const;
};

} // namespace abgui
