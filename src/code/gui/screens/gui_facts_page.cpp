//
// GuiFactsPage: a read-only page of facts in the classic panel. See the header.
//
#include "gui_facts_page.h"
#include "../gui.h"

#include <algorithm>

using namespace std;

//*******************************
// GuiFactsPage::init
//*******************************
void GuiFactsPage::init() {
    font = gui->assets().themeFont;
    firstVisible = 0;
    refresh();
}

//*******************************
// GuiFactsPage::refresh
//*******************************
void GuiFactsPage::refresh() {
    lastRefresh = gui->platform().ticks();
    lines.clear();
    for (const InfoSection &section : collect()) {
        Line heading;
        heading.heading = true;
        heading.label = section.title;
        lines.push_back(heading);
        for (const InfoRow &row : section.rows) {
            Line line;
            line.label = row.label;
            line.value = row.value;
            lines.push_back(line);
        }
    }
    firstVisible = min(firstVisible, maxFirstVisible());
}

//*******************************
// GuiFactsPage::maxFirstVisible / scrollBy
//*******************************
int GuiFactsPage::maxFirstVisible() const {
    return max(0, static_cast<int>(lines.size()) - rowsThatFit);
}

void GuiFactsPage::scrollBy(int rows) {
    int target = max(0, min(maxFirstVisible(), firstVisible + rows));
    if (target == firstVisible) {
        app.audio().cancel.play();
        return;
    }
    firstVisible = target;
    app.audio().cursor.play();
}

//*******************************
// GuiFactsPage::render
//*******************************
void GuiFactsPage::render() {
    renderer.clear();
    gui->renderBackground();
    gui->renderTextBar();
    int yoffset = gui->renderHeader(title());

    // the rows go from below the header to the bottom of the panel, as in the Options menu
    const ableem::Rect panel = gui->text().getOpscreenRectOfTheme();
    const int fontHeight = font.lineHeight();
    rowsThatFit = gui->classicRowsThatFit(font);
    firstVisible = min(firstVisible, maxFirstVisible());

    // the values end at the rows' right edge, a little short of the scroll markers (they sit at the panel's
    // edge minus RowInset and would draw over a long value); one that is too long for the space right of the
    // labels (a path) is cut to what fits
    const int valueX = panel.w * 35 / 100;
    const int valueRight = panel.x + panel.w - PanelStyle::RowInset - 8 - 12;
    const int valueWidth = valueRight - panel.x - valueX;
    const int count = static_cast<int>(lines.size());
    for (int i = firstVisible, row = 0; i < count && row < rowsThatFit; i++, row++) {
        const int y = yoffset + fontHeight * row;
        const Line &line = lines[i];
        if (line.heading) {
            gui->text().renderLabelBox(0, y);
            gui->text().renderTextLine(line.label, -y, 0, XALIGN_LEFT, 0, font);
        } else {
            gui->text().renderTextLine(line.label, -y, 0, XALIGN_LEFT, 0, font);
            gui->text().renderRowValue(gui->text().elide(font, line.value, valueWidth), -y, 0, valueRight, font);
        }
    }

    gui->renderScrollMarkers(firstVisible > 0, firstVisible + rowsThatFit < count);

    // the text before the first hint is the footer's counter, drawn at its right edge
    string status;
    if (count > rowsThatFit) {
        const int page = firstVisible / rowsThatFit + 1;
        const int pages = (count + rowsThatFit - 1) / rowsThatFit;
        status = _("Page") + " " + to_string(page) + "/" + to_string(pages) + "   ";
    }
    const string extra = extraHints();
    if (!extra.empty())
        status += extra + "   ";
    status += "|@O| " + _("Back");
    if (count > rowsThatFit)
        status += "   |@L1|/|@R1| " + _("First/last") + "   |@L2|/|@R2| " + _("Page");
    gui->renderStatus(status);
    renderer.present();
}

//*******************************
// GuiFactsPage::loop
//*******************************
void GuiFactsPage::loop() {
    menuVisible = true;
    gui->input().setFrameNeed(ableem::Input::FrameNeed::Idle); // the refresh interval is far above 4 Hz
    while (menuVisible) {
        if (gui->platform().ticks() - lastRefresh >= refreshInterval)
            refresh();
        if (gui->input().frameDue())
            render();

        Event e;
        while (gui->input().poll(e)) {
            if (e.type == Event::Type::Quit) {
                menuVisible = false;
            }
            switch (e.type) {
            case Event::Type::DpadDown:
            case Event::Type::DpadUp:
                if (gui->input().dpadUp())
                    scrollBy(-1);
                else if (gui->input().dpadDown())
                    scrollBy(1);
                else if (gui->input().dpadLeft())
                    scrollBy(-rowsThatFit);
                else if (gui->input().dpadRight())
                    scrollBy(rowsThatFit);
                break;
            case Event::Type::ButtonDown:
                if (e.button == Button::L1) {
                    scrollBy(-static_cast<int>(lines.size())); // the first row
                } else if (e.button == Button::R1) {
                    scrollBy(static_cast<int>(lines.size())); // the last (scrollBy clamps)
                } else if (e.button == Button::L2) {
                    scrollBy(-rowsThatFit);
                } else if (e.button == Button::R2) {
                    scrollBy(rowsThatFit);
                } else if (onButton(e.button)) {
                    // the page's own; the sub-screen it may have shown could have changed the facts
                    refresh();
                } else if (e.button == Button::Circle) {
                    app.audio().cancel.play();
                    menuVisible = false;
                }
                break;
            default:
                break;
            }
        }
    }
}
