//
// Created by screemer on 2019-01-24.
//
#pragma once

#include "../gui_screen.h"

//********************
// GuiSplash
//********************
// The boot splash, a plain screen (docs/ab-gui-plan.md 7a, UIREV-48): the theme's background and logo with the version
// on the status plate, declared as a Fade in from black (after SplashSettleDuration of black, while the display
// syncs) and a Fade out to black, held SplashHoldDuration in between. The screen stack plays both fades - no own loop,
// no own alpha. Gui::display(false) is its only caller.
class GuiSplash : public GuiScreen {
public:
    explicit GuiSplash(ableem::GuiBase &_gui);

    // closes the screen once the fade in is over and the hold has passed
    bool prepareFrame() override;
    void draw() override; // the background, the logo and the version

private:
    bool holding_ = false;
    unsigned int holdStart_ = 0;
};
