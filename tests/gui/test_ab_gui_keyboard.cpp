//
// abgui::Keyboard (G3n): the pure parts - the key under a place, the pages, the selection's moves against the old
// formulas, the UTF-8 editing - and the events through a headless GuiBase (those cases skip themselves without a
// renderer, like test_ab_gui_confirm): the pad, the typed text and the USB keyboard's keys, the sounds, and the
// keyboard-as-pad switch, off while the keyboard shows and put back on every way out.
//
#include "doctest/doctest.h"

#include <ab_gui/actions.h>
#include <ab_gui/context.h>
#include <ab_gui/keyboard.h>

#include <ableem/ui/gui_base.h>

#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace std;
using abgui::Context;
using abgui::Keyboard;
using abgui::UiSound;
using ableem::Button;
using ableem::Event;
using ableem::GuiBase;
using ableem::Key;

namespace {

struct MaybeGui {
    unique_ptr<GuiBase> gui;

    MaybeGui() {
#ifdef _WIN32
        _putenv_s("AB_HEADLESS", "1");
#else
        setenv("AB_HEADLESS", "1", 1);
#endif
        try {
            gui = make_unique<GuiBase>("ab_gui_test_keyboard", 320, 240);
        } catch (const exception &e) {
            const string why = e.what();
            MESSAGE("test_ab_gui_keyboard: skipping - no usable renderer here (" << why << ")");
        }
        if (gui)
            gui->input().flushEvents();
    }

    bool available() const { return gui != nullptr; }
};

struct Side {
    Context ctx;
    vector<UiSound> sounds;
    explicit Side(GuiBase &gui) : ctx(gui.renderer(), gui.input(), gui.platform()) {
        ctx.soundPlayer = [this](UiSound s) { sounds.push_back(s); };
    }
};

// the keyboard with a drawing that only notes the input switches as the loop has them
struct TestKeyboard : Keyboard {
    using Keyboard::Keyboard;
    int frames = 0;
    bool asPadInDraw = true, rawInDraw = false;
    void draw() override {
        frames++;
        asPadInDraw = gui.input().keyboardAsPad();
        rawInDraw = gui.input().rawKeyboard();
    }
};

Event button(Button b) {
    Event e;
    e.type = Event::Type::ButtonDown;
    e.button = b;
    return e;
}

Event dpad(Button b) {
    Event e;
    e.type = Event::Type::DpadDown;
    e.button = b;
    return e;
}

Event key(Key k) {
    Event e;
    e.type = Event::Type::KeyDown;
    e.key = k;
    return e;
}

Event text(const string &s) {
    Event e;
    e.type = Event::Type::TextInput;
    e.text = s;
    return e;
}

Event quit() {
    Event e;
    e.type = Event::Type::Quit;
    return e;
}

// the event through the Input as the loop reads it, then to the keyboard
void feed(GuiBase &gui, Keyboard &keyboard, const Event &event) {
    gui.input().inject(event);
    Event polled;
    REQUIRE(gui.input().poll(polled));
    keyboard.handle(polled);
}

} // namespace

