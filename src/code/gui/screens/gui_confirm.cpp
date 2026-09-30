//
// Created by screemer on 2019-01-24.
//
// GuiConfirm: a yes/no question in a compact dialog. The layout, the keys and the loop are ab_gui's abgui::Confirm
// (docs/ab-gui-plan.md, G3j); this class keeps its header - the Store extension constructs it, built for ABI 6 - and
// hands its question and answers to one of those for every frame and for the loop.
//

#include "gui_confirm.h"
#include "../gui.h"

#include <ab_gui/confirm.h>

using namespace std;

namespace {

// an abgui::Confirm holding what the classic dialog holds
void fill(abgui::Confirm &dialog, const GuiConfirm &classic) {
    dialog.label = classic.label;
    dialog.title = classic.title;
    dialog.confirmLabel = classic.confirmLabel;
    dialog.cancelLabel = classic.cancelLabel;
    dialog.result = classic.result;
}

} // namespace

//*******************************
// GuiConfirm::render
//*******************************
// the frame through Gui's screen stack (docs/ab-gui-plan.md, G3c): clear, draw, present
void GuiConfirm::render() {
    abgui::Confirm dialog(*gui, gui->uiContext());
    fill(dialog, *this);
    dialog.render();
}

//*******************************
// GuiConfirm::loop
//*******************************
void GuiConfirm::loop() {
    abgui::Confirm dialog(*gui, gui->uiContext());
    fill(dialog, *this);
    menuVisible = true;
    dialog.loop();
    result = dialog.result;
    menuVisible = false;
}
