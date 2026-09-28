//
// GuiTextPage: static text in the classic layout.
//
#include "gui_text_page.h"
#include "../gui.h"
#include "../panel_style.h"

#include <algorithm>
#include <cctype>

using namespace std;

//*******************************
// GuiTextPage::splitItem
//*******************************
GuiTextPage::Item GuiTextPage::splitItem(const string &line) {
    Item item;
    size_t start = line.find_first_not_of(' ');
    if (start == string::npos)
        start = line.size();
    item.indent = start;
    item.text = line.substr(start);
    size_t digits = 0;
    while (digits < item.text.size() && isdigit(static_cast<unsigned char>(item.text[digits])))
        digits++;
    if (digits > 0 && digits + 2 < item.text.size() && item.text[digits] == '.' && item.text[digits + 1] == ' ') {
        item.marker = item.text.substr(0, digits + 2);
        item.text = item.text.substr(digits + 2);
    }
    return item;
}

//*******************************
// GuiTextPage::render
//*******************************
void GuiTextPage::render() {
    gui->renderBackground();
    gui->renderTextBar();
    const ableem::Rect panel = gui->classicPanel();
    const ableem::Font &font = gui->assets().themeFont;
    const int lineHeight = font.lineHeight();
    int yoffset = gui->renderHeader(title);
    // each line wrapped to the panel, at the rows' inset from both edges (the header's text x - the old
    // opscreen rect's 10 px put the text against the panel's edge); a line that wraps takes the rows it needs
    const int inset = PanelStyle::RowInset + 8;
    const int x = panel.x + inset;
    const int width = panel.w - 2 * inset;
    const ableem::Color color = TextRenderer::toColor(app.theme().classic().textColor, 255);
    // the lines from `firstLine` down, each wrapped, until the content rect is full
    const ableem::Rect content = gui->classicContent();
    const int bottom = content.y + content.h - 4;
    rowsThatFit = max(1, (bottom - yoffset) / lineHeight);
    firstLine = max(0, min(firstLine, static_cast<int>(lines.size()) - 1));
    // a numbered item hangs (splitItem): its marker at the indent, every wrapped row of its text under the
    // text's first letter, not back at the panel's edge
    struct Layout {
        string marker, text;
        int textX = 0;
    };
    auto layout = [&](const string &line) {
        const Item item = splitItem(line);
        Layout l;
        l.marker = item.marker;
        l.text = item.text;
        l.textX = (item.indent > 0 ? gui->text().textWidth(font, string(item.indent, ' ')) : 0) +
                  (l.marker.empty() ? 0 : gui->text().textWidth(font, l.marker));
        return l;
    };
    int y = yoffset;
    size_t i = firstLine;
    for (; i < lines.size(); i++) {
        const string &line = lines[i];
        const Layout l = layout(line);
        const int height = (line.empty() || centred)
                               ? lineHeight
                               : max(lineHeight, gui->text().wrappedHeight(font, l.text, width - l.textX));
        if (y + height > bottom)
            break;
        if (line.empty() || centred) {
            gui->text().renderTextLine(line, -y, 0, centred ? XALIGN_CENTER : XALIGN_LEFT);
        } else {
            if (!l.marker.empty())
                gui->text().renderText_WithColor(font, l.marker, x + l.textX - gui->text().textWidth(font, l.marker), y,
                                                 color, XALIGN_LEFT);
            gui->text().renderWrappedText(font, l.text, x + l.textX, y, width - l.textX, color);
        }
        y += height;
    }
    lastLineShown = static_cast<int>(i); // one past the last drawn
    gui->renderScrollMarkers(firstLine > 0, lastLineShown < static_cast<int>(lines.size()));
    string status = "|@O| " + _("Back");
    if (firstLine > 0 || lastLineShown < static_cast<int>(lines.size()))
        status = "|@L2|/|@R2| " + _("Page") + "   " + status;
    gui->renderStatus(status);
    renderer.present();
}

//*******************************
// GuiTextPage::loop
//*******************************
void GuiTextPage::loop() {
    menuVisible = true;
    gui->input().setFrameNeed(ableem::Input::FrameNeed::Idle); // nothing moves between presses
    while (menuVisible) {
        if (gui->input().frameDue())
            render();
        Event e;
        while (gui->input().poll(e)) {
            if (e.type == Event::Type::Quit)
                menuVisible = false;
            if ((e.type == Event::Type::ButtonDown && e.button == Button::Circle) ||
                (e.type == Event::Type::KeyDown && e.key == Key::Escape)) {
                app.audio().cancel.play();
                menuVisible = false;
            }
            // scrolling: a line with the d-pad, a page with L1/R1 (or the keyboard's Page Up/Down)
            const int last = static_cast<int>(lines.size());
            const bool more = lastLineShown < last;
            int move = 0;
            if (e.type == Event::Type::DpadDown) {
                if (gui->input().dpadDown())
                    move = 1;
                else if (gui->input().dpadUp())
                    move = -1;
            } else if (e.type == Event::Type::ButtonDown) {
                if (e.button == Button::R2)
                    move = rowsThatFit;
                else if (e.button == Button::L2)
                    move = -rowsThatFit;
            } else if (e.type == Event::Type::KeyDown) {
                if (e.key == Key::Down)
                    move = 1;
                else if (e.key == Key::Up)
                    move = -1;
                else if (e.key == Key::PageDown)
                    move = rowsThatFit;
                else if (e.key == Key::PageUp)
                    move = -rowsThatFit;
            }
            if (move > 0 && more) {
                app.audio().cursor.play();
                firstLine = min(firstLine + move, last - 1);
            } else if (move < 0 && firstLine > 0) {
                app.audio().cursor.play();
                firstLine = max(0, firstLine + move);
            }
        }
    }
}
