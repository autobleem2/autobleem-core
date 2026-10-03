// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::FactsPage (G3i of docs/ab-gui-plan.md): a read-only page of facts in the classic panel - sections with a
// heading band each and label/value rows under it, the values in a column a third of the way across (a long one
// elided), as many rows as the panel holds, scrolling a row at a time with edge markers, re-read every
// `refreshInterval` while the page is up. Up/Down a row, Left/Right and L2/R2 a page, L1/R1 the first and the last
// row, Circle back. A page of its own adds buttons through onButton() and their hints through extraHints().
//
// The subclass supplies title() and collect() (the sections, fresh); extraHints() is "|@S| Save logs   |@T| ..." - the
// page's own hints, before Back and the page counter; onButton() takes a button of the page's own (true when taken -
// Circle is the page's, when not taken there) and the rows are re-read after it, the sub-screen it may have shown could
// have changed the facts.
//
// The events go through Screen::handle(): the pad buttons through the Context's ActionMap (PrevTab/NextTab the first
// and the last row, PageUp/PageDown a page, Back closes), the d-pad by its live state (up before down before left
// before right, as it always was); a key of the keyboard does nothing here unless it reaches the page as a pad button.
//
#pragma once

#include <ab_gui/screen.h>

#include <ableem/ui/font.h>
#include <ableem/ui/types.h>

#include <functional>
#include <string>
#include <vector>

namespace abgui {

// what collect() gives: sections of label/value rows
struct FactsRow {
    std::string label;
    std::string value;
};

struct FactsSection {
    std::string title;
    std::vector<FactsRow> rows;
};

// `text` if it fits `width` as measure() sees it, else cut (whole UTF-8 characters) to what fits with "..." - the
// pure form of the classic renderer's elide()
std::string elideText(const std::string &text, int width, const std::function<int(const std::string &)> &measure);

//********************
// FactsPage
//********************
class FactsPage : public Screen {
public:
    using Screen::Screen;

    // one drawn line: a section heading, or a label and its value
    struct Line {
        bool heading = false;
        std::string label;
        std::string value;
    };

    unsigned int refreshInterval = 1000; // ms between re-reads of the sections
    ableem::Font font;                   // the rows' font; invalid: the Context's Classic

    //*******************************
    // the pure parts
    //*******************************
    // the lines of the sections: a heading (the section's title) and its rows, section after section
    static std::vector<Line> linesOf(const std::vector<FactsSection> &sections);
    // the last top row that still fills a page of `rowsThatFit` rows
    static int maxFirstVisible(int count, int rowsThatFit);
    // the top row after scrolling `rows` (clamped to the list)
    static int scrolled(int firstVisible, int rows, int count, int rowsThatFit);
    // whether the sections are due to be re-read (the clock may wrap: the difference is unsigned)
    static bool refreshDue(unsigned int now, unsigned int lastRefresh, unsigned int interval);
    // "Page 2/3": the page of the top row, of how many
    struct Counter {
        int page = 1;
        int pages = 1;
    };
    static Counter counter(int firstVisible, int count, int rowsThatFit);
    // where the values go on a panel: `offset` from its left edge (the column), `right` the x their right edge is
    // at (a little short of the scroll markers, which sit at the panel's edge minus the row inset), `width` what a
    // value may take
    struct ValueColumn {
        int offset = 0;
        int right = 0;
        int width = 0;
    };
    static ValueColumn valueColumn(const ableem::Rect &panel, int rowInset);

    //*******************************
    // the page
    //*******************************
    void draw() override;
    // the old page's loop: the rows re-read when due, a frame when due, then each event through handle()
    void loop() override;
    void onAction(const ActionEvent &action) override;
    void onUnmapped(const ableem::Event &event) override;

    // the page opened: from the top, the rows read
    void open();
    // rebuild the rows from collect()
    void refresh();
    // the state, for a caller that keeps it between pages (GuiFactsPage): the rows, the top row, the rows a page
    // holds, the time of the last read
    const std::vector<Line> &lines() const { return lines_; }
    int firstVisible() const { return firstVisible_; }
    int rowsThatFit() const { return rowsThatFit_; }
    unsigned int lastRefresh() const { return lastRefresh_; }
    void restore(const std::vector<Line> &lines, int firstVisible, int rowsThatFit, unsigned int lastRefresh);

protected:
    virtual std::string title() = 0;
    virtual std::vector<FactsSection> collect() = 0; // the sections, fresh
    virtual std::string extraHints() { return ""; }
    virtual bool onButton(ableem::Button /*button*/) { return false; }

    // scroll by `rows`: the Cursor sound when the page moved, Cancel when it could not
    void scrollBy(int rows);
    // one event of the pad, `action` its action (None: unmapped)
    void padEvent(const ableem::Event &event, Action action);

    std::vector<Line> lines_;
    int firstVisible_ = 0; // index into lines_ of the top row on the screen
    int rowsThatFit_ = 1;  // rows the panel holds at the font's height, from the last draw()
    unsigned int lastRefresh_ = 0;
};

} // namespace abgui
