//
// Created by screemer on 2019-01-24.
//
#pragma once

// The classic screens' base. Since G3z of docs/ab-gui-plan.md (AB_SDK_ABI 7) a classic screen is an abgui::Screen on
// Gui's abgui::Context (Gui::uiContext()): it reads the events through the Context's ActionMap (the default
// onAction() calls the old doCross_Pressed()-style hooks exactly as before), it has draw() only - its render() is the
// screen stack's frame, final - and what it does before a frame goes in prepareFrame(). ClassicScreen<Widget> adds the
// members every existing screen file expects to any ab_gui screen: a `gui` shared_ptr<Gui> (the app's
// theme/database-aware singleton, not just the abstract ableem::GuiBase), a bare `renderer` reference and the `app`
// model - so GuiScreen is ClassicScreen<abgui::Screen>, and the classic widgets (GuiConfirm, GuiTextPage, GuiKeyboard,
// GuiActionMenu, GuiFactsPage) are ClassicScreen<abgui::Confirm>... - the ab_gui widgets themselves, under their old
// names (the DebugDriver's screen names are the most derived class's, so they stay).
#include <ableem/ui/gui_screen.h>
#include <ab_gui/screen.h>
#include "gui.h"
#include "../app_base.h"

using ableem::Button;
using ableem::Event;
using ableem::Key;

//********************
// ClassicScreen
//********************
template <class Widget> class ClassicScreen : public Widget {
public:
    explicit ClassicScreen(ableem::GuiBase &_gui)
        : Widget(_gui, Gui::getInstance()->uiContext()), gui(Gui::getInstance()), renderer(_gui.renderer()),
          app(AppBase::get()) {}

    std::shared_ptr<Gui> gui;
    ableem::Renderer &renderer;
    // the model every classic screen has: app.config(), app.theme(), app.audio(), app.lang(). A screen
    // that needs AutoBleem's game model declares its own `App &app = App::get();` over this one (ab_ui)
    AppBase &app;
};

//********************
// GuiScreen
//********************
class GuiScreen : public ClassicScreen<abgui::Screen> {
public:
    explicit GuiScreen(ableem::GuiBase &_gui) : ClassicScreen<abgui::Screen>(_gui) {}
};
