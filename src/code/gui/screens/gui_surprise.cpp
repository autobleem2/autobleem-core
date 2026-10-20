//
// The surprise game as a screen of its own (UIREV-53); it was GuiAbout's second mode.
//

#include "gui_surprise.h"
#include <string>
#include "../gui.h"
#include "../../core/services/environment.h"
#include "about_trace.h"

using about_trace::Stage;

void GuiSurprise::init() {
    Stage whole("surprise init total");
    std::shared_ptr<Gui> gui(Gui::getInstance());
    fx.renderer = &renderer;
    if (ctx.hasStack())
        ctx.stack().declareFrameCanvas(*this, [this] { useFrameCanvas(); });
    // the credits' small font: the launcher's medium face
    font = Fonts::openNewSharedCachedFont(Fonts::defaultClassicFontPath(), 15, renderer);

    savedHighScore = Strings::toInt(app.config().inifile.values["surprisehighscore"]);
    game.seedHighScore(savedHighScore);
    // the ten-row table (UIREV-39 follow-up); the old single high score joins it once
    game.seedScores(app.config().inifile.values["surprisescores"], savedHighScore);

    loadGameAssets();
    const unsigned int ticks = gui->platform().ticks(); // the load took a while: the title starts from now
    game.reset(ticks);
    game.showTitle();
    // the star field turns into the game's (flyingStyle(), set every frame from its speed)
    fx.setStyle(flyingStyle(game.speed()));
    startMusic();
    app.audio().cursor.play();
}

//*******************************
// GuiSurprise::loadGameAssets
//*******************************
// the game's sprites, lettering and sounds: loaded when the screen opens, not when About opens (they were ~90% of its
// open time and the credits draw none of them)
void GuiSurprise::loadGameAssets() {
    if (assets.loaded) {
        font = assets.font;
        sprites = assets.sprites;
        hud = assets.hud;
        game.sounds = assets.sounds;
        surpriseMusic = assets.music;
        return;
    }
    Stage whole("load game assets");
    string sdir = Env::getWorkingPath() + sep + "surprise_game" + sep;
    {
        Stage spritesStage("init sprites (12 png)");
        sprites.ship = ableem::Texture::loadFile(renderer, sdir + "ship.png");
        sprites.enemy1 = ableem::Texture::loadFile(renderer, sdir + "enemy1.png");
        sprites.enemy2 = ableem::Texture::loadFile(renderer, sdir + "enemy2.png");
        sprites.ufo = ableem::Texture::loadFile(renderer, sdir + "ufo.png");
        sprites.laserPlayer = ableem::Texture::loadFile(renderer, sdir + "laser_player.png");
        sprites.laserPierce = ableem::Texture::loadFile(renderer, sdir + "laser_pierce.png");
        sprites.laserEnemy = ableem::Texture::loadFile(renderer, sdir + "laser_enemy.png");
        sprites.powerupRapid = ableem::Texture::loadFile(renderer, sdir + "powerup_rapid.png");
        sprites.powerupSpread = ableem::Texture::loadFile(renderer, sdir + "powerup_spread.png");
        sprites.powerupPower = ableem::Texture::loadFile(renderer, sdir + "powerup_power.png");
        sprites.powerupLife = ableem::Texture::loadFile(renderer, sdir + "powerup_life.png");
        sprites.explosion = ableem::Texture::loadFile(renderer, sdir + "explosion.png");
        {
            Stage stage("init sky.jpg");
            sprites.sky = ableem::Texture::loadFile(renderer, sdir + "sky.jpg"); // missing = the old dark backdrop
        }
        {
            Stage stage("init belt (2 png)");
            sprites.beltFar = ableem::Texture::loadFile(renderer, sdir + "belt_far.png"); // missing = no belt drawn
            sprites.beltNear = ableem::Texture::loadFile(renderer, sdir + "belt_near.png");
        }
    }

    // the game's own lettering: Oxanium (a language the theme's fonts cannot draw gets the CJK font for all of it);
    // the credits' face stands in for a file that did not open
    {
        Stage stage("init Oxanium fonts");
        hud.fonts.load(renderer, Env::getPathToFontsDir(), Fonts::cjkFontFor(app.config().inifile.values["language"]),
                       renderer.fourByThreeOutput());
        hud.fallback = font;
    }

    {
        Stage soundsStage("init sounds+music");
        game.sounds.playerShoot = ableem::Sound::load(sdir + "sfx_player_shoot.ogg");
        game.sounds.enemyShoot = ableem::Sound::load(sdir + "sfx_enemy_shoot.ogg");
        game.sounds.explosion = ableem::Sound::load(sdir + "sfx_explosion.ogg");
        game.sounds.playerHit = ableem::Sound::load(sdir + "sfx_player_hit.ogg");
        game.sounds.waveClear = ableem::Sound::load(sdir + "sfx_wave_clear.ogg");
        game.sounds.powerup = ableem::Sound::load(sdir + "sfx_powerup.ogg");
        surpriseMusic = ableem::Music::load(sdir + "music.ogg");
    }
    assets.font = font;
    assets.sprites = sprites;
    assets.hud = hud;
    assets.sounds = game.sounds;
    assets.music = surpriseMusic;
    assets.loaded = true;
}

