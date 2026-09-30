//
// PanelStyle: the shared look of the menus and dialogs - AutoBleem's adapter over abgui::Style. See the header.
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

vector<abgui::HintItem> toStyleHints(const vector<PanelStyle::HintItem> &hints) {
    vector<abgui::HintItem> out;
    out.reserve(hints.size());
    for (const PanelStyle::HintItem &h : hints)
        out.push_back({h.icons, h.label});
    return out;
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
// PanelStyle::style / fromStyle
//*******************************
abgui::Style PanelStyle::style() const {
    abgui::Style s;
    s.text = text;
    s.secondary = secondary;
    s.hint = hint;
    s.textShadow = textShadow;
    s.row = row;
    s.rowSelected = rowSelected;
    s.heading = heading;
    s.value = value;
    s.description = description;
    s.footerText = footerText;
    s.selectionBand = selectionBand;
    s.edge = edge;
    return s;
}

PanelStyle PanelStyle::fromStyle(const abgui::Style &s) {
    PanelStyle p;
    p.text = s.text;
    p.secondary = s.secondary;
    p.hint = s.hint;
    p.textShadow = s.textShadow;
    p.row = s.row;
    p.rowSelected = s.rowSelected;
    p.heading = s.heading;
    p.value = s.value;
    p.description = s.description;
    p.footerText = s.footerText;
    p.selectionBand = s.selectionBand;
    p.edge = s.edge;
    return p;
}

//*******************************
// PanelStyle::outlineOf
//*******************************
ableem::Texture PanelStyle::outlineOf(ableem::Renderer &renderer, const ableem::Image &image) {
    return abgui::Style::outlineOf(renderer, image);
}

//*******************************
// the primitives: forwarded to abgui::Style
//*******************************
void PanelStyle::dim(ableem::Renderer &renderer) const {
    style().dim(renderer);
}

void PanelStyle::sheet(ableem::Renderer &renderer, const ableem::Rect &panel) const {
    style().sheet(renderer, panel);
}

void PanelStyle::rule(ableem::Renderer &renderer, const ableem::Rect &panel, int y) const {
    style().rule(renderer, panel, y);
}

int PanelStyle::header(Gui &gui, const ableem::Rect &panel, const string &title) const {
    return style().header(gui.uiContext(), panel, title);
}

void PanelStyle::selection(ableem::Renderer &renderer, const ableem::Rect &rect) const {
    style().selection(renderer, rect);
}

void PanelStyle::label(ableem::Renderer &renderer, const ableem::Rect &rect) const {
    style().label(renderer, rect);
}

void PanelStyle::disabled(ableem::Renderer &renderer, const ableem::Rect &rect) const {
    style().disabled(renderer, rect);
}

void PanelStyle::scrollMarker(ableem::Renderer &renderer, int cx, int cy, int direction) const {
    style().scrollMarker(renderer, cx, cy, direction);
}

void PanelStyle::box(ableem::Renderer &renderer, const ableem::Rect &rect, abgui::Tone fill, int fillAlpha,
                     abgui::Tone edgeTone, int edgeAlpha) const {
    style().box(renderer, rect, fill, fillAlpha, edgeTone, edgeAlpha);
}

void PanelStyle::plate(ableem::Renderer &renderer, const ableem::Rect &rect, const ableem::Color &color) const {
    style().plate(renderer, rect, color);
}

void PanelStyle::key(ableem::Renderer &renderer, const ableem::Rect &rect, abgui::KeyState state,
                     bool function) const {
    style().key(renderer, rect, state, function);
}

void PanelStyle::field(ableem::Renderer &renderer, const ableem::Rect &rect) const {
    style().field(renderer, rect);
}

void PanelStyle::caret(ableem::Renderer &renderer, int x, int y, int height) const {
    style().caret(renderer, x, y, height);
}

void PanelStyle::progress(ableem::Renderer &renderer, const ableem::Rect &track, unsigned long long done,
                          unsigned long long total, abgui::Tone trackTone, int trackAlpha, abgui::Tone fillTone,
                          int fillAlpha) const {
    style().progress(renderer, track, done, total, trackTone, trackAlpha, fillTone, fillAlpha);
}

void PanelStyle::spinner(ableem::Renderer &renderer, int cx, int cy, int radius, int dot, int lead) const {
    style().spinner(renderer, cx, cy, radius, dot, lead);
}

void PanelStyle::spinner(ableem::Renderer &renderer, const ableem::Rect &box, int lead) const {
    style().spinner(renderer, box, lead);
}

void PanelStyle::tab(ableem::Renderer &renderer, int x, int y, int w) const {
    style().tab(renderer, x, y, w);
}

void PanelStyle::vrule(ableem::Renderer &renderer, int x, int y, int h, int alpha) const {
    style().vrule(renderer, x, y, h, alpha);
}

//*******************************
// PanelStyle::parseHints / footer
//*******************************
vector<PanelStyle::HintItem> PanelStyle::parseHints(const string &line, string &status) {
    vector<HintItem> items;
    for (const abgui::HintItem &h : abgui::Style::parseHints(line, status))
        items.push_back({h.icons, h.label});
    return items;
}

void PanelStyle::footer(Gui &gui, const ableem::Rect &footerRect, const vector<HintItem> &hints, const string &status,
                        bool withRule) const {
    style().footer(gui.uiContext(), footerRect, toStyleHints(hints), status, withRule);
}

void PanelStyle::footer(Gui &gui, const ableem::Rect &footerRect, const string &line, bool withRule) const {
    style().footer(gui.uiContext(), footerRect, line, withRule);
}

//*******************************
// PanelStyle::button / buttons
//*******************************
int PanelStyle::button(Gui &gui, const string &key, int x, int y, int height) const {
    return style().button(gui.uiContext(), key, x, y, height);
}

int PanelStyle::buttonWidth(Gui &gui, const string &key, int height) const {
    return style().buttonWidth(gui.uiContext(), key, height);
}

int PanelStyle::buttons(Gui &gui, const string &markers, int x, int y, int height) const {
    return style().buttons(gui.uiContext(), markers, x, y, height);
}

int PanelStyle::buttonsWidth(Gui &gui, const string &markers, int height) const {
    return style().buttonsWidth(gui.uiContext(), markers, height);
}

int PanelStyle::layoutButtons(Gui &gui, const string &markers, int x, int y, int height, bool draw) const {
    return draw ? buttons(gui, markers, x, y, height) : buttonsWidth(gui, markers, height);
}
