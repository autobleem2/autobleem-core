//
// GuiFactsPage: a read-only page of facts in the classic panel - abgui::FactsPage (docs/ab-gui-plan.md, G3i) as a
// classic screen (G3z). See the header.
//
#include "gui_facts_page.h"
#include "../gui.h"

using namespace std;

//*******************************
// GuiFactsPage::sectionsOf
//*******************************
vector<abgui::FactsSection> GuiFactsPage::sectionsOf(const vector<InfoSection> &infos) {
    vector<abgui::FactsSection> sections;
    for (const InfoSection &info : infos) {
        abgui::FactsSection section;
        section.title = info.title;
        for (const InfoRow &row : info.rows)
            section.rows.push_back({row.label, row.value});
        sections.push_back(section);
    }
    return sections;
}

//*******************************
// GuiFactsPage::init
//*******************************
void GuiFactsPage::init() {
    font = gui->assets().themeFont;
    open();
}
