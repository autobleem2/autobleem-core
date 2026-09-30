//
// GuiFactsPage: a read-only page of facts in the classic panel - sections with a heading band each and
// label/value rows under it, the values in a column a third of the way across (a long one elided), as many
// rows as the panel holds, scrolling a row at a time with edge markers, re-read every refreshInterval while
// the page is up. Up/Down a row, Left/Right and L2/R2 a page, Circle back; a subclass adds its own buttons
// through onButton() and their hints through extraHints(). Hardware Information (the launcher) and
// PSC-Bios's opening screen are the two.
//
// ab_gui's abgui::FactsPage (docs/ab-gui-plan.md, G3i) as a classic screen (G3z): a page overrides title(),
// collect() (abgui::FactsSection - sectionsOf() turns SystemInfoService's InfoSection into them), extraHints() and
// onButton(), and calls refresh() after a sub-screen that may have changed the facts. The rows are the theme's font.
//
#pragma once

#include <ab_gui/facts_page.h>

#include "../gui_screen.h"
#include "../../core/services/system_info.h"

#include <vector>

//********************
// GuiFactsPage
//********************
class GuiFactsPage : public ClassicScreen<abgui::FactsPage> {
public:
    explicit GuiFactsPage(ableem::GuiBase &_gui) : ClassicScreen<abgui::FactsPage>(_gui) {}

    // the theme's font, from the top, the rows read
    void init() override;

    // SystemInfoService's sections as the page reads them
    static std::vector<abgui::FactsSection> sectionsOf(const std::vector<InfoSection> &infos);
};
