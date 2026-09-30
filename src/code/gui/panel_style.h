//
// PanelStyle: the one look every menu and dialog shares - a dark sheet with a one-pixel edge in the
// launcher's secondary colour over the dimmed screen it came from, a bold header ruled off from the rows, the
// selected row as a translucent band with a bar at its left edge, and the launcher's button hints in the
// footer. The L2+R2 system menu and the update screens drew it first (2026-09); the classic screens took it
// on 2026-09-21 through Gui::renderTextBar/renderHeader/renderStatus and TextRenderer::renderSelectionBox.
//
// Since G2 (docs/ab-gui-plan.md) the look itself is ab_gui's abgui::Style and PanelStyle is AutoBleem's thin
// adapter over it: the same public API as before (the extensions call it), the theme's launcher.colors turned
// into Style's colour roles, and every drawing call forwarded to Style with the abgui::Context that Gui owns
// (Gui::uiContext()). Its data layout is unchanged on purpose - an extension holds a PanelStyle by value -
// so it copies its colours into a Style per call instead of deriving from one; that goes at the next ABI bump.
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

class PanelStyle {
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
    // this PanelStyle's colours in a Style with today's metrics, and back
    abgui::Style style() const;
    static PanelStyle fromStyle(const abgui::Style &style);

    ableem::Color text{255, 255, 255, 255};
    ableem::Color secondary{100, 100, 100, 255};
    ableem::Color hint{100, 100, 100, 255};
    bool textShadow = true; // the launcher's halo under every text on the panel

    // The style roles (UIREV-29), launcher.colors' role keys resolved (a colour, a name of another colour in
    // the block, else the fallback). Every row, heading, value and description of every menu, list and
    // dialog draws in one of these - change one in theme.json and every window follows. Appended after
    // the members above on purpose: an extension holds a PanelStyle by value (AB_SDK_ABI 6).
    ableem::Color row{100, 100, 100, 255};           // an unselected row's text (secondary)
    ableem::Color rowSelected{255, 255, 255, 255};   // the selected row's text and value (text)
    ableem::Color heading{100, 100, 100, 255};       // the text on a heading band (secondary)
    ableem::Color value{100, 100, 100, 255};         // an unselected row's right-hand value (row)
    ableem::Color description{100, 100, 100, 255};   // second lines, subtitles, the strip, counters (secondary)
    ableem::Color footerText{255, 255, 255, 255};    // the footer's hint labels, key `footer` (text)
    ableem::Color selectionBand{255, 255, 255, 255}; // the selected row's band and bar (text)
    ableem::Color edge{100, 100, 100, 255};          // the sheet's edge, rules, the heading band (secondary)

    // a row's text / its value, selected or not
    const ableem::Color &rowColor(bool selected) const { return selected ? rowSelected : row; }
    const ableem::Color &valueColor(bool selected) const { return selected ? rowSelected : value; }

    // the screen behind the panel, darkened
    void dim(ableem::Renderer &renderer) const;
    // the sheet and its edge
    void sheet(ableem::Renderer &renderer, const ableem::Rect &panel) const;
    // a one-pixel rule across the panel, inset, at y
    void rule(ableem::Renderer &renderer, const ableem::Rect &panel, int y) const;
    // the bold title at the top of the panel and the rule under it; returns the y the rows start at
    int header(Gui &gui, const ableem::Rect &panel, const std::string &title) const;
    // the selected row: the band and the bar, `rect` being the row's full extent
    void selection(ableem::Renderer &renderer, const ableem::Rect &rect) const;
    // a row that cannot be changed (a locked setting): drawn over the row once it is drawn, the sheet's
    // black laid over it again so label, value and switch all fall back behind the rows around it
    void disabled(ableem::Renderer &renderer, const ableem::Rect &rect) const;
    // a heading row (a label between the rows): a faint band in the secondary colour
    void label(ableem::Renderer &renderer, const ableem::Rect &rect) const;
    // a small triangle at (cx, cy) pointing up (direction -1) or down (1): more rows that way
    void scrollMarker(ableem::Renderer &renderer, int cx, int cy, int direction) const;
    // a footer hint: one or more button icons ("X", "O", "T", "S", "Start", "Select", "L1", "R1", "L2", "R2",
    // "Esc", "Enter", "Tab" - the launcher's hint icons for the first three, the theme's button textures
    // for the rest) and its label
    struct HintItem {
        std::vector<std::string> icons;
        std::string label;
    };
    // the status-line protocol every screen writes - "Card 1/12   |@L1|/|@R1| Page  |@X| Rename  |@O| Go back |"
    // - taken apart: the text before the first marker is the status (a counter, drawn at the footer's right
    // edge), each marker and the text up to the next one is a hint, a marker whose text is empty or a
    // separator ("/", "|") joins the next hint's icons
    static std::vector<HintItem> parseHints(const std::string &line, std::string &status);
    // the footer: the rule along the top of `footer` (FooterHeight tall, the panel's width), the hints from
    // the left inset in the largest of the launcher's fonts they fit in, the status at the right edge. The
    // hints are drawn in the one order every screen shares, whatever order they were given in: Cross,
    // Circle, Triangle, Square, Start, Select, L1/R1, L2/R2, then the keyboard's keys - so a footer reads
    // "what Cross does, how to get out, then the rest" on every screen
    void footer(Gui &gui, const ableem::Rect &footer, const std::vector<HintItem> &hints,
                const std::string &status = "", bool withRule = true) const;
    // the same from the protocol string
    void footer(Gui &gui, const ableem::Rect &footer, const std::string &line, bool withRule = true) const;

    // a dark outline/halo texture from an image's own alpha shape: the shape drawn in black at alpha 150 at
    // each of the eight 1 px offsets and once more 2 px down-right, composited as TextRenderer's text halo
    // is - what keeps the launcher's Play button and (since UIREV-2/UIREV-27) the d-pad hint arrows and the
    // meta icons readable over a light background. The texture is the image's size plus 5 in each dimension,
    // the image's own shape sitting at (2, 2) in it; draw it at the icon's rect expanded by (-2, -2, +5, +5).
    // An invalid image (or one `Renderer::createStreaming` cannot back) gives an invalid texture back.
    static ableem::Texture outlineOf(ableem::Renderer &renderer, const ableem::Image &image);

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

private:
    int layoutButtons(Gui &gui, const std::string &markers, int x, int y, int height, bool draw) const;
};