TEST_CASE("Keyboard::keyAt: the pages' keys, shifted and not, and the function row's spans") {
    CHECK(Keyboard::keyAt(0, 0, 0, false).text == "1");
    CHECK(Keyboard::keyAt(0, 1, 0, false).text == "q");
    CHECK(Keyboard::keyAt(0, 1, 0, true).text == "Q");
    CHECK(Keyboard::keyAt(0, 2, 9, false).text == "'");
    CHECK(Keyboard::keyAt(0, 2, 9, true).text == "\"");
    CHECK(Keyboard::keyAt(1, 1, 1, false).text == "\\");
    CHECK(Keyboard::keyAt(1, 3, 2, false).text == "€");
    CHECK(Keyboard::keyAt(1, 3, 2, true).text == "€"); // the symbols page is the same both ways
    CHECK(Keyboard::keyAt(2, 0, 0, false).text == "à");
    CHECK(Keyboard::keyAt(2, 0, 0, true).text == "À");
    CHECK(Keyboard::keyAt(3, 0, 0, false).text == "č");
    CHECK(Keyboard::keyAt(3, 1, 4, true).text == "I");
    // a page out of range is the nearest
    CHECK(Keyboard::keyAt(-3, 1, 0, false).text == "q");
    CHECK(Keyboard::keyAt(9, 0, 0, false).text == "č");
    // the function row: Shift 0-1, Page 2-3, Space 4-6, Backspace 7, Done 8-9
    struct Span {
        int column;
        Keyboard::KeyKind kind;
        int first, span;
    };
    const Span spans[] = {{0, Keyboard::KeyKind::Shift, 0, 2},     {1, Keyboard::KeyKind::Shift, 0, 2},
                          {2, Keyboard::KeyKind::Page, 2, 2},      {3, Keyboard::KeyKind::Page, 2, 2},
                          {4, Keyboard::KeyKind::Space, 4, 3},     {6, Keyboard::KeyKind::Space, 4, 3},
                          {7, Keyboard::KeyKind::Backspace, 7, 1}, {8, Keyboard::KeyKind::Done, 8, 2},
                          {9, Keyboard::KeyKind::Done, 8, 2}};
    for (const Span &s : spans) {
        const Keyboard::KeyCap cap = Keyboard::keyAt(0, Keyboard::CharRows, s.column, false);
        CHECK(cap.kind == s.kind);
        CHECK(cap.firstColumn == s.first);
        CHECK(cap.span == s.span);
    }
    CHECK(Keyboard::keyAt(0, Keyboard::CharRows, 5, false).text == " ");
    CHECK(Keyboard::keyAt(0, Keyboard::CharRows, 0, false).text.empty());
}

TEST_CASE("Keyboard: the page key names the page it leads to, and the pages go round") {
    CHECK(Keyboard::pageKeyLabel(0) == "?123");
    CHECK(Keyboard::pageKeyLabel(1) == "àé");
    CHECK(Keyboard::pageKeyLabel(2) == "čş");
    CHECK(Keyboard::pageKeyLabel(3) == "abc");
    CHECK(Keyboard::pageKeyLabel(-1) == "?123");
    CHECK(Keyboard::pageKeyLabel(7) == "abc");
    CHECK(Keyboard::pageName(0) == "Symbols");
    CHECK(Keyboard::pageName(1) == "Accented letters");
    CHECK(Keyboard::pageName(2) == "More accents");
    CHECK(Keyboard::pageName(3) == "Letters");
    CHECK(Keyboard::nextPage(0) == 1);
    CHECK(Keyboard::nextPage(3) == 0);
}

TEST_CASE("Keyboard::shiftAfter: off, once, lock, off") {
    CHECK(Keyboard::shiftAfter(Keyboard::Shift::Off) == Keyboard::Shift::Once);
    CHECK(Keyboard::shiftAfter(Keyboard::Shift::Once) == Keyboard::Shift::Lock);
    CHECK(Keyboard::shiftAfter(Keyboard::Shift::Lock) == Keyboard::Shift::Off);
}

// the old GuiKeyboard::moveSelection, kept here as the reference
static Keyboard::Selection oldMove(int page, Keyboard::Selection s, int dx, int dy) {
    const int functionRow = Keyboard::CharRows;
    if (dy != 0) {
        s.row = (s.row + dy + Keyboard::CharRows + 1) % (Keyboard::CharRows + 1);
        if (s.row == functionRow)
            s.column = Keyboard::keyAt(page, s.row, s.column, false).firstColumn;
    }
    if (dx != 0) {
        if (s.row == functionRow) {
            const Keyboard::KeyCap cap = Keyboard::keyAt(page, s.row, s.column, false);
            s.column = dx > 0 ? cap.firstColumn + cap.span : cap.firstColumn - 1;
            s.column = (s.column + Keyboard::Columns) % Keyboard::Columns;
            s.column = Keyboard::keyAt(page, s.row, s.column, false).firstColumn;
        } else {
            s.column = (s.column + dx + Keyboard::Columns) % Keyboard::Columns;
        }
    }
    return s;
}

