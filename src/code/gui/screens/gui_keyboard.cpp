//
// GuiKeyboard - see the header. The pages, the keys, the editing, the drawing and the loop are ab_gui's
// abgui::Keyboard (docs/ab-gui-plan.md, G3n); this class keeps its header - the Store and PSC-Bios construct it, built
// for ABI 6 - and hands its state to one of those for every frame and for the loop, then takes the typed text back.
//
#include "gui_keyboard.h"

#include "../gui.h"

#include <ab_gui/keyboard.h>

#include <algorithm>

using namespace std;

namespace {

// an abgui::Keyboard holding what the classic keyboard holds
void fill(abgui::Keyboard &keyboard, const GuiKeyboard &classic, size_t cursorIndex, int page, int row, int column,
          GuiKeyboard::Shift shift) {
    keyboard.label = classic.label;
    keyboard.result = classic.result;
    keyboard.cancelled = classic.cancelled;
    keyboard.displayAsterisksInstead = classic.displayAsterisksInstead;
    keyboard.cursorIndex = cursorIndex;
    keyboard.page = page;
    keyboard.row = row;
    keyboard.column = column;
    keyboard.shift = static_cast<abgui::Keyboard::Shift>(shift);
}

} // namespace

//*******************************
// GuiKeyboard::keyAt / pageKeyLabel / pageName
//*******************************
GuiKeyboard::KeyCap GuiKeyboard::keyAt(int page, int row, int column, bool shifted) {
    const abgui::Keyboard::KeyCap from = abgui::Keyboard::keyAt(page, row, column, shifted);
    KeyCap cap;
    cap.kind = static_cast<KeyKind>(from.kind);
    cap.text = from.text;
    cap.firstColumn = from.firstColumn;
    cap.span = from.span;
    return cap;
}

string GuiKeyboard::pageKeyLabel(int page) {
    return abgui::Keyboard::pageKeyLabel(page);
}

// the same "what R1 leads to" as pageKeyLabel(), in words - the footer's R1 hint (K2, UIREV-21) used to read
// "Symbols" no matter which page was showing; this names the page that follows the current one.
string GuiKeyboard::pageName(int page) {
    switch (max(0, min(Pages - 1, page))) {
    case 0:
        return _("Symbols");
    case 1:
        return _("Accented letters");
    case 2:
        return _("More accents");
    default:
        return _("Letters");
    }
}

//*******************************
// GuiKeyboard::previousChar / nextChar
//*******************************
size_t GuiKeyboard::previousChar(const string &text, size_t at) {
    return abgui::Keyboard::previousChar(text, at);
}

size_t GuiKeyboard::nextChar(const string &text, size_t at) {
    return abgui::Keyboard::nextChar(text, at);
}

//*******************************
// GuiKeyboard::init
//*******************************
void GuiKeyboard::init() {
    gui = Gui::getInstance();
    cursorIndex = result.size(); // the cursor starts at the end of the text
}

//*******************************
// GuiKeyboard::render
//*******************************
// the frame through Gui's screen stack: clear, draw, present (docs/ab-gui-plan.md, G3c)
void GuiKeyboard::render() {
    abgui::Keyboard keyboard(*gui, gui->uiContext());
    fill(keyboard, *this, cursorIndex, page, row, column, shift);
    keyboard.render();
}

//*******************************
// GuiKeyboard::loop
//*******************************
// the keyboard-as-pad is off for the loop and put back after it, inside abgui::Keyboard::loop()
void GuiKeyboard::loop() {
    abgui::Keyboard keyboard(*gui, gui->uiContext());
    fill(keyboard, *this, cursorIndex, page, row, column, shift);
    menuVisible = true;
    keyboard.loop();
    result = keyboard.result;
    cancelled = keyboard.cancelled;
    cursorIndex = keyboard.cursorIndex;
    page = keyboard.page;
    row = keyboard.row;
    column = keyboard.column;
    shift = static_cast<Shift>(keyboard.shift);
    menuVisible = false;
}
