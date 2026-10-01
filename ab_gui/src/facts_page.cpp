// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::FactsPage: a read-only page of facts in the classic panel. See the header.
//
#include <ab_gui/facts_page.h>

#include <ab_gui/panel.h>

#include <algorithm>

using namespace std;
using ableem::Button;
using ableem::Event;

namespace abgui {

//*******************************
// elideText
//*******************************
string elideText(const string &text, int width, const function<int(const string &)> &measure) {
    if (measure(text) <= width)
        return text;
    const string dots = "...";
    string cut = text;
    while (!cut.empty() && measure(cut + dots) > width) {
        cut.pop_back();
        while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80)
            cut.pop_back(); // a whole UTF-8 char
    }
    return cut + dots;
}

//*******************************
// FactsPage: the pure parts
//*******************************
vector<FactsPage::Line> FactsPage::linesOf(const vector<FactsSection> &sections) {
    vector<Line> lines;
    for (const FactsSection &section : sections) {
        Line heading;
        heading.heading = true;
        heading.label = section.title;
        lines.push_back(heading);
        for (const FactsRow &row : section.rows) {
            Line line;
            line.label = row.label;
            line.value = row.value;
            lines.push_back(line);
        }
    }
    return lines;
}

int FactsPage::maxFirstVisible(int count, int rowsThatFit) {
    return max(0, count - rowsThatFit);
}

int FactsPage::scrolled(int firstVisible, int rows, int count, int rowsThatFit) {
    return max(0, min(maxFirstVisible(count, rowsThatFit), firstVisible + rows));
}

bool FactsPage::refreshDue(unsigned int now, unsigned int lastRefresh, unsigned int interval) {
    return now - lastRefresh >= interval;
}

FactsPage::Counter FactsPage::counter(int firstVisible, int count, int rowsThatFit) {
    Counter counter;
    counter.page = firstVisible / rowsThatFit + 1;
    counter.pages = (count + rowsThatFit - 1) / rowsThatFit;
    return counter;
}

FactsPage::ValueColumn FactsPage::valueColumn(const ableem::Rect &panel, int rowInset) {
    ValueColumn column;
    column.offset = panel.w * 35 / 100;
    column.right = panel.x + panel.w - rowInset - 8 - 12;
    column.width = column.right - panel.x - column.offset;
    return column;
}

//*******************************
// FactsPage::open / refresh / restore
//*******************************
void FactsPage::open() {
    firstVisible_ = 0;
    refresh();
}

void FactsPage::refresh() {
    lastRefresh_ = ctx.ticks();
    lines_ = linesOf(collect());
    firstVisible_ = min(firstVisible_, maxFirstVisible(static_cast<int>(lines_.size()), rowsThatFit_));
}

void FactsPage::restore(const vector<Line> &lines, int firstVisible, int rowsThatFit, unsigned int lastRefresh) {
    lines_ = lines;
    firstVisible_ = firstVisible;
    rowsThatFit_ = rowsThatFit;
    lastRefresh_ = lastRefresh;
}

//*******************************
// FactsPage::scrollBy
//*******************************
void FactsPage::scrollBy(int rows) {
    const int target = scrolled(firstVisible_, rows, static_cast<int>(lines_.size()), rowsThatFit_);
    if (target == firstVisible_) {
        ctx.play(UiSound::Cancel);
        return;
    }
    firstVisible_ = target;
    ctx.play(UiSound::Cursor);
}

