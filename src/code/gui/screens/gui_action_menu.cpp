//
// GuiActionMenu: a compact panel of actions. The layout, the scrolling, the keys and the loop are ab_gui's
// abgui::ActionMenu (docs/ab-gui-plan.md, G3k); this class keeps its header - extensions construct it, built for ABI 6
// - and hands its items and state to one of those for every frame and for the loop.
//
#include "gui_action_menu.h"
#include "../gui.h"

#include <ab_gui/action_menu.h>

using namespace std;

namespace {

// an abgui::ActionMenu holding what the classic menu holds
void fill(abgui::ActionMenu &menu, const GuiActionMenu &classic, int firstVisible) {
    for (const GuiActionMenu::Item &item : classic.items) {
        abgui::ActionMenu::Item row;
        row.title = item.title;
        row.description = item.description;
        menu.items.push_back(row);
    }
    menu.title = classic.title;
    menu.subtitle = classic.subtitle;
    menu.crossLabel = classic.crossLabel;
    menu.circleLabel = classic.circleLabel;
    menu.result = classic.result;
    menu.selected = classic.selected;
    menu.background = classic.background;
    menu.restore(firstVisible);
}

} // namespace

//*******************************
// GuiActionMenu::init
//*******************************
void GuiActionMenu::init() {
    abgui::ActionMenu menu(*gui, gui->uiContext());
    fill(menu, *this, 0);
    menu.open(); // clamps the kept selection and scrolls it into view
    selected = menu.selected;
    firstVisible = menu.firstVisible();
    result = menu.result;
    style = gui->panelStyle();
}

//*******************************
// GuiActionMenu::render
//*******************************
// the frame through Gui's screen stack: clear, draw, present (docs/ab-gui-plan.md, G3c)
void GuiActionMenu::render() {
    abgui::ActionMenu menu(*gui, gui->uiContext());
    fill(menu, *this, firstVisible);
    menu.render();
}

//*******************************
// GuiActionMenu::loop
//*******************************
void GuiActionMenu::loop() {
    abgui::ActionMenu menu(*gui, gui->uiContext());
    fill(menu, *this, firstVisible);
    menu.loop();
    result = menu.result;
    selected = menu.selected;
    firstVisible = menu.firstVisible();
    menuVisible = false;
}
