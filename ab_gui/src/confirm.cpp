// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::Confirm: a yes/no question in a compact dialog. See the header.
//
#include <ab_gui/confirm.h>

#include <ab_gui/panel.h>
#include <ab_gui/text_page.h>

using namespace std;
using ableem::Event;
using ableem::Key;

namespace abgui {

constexpr int Confirm::Width;
constexpr int Confirm::TextGapTop;
constexpr int Confirm::TextGapBottom;

//*******************************
// Confirm::textWidth / panelRect
//*******************************
int Confirm::textWidth(const Style &style) {
    return Width - 2 * (style.rowInset + 8);
}

ableem::Rect Confirm::panelRect(const Style &style, int textHeight, int canvasWidth, int canvasHeight) {
    const int height = style.headerHeight + TextGapTop + textHeight + TextGapBottom + style.footerHeight;
    return ableem::Rect((canvasWidth - Width) / 2, (canvasHeight - height) / 2, Width, height);
}

//*******************************
// Confirm::draw
//*******************************
// A compact dialog in the shared look, centred over the dimmed screen: the header, the question wrapped to the panel,
// the two hints
void Confirm::draw() {
    ctx.drawBackdrop();
    const Style style = ctx.style();
    const ableem::Font &font = ctx.font(FontRole::Row);
    // the question wrapped to the panel
    const int width = textWidth(style);
    const vector<string> rows = wrapText(label, width, [&font](const string &s) { return font.width(s); });
    const int textHeight = static_cast<int>(rows.size()) * font.lineHeight();
    const Panel panel(panelRect(style, textHeight, ctx.renderer().width(), ctx.renderer().height()), style);
    panel.sheet(ctx);

    // the halo is the style's while the dialog draws, the program's after
    const bool programShadow = ctx.setTextShadow(style.textShadow);

    int y = panel.header(ctx, title.empty() ? ctx.translate("Please confirm") : title) + TextGapTop;
    const int x = panel.rect().x + style.rowInset + 8;
    for (const string &row : rows) {
        if (!row.empty())
            ctx.drawText(font, row, x, y, style.text);
        y += font.lineHeight();
    }
    style.footer(ctx, panel.footer(),
                 {{{"X"}, confirmLabel.empty() ? ctx.translate("Confirm") : confirmLabel},
                  {{"O"}, cancelLabel.empty() ? ctx.translate("Cancel") : cancelLabel}},
                 "", false);

    ctx.setTextShadow(programShadow);
}

//*******************************
// Confirm::loop
//*******************************
void Confirm::loop() {
    menuVisible = true;
    while (menuVisible) {
        // nothing animates here: sleep until a press, and redraw 4 times a second meanwhile (the performance
        // overlay, the DebugDriver's shots)
        if (!gui.input().waitForEvent(250))
            render();
        Event e;
        while (gui.input().poll(e)) {
            // this is for pc Only
            if (handleQuit(e))
                continue;
            handle(e);
        }
    }
}

//*******************************
// Confirm::onAction / onUnmapped
//*******************************
// the pad: Confirm answers yes, Back no (the d-pad and the other buttons do nothing); the keys as keys
void Confirm::onAction(const ActionEvent &action) {
    const Event &e = action.event;
    if (e.type == Event::Type::ButtonDown) {
        if (action.action == Action::Confirm)
            answer(true);
        else if (action.action == Action::Back)
            answer(false);
    } else if (e.type == Event::Type::KeyDown) {
        keyDown(e.key);
    }
}

void Confirm::onUnmapped(const Event &event) {
    if (event.type == Event::Type::KeyDown)
        keyDown(event.key);
}

void Confirm::keyDown(Key key) {
    if (key == Key::Return)
        answer(true);
    else if (key == Key::Escape)
        answer(false);
}

void Confirm::answer(bool yes) {
    ctx.play(yes ? UiSound::Cursor : UiSound::Cancel);
    result = yes;
    menuVisible = false;
}

} // namespace abgui
