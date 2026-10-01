// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::ActionMenu: a compact panel of actions. See the header.
//
#include <ab_gui/action_menu.h>

#include <ab_gui/panel.h>

#include <algorithm>

using namespace std;
using ableem::Event;

namespace abgui {

constexpr int ActionMenu::Width;
constexpr int ActionMenu::HeadingHeight;

//*******************************
// ActionMenu::selectable / rowHeight / roomForRows
//*******************************
bool ActionMenu::selectable(const Item &item) {
    return !item.heading && !item.disabled;
}

int ActionMenu::rowHeight(const Style &style, const Item &item) {
    return item.heading ? HeadingHeight : style.rowHeight;
}

int ActionMenu::roomForRows(const Style &style, int canvasHeight) {
    return canvasHeight - 2 * style.margin - style.headerHeight - style.footerHeight;
}

//*******************************
// ActionMenu::visibleCount / scrolledTo
//*******************************
int ActionMenu::visibleCount(const Style &style, const vector<Item> &items, int first, int room) {
    int used = 0;
    int count = 0;
    for (int i = max(0, first); i < static_cast<int>(items.size()); i++) {
        const int height = rowHeight(style, items[i]);
        if (used + height > room)
            break;
        used += height;
        count++;
    }
    return max(1, count);
}

int ActionMenu::scrolledTo(const Style &style, const vector<Item> &items, int selected, int first, int room) {
    if (selected < first) {
        first = selected;
        if (first > 0 && items[first - 1].heading)
            first--;
    }
    while (selected >= first + visibleCount(style, items, first, room) && first < selected)
        first++;
    return first;
}

//*******************************
// ActionMenu::moved
//*******************************
int ActionMenu::moved(const vector<Item> &items, int from, int step, bool wrap) {
    const int count = static_cast<int>(items.size());
    int at = from;
    for (int tries = 0; tries < count; tries++) {
        at += step;
        if (wrap)
            at = (at + count) % count;
        else if (at < 0 || at >= count)
            return from;
        if (selectable(items[at]))
            return at;
    }
    return from;
}

//*******************************
// ActionMenu::open / room / moveSelection
//*******************************
int ActionMenu::room(const Style &style) const {
    return roomForRows(style, ctx.renderer().height());
}

void ActionMenu::open() {
    selected = max(0, min(selected, static_cast<int>(items.size()) - 1));
    firstVisible_ = 0;
    result = -1;
    if (items.empty())
        return;
    if (!selectable(items[selected]))
        selected = moved(items, selected, 1, true);
    const Style style = ctx.style();
    firstVisible_ = scrolledTo(style, items, selected, firstVisible_, room(style));
}

void ActionMenu::moveSelection(int step) {
    if (items.empty())
        return;
    selected = moved(items, selected, step, wrap);
    const Style style = ctx.style();
    firstVisible_ = scrolledTo(style, items, selected, firstVisible_, room(style));
}

//*******************************
// ActionMenu::draw
//*******************************
void ActionMenu::draw() {
    // a frame to draw over (the launcher's, captured when it started an extension), else the backdrop
    if (background.valid())
        ctx.renderer().copy(background, nullptr, nullptr);
    else
        ctx.drawBackdrop();
    const Style style = ctx.style();
    const int count = visibleCount(style, items, firstVisible_, room(style));
    const int last = min(static_cast<int>(items.size()), firstVisible_ + count);
    int rowsHeight = 0;
    for (int i = firstVisible_; i < last; i++)
        rowsHeight += rowHeight(style, items[i]);
    if (items.empty())
        rowsHeight = style.rowHeight; // an empty menu is still one row tall
    const int panelHeight = style.headerHeight + rowsHeight + style.footerHeight;
    const int width = ctx.renderer().width();
    const int height = ctx.renderer().height();
    const vector<HintItem> hints = {{{"X"}, crossLabel.empty() ? ctx.translate("Select") : crossLabel},
                                    {{"O"}, circleLabel.empty() ? ctx.translate("Back") : circleLabel}};
    // the window makes room for its footer's one row
    const int panelWidth = Panel::compactWidth(ctx, hints, "");
    const Panel panel(ableem::Rect((width - panelWidth) / 2, (height - panelHeight) / 2, panelWidth, panelHeight),
                      style);
    panel.sheet(ctx);
    const ableem::Rect &rect = panel.rect();

    const ableem::Font &rowFont = ctx.font(FontRole::Row);
    const ableem::Font &smallFont = ctx.font(FontRole::Small);
    int rowY = panel.header(ctx, title);
    if (!subtitle.empty()) {
        const int y = rect.y + style.titleTop + (ctx.font(FontRole::Title).lineHeight() - rowFont.lineHeight()) / 2;
        ctx.drawText(rowFont, subtitle, rect.x + rect.w - style.rowInset - ctx.textWidth(rowFont, subtitle), y,
                     style.description);
    }
    const int textX = rect.x + style.rowInset + 8;
    for (int i = firstVisible_; i < last; i++) {
        const Item &item = items[i];
        const int h = rowHeight(style, item);
        const ableem::Rect band(rect.x + 1, rowY, rect.w - 2, h);
        if (item.heading) {
            style.label(ctx, band);
            ctx.drawText(smallFont, item.title, textX, rowY + (h - smallFont.lineHeight()) / 2, style.heading);
        } else {
            if (i == selected)
                style.selection(ctx, band);
            const ableem::Color &titleColor =
                item.disabled ? style.disabledColor(ctx, style.rowColor(i == selected)) : style.rowColor(i == selected);
            ctx.drawText(rowFont, item.title, textX, rowY + 7, titleColor);
            ctx.drawText(smallFont, item.description, textX, rowY + 35, style.description);
            if (item.disabled)
                style.disabled(ctx, band);
        }
        rowY += h;
    }
    // scroll markers: a small triangle at the top or bottom edge of the rows when more are that way
    const int markerX = rect.x + rect.w - style.rowInset;
    if (firstVisible_ > 0)
        style.scrollMarker(ctx, markerX, rect.y + style.headerHeight - 4, -1);
    if (last < static_cast<int>(items.size()))
        style.scrollMarker(ctx, markerX, rect.y + style.headerHeight + rowsHeight + 2, 1);

    style.footer(ctx, panel.footer(), hints, "", false);
}

//*******************************
// ActionMenu::loop
//*******************************
void ActionMenu::loop() {
    menuVisible = true;
    gui.input().setFrameNeed(ableem::Input::FrameNeed::Idle); // nothing moves between presses
    while (menuVisible) {
        if (gui.input().frameDue())
            render();
        hold_.tick(gui.input(), ctx.ticks(), [&](int dir) {
            ctx.play(UiSound::Cursor);
            moveSelection(dir);
        });
        Event e;
        while (gui.input().poll(e)) {
            if (e.type == Event::Type::Quit)
                result = -1;
            if (handleQuit(e))
                continue;
            handle(e);
        }
    }
}

//*******************************
// ActionMenu::onAction / onUnmapped
//*******************************
// the pad: Confirm picks, Back leaves, the d-pad steps by its live state; keys do nothing
void ActionMenu::onAction(const ActionEvent &action) {
    const Event &e = action.event;
    if (e.type == Event::Type::DpadDown || e.type == Event::Type::DpadUp) {
        dpad();
    } else if (e.type == Event::Type::ButtonDown) {
        if (action.action == Action::Confirm)
            pick();
        else if (action.action == Action::Back)
            leave();
    }
}

void ActionMenu::onUnmapped(const Event &event) {
    if (event.type == Event::Type::DpadDown || event.type == Event::Type::DpadUp)
        dpad();
}

void ActionMenu::dpad() {
    if (gui.input().dpadUp()) {
        ctx.play(UiSound::Cursor);
        moveSelection(-1);
    } else if (gui.input().dpadDown()) {
        ctx.play(UiSound::Cursor);
        moveSelection(1);
    }
    hold_.track(gui.input(), ctx.ticks());
}

void ActionMenu::pick() {
    if (items.empty() || !selectable(items[selected]))
        return;
    ctx.play(UiSound::Cursor);
    result = selected;
    menuVisible = false;
}

void ActionMenu::leave() {
    ctx.play(UiSound::Cancel);
    result = -1;
    menuVisible = false;
}

} // namespace abgui