//*******************************
// FactsPage::draw
//*******************************
void FactsPage::draw() {
    ctx.drawBackdrop();
    const Panel panel = Panel::full(ctx);
    panel.sheet(ctx);
    const Style &style = panel.style();
    int yoffset = panel.header(ctx, title());

    // the rows go from below the header to the bottom of the panel, as in the Options menu
    const ableem::Font &rowFont = font.valid() ? font : ctx.font(FontRole::Classic);
    const int fontHeight = rowFont.lineHeight();
    const int count = static_cast<int>(lines_.size());
    rowsThatFit_ = panel.rowsThatFit(ctx, rowFont);
    firstVisible_ = min(firstVisible_, maxFirstVisible(count, rowsThatFit_));

    // the values end at the rows' right edge, a little short of the scroll markers (they sit at the panel's
    // edge minus RowInset and would draw over a long value); one that is too long for the space right of the
    // labels (a path) is cut to what fits
    const ableem::Rect &area = panel.rect();
    const ValueColumn column = valueColumn(area, style.rowInset);
    const int labelX = area.x + style.rowInset + 8; // level with the header's title
    const int bandHeight = ctx.font(FontRole::Classic).lineHeight();
    auto measure = [&rowFont](const string &s) { return rowFont.width(s); };
    for (int i = firstVisible_, row = 0; i < count && row < rowsThatFit_; i++, row++) {
        const int y = yoffset + fontHeight * row;
        const Line &line = lines_[i];
        // the theme's roles (UIREV-29): headings in heading, labels in row, values bright (rowSelected) - a
        // page with no cursor must not read as a dimmed list
        if (line.heading) {
            style.label(ctx, ableem::Rect(area.x + 1, y, area.w - 2, bandHeight));
            ctx.drawText(rowFont, line.label, labelX, y, style.heading);
        } else {
            ctx.drawText(rowFont, line.label, labelX, y, style.row);
            const string value = elideText(line.value, column.width, measure);
            ctx.drawText(rowFont, value, column.right - ctx.textWidth(rowFont, value), y, style.rowSelected);
        }
    }

    panel.scrollMarkers(ctx, firstVisible_ > 0, firstVisible_ + rowsThatFit_ < count);

    // the text before the first hint is the footer's counter, drawn at its right edge
    string status;
    if (count > rowsThatFit_) {
        const Counter page = counter(firstVisible_, count, rowsThatFit_);
        status = ctx.translate("Page") + " " + to_string(page.page) + "/" + to_string(page.pages) + "   ";
    }
    const string extra = extraHints();
    if (!extra.empty())
        status += extra + "   ";
    status += "|@O| " + ctx.translate("Back");
    if (count > rowsThatFit_)
        status += "   |@L1+R1| " + ctx.translate("First/last") + "   |@L2+R2| " + ctx.translate("Page");
    panel.footer(ctx, status);
}

//*******************************
// FactsPage::loop
//*******************************
void FactsPage::loop() {
    menuVisible = true;
    gui.input().setFrameNeed(ableem::Input::FrameNeed::Idle); // the refresh interval is far above 4 Hz
    while (menuVisible) {
        if (refreshDue(ctx.ticks(), lastRefresh_, refreshInterval))
            refresh();
        if (gui.input().frameDue())
            render();

        Event e;
        while (gui.input().poll(e)) {
            if (handleQuit(e))
                continue;
            handle(e);
        }
    }
}

//*******************************
// FactsPage::onAction / onUnmapped
//*******************************
void FactsPage::onAction(const ActionEvent &action) {
    padEvent(action.event, action.action);
}

// a pad button no action is bound to still reaches onButton()
void FactsPage::onUnmapped(const Event &event) {
    padEvent(event, Action::None);
}

void FactsPage::padEvent(const Event &e, Action action) {
    const int count = static_cast<int>(lines_.size());
    switch (e.type) {
    case Event::Type::DpadDown:
    case Event::Type::DpadUp:
        if (gui.input().dpadUp())
            scrollBy(-1);
        else if (gui.input().dpadDown())
            scrollBy(1);
        else if (gui.input().dpadLeft())
            scrollBy(-rowsThatFit_);
        else if (gui.input().dpadRight())
            scrollBy(rowsThatFit_);
        break;
    case Event::Type::ButtonDown:
        if (action == Action::PrevTab) {
            scrollBy(-count); // the first row
        } else if (action == Action::NextTab) {
            scrollBy(count); // the last (scrollBy clamps)
        } else if (action == Action::PageUp) {
            scrollBy(-rowsThatFit_);
        } else if (action == Action::PageDown) {
            scrollBy(rowsThatFit_);
        } else if (onButton(e.button)) {
            // the page's own; the sub-screen it may have shown could have changed the facts
            refresh();
        } else if (action == Action::Back) {
            ctx.play(UiSound::Cancel);
            menuVisible = false;
        }
        break;
    default:
        break;
    }
}

} // namespace abgui