//*******************************
// GuiSurprise::startMusic / stopMusic
//*******************************
void GuiSurprise::startMusic() {
    if (app.audio().music.isPlaying()) {
        // something is already playing (the theme's track or a custom one) - just duck it to 50% behind the game
        app.audio().music.setVolume(64);
        duckedThemeMusic = true;
    } else {
        // a silent theme or "nomusic": the game still gets some music
        surpriseMusic.play(-1);
        surpriseMusic.setVolume(96);
        playingFallbackMusic = true;
    }
}

void GuiSurprise::stopMusic() {
    if (duckedThemeMusic)
        app.audio().music.setVolume(128);
    if (playingFallbackMusic) {
        surpriseMusic.halt();
        app.audio().playMusic(); // resume whatever the theme/config normally plays
    }
    duckedThemeMusic = false;
    playingFallbackMusic = false;
}

//*******************************
// GuiSurprise::prepareFrame
//*******************************
// On a 4:3 output the game takes a 960x720 canvas: the middle of its 1280x720 field (renderSurprise).
bool GuiSurprise::prepareFrame() {
    useFrameCanvas();
    return true;
}

void GuiSurprise::useFrameCanvas() {
    renderer.setCanvas(FieldShownW, SCREEN_HEIGHT); // does nothing on a wide output
}

void GuiSurprise::draw() {
    renderSurprise();
}

//*******************************
// GuiSurprise::renderSurprise
//*******************************
void GuiSurprise::renderSurprise() {
    std::shared_ptr<Gui> gui(Gui::getInstance());
    const bool shown4x3 = renderer.fourByThreeOutput();
    if (!shown4x3) {
        game.setHudInset(0);
        renderSurpriseField();
    } else {
        // 4:3: the field is the 1280x720 design drawn into a layer of its own size, and the middle 960 of it goes on
        // the canvas (the corridor and the walls are there; the HUD's side plates move inward, setHudInset)
        const int w = SCREEN_WIDTH, h = SCREEN_HEIGHT;
        if (!fieldLayer.valid() || fieldLayerAt != renderer.targetsLost()) {
            fieldLayer = ableem::Texture::createTarget(renderer, w, h);
            fieldLayerAt = renderer.targetsLost();
        }
        if (!fieldLayer.valid())
            return;
        game.setHudInset((w - FieldShownW) / 2);
        const ableem::Color keep = renderer.drawColor();
        renderer.pushTarget(&fieldLayer);
        renderer.setBlendMode(ableem::BlendMode::None);
        renderer.setDrawColor(ableem::Color(0, 0, 0, 255));
        renderer.fillRect();
        renderer.setBlendMode(ableem::BlendMode::Blend);
        renderer.setDrawColor(keep);
        renderer.setCanvas(w, h);
        renderSurpriseField();
        renderer.setCanvas(FieldShownW, h);
        renderer.popTarget();
        const ableem::Rect from((w - FieldShownW) / 2, 0, FieldShownW, h), to(0, 0, FieldShownW, h);
        renderer.copy(fieldLayer, &from, &to);
    }
    drawSurpriseFooter();
}

