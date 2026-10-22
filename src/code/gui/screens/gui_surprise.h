//
// The About screen's surprise game ("BleemStrike: Reloaded"), a screen of its own (UIREV-53): About's Start opens it
// with the Slide-from-top drop of UIREV-48, Circle on its title (or the game) closes it with the rise.
//
#pragma once

#include <string>
#include "../gui_screen.h"
#include "../starfx.h"
#include "../surprise_game.h"
#include "../gui_font.h"
#include <ableem/ui/texture.h>
#include <ableem/ui/audio.h>

//********************
// SurpriseAssets
//********************
// the game's sprites, lettering and sounds. The screen that opens GuiSurprise owns one (GuiAbout), so a Start finds
// them loaded: the load is ~170 ms on the PC (about a second on the PSC), more than the drop itself. GuiAbout feeds
// loadStep() one small portion a frame while it is open; GuiSurprise finishes whatever is left. All of them are cheap
// shared handles. Held while About is open: ~17 MB (sky 8.6 MB, the two belts 6.9 MB as RGBA textures, sounds and
// fonts ~1-2 MB); release() gives it all back when About closes.
struct SurpriseAssets {
    bool loaded = false;
    int step = 0; // the next portion of loadStep()
    ableem::Font font; // the credits' face: the HUD's fallback
    SurpriseSprites sprites;
    SurpriseHud hud;
    SurpriseSounds sounds;
    ableem::Music music;

    static constexpr int Steps = 8;
    // loads the next portion (a few ms each, 40 ms at most on the PC); true once everything is loaded
    bool loadStep(ableem::Renderer &renderer, const std::string &language);
    void release() { *this = SurpriseAssets(); }
};

//********************
// GuiSurprise
//********************
class GuiSurprise : public GuiScreen {
public:
    GuiSurprise(ableem::GuiBase &_gui, SurpriseAssets &cache) : GuiScreen(_gui), assets(cache) {
        // the drop in from the top; the out is the in played backwards, a rise. Animations Off: the stack arms
        // nothing, the screen opens and closes instantly
        declareTransitions(abgui::ScreenTransitions(abgui::Transition::drop()));
    }

    StarFx fx;
    void init() override;
    void draw() override;
    // on a 4:3 output the game gets a 960x720 canvas (the middle of its field)
    bool prepareFrame() override;
    void useFrameCanvas();
    void loop() override;

private:
    SurpriseAssets &assets;
    ableem::Font font; // the credits' face, the HUD's fallback
    bool crossHeld = false;
    SurpriseGame game;
    SurpriseSprites sprites;
    SurpriseHud hud;        // the Oxanium lettering and plates
    KonamiCode konami;      // fed every press on the game's title; completing it is SurpriseGame::armGodMode
    int savedHighScore = 0; // mirrors config.ini's "surprisehighscore"; written back only when beaten
                            // (the ten-row table is "surprisescores", SurpriseGame::seedScores/scoresText)

    // the game always has some music: the theme's track is ducked to 50% if it was already playing, or -
    // when the theme/config has no music at all (a silent theme, or "nomusic") - this bundled track takes
    // over for the duration so the game is never silent, then playMusic() sorts out what should resume
    ableem::Music surpriseMusic;
    bool duckedThemeMusic = false;
    bool playingFallbackMusic = false;

    void loadGameAssets();
    void startMusic();
    void stopMusic();
    void renderSurprise();
    void renderSurpriseField();
    void drawSurpriseFooter();

    static constexpr int FieldShownW = 960; // the part of the 1280-wide field a 4:3 canvas shows
    static constexpr int FooterScaled = PanelStyle::FooterHeight * 6 / 5; // the footer strip, on the 960x720 canvas
    ableem::Texture footerLayer; // the footer at 800x600, before it goes on the canvas
    unsigned long footerLayerAt = 0;
    ableem::Texture fieldLayer; // the field at 1280x720, before the middle of it goes on the canvas
    unsigned long fieldLayerAt = 0;
    static StarFx::Style flyingStyle(float speed); // the game's star field at the game's speed
};
