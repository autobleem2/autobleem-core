#pragma once

#include "../gui_screen.h"
#include "../gui.h"
#include "../hold_repeat.h"
#include <ab_gui/list.h>
#include <ab_gui/list_model.h>
#include <ableem/ui/debug_driver.h>
#include <algorithm>
#include <typeinfo>
#include <vector>
#include <string>

//*******************************
// driverRowName
//*******************************
// a row's name for the DebugDriver's `items`/`selected` (the text as displayed, translated): a line type that
// has a text says it by an overload next to its definition (found by argument-dependent lookup - see
// GuiTwoColumnStringMenu, OptionsInfo); one without gives an empty name and its rows still count
inline std::string driverRowName(const std::string &line) {
    return line;
}
template <typename LineDataType> std::string driverRowName(const LineDataType & /*line*/) {
    return std::string();
}

template <typename LineDataType> class GuiMenuBaseList;

//*******************************
// GuiMenuBase template class
//*******************************
// The classic list. Its drawing, row geometry, input and DebugDriver publishing are ab_gui's abgui::List
// (docs/ab-gui-plan.md, G3m): every function below hands this menu's own members to a GuiMenuBaseList (a List
// working on them in place, its hooks this menu's virtuals) and forwards. The members stay what they were - the
// class is compiled into extensions (ABI 6), and the screens built on it read and set them directly.
template <typename LineDataType> class GuiMenuBase : public GuiScreen {
public:
    explicit GuiMenuBase(ableem::GuiBase &_gui) : GuiScreen(_gui) {}

    void init() override;
    // the frame through Gui's screen stack (clear, draw(), present); draw() is what the list puts on the canvas
    void render() override;
    void draw();

    virtual std::string getTitle();
    virtual std::string getStatusLine(); // returns the status line at the bottom.  cross, circle, etc icons.

    bool firstRender = true;
    virtual void renderLineIndexOnRow(int /*index*/, int /*row*/) {
    } // you must inherit from GuiMenuBase and provide this
    void renderLines();
    void renderSelectionBox();

    // controller dpad/joystick pressed
    void doJoyDown() override; // move down one line, may fast forwward
    void doJoyUp() override;   // move up one line, may fast forwward
    void holdRows(int step);   // a step per row while the key is held, at HoldRepeat's pace

    // controller button pressed
    void doCircle_Pressed() override; // default = leave menu.  cancel = true.
    void doCross_Pressed() override;  // default = leave menu.  cancel = false.

    // horizontal lists of choices like the options menu will probably override these virtuals
    void doL1_Pressed() override { doHome(); }     // the first row
    void doR1_Pressed() override { doEnd(); }      // the last row
    void doL2_Pressed() override { doPageUp(); }   // a page up - L2/R2 page on every list
    void doR2_Pressed() override { doPageDown(); } // a page down

    // keyboard
    void doKeyDown() override;                       // move down one line
    void doKeyUp() override;                         // move up one line
    void doEnter() override { doCross_Pressed(); }   // default = doCross
    void doEscape() override { doCircle_Pressed(); } // default = doCircle
    void doPageDown() override;
    void doPageUp() override;
    void doHome() override;
    void doEnd() override;

    ableem::Font font;
    bool useSmallerFont = false; // useful for 2 column menu with long strings

    // plain menu
    std::vector<LineDataType> lines; // these are the menu lines
    virtual int getVerticalSize() { return lines.size(); }

    int selected = 0;                 // the current selected index
    int maxVisible = 8;               // the rows that fit the panel's content at the font's height (init())
    int selectionBoxXOffset = 0;      // a menu whose rows start to the right of something sets this
    int selectionRightEdge = 0;       // ...and one whose rows stop before something (a pane on the right) this
    static const int CompactRows = 8; // up to this many rows the list draws as a compact centred panel
    int firstVisibleIndex = 0;        // current visible range on page
    int lastVisibleIndex = 7;         // current visible range on page
    int firstRow = 0;                 // the first row of the menu item lines, right under the header
    int yoffset = 0;                  // y offset for the line (y=fontHeight*line + yoffset).  set by renderLogo()

    // this is useful in menus that have blank lines like gui_networkMenu.cpp
    virtual bool skipSelectingThisLineWhenMovingByOne(int index) { return false; }
    // the lines are only information (nothing to pick - e.g. "no adapter found"): no selection box, and the
    // cursor keys, paging and L1/R1 do not move anything
    bool labelsOnly = false;

    void adjustPageBy(int moveBy);   // move the page up or down by an amount
    void computePagePosition();      // complete recompute of positions based on the selected value
    void landOnSelectable(int step); // off a heading: on in step's direction, else back; the page follows
    void publishToDriver();          // the rows and the cursor for the DebugDriver (renderLines() calls it)

    // the selection and paging rules are abgui::ListModel's, working in place on the members above
    abgui::ListModel::View modelView() {
        return {selected, firstVisibleIndex, lastVisibleIndex, maxVisible, static_cast<int>(getVerticalSize())};
    }
    auto skipper() {
        return [this](int index) { return skipSelectingThisLineWhenMovingByOne(index); };
    }

    bool changes = false;
    bool cancelled = false;
};