//*******************************
// GuiSurprise::renderSurpriseField
//*******************************
// the star sky, the field and the game's HUD on the 1280x720 canvas
void GuiSurprise::renderSurpriseField() {
    std::shared_ptr<Gui> gui(Gui::getInstance());

    gui->renderBackground();

    // the far layer is the designer's sky; without it the screen is darkened to near black, no edge - the star
    // field shows through what is left
    if (!game.renderSky(renderer, sprites)) {
        gui->panelStyle().box(renderer, ableem::Rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT), abgui::Tone::Black, 235,
                              abgui::Tone::None);
    }

    // the star field follows the game's speed: a cruise on the title, a rush at the launch, streaks when fast
    fx.setStyle(flyingStyle(game.speed()));
    fx.render(gui->platform().ticks());

    // the lives and timer plates stay above the hint bar (on 4:3 the bar is the 800x600 canvas's, shown at 1.2)
    game.setBarTop(renderer.fourByThreeOutput() ? SCREEN_HEIGHT - FooterScaled : gui->classicFooter().y);
    game.render(renderer, gui->text(), font, sprites, hud);
}

//*******************************
// GuiSurprise::drawSurpriseFooter
//*******************************
void GuiSurprise::drawSurpriseFooter() {
    std::shared_ptr<Gui> gui(Gui::getInstance());
    // the title: Circle leaves. In a game: Start restarts, Circle leaves the game (the footer without its rule: no
    // panel on the play field). The initials: the letter, the next, back, done.
    string hints;
    if (game.enteringInitials())
        hints = "|@Up+Down| " + _("Letter") + "  |@X| " + _("Next") + "  |@O| " + _("Back") + "  |@Start| " + _("Done");
    else if (game.onTitle() || game.demo())
        hints = "|@O| " + _("Back");
    else if (game.showingScores())
        hints = "|@Start| " + _("Play") + "  |@X| " + _("Continue") + "  |@O| " + _("Back");
    else
        hints = "|@Start| " + _("Restart") + "  |@O| " + (game.gameOver() ? _("Back") : _("Exit game"));
    if (!renderer.fourByThreeOutput()) {
        gui->panelStyle().footer(*gui, gui->classicFooter(), hints, false);
        return;
    }
    // 4:3: the footer is drawn on the Gui's 800x600 canvas, in a layer, and its strip goes at the foot of the 960x720
    // one scaled 1.2 - its text is as large as on every other 4:3 screen (17.6 px on the screen)
    const int w = 800, h = 600, fh = PanelStyle::FooterHeight;
    if (!footerLayer.valid() || footerLayerAt != renderer.targetsLost()) {
        footerLayer = ableem::Texture::createTarget(renderer, w, h);
        footerLayer.setBlendMode(ableem::BlendMode::Premultiplied);
        footerLayerAt = renderer.targetsLost();
    }
    if (!footerLayer.valid())
        return;
    const ableem::Color keep = renderer.drawColor();
    renderer.pushTarget(&footerLayer);
    renderer.setBlendMode(ableem::BlendMode::None);
    renderer.setDrawColor(ableem::Color(0, 0, 0, 0));
    renderer.fillRect();
    renderer.setBlendMode(ableem::BlendMode::Blend);
    renderer.setDrawColor(keep);
    renderer.setCanvas(w, h);
    gui->panelStyle().footer(*gui, gui->classicFooter(), hints, false);
    renderer.setCanvas(FieldShownW, SCREEN_HEIGHT);
    renderer.popTarget();
    const ableem::Rect from(0, h - fh, w, fh), to(0, SCREEN_HEIGHT - FooterScaled, FieldShownW, FooterScaled);
    renderer.copy(footerLayer, &from, &to);
}

