//
// GuiTextPage: a titled page of static text in the shared panel look - what a tool's instructions or an
// info box are. Longer than the panel, it scrolls: Up/Down a line, L2/R2 a page. Circle (or Escape) goes
// back; the caller sets `title` and `lines` and calls show().
//
// ab_gui's abgui::TextPage (docs/ab-gui-plan.md, G3h) as a classic screen (G3z): `title`, `lines`, `centred`,
// `splitItem`; the lines draw in the classic theme's text colour.
//
#pragma once

#include <ab_gui/text_page.h>

#include "../gui_screen.h"

//********************
// GuiTextPage
//********************
class GuiTextPage : public ClassicScreen<abgui::TextPage> {
public:
    explicit GuiTextPage(ableem::GuiBase &_gui) : ClassicScreen<abgui::TextPage>(_gui) {}

    // shown again, the page starts from its first line
    void init() override { firstLine_ = 0; }
    // the lines' colour: the classic theme's text colour, as it is when the frame is drawn
    bool prepareFrame() override;
};
