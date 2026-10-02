//
// Created by screemer on 2019-01-24.
//

#include "gui_about.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include "../gui.h"
#include "../../core/services/environment.h"

const std::string GuiAbout::HeadingMark = "";

namespace {

// AB_TRACE_ABOUT=1: the ms of each stage of opening the About screen and of the game's first frames, to the log
// (off by default; one getenv, read once)
bool traceAbout() {
    static const bool on = [] {
        const char *v = getenv("AB_TRACE_ABOUT");
        return v && *v && strcmp(v, "0") != 0;
    }();
    return on;
}

// logs the time since its construction, under `name`, when it goes out of scope
class AboutStage {
public:
    explicit AboutStage(const char *stageName) : name(stageName), begin(std::chrono::steady_clock::now()) {}
    ~AboutStage() {
        if (traceAbout()) {
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
            PLOG_INFO << "AB_TRACE_ABOUT " << name << ": " << ms << " ms";
        }
    }
    AboutStage(const AboutStage &) = delete;
    AboutStage &operator=(const AboutStage &) = delete;

private:
    const char *name;
    std::chrono::steady_clock::time_point begin;
};

} // namespace

void GuiAbout::init() {
    AboutStage whole("init total");
    std::shared_ptr<Gui> gui(Gui::getInstance());
    fx.renderer = &renderer;
    // the credits' small font: the launcher's medium face (about.ttf, an SST copy, went with the Sony fonts)
    {
        AboutStage stage("init credits font");
        font = Fonts::openNewSharedCachedFont(Env::getPathToFontsDir() + sep + "OpenSans-Medium.ttf", 15, renderer);
    }
    {
        AboutStage stage("init logo");
        logo = ableem::Texture::loadFile(renderer, Env::getWorkingPath() + sep + "ablogo.png");
    }

    savedHighScore = Strings::toInt(app.config().inifile.values["surprisehighscore"]);
    game.seedHighScore(savedHighScore);
    // the ten-row table (UIREV-39 follow-up); the old single high score joins it once
    game.seedScores(app.config().inifile.values["surprisescores"], savedHighScore);

    if (credits.empty()) {
        credits = autobleemCredits();
        foot = autobleemFoot();
    }
}

//*******************************
// GuiAbout::loadGameAssets
//*******************************
// the surprise game's sprites, lettering and sounds: loaded when Start first asks for the game, not when About opens
// (they were ~90% of its open time and the credits draw none of them); a later Start finds them there
void GuiAbout::loadGameAssets() {
    if (sprites.ship.valid())
        return;
    AboutStage whole("START load game assets");
    string sdir = Env::getWorkingPath() + sep + "surprise_game" + sep;
    {
        AboutStage spritesStage("init sprites (12 png)");
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
            AboutStage stage("init sky.jpg");
            sprites.sky = ableem::Texture::loadFile(renderer, sdir + "sky.jpg"); // missing = the old dark backdrop
        }
        {
            AboutStage stage("init belt (2 png)");
            sprites.beltFar = ableem::Texture::loadFile(renderer, sdir + "belt_far.png"); // missing = no belt drawn
            sprites.beltNear = ableem::Texture::loadFile(renderer, sdir + "belt_near.png");
        }
    }

    // the game's own lettering: Oxanium (a language the theme's fonts cannot draw gets the CJK font for all of it);
    // the credits' face stands in for a file that did not open
    {
        AboutStage stage("init Oxanium fonts");
        hud.fonts.load(renderer, Env::getPathToFontsDir(), Fonts::cjkFontFor(app.config().inifile.values["language"]));
        hud.fallback = font;
    }

    {
        AboutStage soundsStage("init sounds+music");
        game.sounds.playerShoot = ableem::Sound::load(sdir + "sfx_player_shoot.ogg");
        game.sounds.enemyShoot = ableem::Sound::load(sdir + "sfx_enemy_shoot.ogg");
        game.sounds.explosion = ableem::Sound::load(sdir + "sfx_explosion.ogg");
        game.sounds.playerHit = ableem::Sound::load(sdir + "sfx_player_hit.ogg");
        game.sounds.waveClear = ableem::Sound::load(sdir + "sfx_wave_clear.ogg");
        game.sounds.powerup = ableem::Sound::load(sdir + "sfx_powerup.ogg");
        surpriseMusic = ableem::Music::load(sdir + "music.ogg");
    }
}

