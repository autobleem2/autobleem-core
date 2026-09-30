//
// GuiKeyboard: the on-screen keyboard - a text field and a grid of keys in the classic panel.
//
// Laid out like the virtual keyboards people know from phones and consoles: four rows of keys on a page -
// letters (digits, qwerty), symbols (everything a URL, a path or a password needs: / \ : ? & = % @ # ...),
// and two pages of accented letters - and a row of function keys under them: Shift (once for a capital, twice
// for caps lock), the page key, Space, Backspace and Done.
//
//   Pad: Cross types the key, Triangle deletes, Square is a space, L1 is Shift, R1 the next page, L2/R2 move
//        the text cursor, Start is Done, Circle cancels.
//   A USB keyboard types at any time alongside the pad: arrows/Home/End move the cursor, Enter is Done, Esc
//   cancels. While the keyboard shows, a dev host's keyboard-as-pad is off, so letters are letters.
//
// Callers set `label` (the question) and `result` (the text to start from), show() it, then read `result`
// unless `cancelled`. `result` is UTF-8; the cursor moves by whole characters.
//
// ab_gui's abgui::Keyboard (docs/ab-gui-plan.md, G3n) as a classic screen (G3z): the pages, the keys, the editing,
// the drawing and the loop are its own.
//
#pragma once

#include <ab_gui/keyboard.h>

#include <string>

#include "../gui_screen.h"

//********************
// GuiKeyboard
//********************
class GuiKeyboard : public ClassicScreen<abgui::Keyboard> {
public:
    explicit GuiKeyboard(ableem::GuiBase &_gui) : ClassicScreen<abgui::Keyboard>(_gui) {}

    // the page R1 leads to, in words, translated - the footer's R1 hint (abgui::Keyboard::pageName is the English
    // the keyboard hands its translator; these literals are what the language tools find)
    static std::string pageName(int page);
};
