//
// Created by screemer on 2019-01-24.
//

#include "gui_splash.h"
#include "../gui.h"
#include "../../core/model/timing.h"
#include "../../core/services/environment.h"

using namespace std;

//*******************************
// GuiSplash::GuiSplash
//*******************************
// every frame cleared to transparent black; in: black for the settle, then a fade from black; out: none - its picture
// stays on display and the launcher drops in over it (the owner, 2026-10-01)
GuiSplash::GuiSplash(ableem::GuiBase &_gui) : GuiScreen(_gui) {
    frameColor = abgui::OptionalColor(ableem::Color(0x00, 0x00, 0x00, 0x00));
    declareTransitions(abgui::ScreenTransitions(abgui::Transition::fade(SplashFadeDuration, SplashSettleDuration),
                                                abgui::Transition::none()));
}

//*******************************
// GuiSplash::prepareFrame
//*******************************
// before each frame: the hold starts when the fade in is over (at once with the animations off) and ends the screen
bool GuiSplash::prepareFrame() {
    gui->assets().backgroundImg.setBlendMode(ableem::BlendMode::Blend);
    if (ctx.stack().bringsIn(*this))
        return true; // still fading in
    const unsigned int now = ctx.ticks();
    if (!holding_) {
        holding_ = true;
        holdStart_ = now;
    }
    if (now - holdStart_ >= static_cast<unsigned int>(SplashHoldDuration))
        menuVisible = false; // no fade out: the launcher drops in over this picture
    return true;
}

//*******************************
// GuiSplash::draw
//*******************************
void GuiSplash::draw() {
    renderer.copy(gui->assets().backgroundImg, nullptr, &gui->assets().backgroundRect);
    renderer.copy(gui->assets().logo, nullptr, &gui->assets().logoRect);

    const ableem::ThemeStatusBar &bar = app.theme().classic().statusBar;
    ableem::Rect rect = gui->text().getTextRectOfTheme();
    gui->panelStyle().plate(renderer, rect, TextRenderer::toColor(bar.color, bar.alpha));

    string splashText = _("AutoBleem") + " " + Env::productVersion();
    gui->text().renderText(gui->assets().themeFont, splashText, 0, bar.textY, XALIGN_CENTER);
}
