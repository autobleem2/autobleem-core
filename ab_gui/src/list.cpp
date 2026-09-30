// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::List: the classic list's drawing, row geometry and input. See the header.
//
#include <ab_gui/list.h>

#include <ab_gui/hold_repeat.h>
#include <ab_gui/layout.h>

#include <ableem/ui/debug_driver.h>

#include <algorithm>
#include <typeinfo>

using namespace std;
using ableem::Event;
using ableem::Key;
using ableem::Rect;

namespace abgui {

constexpr int List::CompactRows;

//*******************************
// List::List
//*******************************
List::List(ableem::GuiBase &gui, Context &context)
    : Screen(gui, context), selected(own_.selected), firstVisible(own_.firstVisible), lastVisible(own_.lastVisible),
      maxVisible(own_.maxVisible), firstRow(own_.firstRow), yoffset(own_.yoffset),
      selectionXOffset(own_.selectionXOffset), selectionRightEdge(own_.selectionRightEdge),
      firstRender(own_.firstRender), labelsOnly(own_.labelsOnly), cancelled(own_.cancelled), font(own_.font),
      visible(menuVisible) {}

List::List(ableem::GuiBase &gui, Context &context, const Refs &refs)
    : Screen(gui, context), selected(refs.selected), firstVisible(refs.firstVisible), lastVisible(refs.lastVisible),
      maxVisible(refs.maxVisible), firstRow(refs.firstRow), yoffset(refs.yoffset),
      selectionXOffset(refs.selectionXOffset), selectionRightEdge(refs.selectionRightEdge),
      firstRender(refs.firstRender), labelsOnly(refs.labelsOnly), cancelled(refs.cancelled), font(refs.font),
      visible(refs.visible) {}

//*******************************
// the row geometry
//*******************************
int List::rowTop(int line, int yoffset, int lineHeight) {
    return line < 0 ? -line : lineHeight * line + yoffset;
}

int List::textLeft(const Rect &panel, const Style &style, int xoffset) {
    return panel.x + style.rowInset + 8 + xoffset; // level with the header's title
}

int List::valueRight(const Rect &panel, const Style &style, int rightEdge) {
    return rightEdge > 0 ? rightEdge : panel.x + panel.w - style.rowInset - 8;
}

Rect List::band(const Rect &panel, int top, int height, int xoffset, int rightEdge) {
    Rect rect;
    rect.x = panel.x + 1 + xoffset;
    rect.y = top;
    rect.w = (rightEdge > 0 ? rightEdge + 12 : panel.x + panel.w - 1) - rect.x;
    rect.h = height;
    return rect;
}

int List::switchState(const string &text, string *label) {
    int state = -1;
    if (text.find("|@Check|") != string::npos)
        state = 1;
    if (text.find("|@Uncheck|") != string::npos)
        state = 0;
    if (label != nullptr)
        *label = state == -1 ? text : text.substr(0, text.find('|'));
    return state;
}

bool List::drawSwitch(Context &ctx, bool on, int right, int top, int rowHeight) {
    // the images are used only when the theme has both; with one of them the row says ON/OFF in both states
    const ableem::Texture onIcon = ctx.icon("switchOn");
    const ableem::Texture offIcon = ctx.icon("switchOff");
    if (!onIcon.valid() || !offIcon.valid())
        return false;
    const ableem::Texture icon = on ? onIcon : offIcon;
    const ableem::Size size = icon.size();
    const Rect dst = switchRect(right, top, rowHeight, size.w, size.h);
    ctx.renderer().copy(icon, nullptr, &dst);
    return true;
}

bool List::isCompact(int size, int selectionRightEdge) {
    return size <= CompactRows && selectionRightEdge == 0;
}

//*******************************
// what the list draws
//*******************************
int List::size() {
    return static_cast<int>(rows.size());
}

bool List::isEmpty() {
    return size() == 0;
}

bool List::skip(int index) {
    return index >= 0 && index < static_cast<int>(rows.size()) && rows[index].heading;
}

string List::titleText() {
    return title;
}

string List::statusText() {
    return status.empty() ? entryStatus() : status;
}

string List::rowName(int index) {
    return index >= 0 && index < static_cast<int>(rows.size()) ? rows[index].label : string();
}

const char *List::screenName() {
    return typeid(*this).name();
}

string List::entryStatus() {
    return ctx.translate("Entry") + " " + to_string(selected + 1) + "/" + to_string(size()) + "    |@L1|/|@R1| " +
           ctx.translate("First/last") + "   |@L2|/|@R2| " + ctx.translate("Page") + "   |@X| " +
           ctx.translate("Select") + "   |@O| " + ctx.translate("Back") + " |";
}

const ableem::Font &List::rowFont() const {
    return font.valid() ? font : ctx.font(FontRole::Classic);
}

// the list's own row: the label (a switch's without its marker), the value right-aligned - a switch's ON/OFF - in the
// style's row colours; a heading on its band in the heading colour; a disabled row under the veil
void List::drawRow(int index, int line, bool isSelected) {
    if (index < 0 || index >= static_cast<int>(rows.size()))
        return;
    const Row &row = rows[index];
    const ableem::Font &f = rowFont();
    const Style style = ctx.style();
    const Rect panel = currentPanel().rect();
    const int lineHeight = f.lineHeight();
    const int top = rowTop(line, yoffset, lineHeight);
    if (row.heading) {
        style.label(ctx, band(panel, top, lineHeight, 0, selectionRightEdge));
        ctx.drawText(f, row.label, textLeft(panel, style), top, style.heading);
        return;
    }
    string label;
    const int on = switchState(row.label, &label);
    // a switch is the theme's image when it ships one (G5m), else the text ON/OFF
    const bool image =
        on != -1 && drawSwitch(ctx, on == 1, valueRight(panel, style, selectionRightEdge), top, lineHeight);
    const string value = on == -1 ? row.value : image ? string() : ctx.translate(on == 1 ? "ON" : "OFF");
    // a disabled row's text is the `description` role when the theme has a `disabled` role (G5t), else as any row's
    const ableem::Color &labelColor =
        row.disabled ? style.disabledColor(ctx, style.rowColor(isSelected)) : style.rowColor(isSelected);
    ctx.drawText(f, label, textLeft(panel, style), top, labelColor);
    if (!value.empty()) {
        const int right = valueRight(panel, style, selectionRightEdge);
        const ableem::Color &valueColor =
            row.disabled ? style.disabledColor(ctx, style.valueColor(isSelected)) : style.valueColor(isSelected);
        ctx.drawText(f, value, right - ctx.textWidth(f, value), top, valueColor);
    }
    if (row.disabled)
        style.disabled(ctx, band(panel, top, lineHeight, 0, selectionRightEdge));
}

//*******************************
// List::layout / init
//*******************************
void List::layout() {
    // the rows pack at the font's height and scroll a row at a time when there are more than fit
    maxVisible = currentPanel().rowsThatFit(ctx, font);
    lastVisible = firstVisible + maxVisible - 1;
}

void List::init() {
    if (ctx.hasInput())
        ctx.input().setFrameNeed(ableem::Input::FrameNeed::Idle); // a list: nothing moves between presses
    layout();
}

//*******************************
// List::currentPanel
//*******************************
Panel List::currentPanel() const {
    return Panel(ctx.currentPanelRect(), ctx.style());
}

//*******************************
// List::draw
//*******************************
// the classic list's frame, call for call: the backdrop, the compact panel set (a short list with nothing beside it)
// so the rows drawn meanwhile follow it, the sheet, the header, the page from the cursor at the first draw, the rows,
// the band (before the rows when the theme has a `selection` frame), the markers, the footer, the compact panel
// dropped
void List::draw() {
    ctx.drawBackdrop();
    const bool compact = isCompact(size(), selectionRightEdge);
    if (compact)
        ctx.setCompactPanel(Panel::compact(ctx, size(), font).rect());
    const Panel panel = currentPanel();
    panel.sheet(ctx);
    yoffset = panel.header(ctx, titleText());

    if (firstRender) {
        ListModel::computePagePosition(view());
        firstRender = false;
    }
    // a themed selection frame goes under the rows' text; the code-drawn band stays over it, as it always was
    const bool framed = panel.style().selectionFramed(ctx);
    if (framed)
        drawSelection();
    drawRows();
    if (!framed)
        drawSelection();
    currentPanel().scrollMarkers(ctx, firstVisible > 0, lastVisible < size() - 1);

    currentPanel().footer(ctx, statusText());
    if (compact)
        ctx.clearCompactPanel(); // drawn: the stack presents next, and nothing there reads the panel
}

//*******************************
// List::drawRows / drawSelection
//*******************************
void List::drawRows() {
    publish(); // here, not in draw(): a list that draws its own frame (Game Manager) still calls this
    if (selected >= 0 && size() > 0) {
        int line = firstRow;
        for (int i = firstVisible; i <= lastVisible; i++) {
            if (i < 0 || i >= size())
                break;
            drawRow(i, line, !labelsOnly && i == selected);
            line++;
        }
    }
}

void List::drawSelection() {
    if (labelsOnly || size() <= 0)
        return;
    const ableem::Font &f = rowFont();
    const int lineHeight = f.lineHeight();
    const int line = selected - firstVisible + firstRow;
    const Panel panel = currentPanel();
    panel.style().selection(
        ctx, band(panel.rect(), yoffset + lineHeight * line, lineHeight, selectionXOffset, selectionRightEdge));
}

//*******************************
// List::publish
//*******************************
// Skipped once the list is closing: a busy job redraws a closed list as its backdrop, and that must not publish into
// the screen below.
void List::publish() {
    if (!visible || !ableem::DebugDriver::active())
        return;
    const vector<string> names = driverItems();
    ableem::DebugDriver::publish(screenName(), names, driverSelected());
}

vector<string> List::driverItems() {
    vector<string> names;
    const int count = size();
    names.reserve(count > 0 ? count : 0);
    for (int i = 0; i < count; i++) {
        string name = rowName(i);
        std::replace(name.begin(), name.end(), '|', '/');
        names.push_back((skip(i) ? "#" : "") + name);
    }
    return names;
}

int List::driverSelected() {
    return labelsOnly || size() == 0 ? -1 : selected;
}

//*******************************
// the moves
//*******************************
ListModel::View List::view() {
    return {selected, firstVisible, lastVisible, maxVisible, size()};
}

namespace {
// the list's skip() as ListModel's predicate
struct Skipper {
    List &list;
    bool operator()(int index) const { return list.skip(index); }
};
} // namespace

// every move publishes at once (not only in the next frame), so the DebugDriver's `selected` never lags the cursor
void List::stepDown() {
    ctx.play(UiSound::Cursor);
    if (!labelsOnly)
        ListModel::stepDown(view(), Skipper{*this});
    publish();
}

void List::stepUp() {
    ctx.play(UiSound::Cursor);
    if (!labelsOnly)
        ListModel::stepUp(view(), Skipper{*this});
    publish();
}

void List::pageDown() {
    ctx.play(UiSound::HomeUp);
    if (!labelsOnly)
        ListModel::pageDown(view(), Skipper{*this});
    publish();
}

void List::pageUp() {
    ctx.play(UiSound::HomeDown);
    if (!labelsOnly)
        ListModel::pageUp(view(), Skipper{*this});
    publish();
}

void List::first() {
    ctx.play(UiSound::HomeDown);
    if (!labelsOnly)
        ListModel::home(view(), Skipper{*this});
    publish();
}

void List::last() {
    ctx.play(UiSound::HomeDown);
    if (!labelsOnly)
        ListModel::end(view(), Skipper{*this});
    publish();
}

void List::confirm() {
    ctx.play(UiSound::Cursor);
    publish(); // the row this press takes, before the list closes
    cancelled = false;
    if (!isEmpty())
        visible = false;
}

void List::back() {
    ctx.play(UiSound::Cancel);
    cancelled = true;
    visible = false;
}

void List::step(int by) {
    by > 0 ? stepDown() : stepUp();
}

void List::redraw() {
    render();
}

// one step at the press, then - while nothing else comes from the pad or the keyboard - the same step again at
// HoldRepeat's pace (the one every screen's held key uses): its delay first, then its interval, faster when held long
void List::holdRows(int by) {
    HoldRepeat hold;
    hold.press(by, ctx.ticks());
    step(by);
    redraw();
    while (!gui.input().padEventPending()) {
        if (hold.due(ctx.ticks()) != 0) {
            step(by);
            redraw();
        } else {
            ctx.delay(2); // a few ms of repeat timing, not a core spinning on the queue
        }
    }
}

//*******************************
// List::onAction / onUnmapped
//*******************************
// the d-pad by its live state; the buttons by their action; a key as the key it is
void List::onAction(const ActionEvent &action) {
    const Event &e = action.event;
    switch (e.type) {
    case Event::Type::DpadDown:
    case Event::Type::DpadUp:
        dpad();
        return;
    case Event::Type::ButtonDown:
        switch (action.action) {
        case Action::Confirm:
            confirm();
            return;
        case Action::Back:
            back();
            return;
        case Action::PrevTab: // L1/R1: the first/last row on a list
        case Action::First:
            first();
            return;
        case Action::NextTab:
        case Action::Last:
            last();
            return;
        case Action::PageUp:
            pageUp();
            return;
        case Action::PageDown:
            pageDown();
            return;
        default:
            return;
        }
    case Event::Type::KeyDown:
        key(e.key);
        return;
    default:
        return;
    }
}

void List::onUnmapped(const Event &event) {
    if (event.type == Event::Type::DpadDown || event.type == Event::Type::DpadUp)
        dpad();
    else if (event.type == Event::Type::KeyDown)
        key(event.key);
}

void List::dpad() {
    if (gui.input().dpadUp())
        holdRows(-1);
    else if (gui.input().dpadDown())
        holdRows(1);
}

void List::key(Key k) {
    switch (k) {
    case Key::Up:
        stepUp();
        break;
    case Key::Down:
        stepDown();
        break;
    case Key::PageDown:
        pageDown();
        break;
    case Key::PageUp:
        pageUp();
        break;
    case Key::Home:
        first();
        break;
    case Key::End:
        last();
        break;
    case Key::Return:
        confirm();
        break;
    case Key::Escape:
        back();
        break;
    default:
        break;
    }
}

} // namespace abgui