TEST_CASE("Keyboard::moved: round the grid both ways, a key at a time on the function row (the old formulas)") {
    const int steps[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    for (int page = 0; page < Keyboard::Pages; page++)
        for (int row = 0; row <= Keyboard::CharRows; row++)
            for (int column = 0; column < Keyboard::Columns; column++)
                for (const auto &step : steps) {
                    Keyboard::Selection at;
                    at.row = row;
                    at.column = column;
                    const Keyboard::Selection want = oldMove(page, at, step[0], step[1]);
                    const Keyboard::Selection got = Keyboard::moved(page, at, step[0], step[1]);
                    CHECK(got.row == want.row);
                    CHECK(got.column == want.column);
                }
    // a few by hand
    Keyboard::Selection at;
    at.row = 0;
    at.column = 0;
    CHECK(Keyboard::moved(0, at, 0, -1).row == Keyboard::CharRows); // up from the top: the function row
    CHECK(Keyboard::moved(0, at, -1, 0).column == Keyboard::Columns - 1);
    at.row = Keyboard::CharRows;
    at.column = 5; // in Space (4-6): landing on the function row, up/down keeps its first column
    CHECK(Keyboard::moved(0, at, 1, 0).column == 7);
    CHECK(Keyboard::moved(0, at, -1, 0).column == 2);
    at.column = 8;
    CHECK(Keyboard::moved(0, at, 1, 0).column == 0); // Done wraps to Shift
}

TEST_CASE("Keyboard: UTF-8 editing goes by whole characters") {
    const string s = "a\xC3\xA9\xE2\x82\xAC"; // a, e-acute (2 bytes), euro (3 bytes)
    CHECK(Keyboard::previousChar(s, s.size()) == 3);
    CHECK(Keyboard::previousChar(s, 3) == 1);
    CHECK(Keyboard::previousChar(s, 1) == 0);
    CHECK(Keyboard::previousChar(s, 0) == 0);
    CHECK(Keyboard::nextChar(s, 0) == 1);
    CHECK(Keyboard::nextChar(s, 1) == 3);
    CHECK(Keyboard::nextChar(s, 3) == s.size());
    CHECK(Keyboard::nextChar(s, s.size()) == s.size());

    Keyboard::Edit edit{s, s.size()};
    edit = Keyboard::backspaced(edit); // the euro goes whole
    CHECK(edit.text == "a\xC3\xA9");
    CHECK(edit.cursor == 3);
    edit = Keyboard::backspaced(edit);
    CHECK(edit.text == "a");
    edit = Keyboard::backspaced(Keyboard::backspaced(edit));
    CHECK(edit.text.empty());
    CHECK(edit.cursor == 0);

    // insert in the middle, delete forward
    edit = Keyboard::inserted(Keyboard::Edit{"ac", 1}, "b");
    CHECK(edit.text == "abc");
    CHECK(edit.cursor == 2);
    edit = Keyboard::inserted(edit, "\xC5\x82"); // l-stroke
    CHECK(edit.text == "ab\xC5\x82"
                       "c");
    CHECK(edit.cursor == 4);
    CHECK(Keyboard::inserted(edit, "").text == edit.text);
    edit = Keyboard::deletedForward(Keyboard::Edit{s, 1});
    CHECK(edit.text == "a\xE2\x82\xAC");
    CHECK(edit.cursor == 1);
    CHECK(Keyboard::deletedForward(Keyboard::Edit{s, s.size()}).text == s);
}

TEST_CASE("Keyboard: a password shows one star per character, the caret counted in characters") {
    const string s = "a\xC3\xA9\xE2\x82\xAC"; // three characters, six bytes
    CHECK(Keyboard::shown(s, false) == s);
    CHECK(Keyboard::shown(s, true) == "***");
    CHECK(Keyboard::caretIn(s, 3, false) == 3);
    CHECK(Keyboard::caretIn(s, 3, true) == 2);
    CHECK(Keyboard::caretIn(s, s.size(), true) == 3);
    CHECK(Keyboard::caretIn(s, 0, true) == 0);
}

TEST_CASE("Keyboard: Cross types the selected key; a capital only once, the lock until turned off") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Keyboard keyboard(*g.gui, side.ctx);
    keyboard.menuVisible = true;
    keyboard.row = 1;
    keyboard.column = 0; // q
    feed(*g.gui, keyboard, button(Button::Cross));
    CHECK(keyboard.result == "q");
    CHECK(keyboard.cursorIndex == 1);
    feed(*g.gui, keyboard, button(Button::L1)); // Shift once
    CHECK(keyboard.shift == Keyboard::Shift::Once);
    feed(*g.gui, keyboard, button(Button::Cross));
    CHECK(keyboard.result == "qQ");
    CHECK(keyboard.shift == Keyboard::Shift::Off); // once
    feed(*g.gui, keyboard, button(Button::L1));
    feed(*g.gui, keyboard, button(Button::L1)); // lock
    CHECK(keyboard.shift == Keyboard::Shift::Lock);
    feed(*g.gui, keyboard, button(Button::Cross));
    feed(*g.gui, keyboard, button(Button::Cross));
    CHECK(keyboard.result == "qQQQ");
    feed(*g.gui, keyboard, button(Button::L1));
    CHECK(keyboard.shift == Keyboard::Shift::Off);
    CHECK(keyboard.menuVisible);
}

