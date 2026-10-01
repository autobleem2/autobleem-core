// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::Panel: the classic panel's geometry and drawing. See the header.
//
#include <ab_gui/panel.h>

#include <ab_gui/context.h>

#include <algorithm>

using namespace std;
using ableem::Rect;

namespace abgui {

constexpr int Panel::CompactWidth;
constexpr int Panel::CompactRowGap;

//*******************************
// Panel::full / compact
//*******************************
Panel Panel::full(const Context &ctx) {
    return Panel(ctx.panelRect(), ctx.style());
}

Panel Panel::compact(const Context &ctx, int rows, const ableem::Font &font) {
    const Style style = ctx.style();
    return Panel(compactRect(style, rows, font.lineHeight(), ctx.renderer().width(), ctx.renderer().height()), style);
}

Panel Panel::compact(Context &ctx, int rows, const ableem::Font &font, const std::string &footerLine) {
    const Style style = ctx.style();
    const int canvasWidth = ctx.renderer().width();
    const int width = compactWidth(style, style.footerWidth(ctx, footerLine), canvasWidth);
    return Panel(compactRect(style, rows, font.lineHeight(), canvasWidth, ctx.renderer().height(), width), style);
}

int Panel::compactWidth(const Style &style, int footerNeeds, int canvasWidth) {
    const int widest = std::max(CompactWidth, canvasWidth - 2 * style.margin);
    return std::min(std::max(CompactWidth, footerNeeds), widest);
}

int Panel::compactWidth(Context &ctx, const std::string &footerLine) {
    const Style style = ctx.style();
    return compactWidth(style, style.footerWidth(ctx, footerLine), ctx.renderer().width());
}

int Panel::compactWidth(Context &ctx, const std::vector<HintItem> &hints, const std::string &status) {
    const Style style = ctx.style();
    return compactWidth(style, style.footerWidth(ctx, hints, status), ctx.renderer().width());
}

Rect Panel::compactRect(const Style &style, int rows, int lineHeight, int canvasWidth, int canvasHeight, int width) {
    const int height = style.headerHeight + std::max(1, rows) * lineHeight + CompactRowGap + style.footerHeight;
    return Rect((canvasWidth - width) / 2, (canvasHeight - height) / 2, width, height);
}

//*******************************
// Panel::content / footer / rowsThatFit
//*******************************
Rect Panel::content() const {
    return Rect(rect_.x, rect_.y + style_.headerHeight, rect_.w, rect_.h - style_.headerHeight - style_.footerHeight);
}

Rect Panel::footer() const {
    return Rect(rect_.x, rect_.y + rect_.h - style_.footerHeight, rect_.w, style_.footerHeight);
}

int Panel::rowsThatFit(int lineHeight) const {
    return std::max(1, (content().h - 4) / std::max(1, lineHeight));
}

int Panel::rowsThatFit(const Context &ctx, const ableem::Font &font) const {
    return rowsThatFit(font.valid() ? font.lineHeight() : ctx.font(FontRole::Classic).lineHeight());
}

//*******************************
// Panel::scrollMarker*
//*******************************
int Panel::scrollMarkerX() const {
    const Rect c = content();
    return c.x + c.w - style_.rowInset;
}

int Panel::scrollMarkerAboveY() const {
    return content().y - 4;
}

int Panel::scrollMarkerBelowY() const {
    const Rect c = content();
    return c.y + c.h - 6;
}

//*******************************
// Panel::sheet / header / footer / scrollMarkers
//*******************************
void Panel::sheet(Context &ctx) const {
    style_.dim(ctx);
    style_.sheet(ctx, rect_);
}

int Panel::header(Context &ctx, const string &title) const {
    return style_.header(ctx, rect_, title);
}

void Panel::footer(Context &ctx, const string &line, bool withRule) const {
    style_.footer(ctx, footer(), line, withRule);
}

void Panel::scrollMarkers(Context &ctx, bool moreAbove, bool moreBelow) const {
    const int cx = scrollMarkerX();
    if (moreAbove)
        style_.scrollMarker(ctx, cx, scrollMarkerAboveY(), -1);
    if (moreBelow)
        style_.scrollMarker(ctx, cx, scrollMarkerBelowY(), 1);
}

} // namespace abgui
