#pragma once

#include "../gui_screen.h"
#include "../gui.h"
#include "../hold_repeat.h"
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

//*******************************
// GuiMenuBase template class
//*******************************
template <typename LineDataType> class GuiMenuBase : public GuiScreen {
public:
    explicit GuiMenuBase(ableem::GuiBase &_gui) : GuiScreen(_gui) {}

    void init() override;
    void render() override;

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

    bool changes = false;
    bool cancelled = false;
};

//*******************************
// void GuiMenuBase<LineDataType>::init()
//*******************************
template <typename LineDataType> void GuiMenuBase<LineDataType>::init() {
    gui->input().setFrameNeed(ableem::Input::FrameNeed::Idle); // a list: nothing moves between presses
    font = gui->assets().themeFont;
    if (useSmallerFont) {
        // sometimes the left column will overwrite into the right column.
        // and the second column sometimes go off the right side.
        font = gui->assets().themeFonts[FONT_15_BOLD]; // use a smaller font
    }
    // the rows pack at the font's height and scroll a row at a time when there are more than fit
    // (the theme's menuLines used to say how many; the panel decides now)
    maxVisible = gui->classicRowsThatFit(font);
    lastVisibleIndex = firstVisibleIndex + maxVisible - 1;
}

//*******************************
// GuiMenuBase<T>::adjustPageBy
//*******************************
// move the page up or down by an amount
template <typename LineDataType> void GuiMenuBase<LineDataType>::adjustPageBy(int moveBy) {
    selected += moveBy;
    firstVisibleIndex += moveBy;
    lastVisibleIndex += moveBy;
}

//*******************************
// GuiMenuBase<T>::landOnSelectable
//*******************************
// the cursor never rests on a row skipSelectingThisLineWhenMovingByOne() marks (a heading): from `selected` it
// walks on in step's direction (+1/-1), back the other way when that runs off the list, and the page scrolls
// just enough to show it; with no selectable row at all it stays put
template <typename LineDataType> void GuiMenuBase<LineDataType>::landOnSelectable(int step) {
    int size = getVerticalSize();
    int i = selected;
    while (i >= 0 && i < size && skipSelectingThisLineWhenMovingByOne(i))
        i += step;
    if (i < 0 || i >= size) {
        i = selected;
        while (i >= 0 && i < size && skipSelectingThisLineWhenMovingByOne(i))
            i -= step;
    }
    if (i < 0 || i >= size)
        return;
    selected = i;
    if (selected < firstVisibleIndex) {
        firstVisibleIndex = selected;
        lastVisibleIndex = selected + maxVisible - 1;
    } else if (selected > lastVisibleIndex) {
        lastVisibleIndex = selected;
        firstVisibleIndex = selected - maxVisible + 1;
    }
}

//*******************************
// GuiMenuBase<LineDataType>::computePagePosition
//*******************************
// complete recompute of positions based on the selected value
template <typename LineDataType> void GuiMenuBase<LineDataType>::computePagePosition() {
    if (getVerticalSize() == 0) {
        selected = 0;
        firstVisibleIndex = 0;
        lastVisibleIndex = 0;
    } else {
        bool AllLinesFitOnOnePage = getVerticalSize() <= maxVisible;
        bool selectedIsOnTheFirstPage = selected < maxVisible;
        bool selectedIsOnTheLastPage = selected >= (getVerticalSize() - maxVisible);

        if (AllLinesFitOnOnePage) {
            firstVisibleIndex = 0;
        } else if (selectedIsOnTheFirstPage) {
            firstVisibleIndex = 0;
        } else if (selectedIsOnTheLastPage) {
            firstVisibleIndex = getVerticalSize() - maxVisible;
        } else {
            firstVisibleIndex = selected - (maxVisible / 2);
        }
        lastVisibleIndex = firstVisibleIndex + maxVisible - 1;
    }
}