//*******************************
// GuiSurprise::flyingStyle
//*******************************
// the game's star field: faster, darker, more comets than the credits' (the lasers and pickups have to read against
// it), its pace and streaks following the game's speed - the play speed is the old fixed 2.5
StarFx::Style GuiSurprise::flyingStyle(float speed) {
    StarFx::Style flying;
    flying.speedScale = 2.5f * speed / surprise::SpeedPlay;
    flying.brightnessScale = 0.55f;
    flying.cometOdds = 150;
    flying.maxComets = 3;
    flying.streakScale = surprise::starStreak(speed);
    return flying;
}

//*******************************
// GuiSurprise::loop
//*******************************
void GuiSurprise::loop() {
    std::shared_ptr<Gui> gui(Gui::getInstance());
    menuVisible = true;
    crossHeld = false;
    // AB_TRACE_ABOUT: the first frames after each change of what the screen shows (title, game)
    const char *traceMode = "title";
    int traceFrames = 0;
    while (menuVisible) {
        const unsigned int ticks = gui->platform().ticks();

        if (game.onTitle()) {
            game.updateTitle(ticks);
        } else {
            game.update(ticks, gui->input().dpadLeft(), gui->input().dpadRight(), crossHeld);

            // remembered here, written once when the screen closes: config.ini on every point scored was
            // a write per frame at the end of a good game
            if (game.currentHighScore() > savedHighScore)
                savedHighScore = game.currentHighScore();
        }

        if (about_trace::on() && traceFrames < 5) {
            const auto begin = std::chrono::steady_clock::now();
            render();
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
            PLOG_INFO << "AB_TRACE_ABOUT first frames, " << traceMode << " #" << traceFrames << ": " << ms << " ms";
            traceFrames++;
        } else {
            render();
        }
        Event e;
        while (gui->input().poll(e)) {
            // this is for pc Only
            if (e.type == Event::Type::Quit) {
                menuVisible = false;
            }
            const bool press = e.type == Event::Type::ButtonDown || e.type == Event::Type::DpadDown;
            // the initials after a new table score: every press is the entry's (Circle steps back, it does not leave)
            if (game.enteringInitials()) {
                if (press)
                    game.initialsPress(e.button, ticks);
                continue;
            }
            // the attract demo: Start plays, any other press goes back to the title
            if (game.demo() && press && e.button != Button::Start) {
                game.showTitle();
                continue;
            }
            // the table after an entry: Cross goes on to the title (Start plays again and Circle leaves, as below)
            if (game.showingScores() && press && (!game.scoresInputReady() || e.button == Button::Cross)) {
                if (game.scoresInputReady())
                    game.showTitle();
                continue;
            }
            if (game.onTitle() && press) {
                game.titleInput(); // any press starts the attract loop over from the title
                // the Konami code, on the title only (the owner, 2026-10-02): its last press (Circle) is kept from
                // leaving, and GOD MODE splashes
                if (!game.godMode() && konami.feed(e.button)) {
                    game.armGodMode();
                    continue;
                }
            }
            switch (e.type) {
            case Event::Type::ButtonDown:
                if (e.button == Button::Start) {
                    // on the title Start begins the game, in the game it restarts it
                    traceMode = "game";
                    traceFrames = 0;
                    game.reset(ticks);
                    crossHeld = false;
                    app.audio().cursor.play();
                } else if (e.button == Button::Cross) {
                    crossHeld = true;
                } else if (e.button == Button::Circle) {
                    // Circle leaves the game, back to the credits (GOD MODE ends with it)
                    game.disarmGodMode();
                    menuVisible = false;
                    app.audio().cancel.play();
                }
                break;
            case Event::Type::ButtonUp:
                if (e.button == Button::Cross)
                    crossHeld = false;
                break;
            default:
                break;
            }
        }
    }
    stopMusic();
    // the table and the best score, written once as the screen closes and only when they changed
    bool changed = false;
    if (to_string(savedHighScore) != app.config().inifile.values["surprisehighscore"]) {
        app.config().inifile.values["surprisehighscore"] = to_string(savedHighScore);
        changed = true;
    }
    if (game.scoresText() != app.config().inifile.values["surprisescores"]) {
        app.config().inifile.values["surprisescores"] = game.scoresText();
        changed = true;
    }
    if (changed)
        app.config().save();
}
