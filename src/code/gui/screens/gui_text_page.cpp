//
// GuiTextPage: static text in the classic layout - abgui::TextPage (docs/ab-gui-plan.md, G3h) as a classic screen.
//
#include "gui_text_page.h"
#include "../gui.h"

//*******************************
// GuiTextPage::prepareFrame
//*******************************
bool GuiTextPage::prepareFrame() {
    color = abgui::OptionalColor(TextRenderer::toColor(app.theme().classic().textColor, 255));
    return true;
}
