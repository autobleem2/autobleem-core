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
    // the credits (and the game) are a 1280x720 design: on a 4:3 output they keep that canvas, letterboxed
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
    static StarFx::Style flyingStyle(float speed); // the game's star field at the game's speed
};
