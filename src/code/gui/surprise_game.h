//
// SurpriseGame: the "Surprise" easter egg on the About screen. Start shows its title screen, Start again begins the
// game; the starfield already behind the credits keeps running as the backdrop. A small shoot-em-up: dpad moves the
// ship, Cross fires, Start restarts on the spot, Circle goes back to the About screen. The Konami code on the title
// screen (B A = Cross Circle, see KonamiCode) shows a GOD MODE splash: the games that follow have unlimited lives and
// their score does not count (no high score, no table entry) until the player leaves for the About screen.
//
// The follow-up of 2026-10-02 (README "The asteroid belt"): the play field runs between two walls of rock that loop
// down both sides (the ship stops at them, they never cost a life), one speed drives the sky, the stars and the belt
// (a slow cruise on the title, a boost at START while the ship flies from its title spot to its row and GET READY
// blinks, then the play speed; the first wave only after the launch), a POWER bolt also shoots enemy bullets down,
// the last life slows the world again and blows the ship up before GAME OVER, a new best score asks for three
// initials and joins the ten-row table, and the title runs an attract loop: the title, the table, a short demo.
//
// Enemy waves fly in staggered from the top and then hold a continuously undulating, snake-like formation
// (each row swaying on its own sine phase, slowly creeping downward) rather than a rigid marching block -
// closer to Warblade's enemy squadrons than classic Space Invaders. Aliens still occasionally peel off to
// dive at the player before looping back in from the top. Destroyed aliens sometimes drop a power-up
// (rapid-fire/autofire, a spread shot, or a piercing "power" shot) that the ship collects by flying over it,
// and more rarely (about 1% of kills) an extra life instead, collected the same way.
//
// "BleemStrike: Reloaded" (UIREV-39): the look of an early 32-bit shooter - rendered sprites with a halo, drawn over
// the unchanged hitboxes (surprise_layout.h), a chrome HUD in Oxanium (surprise_art.h) with the lives as one ship and
// "x N", a title screen, a scrolling sky. The art is the designer's (autobleem-design launcher/surprise/README.md);
// the sound effects are Kenney's "Space Shooter Redux" (CC0 / public domain, www.kenney.nl) plus one CC0 "NES Shooter
// Music" track by SketchyLogic (opengameart.org), copied into resources/surprise_game/ - see the license.txt
// alongside them.
#pragma once

#include "surprise_art.h"
#include "surprise_layout.h"
#include "surprise_scores.h"

#include <ableem/ui/renderer.h>
#include <ableem/ui/texture.h>
#include <ableem/ui/font.h>
#include <ableem/ui/audio.h>
#include <ableem/ui/input.h>
#include <random>
#include <vector>

class TextRenderer;

//******************
// SurpriseSprites
//******************
// Loaded once by GuiAbout::loadGameAssets() (the first Start) and handed to every render() call - Texture is a cheap
// shared handle.
struct SurpriseSprites {
    ableem::Texture ship;
    ableem::Texture enemy1;
    ableem::Texture enemy2;
    ableem::Texture ufo; // the diving alien
    ableem::Texture laserPlayer;
    ableem::Texture laserPierce; // the "power" (piercing) player shot
    ableem::Texture laserEnemy;  // a strip of two frames that flicker
    ableem::Texture powerupRapid;
    ableem::Texture powerupSpread;
    ableem::Texture powerupPower;
    ableem::Texture powerupLife; // the extra life
    ableem::Texture explosion;   // a strip of four frames
    ableem::Texture sky;         // the seamless vertical loop behind the star field; invalid = the old dark backdrop
    ableem::Texture beltFar;     // the asteroid belt's two layers: 600x1440, left wall | right wall; invalid = none
    ableem::Texture beltNear;
};