//*******************************
// GuiMenuBase<LineDataType>::publishToDriver
//*******************************
// the DebugDriver's `items` and `selected`: every row by the text it shows (a heading with a leading '#', so an
// index matches what is drawn), the cursor's row. Called from render() so it follows the lines however a
// subclass fills them; the driver keeps the rows per screen and drops them when the screen closes. A '|' in a
// row's text (an icon mark) becomes '/', the reply's separator being '|'. Skipped once the menu is closing:
// Gui::beginBusy redraws a closed Options panel as its backdrop, and that must not publish into the screen below.
template <typename LineDataType> void GuiMenuBase<LineDataType>::publishToDriver() {
    if (!menuVisible || !ableem::DebugDriver::active())
        return;
    std::vector<std::string> names;
    const int size = getVerticalSize();
    names.reserve(size > 0 ? size : 0);
    for (int i = 0; i < size; i++) {
        std::string name = i < static_cast<int>(lines.size()) ? driverRowName(lines[i]) : std::string();
        std::replace(name.begin(), name.end(), '|', '/');
        names.push_back((skipSelectingThisLineWhenMovingByOne(i) ? "#" : "") + name);
    }
    ableem::DebugDriver::publish(typeid(*this).name(), names, labelsOnly || size == 0 ? -1 : selected);
}

//*******************************
// GuiMenuBase<LineDataType>::renderLines
//*******************************
template <typename LineDataType> void GuiMenuBase<LineDataType>::renderLines() {
    publishToDriver(); // here, not in render(): Options and Game Manager draw with their own render()
    if (selected >= 0 && getVerticalSize() > 0) {
        // every row in the theme's row colour, the selected one in rowSelected (UIREV-29) - a subclass's
        // renderLineIndexOnRow draws through renderTextLine/renderRowValue, which take it from here
        TextRenderer &text = gui->text();
        const TextRenderer::RowRole before = text.rowRole();
        int row = firstRow;
        for (int i = firstVisibleIndex; i <= lastVisibleIndex; i++) {
            if (i < 0 || i >= getVerticalSize()) {
                break;
            }
            text.setRowRole(!labelsOnly && i == selected ? TextRenderer::RowRole::Selected
                                                         : TextRenderer::RowRole::Row);
            renderLineIndexOnRow(i, row); // call virtual that knows how to display the data
            row++;
        }
        text.setRowRole(before);
    }
}

//*******************************
// GuiMenuBase<LineDataType>::renderSelectionBox
//*******************************
template <typename LineDataType> void GuiMenuBase<LineDataType>::renderSelectionBox() {
    if (!labelsOnly && getVerticalSize() > 0) {
        gui->text().renderSelectionBox(selected - firstVisibleIndex + firstRow, yoffset, selectionBoxXOffset, font,
                                       selectionRightEdge);
    }
}

