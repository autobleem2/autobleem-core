//
// PanelStyle: the shared look of the menus and dialogs - an abgui::Style with AutoBleem's additions. See the header.
//
#include "panel_style.h"
#include "gui.h"
#include "text_renderer.h"

#include <ab_gui/context.h>

#include <ableem/engine/theme_spec.h>

#include <vector>

using namespace std;

namespace {
abgui::OptionalColor optionalColor(const ableem::ThemeColor &color) {
    return color.set ? abgui::OptionalColor(TextRenderer::toColor(color, 255)) : abgui::OptionalColor();
}

abgui::RoleColor role(const ableem::ThemeColorRole &themeRole) {
    abgui::RoleColor r;
    r.color = optionalColor(themeRole.color);
    r.ref = themeRole.ref;
    return r;
}
} // namespace

//*******************************
// PanelStyle::colorRoles / styleFromTheme / fromTheme
//*******************************
abgui::ColorRoles PanelStyle::colorRoles(const ableem::LauncherTheme &theme) {
    const ableem::LauncherTheme::Colors &c = theme.colors;
    abgui::ColorRoles roles;
    roles.text = optionalColor(c.text);
    roles.secondary = optionalColor(c.secondary);
    roles.hint = optionalColor(c.hint);
    roles.selection = optionalColor(c.selection);
    roles.row = role(c.row);
    roles.rowSelected = role(c.rowSelected);
    roles.heading = role(c.heading);
    roles.value = role(c.value);
    roles.description = role(c.description);
    roles.footer = role(c.footer);
    roles.selectionBand = role(c.selectionBand);
    roles.edge = role(c.edge);
    return roles;
}

abgui::Style PanelStyle::styleFromTheme(const ableem::LauncherTheme &theme) {
    abgui::Style s = abgui::Style::fromColors(colorRoles(theme));
    s.textShadow = !theme.textShadow.set || theme.textShadow;
    return s;
}

PanelStyle PanelStyle::fromTheme(const ableem::LauncherTheme &theme) {
    return fromStyle(styleFromTheme(theme));
}

//*******************************
// PanelStyle::fromStyle
//*******************************
PanelStyle PanelStyle::fromStyle(const abgui::Style &s) {
    PanelStyle p;
    static_cast<abgui::Style &>(p) = s;
    return p;
}

//*******************************
// the primitives on the Gui's Context
//*******************************
int PanelStyle::header(Gui &gui, const ableem::Rect &panel, const string &title) const {
    return header(gui.uiContext(), panel, title);
}

void PanelStyle::footer(Gui &gui, const ableem::Rect &footerRect, const vector<HintItem> &hints, const string &status,
                        bool withRule) const {
    footer(gui.uiContext(), footerRect, hints, status, withRule);
}

void PanelStyle::footer(Gui &gui, const ableem::Rect &footerRect, const string &line, bool withRule) const {
    footer(gui.uiContext(), footerRect, line, withRule);
}

int PanelStyle::button(Gui &gui, const string &key, int x, int y, int height) const {
    return button(gui.uiContext(), key, x, y, height);
}

int PanelStyle::buttonWidth(Gui &gui, const string &key, int height) const {
    return buttonWidth(gui.uiContext(), key, height);
}

int PanelStyle::buttons(Gui &gui, const string &markers, int x, int y, int height) const {
    return buttons(gui.uiContext(), markers, x, y, height);
}

int PanelStyle::buttonsWidth(Gui &gui, const string &markers, int height) const {
    return buttonsWidth(gui.uiContext(), markers, height);
}