//******************
// SurpriseSounds
//******************
// Loaded once by GuiAbout::loadGameAssets() (the first Start) and assigned to SurpriseGame::sounds before the first
// update().
struct SurpriseSounds {
    ableem::Sound playerShoot;
    ableem::Sound enemyShoot;
    ableem::Sound explosion;
    ableem::Sound playerHit;
    ableem::Sound waveClear;
    ableem::Sound powerup;
};

//******************
// KonamiCode
//******************
// Up Up Down Down Left Right Left Right B A, fed one press at a time. On the PlayStation pad B and A are
// Cross and Circle - where a SNES pad's B and A sit (bottom and right), as RetroArch maps it.
class KonamiCode {
public:
    // true when this press completes the code (the history is then cleared)
    bool feed(ableem::Button button);
    // true when this press would complete it - the caller can then keep the press from its usual meaning
    bool wouldComplete(ableem::Button button) const;

private:
    std::vector<ableem::Button> recent; // the last presses, at most the code's length
};

//******************
// PowerUpType
//******************
enum class PowerUpType { None, Rapid, Spread, Power, ExtraLife };

//******************
// SurpriseGame
//******************
class SurpriseGame {
public:
    // set once by the owning screen before the first update(), same as StarFx::renderer
    SurpriseSounds sounds;

    // a new game from its launch; `demo` = the attract loop's game, flown by the demo pilot (it never dies, never
    // scores for the table, and ends after DemoMs)
    void reset(unsigned int nowTicks, bool demo = false);

    // moveLeft/moveRight/fireHeld are the current dpad/Cross state, sampled every frame (like the rest of
    // the app's screens); firing itself is cooldown-gated internally (and auto-fires under the Rapid
    // power-up even when fireHeld is false). The demo ignores them.
    void update(unsigned int nowTicks, bool moveLeft, bool moveRight, bool fireHeld);

    // the title screen comes first: Start in it begins the game (reset()), Start in a game restarts it
    void showTitle();
    bool onTitle() const { return onTitle_; }
    // the real top of the hint bar under the play field (the screen's footer rect): the bottom HUD plates stay above it
    void setBarTop(int top) { barTop_ = top; }
    // the title's clock: the sky, the belt and the alien beat, and the attract loop (title -> table -> demo)
    void updateTitle(unsigned int nowTicks);
    // a press on the title (or its table): the attract loop starts over from the title
    void titleInput();
    // the demo of the attract loop is running: any press but Start ends it (showTitle())
    bool demo() const { return demo_; }

    // the speed every layer moves at (the About screen's star field follows it too)
    float speed() const { return speed_; }

    // the Konami code on the title: the GOD MODE splash, and every game until disarmGodMode() has unlimited lives
    void armGodMode();
    void disarmGodMode() { godArmed_ = false; }
    bool godMode() const { return godArmed_; }

    // after a game over with a new best score: three initials. The About screen hands the presses over while this is
    // true (Up/Down the letter, Cross the next, Circle back, Start done)
    bool enteringInitials() const { return entering_; }
    void initialsPress(ableem::Button button, unsigned int nowTicks);
    // the table shown after an entry (Cross ends it early, Start plays again)
    bool showingScores() const { return showingScores_; }
    // the table takes presses only after a moment (the presses of the entry must not skip it)
    bool scoresInputReady() const { return lastTicks - scoresSince_ >= surprise::ScoresInputGuardMs; }

    // the table as config.ini keeps it ("surprisescores"); `legacyHighScore` is the old single high score
    void seedScores(const std::string &text, int legacyHighScore);
    std::string scoresText() const { return surprise::formatScoreTable(table_); }

    // the far layer: the sky, scrolled slowly; false when there is none (the caller then draws its dark backdrop)
    bool renderSky(ableem::Renderer &renderer, const SurpriseSprites &sprites) const;

    // `font` is the launcher's own face, the stand-in for an Oxanium face that did not open (LIFE LOST and GAME OVER
    // are the HUD's lettering now)
    void render(ableem::Renderer &renderer, TextRenderer &text, const ableem::Font &font,
                const SurpriseSprites &sprites, SurpriseHud &hud);

