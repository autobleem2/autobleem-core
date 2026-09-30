//
// GuiTextPage: static text in the classic layout. The layout, the scroll and the keys are ab_gui's
// abgui::TextPage (docs/ab-gui-plan.md, G3h); this class keeps its header - extensions built for ABI 6 share it -
// and hands its title and lines to one of those for every frame and for the loop.
//
#include "gui_text_page.h"
#include "../gui.h"

#include <ab_gui/text_page.h>

using namespace std;

namespace {

// an abgui::TextPage holding what the classic page holds
void fill(abgui::TextPage &page, const GuiTextPage &classic, AppBase &app) {
    page.title = classic.title;
    page.lines = classic.lines;
    page.centred = classic.centred;
    page.color = abgui::OptionalColor(TextRenderer::toColor(app.theme().classic().textColor, 255));
}

} // namespace

//*******************************
// GuiTextPage::splitItem
//*******************************
GuiTextPage::Item GuiTextPage::splitItem(const string &line) {
    const abgui::TextPage::Item split = abgui::TextPage::splitItem(line);
    Item item;
    item.indent = split.indent;
    item.marker = split.marker;
    item.text = split.text;
    return item;
}

//*******************************
// GuiTextPage::render
//*******************************
// the first frame (show() draws one before the loop): the page as the caller set it, from the top
void GuiTextPage::render() {
    abgui::TextPage page(*gui, gui->uiContext());
    fill(page, *this, app);
    page.render();
}

//*******************************
// GuiTextPage::loop
//*******************************
void GuiTextPage::loop() {
    abgui::TextPage page(*gui, gui->uiContext());
    fill(page, *this, app);
    page.loop();
}
