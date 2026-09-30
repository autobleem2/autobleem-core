// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::TextPage (G3h of docs/ab-gui-plan.md): a titled page of static text in the classic panel - what a tool's
// instructions, a licence or an info box are - and the first widget on abgui::Screen, the pattern for the rest.
// A page longer than the panel scrolls: Up/Down a line, L2/R2 (or the keyboard's Page Up/Down) a page. Back (Circle,
// Escape) closes it. The caller sets `title` and `lines`, and runs the screen (show() or loop()).
//
// Layout: each line is wrapped to the panel at the rows' inset from both edges; a line that wraps takes the rows it
// needs. A numbered item ("1. text") hangs: its marker at the line's indent, every wrapped row of the text under the
// text's first letter. A blank line, or every line when `centred`, is one row drawn through the Context's line drawer
// (in the font's own colour, centred on the canvas when `centred`).
//
// The events go through Screen::handle(): the pad buttons through the Context's ActionMap (Back closes, PageUp and
// PageDown page), the d-pad by its live state (down before up, as it always was), the keys as keys (Escape, the arrows,
// Page Up/Down - whatever action they are bound to).
//
#pragma once

#include <ab_gui/screen.h>
#include <ab_gui/style.h>

#include <functional>
#include <string>
#include <vector>

namespace abgui {

// `text` split into rows no wider than `width` (as measure() sees them): words at spaces and tabs, a word wider than
// the width cut into pieces that fit (whole UTF-8 characters); an empty text is one empty row. The pure form of the
// classic renderer's wrapLines().
std::vector<std::string> wrapText(const std::string &text, int width,
                                  const std::function<int(const std::string &)> &measure);

//********************
// TextPage
//********************
class TextPage : public Screen {
public:
    using Screen::Screen;

    std::string title;              // the header
    std::vector<std::string> lines; // one row each, left aligned, wrapped to the panel; "" is a blank row
    bool centred = false;           // centre every line instead
    OptionalColor color;            // the lines' colour; unset: the style's text colour

    // how a line is laid out: its leading spaces indent all of it, and a numbered item's marker ("1. ") hangs in
    // front of the text, whose wrapped rows start under the text's first letter
    struct Item {
        size_t indent = 0;  // leading spaces
        std::string marker; // "12. ", "" when the line is not a numbered item
        std::string text;   // the rest
    };
    static Item splitItem(const std::string &line);

    // whether scrolling `move` lines moves the page: down while more follows the last line shown (`lastShown` is one
    // past it), up while the first line is not the first (the Cursor sound plays exactly then)
    static bool canScroll(int firstLine, int move, int lastShown, int count);
    // the first line after scrolling `move` lines (a page is `rowsThatFit`); unchanged when it cannot scroll
    static int scrolled(int firstLine, int move, int lastShown, int count);

    void draw() override;
    // the old page's loop: a frame when due, then the events through handle(); the page rests between presses
    void loop() override;
    void onAction(const ActionEvent &action) override;
    void onUnmapped(const ableem::Event &event) override;

    int firstLine() const { return firstLine_; }
    int rowsThatFit() const { return rowsThatFit_; }

protected:
    // scroll by `move` lines, with the cursor sound when the page moved
    void scrollBy(int move);
    void keyDown(ableem::Key key);
    void close(); // the Cancel sound and out

    int firstLine_ = 0;     // the first line shown
    int lastLineShown_ = 0; // one past the last line draw() fitted in
    int rowsThatFit_ = 1;   // rows of the font in the content rect - a page
};

} // namespace abgui
