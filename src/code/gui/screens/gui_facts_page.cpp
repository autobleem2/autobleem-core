//
// GuiFactsPage: a read-only page of facts in the classic panel. The layout, the paging, the refresh rule and the keys
// are ab_gui's abgui::FactsPage (docs/ab-gui-plan.md, G3i); this class keeps its header - PSC-Bios's opening screen
// derives from it, built for ABI 6 - and hands its rows and its hooks to one of those for every frame and for the loop.
//
#include "gui_facts_page.h"
#include "../gui.h"

#include <ab_gui/facts_page.h>

#include <algorithm>
#include <functional>

using namespace std;

namespace {

// the sections a classic page collects, as the abgui page reads them
vector<abgui::FactsSection> sectionsOf(const vector<InfoSection> &infos) {
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

// an abgui::FactsPage whose hooks are the classic page's (protected there, so they come in as functions)
class Forward : public abgui::FactsPage {
public:
    Forward(ableem::GuiBase &gui, abgui::Context &ctx) : abgui::FactsPage(gui, ctx) {}

    function<string()> titleOf;
    function<vector<InfoSection>()> collectOf;
    function<string()> extraHintsOf;
    function<bool(ableem::Button)> onButtonOf;

protected:
    string title() override { return titleOf(); }
    vector<abgui::FactsSection> collect() override { return sectionsOf(collectOf()); }
    string extraHints() override { return extraHintsOf(); }
    bool onButton(ableem::Button button) override { return onButtonOf(button); }
};

// the classic page's rows as the abgui page keeps them, and back
template <class ClassicLine> vector<abgui::FactsPage::Line> toRows(const vector<ClassicLine> &lines) {
    vector<abgui::FactsPage::Line> rows;
    for (const ClassicLine &line : lines) {
        abgui::FactsPage::Line row;
        row.heading = line.heading;
        row.label = line.label;
        row.value = line.value;
        rows.push_back(row);
    }
    return rows;
}

template <class ClassicLine> vector<ClassicLine> fromRows(const vector<abgui::FactsPage::Line> &rows) {
    vector<ClassicLine> lines;
    for (const abgui::FactsPage::Line &row : rows) {
        ClassicLine line;
        line.heading = row.heading;
        line.label = row.label;
        line.value = row.value;
        lines.push_back(line);
    }
    return lines;
}

} // namespace

//*******************************
// GuiFactsPage::init
//*******************************
void GuiFactsPage::init() {
    font = gui->assets().themeFont;
    firstVisible = 0;
    refresh();
}

//*******************************
// GuiFactsPage::refresh
//*******************************
void GuiFactsPage::refresh() {
    lastRefresh = gui->platform().ticks();
    lines = fromRows<Line>(abgui::FactsPage::linesOf(sectionsOf(collect())));
    firstVisible = min(firstVisible, maxFirstVisible());
}

//*******************************
// GuiFactsPage::maxFirstVisible
//*******************************
int GuiFactsPage::maxFirstVisible() const {
    return abgui::FactsPage::maxFirstVisible(static_cast<int>(lines.size()), rowsThatFit);
}

//*******************************
// GuiFactsPage::render
//*******************************
// one frame of the page as it stands (show() draws one before the loop); the rows the panel held is kept for the
// next refresh and the loop
void GuiFactsPage::render() {
    Forward page(*gui, gui->uiContext());
    page.titleOf = [this]() { return title(); };
    page.collectOf = [this]() { return collect(); };
    page.extraHintsOf = [this]() { return extraHints(); };
    page.onButtonOf = [this](ableem::Button button) { return onButton(button); };
    page.font = font;
    page.refreshInterval = refreshInterval;
    page.restore(toRows(lines), firstVisible, rowsThatFit, lastRefresh);
    page.render();
    firstVisible = page.firstVisible();
    rowsThatFit = page.rowsThatFit();
}

//*******************************
// GuiFactsPage::loop
//*******************************
void GuiFactsPage::loop() {
    Forward page(*gui, gui->uiContext());
    page.titleOf = [this]() { return title(); };
    page.collectOf = [this]() { return collect(); };
    page.extraHintsOf = [this]() { return extraHints(); };
    page.onButtonOf = [this](ableem::Button button) { return onButton(button); };
    page.font = font;
    page.refreshInterval = refreshInterval;
    page.restore(toRows(lines), firstVisible, rowsThatFit, lastRefresh);
    page.loop();
    lines = fromRows<Line>(page.lines());
    firstVisible = page.firstVisible();
    rowsThatFit = page.rowsThatFit();
    lastRefresh = page.lastRefresh();
    menuVisible = page.menuVisible;
}