//*******************************
// GuiAbout::autobleemCredits
//*******************************
vector<string> GuiAbout::autobleemCredits() {
    auto heading = [](const string &text) { return HeadingMark + text; }; // render() draws it as a heading
    return {
        heading(_("Code C++ and shell scripts")),
        "screemer, Axanar, mGGk, nex, genderbent",
        heading(_("Graphics")),
        "KaonashiFTW, GeekAndy, rubixcube6, NewbornfromHell",
        heading(_("Testing")),
        "MagnusRC, xboxiso, Azazel, Solidius, SupaSAIAN, Kingherb, saptis",
        heading(_("Database maintenance")),
        "Screemer,Kingherb",
        heading(_("Localization support")),
        "nex(German), Azazel(Polish), gadsby(Turkish), GeekAndy(Dutch), Pardubak(Slovak), SupaSAIAN(Spanish), "
        "Mate(Czech)",
        "Sasha(Italian), Jakejj(BR_Portuguese), jolny(Swedish), StepJefli(Danish), alucard73 / MagnusRC(French), "
        "Quenti(Occitan), ",
        heading(_("RetroArch and emulation cores")),
        "genderbent, KMFDManic",
        heading(_("Ported from AutoBleem-NG")),
        "cornelk (AutoBleem-NG), Axanar - lightgun games, RDB metadata, libretro-thumbnails covers, multi-disc merge",
        heading(_("Game data")),
        "libretro-database (Sony - PlayStation.rdb), libretro-thumbnails",
    };
}

//*******************************
// GuiAbout::autobleemFoot
//*******************************
// the lines under the credits: support, and - GPLv3 5(d), the "Appropriate Legal Notices" an interactive
// program shows - the copyright and the licence
vector<string> GuiAbout::autobleemFoot() {
    return {
        _("Support via Discord:") + " https://discord.gg/AHUS3RM",
        "Copyright (C) 2018-2026 screemer and the AutoBleem contributors",
        _("Free software under the GNU GPL v3 or later - no warranty. Source and licence:") +
            " github.com/autobleem/AutoBleem2",
    };
}

//*******************************
// GuiAbout::draw
//*******************************
// what the stack's frame holds (docs/ab-gui-plan.md, G3c): the credits, or the game
void GuiAbout::draw() {
    if (surpriseMode)
        renderSurprise();
    else
        drawCredits();
}

