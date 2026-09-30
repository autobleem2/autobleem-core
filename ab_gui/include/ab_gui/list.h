// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::List (G3m part 2 of docs/ab-gui-plan.md): the classic list - Options, the editors, Game Manager, Memory Cards,
// playlists, a tool's menus. A full panel (or, for a short list with nothing beside it, a compact one centred on the
// screen), the title, as many rows of the list's font as fit one under the other, the selected row's band, the scroll
// markers when rows are hidden above or below, the footer. The cursor and the page are abgui::ListModel's.
//
// A row is the list's own `rows` (a label, a value right-aligned at the row's right edge - a switch's being the text
// ON/OFF - a heading the cursor skips, a disabled row under the veil), or whatever a subclass draws for an index
// (drawRow(); the classic GuiMenuBase's rows are its renderLineIndexOnRow). The row geometry is pure and static here -
// where a row's text starts, where its value ends, the band a selected/heading/disabled row gets - and the program's
// own row drawing (AutoBleem's TextRenderer) takes its numbers from it.
//
// The list's numbers (the cursor, the page, the rows' y, the font, ...) are references: to the list's own (the first
// constructor), or to a caller's members, worked on in place (the second) - the classic GuiMenuBase is header-only and
// compiled into extensions, and the screens built on it read and set its members, so it keeps them and hands them in.
//
// The events go through Screen::handle(): the d-pad by its live state (up before down), a row per step at the shared
// HoldRepeat pace while held; L1/R1 (PrevTab/NextTab) the first/last row, L2/R2 (PageUp/PageDown) a page; Confirm
// closes, Back closes cancelled; keys as keys (the arrows a row, Page Up/Down, Home/End, Enter, Esc). The sounds are
// the classic list's: Cursor on a row, HomeUp/HomeDown on a page and on the first/last row, Cancel on Back.
//
#pragma once

#include <ab_gui/context.h>
#include <ab_gui/list_model.h>
#include <ab_gui/panel.h>
#include <ab_gui/screen.h>

#include <ableem/ui/font.h>
#include <ableem/ui/types.h>

#include <string>
#include <vector>

namespace abgui {

//********************
// List
//********************
class List : public Screen {
private:
    // the list's own numbers (first, so the references below can be bound to them)
    struct Own {
        int selected = 0;
        int firstVisible = 0;
        int lastVisible = 7;
        int maxVisible = 8;
        int firstRow = 0;
        int yoffset = 0;
        int selectionXOffset = 0;
        int selectionRightEdge = 0;
        bool firstRender = true;
        bool labelsOnly = false;
        bool cancelled = false;
        ableem::Font font;
    } own_;

public:
    // up to this many rows the list draws as a compact centred panel (when nothing sits beside it)
    static constexpr int CompactRows = 8;

    // a caller's numbers, which the list then works on in place
    struct Refs {
        int &selected;
        int &firstVisible;
        int &lastVisible;
        int &maxVisible;
        int &firstRow;
        int &yoffset;
        int &selectionXOffset;
        int &selectionRightEdge;
        bool &firstRender;
        bool &labelsOnly;
        bool &cancelled;
        ableem::Font &font;
        bool &visible; // the screen's menuVisible
    };

    struct Row {
        std::string label;
        std::string value;     // right-aligned at the row's right edge; empty = none
        bool heading = false;  // a band with the label in the heading colour; the cursor skips it
        bool disabled = false; // under the disabled veil, still selectable (the cursor passes it)
    };

    // a list with its own numbers
    List(ableem::GuiBase &gui, Context &context);
    // a list over a caller's numbers
    List(ableem::GuiBase &gui, Context &context, const Refs &refs);
    List(const List &) = delete;
    List &operator=(const List &) = delete;

    // the numbers (see the top of this file)
    int &selected;           // the cursor's row
    int &firstVisible;       // the page: the first row shown...
    int &lastVisible;        // ...and the last
    int &maxVisible;         // the rows that fit the panel's content at the font's height (init())
    int &firstRow;           // the first row's line under the header
    int &yoffset;            // the y of line 0: the header's end (draw() sets it)
    int &selectionXOffset;   // a list whose rows start to the right of something moves its band's left edge
    int &selectionRightEdge; // ...and one whose rows stop before something (a pane on the right) its right edge
    bool &firstRender;       // the page is computed from the cursor at the first draw
    bool &labelsOnly;        // nothing to pick: no band, the moves do nothing
    bool &cancelled;         // left with Back
    ableem::Font &font;      // the rows' font; invalid = the Context's Classic font
    bool &visible;           // false once the list closes (Confirm or Back)