    bool gameOver() const { return lives <= 0; }
    // the last life went and the ship is still slowing and blowing up: GAME OVER has not shown yet
    bool dying() const { return gameOver() && lastTicks < gameOverTicks; }
    int currentScore() const { return score; }
    int currentHighScore() const { return highScore; }
    // seeds the in-session high score from Config's saved value; never lowers it. Call once, before reset().
    void seedHighScore(int hs) {
        if (hs > highScore)
            highScore = hs;
    }
    bool infiniteLives() const { return cheating; }

private:
    struct Bullet {
        float x = 0, y = 0;
        float vx = 0;            // horizontal drift per frame, for the spread shot's angled bolts
        int pierceLeft = 0;      // extra aliens this bolt can pass through after its first hit (the Power shot)
        bool pierceShot = false; // fired under the Power shot: drawn as the piercing bolt for its whole flight
        bool alive = false;
    };

    // a dead alien's explosion, played where it died
    struct Explosion {
        float x = 0, y = 0;          // the alien's hitbox position (or a box of that size centred where it happens)
        unsigned int startTicks = 0; // may lie ahead: the ship's chained explosions
        float scale = 1.0f;          // about the box's centre: the ship's big ones, a bullet's small pop
    };

    struct Alien {
        float baseX = 0, baseY = 0; // formation slot (baseY is also this row's sway/rest position)
        float x = 0, y = 0;         // current position
        int row = 0;
        bool alive = true;
        bool diving = false;    // swooping down at the ship along the bezier dive curve
        bool returning = false; // looping back in from off the top, into the formation slot
        float diveT = 0;        // 0..1 progress through the dive curve
        float diveStartX = 0, diveStartY = 0;
        float diveTargetX = 0;
        float returnT = 0; // 0..1 progress of the return/entrance flight
        float returnStartX = 0, returnStartY = 0;
        unsigned int entranceDelayUntil = 0; // holds position until this tick, then the return flight starts
        int kind = 0;                        // 0/1 -> enemy1/enemy2 sprite while in formation
    };

    struct PowerUp {
        float x = 0, y = 0;
        PowerUpType type = PowerUpType::None;
        bool alive = false;
    };

    std::vector<Alien> aliens;
    std::vector<Bullet> playerBullets;
    std::vector<Bullet> alienBullets;
    std::vector<PowerUp> powerUps;
    std::vector<Explosion> explosions;

    bool onTitle_ = false;
    int barTop_ = 720; // see setBarTop()
    float shipX = 0;
    int dropsSinceExtraLife = 0; // power-ups dropped since the last extra life (see maybeDropPowerUp)
    int lives = 3;
    int score = 0;
    int highScore = 0;
    int startHighScore = 0; // the hi-score when this game began: a score above it, uncheated, is a new record
    int wave = 1;
    bool cheating = false; // GOD MODE: this game's lives are unlimited and its score does not count

    // the belt, the speed, the launch
    float speed_ = surprise::SpeedCruise;
    double skyY_ = 0, beltFarY_ = 0, beltNearY_ = 0; // px scrolled so far
    unsigned int launchTicks_ = 0;                   // when this game's launch began (reset())

    // the last life, the attract loop, the table, GOD MODE
    unsigned int deathTicks_ = 0; // the last life went: the slow-down starts
    bool bigBoom_ = false;        // ...and the ship has blown up
    bool demo_ = false;
    enum class Attract { Title, Scores };
    Attract attract_ = Attract::Title;
    unsigned int attractSince_ = 0;
    std::vector<surprise::ScoreRow> table_ = surprise::defaultScoreTable();
    bool entering_ = false;
    bool entryAsked_ = false; // this game over already looked for a table score
    int entryRank_ = -1;
    std::string entryName_ = "A  ";
    int entryPos_ = 0;
    unsigned int entryLastInput_ = 0;
    bool showingScores_ = false;
    unsigned int scoresSince_ = 0;
    unsigned int entryOpenedTicks_ = 0;
    int litRow_ = -1; // the table's new entry, blinking
    bool godArmed_ = false;
    unsigned int godSplashTicks_ = 0;

