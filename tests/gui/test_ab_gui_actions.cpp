//
// abgui::ActionMap and abgui::HoldRepeat (G3f of docs/ab-gui-plan.md). Pure. The default map is checked against the
// pad table written out here and against ableem::KeyboardMap (the keyboard-as-pad map every screen already lives
// with) for every key; the Confirm/Back swap must exchange exactly those two; HoldRepeat, moved into ab_gui, keeps
// its pace to the millisecond and the old global names.
//
#include "doctest/doctest.h"

#include <ab_gui/actions.h>
#include <ab_gui/hold_repeat.h>

#include <ableem/ui/keyboard_map.h>

#include "gui/hold_repeat.h"

#include <cstdint>
#include <type_traits>
#include <vector>

using abgui::Action;
using abgui::ActionMap;
using ableem::Button;
using ableem::Event;
using ableem::Key;

namespace {

const Button AllButtons[] = {Button::None,  Button::Cross,  Button::Circle,   Button::Square,   Button::Triangle,
                             Button::Start, Button::Select, Button::L1,       Button::R1,       Button::L2,
                             Button::R2,    Button::DpadUp, Button::DpadDown, Button::DpadLeft, Button::DpadRight};

// what the classic screens read each pad button as
Action padTable(Button b) {
    switch (b) {
    case Button::Cross:
        return Action::Confirm;
    case Button::Circle:
        return Action::Back;
    case Button::Triangle:
        return Action::Option;
    case Button::Square:
        return Action::Extra;
    case Button::Start:
        return Action::Menu;
    case Button::Select:
        return Action::View;
    case Button::L1:
        return Action::PrevTab;
    case Button::R1:
        return Action::NextTab;
    case Button::L2:
        return Action::PageUp;
    case Button::R2:
        return Action::PageDown;
    case Button::DpadUp:
        return Action::Up;
    case Button::DpadDown:
        return Action::Down;
    case Button::DpadLeft:
        return Action::Left;
    case Button::DpadRight:
        return Action::Right;
    default:
        return Action::None;
    }
}

Event buttonEvent(Event::Type type, Button b) {
    Event e;
    e.type = type;
    e.button = b;
    return e;
}

Event keyEvent(Event::Type type, Key k, int code = 0) {
    Event e;
    e.type = type;
    e.key = k;
    e.code = code;
    return e;
}

} // namespace

TEST_CASE("ActionMap default: every pad button is the action the classic screens read it as") {
    const ActionMap map;
    for (Button b : AllButtons)
        CHECK(map.fromButton(b) == padTable(b));
    CHECK_FALSE(map.swapConfirmBack());
}

TEST_CASE("ActionMap default: every key is the action of the pad button KeyboardMap sends it to") {
    const ActionMap map;
    // the whole Key enum, Other (0) to F12
    for (int k = static_cast<int>(Key::Other); k <= static_cast<int>(Key::F12); k++) {
        const Key key = static_cast<Key>(k);
        const ableem::KeyboardMap::Mapped pad = ableem::KeyboardMap::toPad(key, 0, false);
        CHECK_MESSAGE(map.fromKey(key) == padTable(pad.button), "key " << k);
    }
    // the Space bar is a Key::Other with the character ' '; another character is nothing
    CHECK(map.fromKey(Key::Other, ' ') == padTable(ableem::KeyboardMap::toPad(Key::Other, ' ', false).button));
    CHECK(map.fromKey(Key::Other, ' ') == Action::Extra);
    CHECK(map.fromKey(Key::Other, 'x') == Action::None);
    CHECK(map.fromKey(Key::Other, 0) == Action::None);
    // the named ones, spelled out
    CHECK(map.fromKey(Key::Return) == Action::Confirm);
    CHECK(map.fromKey(Key::Escape) == Action::Back);
    CHECK(map.fromKey(Key::Backspace) == Action::Back);
    CHECK(map.fromKey(Key::Tab) == Action::Option);
    CHECK(map.fromKey(Key::F1) == Action::View);
    CHECK(map.fromKey(Key::F2) == Action::Menu);
    CHECK(map.fromKey(Key::PageUp) == Action::PrevTab);
    CHECK(map.fromKey(Key::PageDown) == Action::NextTab);
    CHECK(map.fromKey(Key::Home) == Action::PageUp);
    CHECK(map.fromKey(Key::End) == Action::PageDown);
    CHECK(map.fromKey(Key::F10) == Action::None); // the System menu chord (L2+R2) is not one action
    CHECK(map.fromKey(Key::Delete) == Action::None);
}

