//
// Created by screemer on 2019-01-24.
//
#pragma once

#include "../gui_screen.h"
#include "../starfx.h"
#include "../gui_font.h"
#include <ableem/ui/texture.h>
#include <ableem/ui/audio.h>

#include <string>
#include <vector>

//********************
// GuiAbout
//********************
class GuiAbout : public GuiScreen {
public:
    StarFx fx;
    void init() override;
    // the credits - between the stack's clear and present
    void draw() override;
    void loop() override;
    ableem::Texture logo;
    ableem::Font font;
    using GuiScreen::GuiScreen;
    // the lines under the logo; AutoBleem's credits when the caller leaves it empty (a tool sets its own).
    // A line starting with HeadingMark is a section heading; the lines after it are the section's
    std::vector<std::string> credits;
    std::vector<std::string> foot; // the lines above the footer (support, copyright, licence)
    static const std::string HeadingMark;
    static std::vector<std::string> autobleemFoot();
    static std::vector<std::string> autobleemCredits();

private:
    // Start opens the "Surprise" easter egg ("BleemStrike: Reloaded"), GuiSurprise, a screen of its own
    void drawCredits();
    // 4:3 output: the credits as pages of one column, text a size up (the 1280 two-column design is too small there)
    void drawCreditsNarrow();
    void buildPages(int width, int firstRoom, int room);
    struct PageLine {
        std::string text;
        bool heading = false;
        int gapAfter = 0;
    };
    std::vector<std::vector<PageLine>> pages;           // the credits' body, one entry a page
    int pageWidth = 0, pageFirstRoom = 0, pageRoom = 0; // what `pages` was built for
    int page = 0;
    unsigned int pageSince = 0;
    static constexpr int TextSize = 22, HeadingSize = 24; // the 4:3 credits: 17.6 / 19 px on the screen
    static constexpr unsigned int PageMs = 9000;          // a page turns by itself this often
};
