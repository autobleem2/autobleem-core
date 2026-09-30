// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::Confirm (G3j of docs/ab-gui-plan.md): a yes/no question in a compact dialog - the header, the question wrapped
// to the panel, the two answers as footer hints - centred over the dimmed backdrop. The caller sets `label` (and
// `title`, `confirmLabel`, `cancelLabel` when the defaults - "Please confirm", "Confirm", "Cancel" - do not fit), runs
// the screen (loop()) and reads `result`: true after Confirm (Cross, Enter), false after Back (Circle, Escape), and
// false when the window is closed. The backdrop is whatever the Context's drawer draws; a caller that wants the screen
// it came from behind the dialog has it captured there.
//
// The dialog rests between presses and redraws four times a second meanwhile (the performance overlay, the
// DebugDriver's shots). Confirm plays the Cursor sound, Back the Cancel sound.
//
#pragma once

#include <ab_gui/screen.h>
#include <ab_gui/style.h>

#include <string>

namespace abgui {

//********************
// Confirm
//********************
class Confirm : public Screen {
public:
    using Screen::Screen;

    std::string label;                     // the question
    std::string title;                     // the header; empty = "Please confirm"
    std::string confirmLabel, cancelLabel; // the footer's hints; empty = "Confirm" / "Cancel"
    bool result = false;

    // the dialog's width, and the space the question's text gets (the width less the rows' inset and 8 px each side)
    static constexpr int Width = 800;
    static int textWidth(const Style &style);
    // the gap above and below the question's text
    static constexpr int TextGapTop = 12;
    static constexpr int TextGapBottom = 24;
    // the dialog's rect for a question `textHeight` tall on a canvasWidth x canvasHeight canvas: centred, the header,
    // the gap, the text, the gap and the footer band tall
    static ableem::Rect panelRect(const Style &style, int textHeight, int canvasWidth, int canvasHeight);

    void draw() override;
    // the old dialog's loop: rest for a press (a frame every 250 ms meanwhile), then every event to handle()
    void loop() override;
    void onAction(const ActionEvent &action) override;
    void onUnmapped(const ableem::Event &event) override;

protected:
    void answer(bool yes); // the sound, `result` and out

private:
    void keyDown(ableem::Key key);
};

} // namespace abgui
