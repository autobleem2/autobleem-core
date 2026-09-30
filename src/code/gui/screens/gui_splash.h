//
// Created by screemer on 2019-01-24.
//
#pragma once

#include "../gui_screen.h"

//********************
// GuiSplash
//********************
class GuiSplash : public GuiScreen {
public:
    // every frame cleared to transparent black
    explicit GuiSplash(ableem::GuiBase &_gui) : GuiScreen(_gui) {
        frameColor = abgui::OptionalColor(ableem::Color(0x00, 0x00, 0x00, 0x00));
    }

    bool prepareFrame() override;
    void draw() override; // the background, the logo and the version, faded by `alpha`
    void loop() override;

    int alpha = 0;
    int start = 0;

    // fade in, hold at full brightness for SplashHoldDuration, fade back out, then loop() returns and the
    // launcher takes over (its own fade-in - see GuiLauncher - picks up where this leaves off)
    enum class Phase { Settle, FadeIn, Hold, FadeOut };
    Phase phase = Phase::Settle;
    long holdStart = 0;
};
