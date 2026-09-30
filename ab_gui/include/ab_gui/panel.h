// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::Panel: the classic panel every list, page and editor draws in - a sheet over the dimmed screen, the
// header's title ruled off at the top, the footer band with the hints at the bottom, the rows between them - as
// a rect and a Style, with its geometry (content, footer, how many rows fit, the compact panel of a short list)
// and its drawing (the sheet, the header, the footer, the scroll markers).
//
// The full panel's rect is the program's (Context::panelRect - AutoBleem's is the theme's menu panel); a compact
// panel is centred on the canvas, sized to its rows. A Panel holds no state beyond its rect and style: a widget
// makes one when it draws.
//
#pragma once

#include <ab_gui/style.h>

#include <ableem/ui/font.h>
#include <ableem/ui/types.h>

#include <string>

namespace abgui {

class Context;

//********************
// Panel
//********************
class Panel {
public:
    // a compact panel's width (a dialog's)
    static constexpr int CompactWidth = 800;
    // the gap under a compact panel's last row, above the footer band
    static constexpr int CompactRowGap = 8;

    Panel(const ableem::Rect &rect, const Style &style) : rect_(rect), style_(style) {}

    // the full panel: the Context's panel rect in its current style
    static Panel full(const Context &ctx);
    // a compact panel for a short list: CompactWidth wide, the header, `rows` (at least one) rows of `font`, a
    // CompactRowGap and the footer band tall, centred on the Context's canvas
    static Panel compact(const Context &ctx, int rows, const ableem::Font &font);
    // the compact panel's rect on a canvasWidth x canvasHeight canvas, rows `lineHeight` tall (the pure form)
    static ableem::Rect compactRect(const Style &style, int rows, int lineHeight, int canvasWidth, int canvasHeight);

    const ableem::Rect &rect() const { return rect_; }
    const Style &style() const { return style_; }

    //*******************************
    // geometry
    //*******************************
    // between the header and the footer band: where the rows go
    ableem::Rect content() const;
    // the footer band at the bottom (footerHeight tall, the panel's width)
    ableem::Rect footer() const;
    // how many rows `lineHeight` tall fit the content, one under the other (at least one)
    int rowsThatFit(int lineHeight) const;
    // ... of `font`; an invalid font counts as the Context's Classic font (the classic rows' own)
    int rowsThatFit(const Context &ctx, const ableem::Font &font) const;
    // where the scroll markers sit: their centre x (rowInset from the right edge), the up marker's point just
    // above the content, the down marker's just inside its bottom
    int scrollMarkerX() const;
    int scrollMarkerAboveY() const;
    int scrollMarkerBelowY() const;

    //*******************************
    // drawing
    //*******************************
    // the screen behind dimmed and the sheet with its edge over the panel's rect
    void sheet(Context &ctx) const;
    // the title at the top and the rule under it; returns the y the rows start at
    int header(Context &ctx, const std::string &title) const;
    // the footer band from the status-line protocol ("Card 1/12   |@X| Rename  |@O| Go back |"): the rule, the
    // hints from the left, the status at the right
    void footer(Context &ctx, const std::string &line, bool withRule = true) const;
    // a triangle at the top when rows are hidden above the first shown, at the bottom when below the last
    void scrollMarkers(Context &ctx, bool moreAbove, bool moreBelow) const;

private:
    ableem::Rect rect_;
    Style style_;
};

} // namespace abgui