    // the list's own rows (a subclass drawing its own leaves them empty and overrides size()/drawRow())
    std::vector<Row> rows;
    std::string title;
    std::string status; // the footer; empty = entryStatus()

    //*******************************
    // the row geometry (pure)
    //*******************************
    // a row's top: `line` rows of `lineHeight` under `yoffset`, or the absolute y -line when line < 0
    static int rowTop(int line, int yoffset, int lineHeight);
    // where a row's text starts: level with the header's title, `xoffset` further in
    static int textLeft(const ableem::Rect &panel, const Style &style, int xoffset = 0);
    // where a row's right-aligned value ends: `rightEdge` when given (a pane on the right), else the panel's right
    // edge less the same inset as the text's
    static int valueRight(const ableem::Rect &panel, const Style &style, int rightEdge = 0);
    // the band of a selected, heading or disabled row: from the panel's inner left edge (`xoffset` further in) to its
    // inner right edge, or to 12 px past `rightEdge` when given; `top` and `height` the row's
    static ableem::Rect band(const ableem::Rect &panel, int top, int height, int xoffset = 0, int rightEdge = 0);
    // a switch row ("Label|@Check|" / "Label|@Uncheck|"): 1 on, 0 off, -1 not a switch; `label` (when given) gets the
    // text before the marker for a switch, the whole text otherwise
    static int switchState(const std::string &text, std::string *label = nullptr);
    // whether a list of `size` rows draws in a compact panel
    static bool isCompact(int size, int selectionRightEdge);

    //*******************************
    // what the list draws and publishes
    //*******************************
    // how many rows; the list's own `rows` unless a subclass says otherwise
    virtual int size();
    // no rows to confirm (Confirm does not close then); size() == 0 by default
    virtual bool isEmpty();
    // a heading the cursor never rests on
    virtual bool skip(int index);
    // the title and the footer line, asked for when they are drawn
    virtual std::string titleText();
    virtual std::string statusText();
    // one row, `line` lines under firstRow's... (see drawRows); `selected` = it is the cursor's
    virtual void drawRow(int index, int line, bool selected);
    // a row's name for the DebugDriver (the text as shown); its label by default
    virtual std::string rowName(int index);
    // the screen's name for the DebugDriver (typeid of the screen); this list's by default
    virtual const char *screenName();

    // "Entry 3/21   L1/R1 First/last   L2/R2 Page   X Select   O Back" - the classic list's default footer
    std::string entryStatus();

    // the rows that fit, from the current panel at the font's height; the page from firstVisible
    void layout();
    // layout(), with the frame need at rest (a list moves only on a press)
    void init() override;
    // the whole list: the backdrop, the panel (compact for a short list), the header, the rows, the band, the markers,
    // the footer
    void draw() override;
    // the rows on the page (the DebugDriver's items first), each through drawRow()
    void drawRows();
    // the cursor's band
    void drawSelection();
    // the DebugDriver's `items` and `selected` (publish()): every row by its name (a heading with a leading '#', a '|'
    // as
    // '/'), the cursor's row (-1 with labelsOnly or no rows)
    std::vector<std::string> driverItems();
    int driverSelected();
    // ... handed to the DebugDriver under screenName(); nothing once the list is closing or without a driver
    void publish();
    // the panel the rows are in now: the compact one while a compact list draws, else the full one
    Panel currentPanel() const;

    //*******************************
    // the moves (the sounds, then ListModel)
    //*******************************
    void stepDown(); // Cursor, a row down (wrapping)
    void stepUp();   // Cursor, a row up (wrapping)
    void pageDown(); // HomeUp, a page down
    void pageUp();   // HomeDown, a page up
    void first();    // HomeDown, the first row
    void last();     // HomeDown, the last row
    void confirm();  // Cursor; closes unless isEmpty()
    void back();     // Cancel; closes cancelled
    // a held d-pad: one step at once, then - while nothing else is pending - the same step at HoldRepeat's pace, a
    // frame after each (step() and redraw(), so a subclass's own step and frame run)
    void holdRows(int step);
    // one step for holdRows (+1 stepDown, else stepUp) and the frame after it (render())
    virtual void step(int step);
    virtual void redraw();

    // the model's view of the numbers, and the skip predicate
    ListModel::View view();

    void onAction(const ActionEvent &action) override;
    void onUnmapped(const ableem::Event &event) override;

protected:
    void dpad();                         // a d-pad event: a held row step by the live state, up first
    void key(ableem::Key key);           // a key that reached the list as a key
    const ableem::Font &rowFont() const; // font, else the Context's Classic font
};

} // namespace abgui