TEST_CASE("Keyboard: the function row's keys - Shift, Page, Space, Backspace, Done - by Cross") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Keyboard keyboard(*g.gui, side.ctx);
    keyboard.menuVisible = true;
    keyboard.row = Keyboard::CharRows;
    keyboard.column = 1; // Shift
    feed(*g.gui, keyboard, button(Button::Cross));
    CHECK(keyboard.shift == Keyboard::Shift::Once);
    keyboard.column = 3; // Page
    feed(*g.gui, keyboard, button(Button::Cross));
    CHECK(keyboard.page == 1);
    keyboard.column = 5; // Space
    feed(*g.gui, keyboard, button(Button::Cross));
    CHECK(keyboard.result == " ");
    CHECK(keyboard.shift == Keyboard::Shift::Off); // typing used the once-shift up
    keyboard.column = 7;                           // Backspace
    feed(*g.gui, keyboard, button(Button::Cross));
    CHECK(keyboard.result.empty());
    keyboard.column = 9; // Done
    CHECK(keyboard.cancelled);
    feed(*g.gui, keyboard, button(Button::Cross));
    CHECK(!keyboard.cancelled);
    CHECK(!keyboard.menuVisible);
}

TEST_CASE("Keyboard: the other pad buttons - Triangle, Square, R1, L2/R2, Start, Circle") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Keyboard keyboard(*g.gui, side.ctx);
    keyboard.menuVisible = true;
    keyboard.result = "ab\xC3\xA9";
    keyboard.cursorIndex = keyboard.result.size();
    feed(*g.gui, keyboard, button(Button::Triangle)); // backspace, the e-acute whole
    CHECK(keyboard.result == "ab");
    feed(*g.gui, keyboard, button(Button::L2));
    CHECK(keyboard.cursorIndex == 1);
    feed(*g.gui, keyboard, button(Button::Square)); // a space at the cursor
    CHECK(keyboard.result == "a b");
    CHECK(keyboard.cursorIndex == 2);
    feed(*g.gui, keyboard, button(Button::R2));
    CHECK(keyboard.cursorIndex == 3);
    feed(*g.gui, keyboard, button(Button::R2)); // at the end: stays
    CHECK(keyboard.cursorIndex == 3);
    feed(*g.gui, keyboard, button(Button::R1)); // the next page
    CHECK(keyboard.page == 1);
    side.sounds.clear();
    feed(*g.gui, keyboard, button(Button::Select)); // no meaning, but a button plays the cursor sound
    CHECK(side.sounds == vector<UiSound>{UiSound::Cursor});
    CHECK(keyboard.menuVisible);

    feed(*g.gui, keyboard, button(Button::Start)); // Done
    CHECK(!keyboard.cancelled);
    CHECK(!keyboard.menuVisible);

    keyboard.menuVisible = true;
    keyboard.cancelled = false;
    side.sounds.clear();
    feed(*g.gui, keyboard, button(Button::Circle));
    CHECK(keyboard.cancelled);
    CHECK(!keyboard.menuVisible);
    CHECK(side.sounds == vector<UiSound>{UiSound::Cursor, UiSound::Cancel});
}

