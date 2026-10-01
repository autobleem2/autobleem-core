// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::Screen (G3g of docs/ab-gui-plan.md, 7): the base of ab_gui's screens, on top of ableem::GuiScreen. A screen
// draws (draw()), the stack presents (render() is ScreenStack::frame(draw) and final), and its loop reads the events
// through the Context's ActionMap: each press or release of a mapped button or key reaches onAction(), anything else
// (a key no action is bound to, typed text, a device event) reaches onUnmapped().
//
// The default onAction() is the adapter under the old hooks: it calls the doCross_Pressed()/doJoyUp()/doEnter()-style
// hook ableem::GuiScreen::loop would have called for the same event, in the same order - so a screen moved onto this
// base behaves as it did until it overrides onAction() itself:
// - a pad button: the hook of the button its action belongs to (Confirm Cross, Back Circle, Option Triangle, Extra
//   Square, Menu Start, View Select, PrevTab L1, NextTab R1, PageUp L2, PageDown R2; First L1, Last R1 - the classic
//   lists' first/last row keys), pressed or released. With the default map and the swap off that is the button's own
//   hook; with the Confirm/Back swap on, Circle reaches doCross_Pressed().
// - the d-pad: doJoyUp/Down/Right/Left/Center by the live d-pad state, whichever direction the event was, on the press
//   and on the release (the old PadMapper's priority) - GuiScreen::dispatchDpad(). (The d-pad hooks follow the d-pad's
//   own state, so a button bound to a direction keeps its own button hook here.)
// - a key that reaches the screen as a key (the keyboard-as-pad is off - the typing screens - or the key is not one
//   the PC map turns into a pad button): its own hook, Enter doEnter, Esc doEscape, the arrows doKeyUp..., and nothing
//   on its release - the typing keys keep their meaning whatever their action is. (With the keyboard-as-pad on, Input
//   has already made a mapped key into the pad event, which comes here as a pad button.)
// The default onUnmapped() is GuiScreen::dispatchEvent(): the key's hook, doTextInput(), or nothing.
//
// Hold-repeat is the hooks' own, as before (fastForwardUntilAnotherEvent, HoldRepeat on the live d-pad state), and the
// busy rule is Input's (poll() and flushInputEvents()), so neither changes here. Since G3z (AB_SDK_ABI 7) every screen
// of AutoBleem is one: the classic GuiScreen shim derives from it, so a classic screen has draw() only and its frame is
// the stack's.
//
// What a screen does before its frame (a refresh when due, a busy state ended, the pad read) goes in prepareFrame(),
// outside the frame - so a busy frame it causes is a frame of its own, never one nested in the screen's; it returns
// false when that closed the screen and no frame is to be drawn. A screen that clears to another colour than opaque
// black (the launcher's carousel, the splash: transparent black) sets frameColor.
//
#pragma once

#include <ab_gui/actions.h>
#include <ab_gui/context.h>

#include <ableem/ui/gui_base.h>
#include <ableem/ui/gui_screen.h>

namespace abgui {

//********************
// Screen
//********************
class Screen : public ableem::GuiScreen {
public:
    // `gui` is the GuiBase the screen's input and platform are (ableem::GuiScreen's), `context` the program's
    // Context over the same GuiBase (AutoBleem: Gui::uiContext())
    Screen(ableem::GuiBase &gui, Context &context) : ableem::GuiScreen(gui), ctx(context) {}
    // the stack forgets what it declared (out of line: a screen built before it existed simply never declared)
    ~Screen() override;

    Context &ctx;

    // the screen's in and out transitions (screen_transition.h, the plan's 7a), kept by the Context's stack - declared
    // before show(), usually in the constructor. A screen that declares nothing cross-fades in and back
    void declareTransitions(const ScreenTransitions &transitions);

    // the screen's picture, between the stack's clear and its present - never clear() or present() here
    virtual void draw() = 0;
    // prepareFrame(), then one frame through the Context's stack (ScreenStack::frame(draw), cleared to frameColor when
    // it is set); without a stack, one on the renderer
    void render() final;
    // what the screen does before each frame, outside it; false = no frame this time (the screen closed in it)
    virtual bool prepareFrame() { return true; }
    // the colour the frame is cleared to; unset: opaque black (never the draw colour a last drawing left, BUG-31)
    OptionalColor frameColor;
    // ableem::GuiScreen::loop with the events through the ActionMap: until menuVisible goes false, every polled
    // event (a Quit closes the screen) to handle(), then a frame when the pacer says one is due
    void loop() override;

    // one event as the loop hands it out: through the Context's ActionMap to onAction() when it is a mapped
    // button's or key's press or release, else to onUnmapped()
    void handle(const ableem::Event &event);

    // a mapped press or release; the default is the adapter under the old hooks (legacyAction)
    virtual void onAction(const ActionEvent &action);
    // an event no action is bound to; the default is its old hook (GuiScreen::dispatchEvent)
    virtual void onUnmapped(const ableem::Event &event);

protected:
    // the old hook for `action`, as ableem::GuiScreen::loop called it (see the top of this file)
    void legacyAction(const ActionEvent &action);
};

// the pad button whose classic hooks an action reaches (Confirm Cross ... PageDown R2, First L1, Last R1);
// Button::None for None and the directions
ableem::Button classicButton(Action action);

} // namespace abgui
