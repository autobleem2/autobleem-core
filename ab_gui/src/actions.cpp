// SPDX-License-Identifier: GPL-3.0-or-later
#include <ab_gui/actions.h>

namespace abgui {

using ableem::Button;
using ableem::Event;
using ableem::Key;

ActionMap::ActionMap() {
    // the pad
    buttons_[Button::Cross] = Action::Confirm;
    buttons_[Button::Circle] = Action::Back;
    buttons_[Button::Triangle] = Action::Option;
    buttons_[Button::Square] = Action::Extra;
    buttons_[Button::Start] = Action::Menu;
    buttons_[Button::Select] = Action::View;
    buttons_[Button::L1] = Action::PrevTab;
    buttons_[Button::R1] = Action::NextTab;
    buttons_[Button::L2] = Action::PageUp;
    buttons_[Button::R2] = Action::PageDown;
    buttons_[Button::DpadUp] = Action::Up;
    buttons_[Button::DpadDown] = Action::Down;
    buttons_[Button::DpadLeft] = Action::Left;
    buttons_[Button::DpadRight] = Action::Right;
    // the keyboard, as ableem::KeyboardMap::toPad (the PC-style map) sends each key to a pad button
    keys_[Key::Up] = Action::Up;
    keys_[Key::Down] = Action::Down;
    keys_[Key::Left] = Action::Left;
    keys_[Key::Right] = Action::Right;
    keys_[Key::Return] = Action::Confirm;
    keys_[Key::Backspace] = Action::Back;
    keys_[Key::Escape] = Action::Back;
    keys_[Key::Tab] = Action::Option;
    keys_[Key::F1] = Action::View;
    keys_[Key::F2] = Action::Menu;
    keys_[Key::PageUp] = Action::PrevTab;
    keys_[Key::PageDown] = Action::NextTab;
    keys_[Key::Home] = Action::PageUp;
    keys_[Key::End] = Action::PageDown;
}

Action ActionMap::fromButton(Button button) const {
    const auto it = buttons_.find(button);
    if (it == buttons_.end())
        return Action::None;
    if (swap_) {
        if (it->second == Action::Confirm)
            return Action::Back;
        if (it->second == Action::Back)
            return Action::Confirm;
    }
    return it->second;
}

Action ActionMap::fromKey(Key key, int code) const {
    if (key == Key::Other)
        return code == ' ' ? space_ : Action::None;
    const auto it = keys_.find(key);
    return it == keys_.end() ? Action::None : it->second;
}

ActionEvent ActionMap::fromEvent(const Event &event) const {
    ActionEvent result;
    switch (event.type) {
    case Event::Type::ButtonDown:
    case Event::Type::DpadDown:
        result.action = fromButton(event.button);
        result.pressed = true;
        break;
    case Event::Type::ButtonUp:
    case Event::Type::DpadUp:
        result.action = fromButton(event.button);
        result.released = true;
        break;
    case Event::Type::KeyDown:
        result.action = fromKey(event.key, event.code);
        result.pressed = true;
        break;
    case Event::Type::KeyUp:
        result.action = fromKey(event.key, event.code);
        result.released = true;
        break;
    default:
        break;
    }
    if (result.action == Action::None)
        result.pressed = result.released = false;
    return result;
}

void ActionMap::bind(Button button, Action action) {
    if (action == Action::None)
        buttons_.erase(button);
    else
        buttons_[button] = action;
}

void ActionMap::bind(Key key, Action action) {
    if (action == Action::None)
        keys_.erase(key);
    else
        keys_[key] = action;
}

} // namespace abgui