TEST_CASE("Keyboard: the d-pad moves the selection by its live state, with the cursor sound") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Keyboard keyboard(*g.gui, side.ctx);
    keyboard.menuVisible = true;
    // each direction pressed and let go, as a pad does
    const struct {
        Button direction;
        int row, column;
    } steps[] = {{Button::DpadRight, 1, 1}, {Button::DpadDown, 2, 1}, {Button::DpadLeft, 2, 0}, {Button::DpadUp, 1, 0}};
    for (const auto &step : steps) {
        feed(*g.gui, keyboard, dpad(step.direction));
        CHECK(keyboard.row == step.row);
        CHECK(keyboard.column == step.column);
        Event up = dpad(step.direction);
        up.type = Event::Type::DpadUp;
        feed(*g.gui, keyboard, up);
    }
    CHECK(keyboard.result.empty());
    CHECK(side.sounds == vector<UiSound>(4, UiSound::Cursor));
    CHECK(keyboard.result.empty());
}

TEST_CASE("Keyboard: typed text is inserted at the cursor with the cursor sound, and a once-shift is used up") {
    MaybeGui g;
    if (!g.available())
        return;
    g.gui->input().setKeyboardAsPad(false);
    Side side(*g.gui);
    Keyboard keyboard(*g.gui, side.ctx);
    keyboard.menuVisible = true;
    keyboard.shift = Keyboard::Shift::Once;
    feed(*g.gui, keyboard, text("h"));
    feed(*g.gui, keyboard, text("i\xC5\x82")); // a chunk of two characters
    CHECK(keyboard.result == "hi\xC5\x82");
    CHECK(keyboard.cursorIndex == keyboard.result.size());
    CHECK(keyboard.shift == Keyboard::Shift::Off);
    CHECK(side.sounds == vector<UiSound>(2, UiSound::Cursor));
    feed(*g.gui, keyboard, text(""));
    CHECK(keyboard.result == "hi\xC5\x82");
}

TEST_CASE("Keyboard: the USB keyboard's keys, as keys - arrows, Home, End, Backspace, Delete, Enter, Esc") {
    MaybeGui g;
    if (!g.available())
        return;
    g.gui->input().setKeyboardAsPad(false);
    Side side(*g.gui);
    Keyboard keyboard(*g.gui, side.ctx);
    keyboard.menuVisible = true;
    keyboard.result = "a\xC3\xA9z";
    keyboard.cursorIndex = keyboard.result.size();
    feed(*g.gui, keyboard, key(Key::Left));
    CHECK(keyboard.cursorIndex == 3);
    feed(*g.gui, keyboard, key(Key::Left));
    CHECK(keyboard.cursorIndex == 1);
    feed(*g.gui, keyboard, key(Key::Right));
    CHECK(keyboard.cursorIndex == 3);
    feed(*g.gui, keyboard, key(Key::Home));
    CHECK(keyboard.cursorIndex == 0);
    feed(*g.gui, keyboard, key(Key::Delete)); // the a
    CHECK(keyboard.result == "\xC3\xA9z");
    feed(*g.gui, keyboard, key(Key::End));
    CHECK(keyboard.cursorIndex == keyboard.result.size());
    feed(*g.gui, keyboard, key(Key::Backspace)); // the z
    CHECK(keyboard.result == "\xC3\xA9");
    feed(*g.gui, keyboard, key(Key::Backspace)); // the e-acute whole
    CHECK(keyboard.result.empty());
    CHECK(side.sounds.empty()); // keys play nothing
    CHECK(keyboard.menuVisible);
    feed(*g.gui, keyboard, key(Key::Return));
    CHECK(!keyboard.cancelled);
    CHECK(!keyboard.menuVisible);

    keyboard.menuVisible = true;
    keyboard.cancelled = false;
    feed(*g.gui, keyboard, key(Key::Escape));
    CHECK(keyboard.cancelled);
    CHECK(!keyboard.menuVisible);
    CHECK(side.sounds.empty());
}

TEST_CASE("Keyboard: Confirm and Back swapped, Circle types the key and Cross cancels") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    side.ctx.actions.setSwapConfirmBack(true);
    Keyboard keyboard(*g.gui, side.ctx);
    keyboard.menuVisible = true;
    feed(*g.gui, keyboard, button(Button::Circle));
    CHECK(keyboard.result == "q");
    CHECK(keyboard.menuVisible);
    feed(*g.gui, keyboard, button(Button::Cross));
    CHECK(keyboard.cancelled);
    CHECK(!keyboard.menuVisible);
}