//*******************************
// GuiMenuBaseList
//*******************************
// an abgui::List over a GuiMenuBase's own members (in place), whose hooks are the menu's virtuals: the rows are
// renderLineIndexOnRow (in the theme's row / selected row colour - TextRenderer's row role), the title and footer
// getTitle()/getStatusLine(), the headings skipSelectingThisLineWhenMovingByOne, a held row's step doKeyDown/
// doKeyUp and its frame render() - so a screen overriding any of them keeps doing so
template <typename LineDataType> class GuiMenuBaseList : public abgui::List {
public:
    explicit GuiMenuBaseList(GuiMenuBase<LineDataType> &menu)
        : abgui::List(*menu.gui, menu.gui->uiContext(),
                      abgui::List::Refs{menu.selected, menu.firstVisibleIndex, menu.lastVisibleIndex, menu.maxVisible,
                                        menu.firstRow, menu.yoffset, menu.selectionBoxXOffset, menu.selectionRightEdge,
                                        menu.firstRender, menu.labelsOnly, menu.cancelled, menu.font,
                                        menu.menuVisible}),
          menu_(menu) {}

    int size() override { return menu_.getVerticalSize(); }
    bool isEmpty() override { return menu_.lines.empty(); }
    bool skip(int index) override { return menu_.skipSelectingThisLineWhenMovingByOne(index); }
    std::string titleText() override { return menu_.getTitle(); }
    std::string statusText() override { return menu_.getStatusLine(); }
    void drawRow(int index, int line, bool isSelected) override {
        // the row's colour role (UIREV-29): renderTextLine/renderRowValue take it from here
        TextRenderer::RowRoleScope role(menu_.gui->text(),
                                        isSelected ? TextRenderer::RowRole::Selected : TextRenderer::RowRole::Row);
        menu_.renderLineIndexOnRow(index, line);
    }
    std::string rowName(int index) override {
        return index < static_cast<int>(menu_.lines.size()) ? driverRowName(menu_.lines[index]) : std::string();
    }
    const char *screenName() override { return typeid(menu_).name(); }
    void step(int by) override { by > 0 ? menu_.doKeyDown() : menu_.doKeyUp(); }
    void redraw() override { menu_.render(); }

private:
    GuiMenuBase<LineDataType> &menu_;
};

//*******************************
// void GuiMenuBase<LineDataType>::init()
//*******************************
template <typename LineDataType> void GuiMenuBase<LineDataType>::init() {
    font = gui->assets().themeFont;
    if (useSmallerFont) {
        // sometimes the left column will overwrite into the right column.
        // and the second column sometimes go off the right side.
        font = gui->assets().themeFonts[FONT_15_BOLD]; // use a smaller font
    }
    // the frame need at rest; the rows that fit the panel at the font's height (abgui::List::init)
    GuiMenuBaseList<LineDataType>(*this).init();
}

//*******************************
// GuiMenuBase<T>::adjustPageBy
//*******************************
// move the page up or down by an amount
template <typename LineDataType> void GuiMenuBase<LineDataType>::adjustPageBy(int moveBy) {
    abgui::ListModel::adjustPageBy(modelView(), moveBy);
}

//*******************************
// GuiMenuBase<T>::landOnSelectable
//*******************************
// the cursor never rests on a row skipSelectingThisLineWhenMovingByOne() marks (a heading): from `selected` it
// walks on in step's direction (+1/-1), back the other way when that runs off the list, and the page scrolls
// just enough to show it; with no selectable row at all it stays put
template <typename LineDataType> void GuiMenuBase<LineDataType>::landOnSelectable(int step) {
    abgui::ListModel::landOnSelectable(modelView(), step, skipper());
}

//*******************************
// GuiMenuBase<LineDataType>::computePagePosition
//*******************************
// complete recompute of positions based on the selected value
template <typename LineDataType> void GuiMenuBase<LineDataType>::computePagePosition() {
    abgui::ListModel::computePagePosition(modelView());
}

