//
// GuiActionMenu: a compact panel of actions, in the look of the launcher's system menu - a title, rows of
// a name over a line of description (PanelStyle::RowHeight each), as tall as the rows need and scrolling
// with edge markers past what fits, Up/Down (wrapping), Cross picks, Circle leaves. A tool's opening
// screen (ABFlashKit: flash, back up, restore); the caller fills `items`, shows it and reads `result`.
//
#pragma once

#include "../gui_screen.h"
#include "../panel_style.h"

#include <string>
#include <vector>

//********************
// GuiActionMenu
//********************
class GuiActionMenu : public GuiScreen {
public:
    using GuiScreen::GuiScreen;

    struct Item {
        std::string title;
        std::string description;
    };
    std::vector<Item> items;
    std::string title;
    std::string subtitle;                // at the header's right, in the secondary colour (a version)
    std::string crossLabel, circleLabel; // the footer's two hints; empty = "Select" / "Back"
    int result = -1;                     // the index picked, -1 when left with Circle
    int selected = 0;                    // kept across shows, so a menu reopens where it was
    // drawn full-screen and dimmed under the panel instead of the theme's background when set: a screen
    // an extension opens from the launcher passes the launcher's frame, gui.renderer().lastCapture() (the
    // launcher captures it right before it runs an extension), so the panel reads as an overlay on it
    ableem::Texture background;

    void init() override;
    void render() override;
    void loop() override;

private:
    int firstVisible = 0;
    PanelStyle style;
    int visibleRows() const;
    void moveSelection(int step);
};
