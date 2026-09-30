// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::Action and abgui::ActionMap (G3f of docs/ab-gui-plan.md, 5): screens react to what the player wants
// (confirm, go back, page down), not to which button or key it was. The ActionMap turns a pad button or a keyboard
// key into an action - the pad as it always was, the keyboard as the fallback - and holds the Confirm/Back swap
// switch (the Japanese and Nintendo layout, off by default).
//
// The default map is today's: the pad buttons as the classic screens read them, the keys as
// ableem::KeyboardMap::toPad turns them into pad buttons and those into actions, so what a key does does not change
// by going through an action. Nothing reads an ActionMap yet (G3g wires it); the screens' doCross_Pressed()-style
// hooks stay.
//
//   Cross Confirm    Circle Back      Triangle Option   Square Extra    Start Menu     Select View
//   L1 PrevTab       R1 NextTab       L2 PageUp         R2 PageDown     d-pad Up/Down/Left/Right
//   Enter Confirm    Backspace/Esc Back   Tab Option    Space Extra     F2 Menu        F1 View
//   PgUp PrevTab     PgDn NextTab     Home PageUp       End PageDown    arrows Up/Down/Left/Right
//
// First and Last have no button of their own today (the classic lists take L1/R1 for them where there are no tabs,
// which is the same L1/R1 as PrevTab/NextTab - the screen decides), so no button or key is bound to them by default;
// bind() gives them one.
//
// The swap exchanges Confirm and Back on the pad's buttons only: a keyboard's Enter and Esc keep their meaning.
//
#pragma once

#include <ableem/ui/input.h>

#include <map>

namespace abgui {

enum class Action {
    None, // nothing: the button or key means nothing to a screen
    Confirm,
    Back,
    Option, // Triangle
    Extra,  // Square
    Menu,   // Start
    View,   // Select
    PrevTab,
    NextTab,
    PageUp,
    PageDown,
    Up,
    Down,
    Left,
    Right,
    First,
    Last
};

// An event as an action: what it is, and whether it is the press or the release (a hold-repeat needs both).
struct ActionEvent {
    Action action = Action::None;
    bool pressed = false;  // a ButtonDown, DpadDown or KeyDown
    bool released = false; // a ButtonUp, DpadUp or KeyUp
    bool mapped() const { return action != Action::None; }
};

class ActionMap {
public:
    ActionMap(); // the default map

    // the action of a pad button (d-pad included), with the swap applied
    Action fromButton(ableem::Button button) const;
    // the action of a key; `code` is Event::code - Key::Other with the character ' ' is the Space bar
    Action fromKey(ableem::Key key, int code = 0) const;
    // the action of an event: ButtonDown/Up and DpadDown/Up by the button, KeyDown/Up by the key, anything else None
    ActionEvent fromEvent(const ableem::Event &event) const;

    // Confirm <-> Back on the pad's buttons (default off)
    void setSwapConfirmBack(bool swap) { swap_ = swap; }
    bool swapConfirmBack() const { return swap_; }

    // rebind a button or a key (Action::None unbinds it); the Space bar is bindSpace()
    void bind(ableem::Button button, Action action);
    void bind(ableem::Key key, Action action);
    void bindSpace(Action action) { space_ = action; }

private:
    std::map<ableem::Button, Action> buttons_;
    std::map<ableem::Key, Action> keys_;
    Action space_ = Action::Extra;
    bool swap_ = false;
};

} // namespace abgui