//*******************************
// GuiMenuBase<LineDataType>::publishToDriver
//*******************************
// the DebugDriver's `items` and `selected` (abgui::List::publish): every row by the text it shows (a heading with
// a leading '#'), the cursor's row; skipped once the menu is closing. Called from renderLines(), so it follows the
// lines however a subclass fills them; Options calls it from its own render().
template <typename LineDataType> void GuiMenuBase<LineDataType>::publishToDriver() {
    GuiMenuBaseList<LineDataType>(*this).publish();
}

//*******************************
// GuiMenuBase<LineDataType>::renderLines
//*******************************
// the DebugDriver's rows, then each row on the page through renderLineIndexOnRow (abgui::List::drawRows) - here,
// not only in draw(): Game Manager draws with its own render()
template <typename LineDataType> void GuiMenuBase<LineDataType>::renderLines() {
    GuiMenuBaseList<LineDataType>(*this).drawRows();
}

//*******************************
// GuiMenuBase<LineDataType>::renderSelectionBox
//*******************************
template <typename LineDataType> void GuiMenuBase<LineDataType>::renderSelectionBox() {
    GuiMenuBaseList<LineDataType>(*this).drawSelection();
}

//*******************************
// GuiMenuBase<LineDataType>::render
//*******************************
template <typename LineDataType> void GuiMenuBase<LineDataType>::render() {
    // the stack clears and presents (docs/ab-gui-plan.md, G3c); the list only draws
    gui->uiContext().stack().frame([this]() { draw(); });
}

//*******************************
// GuiMenuBase<LineDataType>::draw
//*******************************
// the backdrop, the panel (compact for a short list with nothing beside it), the header, the rows, the band, the
// scroll markers, the footer (abgui::List::draw)
template <typename LineDataType> void GuiMenuBase<LineDataType>::draw() {
    GuiMenuBaseList<LineDataType>(*this).draw();
}

//*******************************
// GuiMenuBase<LineDataType>::getTitle
//*******************************
template <typename LineDataType> std::string GuiMenuBase<LineDataType>::getTitle() {
    return "****** MISSING TITLE ******";
}

//*******************************
// GuiMenuBase<LineDataType>::getStatusLine
//*******************************
// the default status line for menus.  override if needed.
template <typename LineDataType> std::string GuiMenuBase<LineDataType>::getStatusLine() {
    return _("Entry") + " " + to_string(selected + 1) + "/" + to_string(getVerticalSize()) + "    |@L1|/|@R1| " +
           _("First/last") + "   |@L2|/|@R2| " + _("Page") + "   |@X| " + _("Select") + "   |@O| " + _("Back") + " |";
}

//*******************************
// the moves: abgui::List's (the sound, then ListModel)
//*******************************
template <typename LineDataType> void GuiMenuBase<LineDataType>::doKeyDown() {
    GuiMenuBaseList<LineDataType>(*this).stepDown();
}

template <typename LineDataType> void GuiMenuBase<LineDataType>::doKeyUp() {
    GuiMenuBaseList<LineDataType>(*this).stepUp();
}

template <typename LineDataType> void GuiMenuBase<LineDataType>::doJoyDown() {
    holdRows(1);
}

template <typename LineDataType> void GuiMenuBase<LineDataType>::doJoyUp() {
    holdRows(-1);
}

// one step at the press, then - while nothing else comes from the pad or the keyboard - the same step again at
// HoldRepeat's pace: doKeyDown()/doKeyUp() and render() each time, so a screen's own step and frame run
template <typename LineDataType> void GuiMenuBase<LineDataType>::holdRows(int step) {
    GuiMenuBaseList<LineDataType>(*this).holdRows(step);
}

template <typename LineDataType> void GuiMenuBase<LineDataType>::doPageDown() {
    GuiMenuBaseList<LineDataType>(*this).pageDown();
}

template <typename LineDataType> void GuiMenuBase<LineDataType>::doPageUp() {
    GuiMenuBaseList<LineDataType>(*this).pageUp();
}

template <typename LineDataType> void GuiMenuBase<LineDataType>::doHome() {
    GuiMenuBaseList<LineDataType>(*this).first();
}

template <typename LineDataType> void GuiMenuBase<LineDataType>::doEnd() {
    GuiMenuBaseList<LineDataType>(*this).last();
}

// Circle: the Cancel sound, left cancelled
template <typename LineDataType> void GuiMenuBase<LineDataType>::doCircle_Pressed() {
    GuiMenuBaseList<LineDataType>(*this).back();
}

// Cross: the Cursor sound, left (unless there are no lines)
template <typename LineDataType> void GuiMenuBase<LineDataType>::doCross_Pressed() {
    GuiMenuBaseList<LineDataType>(*this).confirm();
}