TEST_CASE("ActionMap swap: exactly Confirm and Back change places, on the pad's buttons") {
    ActionMap map;
    map.setSwapConfirmBack(true);
    CHECK(map.swapConfirmBack());
    for (Button b : AllButtons) {
        Action expected = padTable(b);
        if (expected == Action::Confirm)
            expected = Action::Back;
        else if (expected == Action::Back)
            expected = Action::Confirm;
        CHECK(map.fromButton(b) == expected);
    }
    CHECK(map.fromButton(Button::Cross) == Action::Back);
    CHECK(map.fromButton(Button::Circle) == Action::Confirm);
    // the keyboard keeps Enter and Esc
    const ActionMap plain;
    for (int k = static_cast<int>(Key::Other); k <= static_cast<int>(Key::F12); k++)
        CHECK(map.fromKey(static_cast<Key>(k)) == plain.fromKey(static_cast<Key>(k)));
    // and back off, the default again
    map.setSwapConfirmBack(false);
    for (Button b : AllButtons)
        CHECK(map.fromButton(b) == padTable(b));
}

TEST_CASE("ActionMap swap: a pad event the keyboard-as-pad made from a key keeps the key's meaning (G3z)") {
    ActionMap map;
    map.setSwapConfirmBack(true);
    for (Event::Type type : {Event::Type::ButtonDown, Event::Type::ButtonUp}) {
        Event fromPad = buttonEvent(type, Button::Cross);
        Event fromKey = fromPad;
        fromKey.fromKey = true; // Enter, made Cross by the keyboard-as-pad
        CHECK(map.fromEvent(fromPad).action == Action::Back);
        CHECK(map.fromEvent(fromKey).action == Action::Confirm);
        fromKey.button = Button::Circle; // Esc
        CHECK(map.fromEvent(fromKey).action == Action::Back);
    }
    // every other button the same either way, and without the swap a key-made event is the button's
    for (Button b : AllButtons) {
        Event e = buttonEvent(Event::Type::ButtonDown, b);
        e.fromKey = true;
        CHECK(map.fromEvent(e).action == padTable(b));
        map.setSwapConfirmBack(false);
        CHECK(map.fromEvent(e).action == padTable(b));
        map.setSwapConfirmBack(true);
    }
}

TEST_CASE("ActionMap events: press and release, by button, d-pad and key; the rest is nothing") {
    const ActionMap map;
    abgui::ActionEvent a = map.fromEvent(buttonEvent(Event::Type::ButtonDown, Button::Cross));
    CHECK(a.action == Action::Confirm);
    CHECK(a.pressed);
    CHECK_FALSE(a.released);
    a = map.fromEvent(buttonEvent(Event::Type::ButtonUp, Button::Cross));
    CHECK(a.action == Action::Confirm);
    CHECK_FALSE(a.pressed);
    CHECK(a.released);
    a = map.fromEvent(buttonEvent(Event::Type::DpadDown, Button::DpadDown));
    CHECK(a.action == Action::Down);
    CHECK(a.pressed);
    a = map.fromEvent(buttonEvent(Event::Type::DpadUp, Button::DpadDown));
    CHECK(a.action == Action::Down);
    CHECK(a.released);
    a = map.fromEvent(keyEvent(Event::Type::KeyDown, Key::Return));
    CHECK(a.action == Action::Confirm);
    CHECK(a.pressed);
    a = map.fromEvent(keyEvent(Event::Type::KeyUp, Key::Home));
    CHECK(a.action == Action::PageUp);
    CHECK(a.released);
    a = map.fromEvent(keyEvent(Event::Type::KeyDown, Key::Other, ' '));
    CHECK(a.action == Action::Extra);
    // unmapped, and event kinds that are not input
    a = map.fromEvent(keyEvent(Event::Type::KeyDown, Key::F5));
    CHECK_FALSE(a.mapped());
    CHECK_FALSE(a.pressed);
    CHECK_FALSE(map.fromEvent(buttonEvent(Event::Type::Quit, Button::Cross)).mapped());
    CHECK_FALSE(map.fromEvent(Event()).mapped());
}

