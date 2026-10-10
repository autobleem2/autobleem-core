//
// Created by screemer on 2019-01-24.
//
#pragma once

#include "../gui_screen.h"

#include <functional>
#include <vector>

//********************
// GuiSplash
//********************
// The boot splash, a plain screen (docs/ab-gui-plan.md 7a, UIREV-48): the theme's background and logo with the version
// on the status plate, declared as a Fade in from black (after SplashSettleDuration of black, while the display
// syncs) and a Fade out to black, held in between. The screen stack plays both fades - no own loop, no own alpha.
// Gui::display(false) is its only caller.
//
// The hold is the launcher's start-up time: the work given to setWork() runs one step a frame once the fade in is
// over, on this thread, and the hold ends when the work is done and at least SplashHoldDuration has passed - the
// splash is the loading screen, not a wait of its own after the loading.
class GuiSplash : public GuiScreen {
public:
    explicit GuiSplash(ableem::GuiBase &_gui);

    // the start-up steps for the next splash (they replace any not run yet)
    static void setWork(std::vector<std::function<void()>> steps);
    // steps that go before the ones already given (Gui::display's theme rest and pad setup)
    static void pushWorkFront(std::vector<std::function<void()>> steps);
    // runs the steps no splash ran: the splash is off (Options -> Interface, AB_NO_SPLASH) or ended early (a Quit)
    static void runPendingWork();

    // runs the next step of the work once the fade in is over; closes the screen when the work is done and the
    // hold has passed
    bool prepareFrame() override;
    void draw() override; // the background, the logo and the version

private:
    bool holding_ = false;
    unsigned int holdStart_ = 0;
};