TEST_CASE("Keyboard::init puts the cursor at the end of the text") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Keyboard keyboard(*g.gui, side.ctx);
    keyboard.result = "abc\xC3\xA9";
    keyboard.init();
    CHECK(keyboard.cursorIndex == 5);
}

TEST_CASE("Keyboard::loop: the keyboard-as-pad is off and the raw keys on while it shows, and put back after Done") {
    MaybeGui g;
    if (!g.available())
        return;
    ableem::Input &input = g.gui->input();
    REQUIRE(input.keyboardAsPad()); // on by default
    REQUIRE(!input.rawKeyboard());
    Side side(*g.gui);
    TestKeyboard keyboard(*g.gui, side.ctx);
    keyboard.result = "ab";
    keyboard.init();
    input.inject(text("c"));
    input.inject(key(Key::Backspace)); // a key (not Circle): backspace
    input.inject(text("d"));
    input.inject(button(Button::Start));
    keyboard.loop();
    CHECK(keyboard.result == "abd");
    CHECK(!keyboard.cancelled);
    CHECK(!keyboard.menuVisible);
    CHECK(keyboard.frames > 0);
    CHECK(!keyboard.asPadInDraw);
    CHECK(keyboard.rawInDraw);
    CHECK(input.keyboardAsPad());
    CHECK(!input.rawKeyboard());
}

TEST_CASE("Keyboard::loop: Enter is a key here, not Cross - it is Done, and types nothing") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    TestKeyboard keyboard(*g.gui, side.ctx);
    g.gui->input().inject(key(Key::Return));
    keyboard.loop();
    CHECK(keyboard.result.empty());
    CHECK(!keyboard.cancelled);
    CHECK(g.gui->input().keyboardAsPad());
}

TEST_CASE("Keyboard::loop: Circle cancels and puts the switches back, whatever they were") {
    MaybeGui g;
    if (!g.available())
        return;
    ableem::Input &input = g.gui->input();
    input.setKeyboardAsPad(false);
    input.setRawKeyboard(true);
    Side side(*g.gui);
    TestKeyboard keyboard(*g.gui, side.ctx);
    keyboard.cancelled = false;
    input.inject(button(Button::Circle));
    keyboard.loop();
    CHECK(keyboard.cancelled);
    CHECK(!input.keyboardAsPad());
    CHECK(input.rawKeyboard());
    CHECK(side.sounds == vector<UiSound>{UiSound::Cursor, UiSound::Cancel});
}

TEST_CASE("Keyboard::loop: Esc cancels and puts the switches back") {
    MaybeGui g;
    if (!g.available())
        return;
    ableem::Input &input = g.gui->input();
    Side side(*g.gui);
    TestKeyboard keyboard(*g.gui, side.ctx);
    keyboard.result = "keep";
    keyboard.init();
    keyboard.cancelled = false;
    input.inject(text("x"));
    input.inject(key(Key::Escape));
    keyboard.loop();
    CHECK(keyboard.cancelled);
    CHECK(keyboard.result == "keepx"); // the text stays where the caller can read it (it ignores it when cancelled)
    CHECK(input.keyboardAsPad());
    CHECK(!input.rawKeyboard());
}

TEST_CASE("Keyboard::loop: the window's Quit cancels, and the switches are back") {
    MaybeGui g;
    if (!g.available())
        return;
    ableem::Input &input = g.gui->input();
    Side side(*g.gui);
    TestKeyboard keyboard(*g.gui, side.ctx);
    keyboard.cancelled = false;
    g.gui->input().inject(quit());
    keyboard.loop();
    CHECK(keyboard.cancelled);
    CHECK(!keyboard.menuVisible);
    CHECK(input.keyboardAsPad());
    CHECK(!input.rawKeyboard());
    CHECK(side.sounds.empty());
}

TEST_CASE("Keyboard::loop: events after the one that closes it are left in the queue") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    TestKeyboard keyboard(*g.gui, side.ctx);
    g.gui->input().inject(button(Button::Start));
    g.gui->input().inject(text("z"));
    keyboard.loop();
    CHECK(keyboard.result.empty());
    Event left;
    REQUIRE(g.gui->input().poll(left));
    CHECK(left.type == Event::Type::TextInput);
}