TEST_CASE("ActionMap bind: First and Last on a button, a key unbound") {
    ActionMap map;
    map.bind(Button::L1, Action::First);
    map.bind(Button::R1, Action::Last);
    CHECK(map.fromButton(Button::L1) == Action::First);
    CHECK(map.fromButton(Button::R1) == Action::Last);
    map.bind(Key::Home, Action::First);
    CHECK(map.fromKey(Key::Home) == Action::First);
    map.bind(Key::Tab, Action::None);
    CHECK(map.fromKey(Key::Tab) == Action::None);
    map.bindSpace(Action::Menu);
    CHECK(map.fromKey(Key::Other, ' ') == Action::Menu);
    // a default map has no First/Last at all
    const ActionMap plain;
    for (Button b : AllButtons) {
        CHECK(plain.fromButton(b) != Action::First);
        CHECK(plain.fromButton(b) != Action::Last);
    }
}

TEST_CASE("HoldRepeat in ab_gui: the old names are the same class, laid out as before") {
    CHECK(std::is_same<::HoldRepeat, abgui::HoldRepeat>::value);
    CHECK(std::is_same<::DpadHold, abgui::DpadHold>::value);
    CHECK(sizeof(abgui::HoldRepeat) == 28); // step, start, next, and the four-field Timing
}

TEST_CASE("HoldRepeat pace: the same delays and rates to the millisecond") {
    CHECK(uint32_t(abgui::HoldRepeat::RepeatDelayMs) == 350);
    CHECK(uint32_t(abgui::HoldRepeat::RepeatIntervalMs) == 80);
    CHECK(uint32_t(abgui::HoldRepeat::RepeatFastAfterMs) == 1200);
    CHECK(uint32_t(abgui::HoldRepeat::RepeatFastIntervalMs) == 30);
    const abgui::HoldRepeat::Timing rows = abgui::HoldRepeat::rows();
    CHECK(rows.delay == 350);
    CHECK(rows.interval == 80);
    CHECK(rows.fastAfter == 1200);
    CHECK(rows.fastInterval == 30);
    const abgui::HoldRepeat::Timing pages = abgui::HoldRepeat::pages();
    CHECK(pages.delay == 400);
    CHECK(pages.interval == 220);
    CHECK(pages.fastAfter == 1500);
    CHECK(pages.fastInterval == 110);

    // rows held from t=100, polled every millisecond: the first at 450, then every 80, every 30 from 1200 held
    abgui::HoldRepeat hold;
    hold.press(1, 100);
    std::vector<uint32_t> fired;
    for (uint32_t t = 100; t <= 1700; t++)
        if (hold.due(t) == 1)
            fired.push_back(t);
    const uint32_t first[] = {450, 530, 610, 690, 770, 850, 930, 1010, 1090, 1170, 1250, 1330};
    REQUIRE(fired.size() > 12);
    for (size_t i = 0; i < 12; i++)
        CHECK(fired[i] == first[i]);
    // 1330 held 1230: past the fast mark, so from the next call on every 30
    CHECK(fired[12] == 1360);
    CHECK(fired[13] == 1390);

    // a page (negative step): 400, then every 220
    abgui::HoldRepeat page;
    page.press(-5, 0, abgui::HoldRepeat::pages());
    CHECK(page.due(399) == 0);
    CHECK(page.due(400) == -5);
    CHECK(page.due(619) == 0);
    CHECK(page.due(620) == -5);
    CHECK(page.due(840) == -5);
}
