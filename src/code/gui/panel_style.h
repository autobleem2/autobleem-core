//
// PanelStyle: the one look every menu and dialog shares - a dark sheet with a one-pixel edge in the
// launcher's secondary colour over the dimmed screen it came from, a bold header ruled off from the rows, the
// selected row as a translucent band with a bar at its left edge, and the launcher's button hints in the
// footer. The L2+R2 system menu and the update screens drew it first (2026-09); the classic screens took it
// on 2026-09-21 through Gui::renderTextBar/renderHeader/renderStatus and TextRenderer::renderSelectionBox.
//
// Since G2 (docs/ab-gui-plan.md) the look itself is ab_gui's abgui::Style, and since G3z (AB_SDK_ABI 7) PanelStyle
// is one: an abgui::Style (the colour roles, the metrics, every primitive on a Renderer or a Context) with AutoBleem's
// additions - the theme's launcher.colors turned into it (fromTheme), today's geometry as the old constants, and the
// primitives that need the program's text and glyphs taking the Gui (drawn with its abgui::Context,
// Gui::uiContext()). A footer hint is abgui::HintItem.
//
#pragma once

#include <ab_gui/style.h>

#include <ableem/ui/renderer.h>
#include <ableem/ui/texture.h>

#include <string>
#include <utility>
#include <vector>

class Gui;
namespace ableem {
struct LauncherTheme;
}
class ThemeAssets;

class PanelStyle : public abgui::Style {
public:
    // the geometry the system menu set - abgui::Style's defaults
    static const int HeaderHeight = abgui::Style::DefaultHeaderHeight; // the title's band, the rule 8 px above its end
    static const int FooterHeight = abgui::Style::DefaultFooterHeight; // the hints' band
    static const int RowHeight = abgui::Style::DefaultRowHeight;       // a title-and-description row
    static const int RowInset = abgui::Style::DefaultRowInset;         // the text from the panel's edge
    static const int Margin = abgui::Style::DefaultMargin;             // the panel from the screen's edge
    static const int SelectionBar = abgui::Style::DefaultSelectionBar; // the bar at the selected row's left edge

    // the launcher theme's colours, resolved the way GuiLauncher resolves them (white / grey where the theme
    // says nothing, the hint colour falling back to the secondary one)
    static PanelStyle fromTheme(const ableem::LauncherTheme &theme);
    // the same as ab_gui's types: the theme's launcher.colors as Style's colour roles, and the Style built from
    // them (textShadow from launcher.textShadow) - what Gui's abgui::Context hands its widgets
    static abgui::ColorRoles colorRoles(const ableem::LauncherTheme &theme);
    static abgui::Style styleFromTheme(const ableem::LauncherTheme &theme);
    // the colour a frame's `tint` names when it is a launcher.colors colour the Style has no role for (G5k: `selection`,
    // the cover glow's - white when the theme sets none, as the procedural glow is); false for any other name
    static bool frameTintColor(const ableem::LauncherTheme &theme, const std::string &name, ableem::Color &out);
    // this PanelStyle as the plain Style it is, and back
    abgui::Style style() const { return *this; }
    static PanelStyle fromStyle(const abgui::Style &style);

    // a footer hint: one or more button icons ("X", "O", "T", "S", "Start", "Select", "L1", "R1", "L2", "R2",
    // "Esc", "Enter", "Tab" - the launcher's hint icons for the first three, the theme's button textures
    // for the rest) and its label
    using HintItem = abgui::HintItem;

    // the primitives that draw text or glyphs, on the program's Context (Gui::uiContext()); the Style's own
    // (a Context's) stay reachable beside them
    using abgui::Style::button;
    using abgui::Style::buttons;
    using abgui::Style::buttonsWidth;
    using abgui::Style::buttonWidth;
    using abgui::Style::footer;
    using abgui::Style::header;

    // the bold title at the top of the panel and the rule under it; returns the y the rows start at
    int header(Gui &gui, const ableem::Rect &panel, const std::string &title) const;
    // the footer: the rule along the top of `footer` (FooterHeight tall, the panel's width), the hints from
    // the left inset in the largest of the launcher's fonts they fit in, the status at the right edge. The
    // hints are drawn in the one order every screen shares, whatever order they were given in: Cross,
    // Circle, Triangle, Square, Start, Select, L1/R1, L2/R2, then the keyboard's keys - so a footer reads
    // "what Cross does, how to get out, then the rest" on every screen (the status-line protocol: parseHints)
    void footer(Gui &gui, const ableem::Rect &footer, const std::vector<HintItem> &hints,
                const std::string &status = "", bool withRule = true) const;
    // the same from the protocol string
    void footer(Gui &gui, const ableem::Rect &footer, const std::string &line, bool withRule = true) const;

    // one button as the footer draws it at (x, y), `height` tall: the face buttons (X, O, T, S) as the
    // theme's 30 px images, every named button (Start, Select, L1..R2, Esc, Enter, Tab, or any word such as
    // RESET) as a chip - a small dark box with a light edge and the name in small bold capitals - so a
    // "START" reads at the size of the icons next to it. Returns the width drawn.
    int button(Gui &gui, const std::string &key, int x, int y, int height = 30) const;
    // a marker string as the button guide writes it - "|@L2| + |@Select|", "|@X| / |@O|", "RESET" - drawn as
    // icons, chips and the text between them; returns the width
    int buttons(Gui &gui, const std::string &markers, int x, int y, int height = 30) const;
    // the width the two above would draw, without drawing - for laying a row out first
    int buttonWidth(Gui &gui, const std::string &key, int height = 30) const;
    int buttonsWidth(Gui &gui, const std::string &markers, int height = 30) const;
};