//*******************************
// GuiMenuBase<LineDataType>::render
//*******************************
template <typename LineDataType> void GuiMenuBase<LineDataType>::render() {
    renderer.clear();
    gui->renderBackground();
    // a short list without a pane beside it draws as a compact panel centred on the screen
    const bool compact = getVerticalSize() <= CompactRows && selectionRightEdge == 0;
    if (compact)
        gui->setCompactPanel(getVerticalSize(), font);
    gui->renderTextBar();
    yoffset = gui->renderHeader(getTitle());

    if (firstRender) {
        computePagePosition();
        firstRender = false;
    }
    renderLines();
    renderSelectionBox();
    gui->renderScrollMarkers(firstVisibleIndex > 0, lastVisibleIndex < getVerticalSize() - 1);

    gui->renderStatus(getStatusLine());
    renderer.present();
    if (compact)
        gui->clearCompactPanel();
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
// GuiMenuBase<LineDataType>::doKeyDown
//*******************************
template <typename LineDataType> void GuiMenuBase<LineDataType>::doKeyDown() {
    app.audio().cursor.play();
    if (!labelsOnly && getVerticalSize() > 1) {
        int before = selected;
        if (selected < getVerticalSize() - 1) {
            if (selected == lastVisibleIndex)
                adjustPageBy(1);
            else
                ++selected;
            landOnSelectable(1);
        }
        if (selected == before) { // the last selectable row: wrap to the first
            selected = 0;
            computePagePosition();
            landOnSelectable(1);
        }
    }
}

//*******************************
// GuiMenuBase<LineDataType>::doKeyUp
//*******************************
template <typename LineDataType> void GuiMenuBase<LineDataType>::doKeyUp() {
    app.audio().cursor.play();
    if (!labelsOnly && getVerticalSize() > 1) {
        int before = selected;
        if (selected > 0) {
            if (selected == firstVisibleIndex)
                adjustPageBy(-1);
            else
                --selected;
            landOnSelectable(-1);
        }
        if (selected == before) { // the first selectable row: wrap to the last
            selected = getVerticalSize() - 1;
            computePagePosition();
            landOnSelectable(-1);
        }
    }
}

//*******************************
// GuiMenuBase<LineDataType>::doJoyDown
//*******************************
template <typename LineDataType> void GuiMenuBase<LineDataType>::doJoyDown() {
    holdRows(1);
}

//*******************************
// GuiMenuBase<LineDataType>::doJoyUp
//*******************************
template <typename LineDataType> void GuiMenuBase<LineDataType>::doJoyUp() {
    holdRows(-1);
}

//*******************************
// GuiMenuBase<LineDataType>::holdRows
//*******************************
// one step at the press, then - while nothing else comes from the pad or the keyboard - the same step again at
// HoldRepeat's pace (the one every screen's held key uses): its delay first, then its interval, faster when
// held long
template <typename LineDataType> void GuiMenuBase<LineDataType>::holdRows(int step) {
    HoldRepeat hold;
    hold.press(step, gui->platform().ticks());
    step > 0 ? doKeyDown() : doKeyUp();
    render();
    while (!gui->input().padEventPending()) {
        if (hold.due(gui->platform().ticks()) != 0) {
            step > 0 ? doKeyDown() : doKeyUp();
            render();
        } else {
            gui->platform().delay(2); // a few ms of repeat timing, not a core spinning on the queue
        }
    }
}

//*******************************
// GuiMenuBase<LineDataType>::doPageDown
//*******************************
template <typename LineDataType> void GuiMenuBase<LineDataType>::doPageDown() {
    app.audio().home_up.play();
    if (!labelsOnly && getVerticalSize() > 1) {
        if (lastVisibleIndex + maxVisible >= getVerticalSize()) {
            selected = getVerticalSize() - 1;
            computePagePosition();
        } else {
            adjustPageBy(maxVisible);
        }
        landOnSelectable(1);
    }
}

//*******************************
// GuiMenuBase<LineDataType>::doPageUp
//*******************************
template <typename LineDataType> void GuiMenuBase<LineDataType>::doPageUp() {
    app.audio().home_down.play();
    if (!labelsOnly && getVerticalSize() > 1) {
        if (firstVisibleIndex - maxVisible < 0) {
            selected = 0;
            computePagePosition();
        } else {
            adjustPageBy(-maxVisible);
        }
        landOnSelectable(-1);
    }
}

//*******************************
// GuiMenuBase<LineDataType>::doHome
//*******************************
template <typename LineDataType> void GuiMenuBase<LineDataType>::doHome() {
    app.audio().home_down.play();
    if (!labelsOnly && getVerticalSize() > 1) {
        selected = 0;
        computePagePosition();
        landOnSelectable(1);
    }
}

//*******************************
// GuiMenuBase<LineDataType>::doEnd
//*******************************
template <typename LineDataType> void GuiMenuBase<LineDataType>::doEnd() {
    app.audio().home_down.play();
    if (!labelsOnly && getVerticalSize() > 1) {
        selected = getVerticalSize() - 1;
        computePagePosition();
        landOnSelectable(-1);
    }
}

//*******************************
// GuiMenuBase::doCircle_Pressed
//*******************************
template <typename LineDataType> void GuiMenuBase<LineDataType>::doCircle_Pressed() {
    app.audio().cancel.play();
    cancelled = true;
    menuVisible = false;
}

//*******************************
// GuiMenuBase<LineDataType>::doCross_Pressed
//*******************************
template <typename LineDataType> void GuiMenuBase<LineDataType>::doCross_Pressed() {
    app.audio().cursor.play();
    cancelled = false;
    if (!lines.empty()) {
        menuVisible = false;
    }
}
