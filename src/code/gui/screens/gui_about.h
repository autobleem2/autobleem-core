//
// Created by screemer on 2019-01-24.
//
#pragma once

#include "../gui_screen.h"
#include "../starfx.h"
#include "../surprise_game.h"
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
    // the credits, or the game - between the stack's clear and present
    void draw() override;
    // on a 4:3 output the game gets a 960x720 canvas (the middle of its field), the credits the Gui's rest canvas
    bool prepareFrame() override;
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
    // the "Surprise" easter egg ("BleemStrike: Reloaded"): Start swaps the credits for its title screen, Start again
    // for a small shoot-em-up over the same starfield
    bool surpriseMode = false;
    bool crossHeld = false;
    SurpriseGame game;
    SurpriseSprites sprites;
    SurpriseHud hud;        // the Oxanium lettering and plates
    KonamiCode konami;      // fed every press on the game's title; completing it is SurpriseGame::armGodMode
    int savedHighScore = 0; // mirrors config.ini's "surprisehighscore"; written back only when beaten
                            // (the ten-row table is "surprisescores", SurpriseGame::seedScores/scoresText)

    // the game always has some music: the theme's track is ducked to 50% if it was already playing, or -
    // when the theme/config has no music at all (a silent theme, or "nomusic") - this bundled track takes
    // over for the duration so Surprise mode is never silent, then playMusic() sorts out what should resume
    ableem::Music surpriseMusic;
    bool duckedThemeMusic = false;
    bool playingFallbackMusic = false;

    void loadGameAssets(); // on the first Start, not at open
    void drawCredits();
    void renderSurprise();
    void renderSurpriseField();
    void drawSurpriseFooter();
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
    static constexpr int FieldShownW = 960;               // the part of the game's 1280-wide field a 4:3 canvas shows
    ableem::Texture fieldLayer; // the field at 1280x720, before the middle of it goes on the canvas
    unsigned long fieldLayerAt = 0;
    static StarFx::Style flyingStyle(float speed); // the game's star field at the game's speed
};
