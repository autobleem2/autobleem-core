//
// GuiActionMenu: a compact panel of actions, in the look of the launcher's system menu - a title, rows of
// a name over a line of description (PanelStyle::RowHeight each), as tall as the rows need and scrolling
// with edge markers past what fits, Up/Down (wrapping), Cross picks, Circle leaves. A tool's opening
// screen (ABFlashKit: flash, back up, restore); the caller fills `items`, shows it and reads `result`.
//
// ab_gui's abgui::ActionMenu (docs/ab-gui-plan.md, G3k) as a classic screen (G3z): `items`, `title`, `subtitle`,
// `crossLabel`/`circleLabel`, `selected` (kept across shows, so a menu reopens where it was), `background` (a screen
// an extension opens from the launcher passes the launcher's frame, gui.renderer().lastCapture(), so the panel reads
// as an overlay on it), `result` (-1 when left with Circle).
//
#pragma once

#include <ab_gui/action_menu.h>

#include "../gui_screen.h"

//********************
// GuiActionMenu
//********************
class GuiActionMenu : public ClassicScreen<abgui::ActionMenu> {
public:
    explicit GuiActionMenu(ableem::GuiBase &_gui) : ClassicScreen<abgui::ActionMenu>(_gui) {}

    // the kept selection clamped onto a row that can be picked and scrolled into view, `result` forgotten
    void init() override { open(); }
};