//*******************************
// GuiAbout::drawCredits
//*******************************
void GuiAbout::drawCredits() {
    std::shared_ptr<Gui> gui(Gui::getInstance());

    gui->renderBackground();

    // the screen darkened to near black, no edge: the star field shows through what is left
    gui->panelStyle().box(renderer, ableem::Rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT), abgui::Tone::Black, 235,
                          abgui::Tone::None);

    fx.render(gui->platform().ticks());

    // UIREV-38: the screen draws in the theme's fonts and colour roles (no fixed fonts: every language's longest
    // heading and foot line fits - measured, see the design) and the credits sit on the theme's `panel` frame
    PanelStyle style = gui->panelStyle();
    Fonts &fonts = gui->assets().themeFonts;
    const ableem::Font &versionFont = fonts.atSize(FONT_MED, 16);
    const ableem::Font &lineFont = fonts.atSize(FONT_MED, 17);
    const ableem::Font &headingFont = fonts.atSize(FONT_BOLD, 18);
    const ableem::Font &textFont = fonts.atSize(FONT_MED, 15);
    auto centred = [&](const ableem::Font &f, const string &text, int y, const ableem::Color &color) {
        gui->text().renderText_WithColor(f, text, SCREEN_WIDTH / 2 - gui->text().textWidth(f, text) / 2, y, color,
                                         XALIGN_LEFT);
    };

    // the logo, centred: the theme's launcher.logo 400 wide (its height by its own aspect); a theme without one
    // keeps the old ablogo.png as it was
    int logoBottom;
    if (gui->launcherLogo().valid() && gui->launcherLogoRect().w > 0) {
        const ableem::Rect &place = gui->launcherLogoRect();
        const int logoWidth = 400;
        ableem::Rect rect;
        rect.w = logoWidth;
        rect.h = max(1, logoWidth * place.h / place.w);
        rect.x = (SCREEN_WIDTH - rect.w) / 2;
        rect.y = 30;
        renderer.copy(gui->launcherLogo(), nullptr, &rect);
        logoBottom = rect.y + rect.h;
    } else {
        ableem::Rect rect(SCREEN_WIDTH / 2 - 100, 5, 200, 141);
        renderer.copy(logo, nullptr, &rect);
        logoBottom = rect.y + rect.h;
    }
    const int versionTop = logoBottom + 14;
    centred(versionFont, Env::productVersion(), versionTop, style.secondary);
    const int creditTop = versionTop + 24;
    centred(lineFont, _("This version is brought to you by screemer. The AutoBleem team is back, baby!"), creditTop,
            style.text);

    // the credits as sections - a heading and its names wrapped under it - in two columns on the panel; the
    // sections stay in order and the split is where the taller column is shortest
    struct Section {
        string heading;
        vector<string> lines;
    };
    vector<Section> sections;
    for (const string &s : credits) {
        if (s.compare(0, HeadingMark.size(), HeadingMark) == 0)
            sections.push_back({s.substr(HeadingMark.size()), {}});
        else if (!sections.empty())
            sections.back().lines.push_back(s);
        else
            sections.push_back({"", {s}});
    }
    const int panelX = 64, panelWidth = SCREEN_WIDTH - 2 * panelX;
    const int columnGap = 48, columnWidth = 520;
    const int panelTop = creditTop + 36;
    auto sectionHeight = [&](const Section &sec) {
        int h = sec.heading.empty() ? 0 : 26;
        for (const string &line : sec.lines)
            h += max(20, gui->text().wrappedHeight(textFont, line, columnWidth));
        return h + 14;
    };
    vector<int> heights;
    for (const Section &sec : sections)
        heights.push_back(sectionHeight(sec));
    int total = 0;
    for (int h : heights)
        total += h;
    // the first section of the right column (== size: no right column)
    size_t split = sections.size();
    int columnHeight = total;
    int before = 0;
    for (size_t k = 1; k < sections.size(); k++) {
        before += heights[k - 1];
        const int taller = max(before, total - before);
        if (taller < columnHeight) {
            columnHeight = taller;
            split = k;
        }
    }
    const int panelHeight = columnHeight + 34;
    style.drawFrame(gui->uiContext(), "panel", ableem::Rect(panelX, panelTop, panelWidth, panelHeight));

    const int leftX = panelX + 32;
    int x = leftX, y = panelTop + 24;
    for (size_t i = 0; i < sections.size(); i++) {
        const Section &sec = sections[i];
        if (i == split) {
            x = leftX + columnWidth + columnGap;
            y = panelTop + 24;
        }
        if (!sec.heading.empty()) {
            gui->text().renderText_WithColor(headingFont, sec.heading, x, y, style.heading, XALIGN_LEFT);
            y += 26;
        }
        for (const string &line : sec.lines)
            y += max(20, gui->text().renderWrappedText(textFont, line, x, y, columnWidth, style.hint));
        y += 14;
    }

    // the foot: support, copyright, licence - centred under the panel
    y = panelTop + panelHeight + 22;
    for (const string &line : foot) {
        centred(textFont, line, y, style.secondary);
        y += 20;
    }

    // the footer's hints without its rule: the starfield is the panel here
    style.footer(*gui, gui->classicFooter(), "|@O| " + _("Back") + " |@Start| " + _("Surprise"), false);
}

//*******************************
// GuiAbout::renderSurprise
//*******************************
void GuiAbout::renderSurprise() {
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

    game.setBarTop(gui->classicFooter().y); // the lives and timer plates stay above the hint bar
    game.render(renderer, gui->text(), font, sprites, hud);

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
    gui->panelStyle().footer(*gui, gui->classicFooter(), hints, false);
}

//*******************************
// GuiAbout::flyingStyle
//*******************************
// the game's star field: faster, darker, more comets than the credits' (the lasers and pickups have to read against
// it), its pace and streaks following the game's speed - the play speed is the old fixed 2.5
StarFx::Style GuiAbout::flyingStyle(float speed) {
    StarFx::Style flying;
    flying.speedScale = 2.5f * speed / surprise::SpeedPlay;
    flying.brightnessScale = 0.55f;
    flying.cometOdds = 150;
    flying.maxComets = 3;
    flying.streakScale = surprise::starStreak(speed);
    return flying;
}