    void bumpScore(int delta) {
        score += delta;
        if (!cheating && !demo_ && score > highScore)
            highScore = score;
    }
    void loseLife() {
        if (!cheating && !demo_)
            lives--;
    }

    // the formation's continuous Warblade-style weave: each row sways on its own sine phase and the whole
    // wave slowly creeps downward (capped) rather than bouncing off the screen edges as a rigid block
    float formationY = 0;
    float waveSpeedScale = 1.0f;  // faster sway/descent/dive frequency on later waves
    float enemySpeedScale = 1.0f; // the every-5th-wave step: dive and shot speed (also folded into the above)

    PowerUpType activePowerUp = PowerUpType::None;
    unsigned int powerUpUntilTicks = 0;

    unsigned int lastTicks = 0;
    unsigned int lastShotTicks = 0;
    unsigned int hitInvulnUntil = 0;
    unsigned int nextDiveAtTicks = 0;

    // losing a life clears every laser on screen and freezes all game logic for a couple of seconds (with
    // a "LIFE LOST" banner) before play resumes - totalFrozenMs keeps the alien formation's sine sway from
    // jumping by the frozen duration in a single frame once play resumes
    unsigned int freezeUntilTicks = 0;
    unsigned int totalFrozenMs = 0;
    bool freezeStatic = false;      // the hit came on a slow frame: the LIFE LOST plate does not animate
    unsigned int lastDtMs = 0;      // the last update's frame time, uncapped
    unsigned int gameOverTicks = 0; // when the last life went: the clock of the GAME OVER screen

    std::mt19937 rng{std::random_device{}()};

    void spawnWave(unsigned int nowTicks);
    float targetSpeed() const;
    void advanceLayers(unsigned int dtMs); // the speed eases, the sky and the belt scroll
    void updateAfterGame(unsigned int nowTicks);
    void finishInitials(unsigned int nowTicks);
    void demoPilot(unsigned int nowTicks, bool &moveLeft, bool &moveRight, bool &fireHeld) const;
    void addExplosion(float centreX, float centreY, unsigned int startTicks, float scale);
    void renderBelt(ableem::Renderer &renderer, const SurpriseSprites &sprites) const;
    void renderShip(ableem::Renderer &renderer, const SurpriseSprites &sprites);
    void renderScores(ableem::Renderer &renderer, SurpriseHud &hud);
    void renderInitials(ableem::Renderer &renderer, SurpriseHud &hud);
    void renderGodSplash(ableem::Renderer &renderer, SurpriseHud &hud);
    void maybeStartDive(unsigned int nowTicks);
    void updateAliens(float dtFrames, unsigned int nowTicks);
    void updateBullets(float dtFrames);
    void updatePowerUps(float dtFrames, unsigned int nowTicks);
    void handleCollisions(unsigned int nowTicks);
    void tryFire(unsigned int nowTicks);
    void maybeDropPowerUp(float x, float y);
    void killAlien(Alien &a, unsigned int nowTicks);
    void renderTitle(ableem::Renderer &renderer, const SurpriseSprites &sprites, SurpriseHud &hud);
    void renderHud(ableem::Renderer &renderer, const SurpriseSprites &sprites, SurpriseHud &hud, unsigned char alpha);
    void renderLifeLost(ableem::Renderer &renderer, SurpriseHud &hud);
    void renderGameOver(ableem::Renderer &renderer, SurpriseHud &hud);
    bool newRecord() const { return !cheating && !demo_ && score > startHighScore; }
    unsigned int sinceHit() const { return lastTicks - (freezeUntilTicks - surprise::LifeLostFreezeMs); }
    int awayFromFormationCount() const; // aliens currently diving or returning
    float restX(const Alien &a, unsigned int nowTicks) const;
    float restY(const Alien &a) const;
};
