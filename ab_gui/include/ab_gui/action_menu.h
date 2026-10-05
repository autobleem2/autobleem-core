// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::ActionMenu (G3k of docs/ab-gui-plan.md): a compact panel of actions in the look of the launcher's system menu
// - a title (and a subtitle at its right, in the secondary colour), rows of a name over a line of description, as tall
// as the rows need and scrolling with edge markers past what fits, and the two footer hints (default "Select" /
// "Back"). A tool's opening screen, the Quick and System menus' panel. The caller fills `items`, runs the screen
// (loop()) and reads `result`: the index picked, or -1 when it was left with Back or the window was closed.
//
// A row is an item, or a heading (a thin band the cursor skips), and an item may be disabled: drawn under the disabled
// veil, its description saying why, skipped by the cursor and never picked. Up/Down step to the next row that can be
// picked, wrapping round the ends unless `wrap` is off. `selected` and the scroll are kept across shows by the caller
// (open() clamps them), so a menu reopens where it was. With `background` set, that texture is drawn full-screen under
// the panel instead of the Context's backdrop (a frame the launcher captured); the panel dims it.
//
// The events go through Screen::handle(): the pad buttons through the Context's ActionMap (Confirm picks with the
// Cursor sound, Back leaves with Cancel), the d-pad by its live state (up before down) with hold-repeat at the shared
// HoldRepeat pace; a key of the keyboard does nothing here unless it reaches the menu as a pad button.
//
#pragma once

#include <ab_gui/hold_repeat.h>
#include <ab_gui/screen.h>
#include <ab_gui/style.h>

#include <ableem/ui/texture.h>

#include <string>
#include <vector>

namespace abgui {

//********************
// ActionMenu
//********************
class ActionMenu : public Screen {
public:
    // a compact panel: it pops in and back out, as Confirm does
    ActionMenu(ableem::GuiBase &gui, Context &context) : Screen(gui, context) {
        declareTransitions(ScreenTransitions(Transition::pop()));
    }

    struct Item {
        std::string title;
        std::string description; // under the title; of a disabled item the reason
        bool heading = false;    // a band with `title`, no description; the cursor skips it
        bool disabled = false;   // greyed out, skipped by the cursor
    };
    std::vector<Item> items;
    std::string title;
    std::string subtitle;                // at the header's right, in the secondary colour (a version)
    std::string crossLabel, circleLabel; // the footer's two hints; empty = "Select" / "Back"
    int result = -1;                     // the index picked, -1 when left
    int selected = 0;                    // the cursor's row
    ableem::Texture background;          // drawn full-screen under the dimmed panel when valid
    bool wrap = true;                    // Up on the first row goes to the last, and back

    // the panel's width, and a heading row's height (an item's is the style's rowHeight)
    static constexpr int Width = 800;
    static constexpr int HeadingHeight = 24;

    // whether the cursor may rest on the item, and pick it
    static bool selectable(const Item &item);
    // the row's height in `style`
    static int rowHeight(const Style &style, const Item &item);
    // the height rows can use on a canvas `canvasHeight` tall: the canvas less the margins, the header and the footer
    static int roomForRows(const Style &style, int canvasHeight);
    // how many rows fit from `first` on within `room` (at least one, even when the first is taller than the room)
    static int visibleCount(const Style &style, const std::vector<Item> &items, int first, int room);
    // the first row shown so that `selected` is visible: scrolled up to it (a heading above comes along), or down a row
    // at a time
    static int scrolledTo(const Style &style, const std::vector<Item> &items, int selected, int first, int room);
    // the row `step` (+1/-1) from `from` that can be picked, wrapping round the ends or not; `from` when there is none
    static int moved(const std::vector<Item> &items, int from, int step, bool wrap);

    // clamp `selected`, land on a row that can be picked, forget `result`, scroll the cursor into view from the top
    void open();
    int firstVisible() const { return firstVisible_; }
    void restore(int firstVisible) { firstVisible_ = firstVisible; }

    void draw() override;
    // the old menu's loop: a frame when due, the held d-pad's repeats, then the events; rests between presses
    void loop() override;
    void onAction(const ActionEvent &action) override;
    void onUnmapped(const ableem::Event &event) override;

protected:
    void moveSelection(int step);              // a press: wraps (when `wrap`)
    void moveSelection(int step, bool repeat); // repeat = a held d-pad's step: never wraps
    void dpad(); // a d-pad event: one step by the live state, and the hold-repeat's bookkeeping
    void pick();
    void leave();

private:
    int room(const Style &style) const;

    int firstVisible_ = 0;
    DpadHold hold_;
};

} // namespace abgui