//*******************************
// GuiAbout::loop
//*******************************
void GuiAbout::loop() {
    std::shared_ptr<Gui> gui(Gui::getInstance());
    menuVisible = true;
    surpriseMode = false;
    crossHeld = false;
    // AB_TRACE_ABOUT: the first frames after each change of what the screen shows (credits, title, game)
    const char *traceMode = "credits";
    int traceFrames = 0;
    while (menuVisible) {
        unsigned int ticks = gui->platform().ticks();

        if (surpriseMode && game.onTitle()) {
            game.updateTitle(ticks);
        } else if (surpriseMode) {
            game.update(ticks, gui->input().dpadLeft(), gui->input().dpadRight(), crossHeld);

            // remembered here, written once when the screen closes: config.ini on every point scored was
            // a write per frame at the end of a good game
            if (game.currentHighScore() > savedHighScore)
                savedHighScore = game.currentHighScore();
        }

        if (traceAbout() && traceFrames < 5) {
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
            if (surpriseMode && game.enteringInitials()) {
                if (press)
                    game.initialsPress(e.button, ticks);
                continue;
            }
            // the attract demo: Start plays, any other press goes back to the title
            if (surpriseMode && game.demo() && press && e.button != Button::Start) {
                game.showTitle();
                continue;
            }
            // the table after an entry: Cross goes on to the title (Start plays again and Circle leaves, as below)
            if (surpriseMode && game.showingScores() && press && (!game.scoresInputReady() || e.button == Button::Cross)) {
                if (game.scoresInputReady())
                    game.showTitle();
                continue;
            }
            if (surpriseMode && game.onTitle() && press) {
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
                    if (surpriseMode) {
                        // on the title Start begins the game, in the game it restarts it
                        traceMode = "game";
                        traceFrames = 0;
                        game.reset(ticks);
                        crossHeld = false;
                        app.audio().cursor.play();
                    } else {
                        loadGameAssets();
                        ticks = gui->platform().ticks(); // the load took a while: the title starts from now
                        surpriseMode = true;
                        traceMode = "title";
                        traceFrames = 0;
                        crossHeld = false;
                        game.reset(ticks);
                        game.showTitle();
                        // the star field turns into the game's (flyingStyle(), set every frame from its speed)
                        fx.setStyle(flyingStyle(game.speed()));
                        if (app.audio().music.isPlaying()) {
                            // something is already playing (the theme's track or a custom one) -
                            // just duck it to 50% behind the game
                            app.audio().music.setVolume(64);
                            duckedThemeMusic = true;
                        } else {
                            // a silent theme or "nomusic": Surprise mode still gets some music
                            surpriseMusic.play(-1);
                            surpriseMusic.setVolume(96);
                            playingFallbackMusic = true;
                        }
                        app.audio().cursor.play();
                    }
                } else if (surpriseMode && e.button == Button::Cross) {
                    crossHeld = true;
                } else if (surpriseMode && e.button == Button::Circle) {
                    // Circle leaves the game, back to the credits (GOD MODE ends with it)
                    surpriseMode = false;
                    game.disarmGodMode();
                    fx.setStyle(StarFx::Style()); // back to the About screen's calm backdrop
                    if (duckedThemeMusic)
                        app.audio().music.setVolume(128);
                    if (playingFallbackMusic) {
                        surpriseMusic.halt();
                        app.audio().playMusic(); // resume whatever the theme/config normally plays
                    }
                    duckedThemeMusic = false;
                    playingFallbackMusic = false;
                    app.audio().cancel.play();
                } else if (!surpriseMode && e.button == Button::Circle) {
                    app.audio().cancel.play();
                    menuVisible = false;
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
    // the table and the best score, written once as the screen closes and only when they changed
    bool changed = false;
    if (to_string(savedHighScore) != app.config().inifile.values["surprisehighscore"]) {
        app.config().inifile.values["surprisehighscore"] = to_string(savedHighScore);
        changed = true;
    }
    if (sprites.ship.valid() && game.scoresText() != app.config().inifile.values["surprisescores"]) {
        app.config().inifile.values["surprisescores"] = game.scoresText();
        changed = true;
    }
    if (changed)
        app.config().save();
}
