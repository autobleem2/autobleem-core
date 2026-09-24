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
#pragma once

#include <cstddef>
#include <string>

#include "../gui.h"
#include "../gui_screen.h"

//********************
// GuiKeyboard
//********************
class GuiKeyboard : public GuiScreen {
public:
    void render() override;
    void loop() override;
    void init() override;

    std::string label = "";
    std::string result = "";
    bool cancelled = true;
    bool displayAsterisksInstead = false; // a password: shown as *****

    using GuiScreen::GuiScreen;

    // what a key does
    enum class KeyKind { Char, Shift, Page, Space, Backspace, Done };
    enum class Shift { Off, Once, Lock };
    static const int Columns = 10;
    static const int CharRows = 4;
    static const int Pages = 4; // letters, symbols, accents, more accents

    // the key under (row, column) of a page, shifted or not: row 4 is the function row, its keys spanning
    // columns (Shift 2, Page 2, Space 3, Backspace 1, Done 2)
    struct KeyCap {
        KeyKind kind = KeyKind::Char;
        std::string text; // what a Char key types
        int firstColumn = 0, span = 1;
    };
    static KeyCap keyAt(int page, int row, int column, bool shifted);
    static std::string pageKeyLabel(int page); // the page key names the page it leads to

    // UTF-8 editing, by whole characters (the cursor is a byte offset on a character's start)
    static size_t previousChar(const std::string &text, size_t at);
    static size_t nextChar(const std::string &text, size_t at);

private:
    void type(const std::string &text);
    void backspace();
    void deleteForward();
    void press(); // the selected key
    void moveSelection(int dx, int dy);
    void nextShift();
    void confirm();
    void cancel();
    void drawKey(const ableem::Rect &key, const KeyCap &cap, bool selected);

    std::shared_ptr<Gui> gui;
    size_t cursorIndex = 0;
    int page = 0;
    int row = 1, column = 0; // the selected key (row 4: the function row)
    Shift shift = Shift::Off;
};
