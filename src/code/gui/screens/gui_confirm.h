//
// Created by screemer on 2019-01-24.
//
// GuiConfirm: a yes/no question in a compact dialog - ab_gui's abgui::Confirm (docs/ab-gui-plan.md, G3j) as a classic
// screen (G3z): `label`, `title`, `confirmLabel`/`cancelLabel`, show(), then `result`.
//
#pragma once

#include <ab_gui/confirm.h>

#include "../gui_screen.h"

//********************
// GuiConfirm
//********************
class GuiConfirm : public ClassicScreen<abgui::Confirm> {
public:
    explicit GuiConfirm(ableem::GuiBase &_gui) : ClassicScreen<abgui::Confirm>(_gui) {}
};
