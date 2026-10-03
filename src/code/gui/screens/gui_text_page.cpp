//
// GuiTextPage: static text in the classic layout - abgui::TextPage (docs/ab-gui-plan.md, G3h) as a classic screen.
//
#include "gui_text_page.h"
#include "../gui.h"

//*******************************
// GuiTextPage::prepareFrame
//*******************************
bool GuiTextPage::prepareFrame() {
    // the lines draw in the theme's `row` role when its launcher.colors sets one (a colour or the name of another),
    // else - as before, and on every theme that does not - in the classic theme's text colour
    const ableem::ThemeColorRole &row = app.theme().launcher().colors.row;
    if (row.color.set || !row.ref.empty())
        color = abgui::OptionalColor(PanelStyle::styleFromTheme(app.theme().launcher()).row);
    else
        color = abgui::OptionalColor(TextRenderer::toColor(app.theme().classic().textColor, 255));
    return true;
}
