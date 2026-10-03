// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::Keyboard (G3n of docs/ab-gui-plan.md): the on-screen keyboard - a text field and a grid of keys in the
// classic panel.
//
// Laid out like the virtual keyboards people know from phones and consoles: four rows of keys on a page - letters
// (digits, qwerty), symbols (everything a URL, a path or a password needs: / \ : ? & = % @ # ...), and two pages of
// accented letters - and a row of function keys under them: Shift (once for a capital, twice for caps lock), the page
// key, Space, Backspace and Done.
//
//   Pad: Cross types the key, Triangle deletes, Square is a space, L1 is Shift, R1 the next page, L2/R2 move the text
//        cursor, Start is Done, Circle cancels (by the actions the Context's ActionMap gives those buttons).
//   A USB keyboard types at any time alongside the pad: arrows/Home/End move the cursor, Enter is Done, Esc cancels.
//   While the keyboard shows, the keyboard-as-pad is off (letters are letters, not buttons) and the raw keyboard is on;
//   both are put back as they were when it goes, whichever way it goes.
//
// The caller sets `label` (the question) and `result` (the text to start from), runs the screen (loop()) and reads
// `result` unless `cancelled`. `result` is UTF-8; the cursor moves by whole characters. The pure parts - the key
// under a place, the selection's moves, the UTF-8 editing - are static and tested.
//
#pragma once

#include <ab_gui/screen.h>
#include <ab_gui/style.h>

#include <cstddef>
#include <string>

namespace abgui {

//********************
// Keyboard
//********************
class Keyboard : public Screen {
public:
    // it slides up from the bottom over the screen that asked for the text, and back down
    Keyboard(ableem::GuiBase &gui, Context &context) : Screen(gui, context) {
        declareTransitions(ScreenTransitions(Transition::slide(SlideFrom::Bottom)));
    }

    std::string label;                    // the header
    std::string result;                   // the text
    bool cancelled = true;                // false after Done
    bool displayAsterisksInstead = false; // a password: shown as *****

    // what a key does
    enum class KeyKind { Char, Shift, Page, Space, Backspace, Done };
    enum class Shift { Off, Once, Lock };
    static const int Columns = 10;
    static const int CharRows = 4;
    static const int Pages = 4; // letters, symbols, accents, more accents

    // the key under (row, column) of a page, shifted or not: row 4 is the function row, its keys spanning columns
    // (Shift 2, Page 2, Space 3, Backspace 1, Done 2)
    struct KeyCap {
        KeyKind kind = KeyKind::Char;
        std::string text; // what a Char key types
        int firstColumn = 0, span = 1;
    };
    static KeyCap keyAt(int page, int row, int column, bool shifted);
    static std::string pageKeyLabel(int page); // the page key names the page it leads to
    // the same, in words (English, for the translator) - the footer's R1 hint names the page that follows
    static std::string pageName(int page);

    // UTF-8 editing, by whole characters (the cursor is a byte offset on a character's start)
    static size_t previousChar(const std::string &text, size_t at);
    static size_t nextChar(const std::string &text, size_t at);

    // the text and where the cursor is in it
    struct Edit {
        std::string text;
        size_t cursor = 0;
    };
    static Edit inserted(const Edit &edit, const std::string &text); // `text` at the cursor, the cursor after it
    static Edit backspaced(const Edit &edit);                        // the character before the cursor gone
    static Edit deletedForward(const Edit &edit);                    // the character after the cursor gone
    // what the field shows: the text, or one '*' per character; and the caret's place in it, in shown characters
    static std::string shown(const std::string &text, bool asterisks);
    static size_t caretIn(const std::string &text, size_t cursor, bool asterisks);

    // the selected key
    struct Selection {
        int row = 1, column = 0;
    };
    // round the grid both ways; on the function row a step is a key, and going up or down keeps the column
    static Selection moved(int page, const Selection &at, int dx, int dy);
    static Shift shiftAfter(Shift shift); // Off -> Once -> Lock -> Off
    static int nextPage(int page);

    // the state - public so a forwarding class can seed it and read it back
    size_t cursorIndex = 0;
    int page = 0;
    int row = 1, column = 0; // the selected key (row 4: the function row)
    Shift shift = Shift::Off;

    // the cursor at the end of the text, as a fresh keyboard starts
    void init() override;
    void draw() override;
    // the old keyboard's loop: keyboard-as-pad off and raw keys on for its duration, a frame when due, then each event
    // to handle()
    void loop() override;
    void onAction(const ActionEvent &action) override;
    void onUnmapped(const ableem::Event &event) override;

protected:
    void type(const std::string &text);
    void backspace();
    void deleteForward();
    void press(); // the selected key
    void moveSelection(int dx, int dy);
    void nextShift();
    void confirm();
    void cancel();

private:
    void drawKey(const Style &style, const ableem::Rect &key, const KeyCap &cap, bool selected);
    void buttonDown(Action action);
    void dpadDown();
    void keyDown(ableem::Key key);
};

} // namespace abgui
