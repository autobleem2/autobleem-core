//
// Created by screemer on 2019-01-24.
//

#include "gui_splash.h"
#include "../gui.h"
#include "../../core/model/timing.h"
#include "../../core/services/environment.h"

#include <ableem/engine/startup_timer.h>

#include <deque>

using namespace std;

namespace {
// the steps setWork() gave, not run yet - the main thread's only (the splash and the launcher's start-up)
deque<function<void()>> &pendingWork() {
    static deque<function<void()>> steps;
    return steps;
}
} // namespace

//*******************************
// GuiSplash::setWork / runPendingWork
//*******************************
void GuiSplash::setWork(vector<function<void()>> steps) {
    pendingWork().assign(steps.begin(), steps.end());
}

void GuiSplash::pushWorkFront(vector<function<void()>> steps) {
    pendingWork().insert(pendingWork().begin(), steps.begin(), steps.end());
}

void GuiSplash::runPendingWork() {
    deque<function<void()>> &steps = pendingWork();
    while (!steps.empty()) {
        function<void()> step = std::move(steps.front());
        steps.pop_front();
        step();
    }
}

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
    if (!firstFrameLogged_) {
        firstFrameLogged_ = true;
        ableem::StartupTimer::milestone("splash-first-frame");
    }
    if (ctx.stack().bringsIn(*this))
        return true; // still fading in
    if (!holding_) {
        holding_ = true;
        holdStart_ = ctx.ticks();
        ableem::StartupTimer::milestone("splash-up");
    }
    // one step of the start-up work a frame: the picture stays as it is meanwhile, and the next frame comes when
    // the step is done
    deque<function<void()>> &steps = pendingWork();
    if (!steps.empty()) {
        function<void()> step = std::move(steps.front());
        steps.pop_front();
        step();
        return true;
    }
    if (ctx.ticks() - holdStart_ >= static_cast<unsigned int>(SplashHoldDuration)) {
        ctx.stack().keepPictureOnClose(*this); // no fade out: the launcher drops in over this picture
        menuVisible = false;
    }
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
