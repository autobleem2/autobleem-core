// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::TextPage: static text in the classic panel. See the header.
//
#include <ab_gui/text_page.h>

#include <ab_gui/panel.h>

#include <algorithm>
#include <cctype>

using namespace std;
using ableem::Event;
using ableem::Key;

namespace abgui {

//*******************************
// wrapText
//*******************************
vector<string> wrapText(const string &text, int width, const function<int(const string &)> &measure) {
    vector<string> rows;
    // the pieces a row may end after (Font::lineBreaks: Unicode line breaking - after a space, and between two
    // Chinese characters); the blanks after a piece are one space between it and the next, a tab is a space.
    // A piece wider than the column is cut into pieces that fit.
    struct Piece {
        string text;
        bool spaceAfter;
    };
    vector<Piece> pieces;
    auto addPiece = [&](string word, bool spaceAfter) {
        while (width > 0 && measure(word) > width && word.size() > 1) {
            size_t cut = word.size();
            do {
                cut--;
                while (cut > 0 && (static_cast<unsigned char>(word[cut]) & 0xC0) == 0x80)
                    cut--; // a whole UTF-8 char
            } while (cut > 1 && measure(word.substr(0, cut)) > width);
            if (cut == 0)
                break;
            pieces.push_back({word.substr(0, cut), false});
            word = word.substr(cut);
        }
        pieces.push_back({word, spaceAfter});
    };
    vector<size_t> ends = ableem::Font::lineBreaks(text);
    ends.push_back(text.size());
    size_t start = 0;
    for (size_t end : ends) {
        string piece = text.substr(start, end - start);
        start = end;
        const size_t ink = piece.find_last_not_of(" \t");
        const bool spaceAfter = ink == string::npos || ink + 1 < piece.size();
        piece = ink == string::npos ? "" : piece.substr(0, ink + 1);
        const size_t lead = piece.find_first_not_of(" \t"); // blanks at the very start of the text
        if (!piece.empty() && lead != 0)
            piece = piece.substr(lead);
        if (!piece.empty())
            addPiece(piece, spaceAfter);
        else if (!pieces.empty())
            pieces.back().spaceAfter = true;
    }

    string row;
    bool spaceBefore = false;
    for (const Piece &p : pieces) {
        string candidate = row.empty() ? p.text : row + (spaceBefore ? " " : "") + p.text;
        if (!row.empty() && measure(candidate) > width) {
            rows.push_back(row);
            row = p.text;
        } else {
            row = candidate;
        }
        spaceBefore = p.spaceAfter;
    }
    if (!row.empty() || rows.empty())
        rows.push_back(row);
    return rows;
}

//*******************************
// TextPage::splitItem
//*******************************
TextPage::Item TextPage::splitItem(const string &line) {
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
// TextPage::canScroll / scrolled
//*******************************
bool TextPage::canScroll(int firstLine, int move, int lastShown, int count) {
    return (move > 0 && lastShown < count) || (move < 0 && firstLine > 0);
}

int TextPage::scrolled(int firstLine, int move, int lastShown, int count) {
    if (!canScroll(firstLine, move, lastShown, count))
        return firstLine;
    return move > 0 ? min(firstLine + move, count - 1) : max(0, firstLine + move);
}

//*******************************
// TextPage::draw
//*******************************
void TextPage::draw() {
    ctx.drawBackdrop();
    const Panel panel = Panel::full(ctx);
    panel.sheet(ctx);
    const ableem::Font &font = ctx.font(FontRole::Classic);
    const int lineHeight = font.lineHeight();
    const int yoffset = panel.header(ctx, title);
    // each line wrapped to the panel, at the rows' inset from both edges (the header's text x - the old
    // opscreen rect's 10 px put the text against the panel's edge); a line that wraps takes the rows it needs
    const int inset = panel.style().rowInset + 8;
    const int x = panel.rect().x + inset;
    const int width = panel.rect().w - 2 * inset;
    const ableem::Color textColor = color.set ? color.color : panel.style().text;
    // the lines from `firstLine_` down, each wrapped, until the content rect is full
    const ableem::Rect content = panel.content();
    const int bottom = content.y + content.h - 4;
    rowsThatFit_ = max(1, (bottom - yoffset) / lineHeight);
    firstLine_ = max(0, min(firstLine_, static_cast<int>(lines.size()) - 1));
    auto measure = [&](const string &s) { return font.width(s); };
    int y = yoffset;
    size_t i = firstLine_;
    for (; i < lines.size(); i++) {
        const string &line = lines[i];
        // a numbered item hangs (splitItem): its marker at the indent, every wrapped row of its text under the
        // text's first letter, not back at the panel's edge
        const Item item = splitItem(line);
        const int markerWidth = item.marker.empty() ? 0 : ctx.textWidth(font, item.marker);
        const int textX = (item.indent > 0 ? ctx.textWidth(font, string(item.indent, ' ')) : 0) + markerWidth;
        const bool plain = line.empty() || centred;
        vector<string> rows;
        if (!plain)
            rows = wrapText(item.text, width - textX, measure);
        const int height = plain ? lineHeight : max(lineHeight, static_cast<int>(rows.size()) * lineHeight);
        if (y + height > bottom)
            break;
        if (plain) {
            ctx.drawLine(font, line, x, y, centred ? Context::LineAlign::Centre : Context::LineAlign::Left);
        } else {
            if (!item.marker.empty())
                ctx.drawText(font, item.marker, x + textX - markerWidth, y, textColor);
            int rowY = y;
            for (const string &row : rows) {
                if (!row.empty())
                    ctx.drawText(font, row, x + textX, rowY, textColor);
                rowY += lineHeight;
            }
        }
        y += height;
    }
    lastLineShown_ = static_cast<int>(i); // one past the last drawn
    panel.scrollMarkers(ctx, firstLine_ > 0, lastLineShown_ < static_cast<int>(lines.size()));
    string status = "|@O| " + ctx.translate("Back");
    if (firstLine_ > 0 || lastLineShown_ < static_cast<int>(lines.size()))
        status = "|@L2/R2| " + ctx.translate("Page") + "   " + status;
    panel.footer(ctx, status);
}

//*******************************
// TextPage::loop
//*******************************
void TextPage::loop() {
    menuVisible = true;
    gui.input().setFrameNeed(ableem::Input::FrameNeed::Idle); // nothing moves between presses
    while (menuVisible) {
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
// TextPage::onAction / onUnmapped
//*******************************
// the pad: Back closes, PageUp/PageDown page; the d-pad by its live state; the keys as keys
void TextPage::onAction(const ActionEvent &action) {
    const Event &e = action.event;
    switch (e.type) {
    case Event::Type::DpadDown:
        if (gui.input().dpadDown())
            scrollBy(1);
        else if (gui.input().dpadUp())
            scrollBy(-1);
        break;
    case Event::Type::ButtonDown:
        if (action.action == Action::Back)
            close();
        else if (action.action == Action::PageDown)
            scrollBy(rowsThatFit_);
        else if (action.action == Action::PageUp)
            scrollBy(-rowsThatFit_);
        break;
    case Event::Type::KeyDown:
        keyDown(e.key);
        break;
    default:
        break;
    }
}

void TextPage::onUnmapped(const Event &event) {
    if (event.type == Event::Type::KeyDown)
        keyDown(event.key);
}

void TextPage::keyDown(Key key) {
    switch (key) {
    case Key::Escape:
        close();
        break;
    case Key::Down:
        scrollBy(1);
        break;
    case Key::Up:
        scrollBy(-1);
        break;
    case Key::PageDown:
        scrollBy(rowsThatFit_);
        break;
    case Key::PageUp:
        scrollBy(-rowsThatFit_);
        break;
    default:
        break;
    }
}

void TextPage::close() {
    ctx.play(UiSound::Cancel);
    menuVisible = false;
}

void TextPage::scrollBy(int move) {
    const int count = static_cast<int>(lines.size());
    if (!canScroll(firstLine_, move, lastLineShown_, count))
        return;
    ctx.play(UiSound::Cursor);
    firstLine_ = scrolled(firstLine_, move, lastLineShown_, count);
}

} // namespace abgui
