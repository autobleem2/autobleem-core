// SPDX-License-Identifier: GPL-3.0-or-later
#include <ab_gui/screen.h>

namespace abgui {

using ableem::Button;
using ableem::Event;

namespace {

bool isDirection(Action action) {
    return action == Action::Up || action == Action::Down || action == Action::Left || action == Action::Right;
}

} // namespace

Button classicButton(Action action) {
    switch (action) {
    case Action::Confirm:
        return Button::Cross;
    case Action::Back:
        return Button::Circle;
    case Action::Option:
        return Button::Triangle;
    case Action::Extra:
        return Button::Square;
    case Action::Menu:
        return Button::Start;
    case Action::View:
        return Button::Select;
    case Action::PrevTab:
    case Action::First:
        return Button::L1;
    case Action::NextTab:
    case Action::Last:
        return Button::R1;
    case Action::PageUp:
        return Button::L2;
    case Action::PageDown:
        return Button::R2;
    default:
        return Button::None;
    }
}

//********************
// Screen::render
//********************
void Screen::render() {
    const ScreenStack::Draw drawing = [this] { draw(); };
    if (ctx.hasStack()) {
        ctx.stack().frame(drawing);
        return;
    }
    ScreenStack own(ctx.renderer()); // a Context without a stack (a tool's, a test's): the same frame on the renderer
    own.frame(drawing);
}

//********************
// Screen::loop
//********************
// ableem::GuiScreen::loop, but every event through handle()
void Screen::loop() {
    menuVisible = true;
    ableem::Input &input = gui.input();

    while (menuVisible) {
        Event e;
        while (input.poll(e)) {
            if (handleQuit(e))
                continue;
            handle(e);
        }
        if (input.frameDue()) // the pacer: every pass unless the screen said it rests
            render();
    }
}

//********************
// Screen::handle
//********************
void Screen::handle(const Event &event) {
    const ActionEvent action = ctx.actions.fromEvent(event);
    if (action.mapped())
        onAction(action);
    else
        onUnmapped(event);
}

void Screen::onAction(const ActionEvent &action) {
    legacyAction(action);
}

void Screen::onUnmapped(const Event &event) {
    dispatchEvent(event);
}

//********************
// Screen::legacyAction
//********************
// The adapter under the old hooks: what ableem::GuiScreen::loop called for the same event.
void Screen::legacyAction(const ActionEvent &action) {
    const Event &e = action.event;
    switch (e.type) {
    case Event::Type::ButtonDown:
    case Event::Type::ButtonUp:
    case Event::Type::DpadDown:
    case Event::Type::DpadUp:
        // the d-pad hooks follow the d-pad's own live state, not the action: a direction is the old dispatch of the
        // event (the d-pad's priority for a d-pad event; for a button event its own hook, if it has one)
        if (isDirection(action.action)) {
            dispatchEvent(e);
            return;
        }
        // any other action: the hook of the button it belongs to (the button's own with the default map)
        dispatchButton(classicButton(action.action), action.pressed);
        return;
    default:
        // a key (KeyDown/KeyUp): its own hook on the press, none on the release - Enter stays doEnter, Esc doEscape
        dispatchEvent(e);
        return;
    }
}

} // namespace abgui
