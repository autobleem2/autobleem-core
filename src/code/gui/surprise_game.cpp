//
// Created for the "Surprise" easter egg on the About screen.
//

#include "surprise_game.h"
#include "gui.h"
#include "surprise_layout.h"
#include "text_renderer.h"

#include <algorithm>
#include <cmath>
#include <string>

using namespace std;

namespace {
const int ShipW = 56, ShipH = 42;
const int AlienW = 48, AlienH = 42;
const int LaserW = 8, LaserH = 32;
const int PowerUpSize = 30;

const int Rows = 4, Cols = 7;
const int ColSpacing = 68, RowSpacing = 56;
const int TopMargin = 100;

const float PlayerBulletSpeed = 9.0f; // px per 16ms frame
const float AlienBulletSpeed = 4.0f;
const float PowerUpFallSpeed = 2.2f;
const unsigned int FireCooldownMs = 220;
const unsigned int HitInvulnMs = 5000; // 5s - long enough to recover after a hit
const int MaxPlayerBullets = 6;        // generous enough for a 3-bolt Spread volley plus a bit more
const float FormationMaxY = 220.0f;    // never lets the wave crawl down onto the ship

const float DiveDurationFrames = 90.0f;   // ~1.5s at 60fps: swoop from formation to off the bottom
const float ReturnDurationFrames = 60.0f; // ~1s: fly back in, either after a dive or at wave start

// the Warblade-style continuous weave: each row sways on its own sine phase, so the wave ripples as a
// whole instead of marching and bouncing as a rigid block
const float SwayAmplitude = 46.0f;
const float SwayAngularSpeed = 1.0f;            // rad/s at wave 1
const float RowPhaseStep = 0.7f;                // radians of phase offset between adjacent rows
const float FormationDriftSpeed = 0.05f;        // px per 16ms frame, downward, scaled by waveSpeedScale
const float EnemySpeedStepPerFiveWaves = 0.15f; // dives, shots and the wave itself, every 5th wave

const unsigned int EntranceStaggerMs = 180;   // extra entrance delay per row
const unsigned int EntranceColStaggerMs = 40; // extra entrance delay per column, for a diagonal cascade

const int PowerUpDropPercent = 10;     // chance an exploded alien drops a timed power-up (rapid/spread/power; was 20)
const int ExtraLifeDropPercent = 1;    // separate chance it drops an extra life instead (was 2; 10 was a life a wave)
const int ExtraLifeEveryNthDrop = 50;  // ...and whatever the dice say, the Nth drop since the last one is a life
const float ExtraLifeFallScale = 0.6f; // a life falls slower than the timed power-ups, so it can be caught
const unsigned int PowerUpDurationMs = 10000;

const unsigned int LifeLostFreezeMs = surprise::LifeLostFreezeMs;
const unsigned int SlowFrameMs = 100; // a frame this long means the pop-in cannot animate
const bool CountUpFinalScore =
    true; // false: the final score shows at once (if the count's rebuilds cost on the console)

const float ShipY = SCREEN_HEIGHT - 90.0f; // the ship's row

bool overlaps(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

const ableem::Button Konami[] = {ableem::Button::DpadUp,   ableem::Button::DpadUp,    ableem::Button::DpadDown,
                                 ableem::Button::DpadDown, ableem::Button::DpadLeft,  ableem::Button::DpadRight,
                                 ableem::Button::DpadLeft, ableem::Button::DpadRight, ableem::Button::Cross,
                                 ableem::Button::Circle};
const size_t KonamiLength = sizeof(Konami) / sizeof(Konami[0]);

// the lives counter under the cheat: an infinity sign (Oxanium has it, as Open Sans does)
const char *const InfinitySign = "\xE2\x88\x9E";
} // namespace

//*******************************
// KonamiCode::wouldComplete / feed
//*******************************
bool KonamiCode::wouldComplete(ableem::Button button) const {
    if (recent.size() + 1 < KonamiLength)
        return false;
    // the last KonamiLength - 1 presses plus this one
    for (size_t i = 0; i + 1 < KonamiLength; i++)
        if (recent[recent.size() - (KonamiLength - 1) + i] != Konami[i])
            return false;
    return button == Konami[KonamiLength - 1];
}

bool KonamiCode::feed(ableem::Button button) {
    if (wouldComplete(button)) {
        recent.clear();
        return true;
    }
    recent.push_back(button);
    if (recent.size() > KonamiLength - 1)
        recent.erase(recent.begin());
    return false;
}

//*******************************
// SurpriseGame::armGodMode
//*******************************
void SurpriseGame::armGodMode() {
    if (godArmed_)
        return;
    godArmed_ = true;
    godSplashTicks_ = lastTicks;
    attract_ = Attract::Title;
    attractSince_ = lastTicks;
    sounds.powerup.play();
    sounds.waveClear.play();
}

//*******************************
// SurpriseGame::showTitle / titleInput
//*******************************
void SurpriseGame::showTitle() {
    onTitle_ = true;
    demo_ = false;
    entering_ = false;
    showingScores_ = false;
    litRow_ = -1;
    attract_ = Attract::Title;
    attractSince_ = lastTicks;
}

void SurpriseGame::titleInput() {
    attract_ = Attract::Title;
    attractSince_ = lastTicks;
}

//*******************************
// SurpriseGame::seedScores
//*******************************
void SurpriseGame::seedScores(const std::string &text, int legacyHighScore) {
    table_ = surprise::parseScoreTable(text, legacyHighScore);
    if (!table_.empty())
        seedHighScore(table_.front().score);
}

//*******************************
// SurpriseGame::targetSpeed / advanceLayers
//*******************************
float SurpriseGame::targetSpeed() const {
    if (onTitle_ || showingScores_)
        return surprise::SpeedCruise;
    if (gameOver())
        return dying() ? surprise::SpeedCruise : surprise::SpeedOver;
    return lastTicks - launchTicks_ < surprise::BoostMs ? surprise::SpeedBoost : surprise::SpeedPlay;
}

void SurpriseGame::advanceLayers(unsigned int dtMs) {
    speed_ = surprise::easedSpeed(speed_, targetSpeed(), dtMs);
    skyY_ += dtMs * surprise::SkyPxPerMs * speed_;
    beltFarY_ = fmod(beltFarY_ + dtMs * surprise::BeltFarPxPerMs * speed_, surprise::BeltStripH);
    beltNearY_ = fmod(beltNearY_ + dtMs * surprise::BeltNearPxPerMs * speed_, surprise::BeltStripH);
}

//*******************************
// SurpriseGame::updateTitle
//*******************************
// the title's clock, and the attract loop: the title, then the table, then a short demo, then the title again
void SurpriseGame::updateTitle(unsigned int nowTicks) {
    unsigned int dt = (lastTicks == 0 || nowTicks < lastTicks) ? 16 : min(200u, nowTicks - lastTicks);
    lastTicks = nowTicks;
    advanceLayers(dt);
    if (godArmed_ && nowTicks - godSplashTicks_ < surprise::GodSplashMs)
        return; // the splash holds the loop
    const unsigned int shown = nowTicks - attractSince_;
    if (attract_ == Attract::Title && shown >= surprise::AttractTitleMs) {
        attract_ = Attract::Scores;
        attractSince_ = nowTicks;
    } else if (attract_ == Attract::Scores && shown >= surprise::AttractScoresMs) {
        reset(nowTicks, true);
    }
}

//*******************************
// SurpriseGame::reset
//*******************************
void SurpriseGame::reset(unsigned int nowTicks, bool demo) {
    shipX = SCREEN_WIDTH / 2.0f - ShipW / 2.0f;
    lives = 3;
    score = 0;
    wave = 1;
    demo_ = demo;
    cheating = godArmed_ && !demo; // GOD MODE: unlimited lives, the score does not count
    launchTicks_ = nowTicks;
    deathTicks_ = 0;
    bigBoom_ = false;
    entering_ = false;
    entryAsked_ = false;
    showingScores_ = false;
    litRow_ = -1;
    activePowerUp = PowerUpType::None;
    powerUpUntilTicks = 0;
    dropsSinceExtraLife = 0;
    playerBullets.clear();
    alienBullets.clear();
    powerUps.clear();
    explosions.clear();
    onTitle_ = false;
    lastTicks = nowTicks;
    lastShotTicks = 0;
    hitInvulnUntil = 0;
    nextDiveAtTicks = nowTicks + surprise::LaunchMs + 2500;
    freezeUntilTicks = 0;
    totalFrozenMs = 0;
    freezeStatic = false;
    lastDtMs = 0;
    gameOverTicks = 0;
    startHighScore = highScore;
    // the first wave's clock starts after the launch: its entrance cascade runs from there
    spawnWave(nowTicks + surprise::LaunchMs);
}

//*******************************
// SurpriseGame::addExplosion
//*******************************
void SurpriseGame::addExplosion(float centreX, float centreY, unsigned int startTicks, float scale) {
    Explosion e;
    e.x = centreX - AlienW / 2.0f;
    e.y = centreY - AlienH / 2.0f;
    e.startTicks = startTicks;
    e.scale = scale;
    explosions.push_back(e);
}

//*******************************
// SurpriseGame::initialsPress / finishInitials
//*******************************
// the classic way: Up/Down change the letter, Cross takes it and goes to the next, Circle goes back one (it does not
// leave here), Start takes the name as it is; the third Cross ends it
void SurpriseGame::initialsPress(ableem::Button button, unsigned int nowTicks) {
    // the first moments ignore presses: the fire button still being hammered from the game must not type the name
    if (!entering_ || nowTicks - entryOpenedTicks_ < surprise::EntryInputGuardMs)
        return;
    entryLastInput_ = nowTicks;
    char &c = entryName_[static_cast<size_t>(entryPos_)];
    switch (button) {
    case ableem::Button::DpadUp:
        c = surprise::stepInitial(c, 1);
        sounds.playerShoot.play();
        break;
    case ableem::Button::DpadDown:
        c = surprise::stepInitial(c, -1);
        sounds.playerShoot.play();
        break;
    case ableem::Button::Cross:
    case ableem::Button::DpadRight:
        if (entryPos_ + 1 >= surprise::InitialsLength) {
            if (button == ableem::Button::Cross)
                finishInitials(nowTicks);
            break;
        }
        entryPos_++;
        if (entryName_[static_cast<size_t>(entryPos_)] == ' ')
            entryName_[static_cast<size_t>(entryPos_)] = c; // the next letter starts where this one is
        sounds.powerup.play();
        break;
    case ableem::Button::Circle:
    case ableem::Button::DpadLeft:
        if (entryPos_ > 0) {
            entryPos_--;
            sounds.enemyShoot.play();
        }
        break;
    case ableem::Button::Start:
        finishInitials(nowTicks);
        break;
    default:
        break;
    }
}

void SurpriseGame::finishInitials(unsigned int nowTicks) {
    if (!entering_)
        return;
    entering_ = false;
    surprise::insertScore(table_, entryRank_, {surprise::cleanInitials(entryName_), score});
    litRow_ = entryRank_;
    showingScores_ = true;
    scoresSince_ = nowTicks;
    sounds.waveClear.play();
}

//*******************************
// SurpriseGame::updateAfterGame
//*******************************
// the last life's slow-down and explosion, GAME OVER, then the initials (a table score) and the table with them lit
void SurpriseGame::updateAfterGame(unsigned int nowTicks) {
    if (dying()) {
        if (!bigBoom_ && nowTicks - deathTicks_ >= surprise::DyingSlowMs) {
            bigBoom_ = true;
            // five blasts over the ship, one after another
            const float cx = shipX + ShipW / 2.0f, cy = ShipY + ShipH / 2.0f;
            const float offsets[5][3] = {{0, 0, 0}, {-22, -14, 90}, {24, -10, 170}, {-6, 16, 250}, {10, -26, 330}};
            for (const auto &o : offsets)
                addExplosion(cx + o[0], cy + o[1], nowTicks + static_cast<unsigned int>(o[2]), surprise::BigBoomScale);
            sounds.explosion.play();
        }
        return;
    }
    if (entering_) {
        if (nowTicks - entryLastInput_ >= surprise::EntryIdleMs)
            finishInitials(nowTicks);
        return;
    }
    if (showingScores_) {
        if (nowTicks - scoresSince_ >= surprise::ScoresAfterEntryMs)
            showTitle();
        return;
    }
    // GAME OVER has shown for a while: a score for the table asks for the initials
    // (asked once a game: without one GAME OVER stays until Start or Circle, as before)
    if (!demo_ && !cheating && !entryAsked_ && nowTicks - gameOverTicks >= surprise::EntryAfterGameOverMs) {
        entryAsked_ = true;
        const int rank = surprise::scoreRank(table_, score);
        if (rank >= 0) {
            entering_ = true;
            entryRank_ = rank;
            entryName_ = "A  ";
            entryPos_ = 0;
            entryLastInput_ = nowTicks;
            entryOpenedTicks_ = nowTicks;
        }
    }
}

//*******************************
// SurpriseGame::demoPilot
//*******************************
// the attract demo's pilot: steers under an alien of the formation (another every DemoTargetMs) and fires all along
void SurpriseGame::demoPilot(unsigned int nowTicks, bool &moveLeft, bool &moveRight, bool &fireHeld) const {
    vector<const Alien *> resting;
    for (const Alien &a : aliens)
        if (a.alive && !a.diving && !a.returning)
            resting.push_back(&a);
    float target = SCREEN_WIDTH / 2.0f;
    if (!resting.empty())
        target = resting[(nowTicks / surprise::DemoTargetMs) % resting.size()]->x + AlienW / 2.0f;
    const float diff = target - (shipX + ShipW / 2.0f);
    moveLeft = diff < -6;
    moveRight = diff > 6;
    fireHeld = true;
}

//*******************************
// SurpriseGame::spawnWave
//*******************************
void SurpriseGame::spawnWave(unsigned int nowTicks) {
    aliens.clear();
    int gridWidth = (Cols - 1) * ColSpacing + AlienW;
    float startX = (SCREEN_WIDTH - gridWidth) / 2.0f;

    for (int r = 0; r < Rows; r++) {
        for (int c = 0; c < Cols; c++) {
            Alien a;
            a.baseX = startX + c * ColSpacing;
            a.baseY = TopMargin + r * RowSpacing;
            a.row = r;
            a.kind = r % 2;
            a.returning = true;
            a.returnT = 0;
            a.returnStartX = a.baseX;
            a.returnStartY = -static_cast<float>(AlienH) - r * 30.0f;
            a.entranceDelayUntil = nowTicks + r * EntranceStaggerMs + c * EntranceColStaggerMs;
            a.x = a.returnStartX;
            a.y = a.returnStartY;
            aliens.push_back(a);
        }
    }
    formationY = 0;
    // 12% quicker each wave, and a further step every fifth wave (5, 10, 15, ...) - the step is what makes
    // the dives and the shots faster too, which the per-wave ramp deliberately leaves alone
    waveSpeedScale = 1.0f + (wave - 1) * 0.12f;
    enemySpeedScale = 1.0f + ((wave - 1) / 5) * EnemySpeedStepPerFiveWaves;
    waveSpeedScale *= enemySpeedScale;
    playerBullets.clear();
    alienBullets.clear();
}

//*******************************
// SurpriseGame::tryFire
//*******************************
void SurpriseGame::tryFire(unsigned int nowTicks) {
    if (gameOver())
        return;

    unsigned int cooldown = (activePowerUp == PowerUpType::Rapid) ? FireCooldownMs / 3 : FireCooldownMs;
    if (nowTicks - lastShotTicks < cooldown)
        return;

    int aliveCount = 0;
    for (const Bullet &b : playerBullets)
        if (b.alive)
            aliveCount++;
    if (aliveCount >= MaxPlayerBullets)
        return;

    float startX = shipX + ShipW / 2.0f - LaserW / 2.0f;
    float startY = SCREEN_HEIGHT - 90.0f - LaserH;

    if (activePowerUp == PowerUpType::Spread) {
        const float driftAngles[3] = {-2.2f, 0.0f, 2.2f};
        for (float vx : driftAngles) {
            Bullet b;
            b.x = startX;
            b.y = startY;
            b.vx = vx;
            b.alive = true;
            playerBullets.push_back(b);
        }
    } else if (activePowerUp == PowerUpType::Power) {
        Bullet b;
        b.x = startX;
        b.y = startY;
        b.pierceLeft = 2; // this bolt can pass through up to 3 aliens total
        b.pierceShot = true;
        b.alive = true;
        playerBullets.push_back(b);
    } else {
        Bullet b;
        b.x = startX;
        b.y = startY;
        b.alive = true;
        playerBullets.push_back(b);
    }

    lastShotTicks = nowTicks;
    sounds.playerShoot.play();
}

//*******************************
// SurpriseGame::awayFromFormationCount
//*******************************
int SurpriseGame::awayFromFormationCount() const {
    int n = 0;
    for (const Alien &a : aliens)
        if (a.alive && (a.diving || a.returning))
            n++;
    return n;
}

//*******************************
// SurpriseGame::restX / restY
// the alien's current "at rest" position: baseY plus the wave's slow downward creep for Y, baseX plus this
// row's own sine sway for X - this is what gives the formation its continuous Warblade-style ripple.
//*******************************
float SurpriseGame::restX(const Alien &a, unsigned int nowTicks) const {
    // frozen time doesn't count towards the sway phase, or the whole formation would visibly jump ahead by
    // the frozen duration the instant a "life lost" freeze ends
    float t = (nowTicks - totalFrozenMs) / 1000.0f;
    return a.baseX + SwayAmplitude * sinf(SwayAngularSpeed * waveSpeedScale * t + a.row * RowPhaseStep);
}

float SurpriseGame::restY(const Alien &a) const {
    return a.baseY + formationY;
}

//*******************************
// SurpriseGame::maybeStartDive
//*******************************
void SurpriseGame::maybeStartDive(unsigned int nowTicks) {
    if (gameOver())
        return;
    if (nowTicks < nextDiveAtTicks)
        return;
    if (awayFromFormationCount() >= 2)
        return;

    vector<int> candidates;
    for (size_t i = 0; i < aliens.size(); i++) {
        if (aliens[i].alive && !aliens[i].diving && !aliens[i].returning)
            candidates.push_back(static_cast<int>(i));
    }
    if (candidates.empty())
        return;

    uniform_int_distribution<int> pick(0, static_cast<int>(candidates.size()) - 1);
    Alien &a = aliens[candidates[pick(rng)]];
    a.diving = true;
    a.diveT = 0;
    a.diveStartX = a.x;
    a.diveStartY = a.y;
    a.diveTargetX = shipX + ShipW / 2.0f;

    uniform_int_distribution<int> nextDelay(1400, 3200);
    nextDiveAtTicks = nowTicks + static_cast<unsigned int>(nextDelay(rng) / waveSpeedScale);
}

//*******************************
// SurpriseGame::updateAliens
//*******************************
void SurpriseGame::updateAliens(float dtFrames, unsigned int nowTicks) {
    bool anyResting = false;
    for (const Alien &a : aliens) {
        if (a.alive && !a.diving && !a.returning) {
            anyResting = true;
            break;
        }
    }
    if (anyResting && formationY < FormationMaxY) {
        formationY += FormationDriftSpeed * waveSpeedScale * dtFrames;
        if (formationY > FormationMaxY)
            formationY = FormationMaxY;
    }

    for (Alien &a : aliens) {
        if (!a.alive)
            continue;

        if (a.diving) {
            a.diveT += dtFrames * enemySpeedScale / DiveDurationFrames;
            if (a.diveT >= 1.0f) {
                // off the bottom of the screen now - loop back in from the top instead of teleporting
                // straight back into the formation slot, so it reads as "flying back", not "reappearing"
                a.diving = false;
                a.returning = true;
                a.returnT = 0;
                a.returnStartX = a.baseX;
                a.returnStartY = -static_cast<float>(AlienH);
                a.entranceDelayUntil = 0;
                a.x = a.returnStartX;
                a.y = a.returnStartY;
                continue;
            }
            float t = a.diveT;
            float u = 1.0f - t;
            float endY = SCREEN_HEIGHT + 40.0f;
            float ctrlY = SCREEN_HEIGHT * 0.55f;
            // quadratic bezier: start -> (target x, mid height) -> (target x, off the bottom of the screen)
            a.x = u * u * a.diveStartX + 2 * u * t * a.diveTargetX + t * t * a.diveTargetX;
            a.y = u * u * a.diveStartY + 2 * u * t * ctrlY + t * t * endY;
            continue;
        }

        if (a.returning) {
            if (nowTicks < a.entranceDelayUntil) {
                a.x = a.returnStartX;
                a.y = a.returnStartY;
                continue; // still waiting for its turn in the cascading entrance
            }
            a.returnT += dtFrames / ReturnDurationFrames;
            float targetX = restX(a, nowTicks);
            float targetY = restY(a);
            if (a.returnT >= 1.0f) {
                a.returning = false;
                a.x = targetX;
                a.y = targetY;
                continue;
            }
            a.x = a.returnStartX + (targetX - a.returnStartX) * a.returnT;
            a.y = a.returnStartY + (targetY - a.returnStartY) * a.returnT;
            continue;
        }

        a.x = restX(a, nowTicks);
        a.y = restY(a);
    }

    maybeStartDive(nowTicks);

    // an alien firing down at the ship: only the bottom-most alive alien in each column may fire, so shots
    // always look like they come from the front rank
    if (!gameOver()) {
        uniform_int_distribution<int> chance(0, 219);
        for (auto &a : aliens) {
            if (!a.alive || a.diving || a.returning)
                continue;
            bool blocked = false;
            for (const Alien &other : aliens) {
                if (&other == &a || !other.alive)
                    continue;
                if (other.baseX == a.baseX && other.baseY > a.baseY) {
                    blocked = true;
                    break;
                }
            }
            if (blocked)
                continue;
            if (chance(rng) != 0)
                continue;

            Bullet b;
            b.x = a.x + AlienW / 2.0f - LaserW / 2.0f;
            b.y = a.y + AlienH;
            b.alive = true;
            alienBullets.push_back(b);
            sounds.enemyShoot.play();
        }
    }
}

//*******************************
// SurpriseGame::updateBullets
//*******************************
void SurpriseGame::updateBullets(float dtFrames) {
    for (Bullet &b : playerBullets) {
        if (!b.alive)
            continue;
        b.y -= PlayerBulletSpeed * dtFrames;
        b.x += b.vx * dtFrames;
        if (b.y < -LaserH || b.x < -LaserW || b.x > SCREEN_WIDTH)
            b.alive = false;
    }
    for (Bullet &b : alienBullets) {
        if (!b.alive)
            continue;
        b.y += AlienBulletSpeed * enemySpeedScale * dtFrames;
        if (b.y > SCREEN_HEIGHT)
            b.alive = false;
    }
    playerBullets.erase(remove_if(playerBullets.begin(), playerBullets.end(), [](const Bullet &b) { return !b.alive; }),
                        playerBullets.end());
    alienBullets.erase(remove_if(alienBullets.begin(), alienBullets.end(), [](const Bullet &b) { return !b.alive; }),
                       alienBullets.end());
}

//*******************************
// SurpriseGame::killAlien
//*******************************
void SurpriseGame::killAlien(Alien &a, unsigned int nowTicks) {
    a.alive = false;
    Explosion e;
    e.x = a.x;
    e.y = a.y;
    e.startTicks = nowTicks;
    explosions.push_back(e);
}

//*******************************
// SurpriseGame::maybeDropPowerUp
//*******************************
void SurpriseGame::maybeDropPowerUp(float x, float y) {
    // one roll picks between an extra life (ExtraLifeDropPercent), one of the three timed power-ups
    // (PowerUpDropPercent), or nothing - so the two chances don't stack into two pickups on one kill.
    uniform_int_distribution<int> chance(0, 99);
    int roll = chance(rng);

    PowerUpType type;
    if (roll < ExtraLifeDropPercent) {
        type = PowerUpType::ExtraLife;
    } else if (roll < ExtraLifeDropPercent + PowerUpDropPercent) {
        uniform_int_distribution<int> pick(0, 2);
        static const PowerUpType types[3] = {PowerUpType::Rapid, PowerUpType::Spread, PowerUpType::Power};
        type = types[pick(rng)];
    } else {
        return;
    }

    // a bad streak of dice must not go on forever: every ExtraLifeEveryNthDrop-th drop is a life regardless
    if (type != PowerUpType::ExtraLife && ++dropsSinceExtraLife >= ExtraLifeEveryNthDrop) {
        type = PowerUpType::ExtraLife;
    }
    if (type == PowerUpType::ExtraLife) {
        dropsSinceExtraLife = 0;
    }

    PowerUp p;
    p.x = x;
    p.y = y;
    p.type = type;
    p.alive = true;
    powerUps.push_back(p);
}

//*******************************
// SurpriseGame::updatePowerUps
//*******************************
void SurpriseGame::updatePowerUps(float dtFrames, unsigned int nowTicks) {
    float shipY = SCREEN_HEIGHT - 90.0f;
    for (PowerUp &p : powerUps) {
        if (!p.alive)
            continue;
        p.y += PowerUpFallSpeed * (p.type == PowerUpType::ExtraLife ? ExtraLifeFallScale : 1.0f) * dtFrames;
        if (p.y > SCREEN_HEIGHT) {
            p.alive = false;
            continue;
        }
        if (!gameOver() && overlaps(p.x, p.y, PowerUpSize, PowerUpSize, shipX, shipY, ShipW, ShipH)) {
            if (p.type == PowerUpType::ExtraLife) {
                lives++;
            } else {
                activePowerUp = p.type;
                powerUpUntilTicks = nowTicks + PowerUpDurationMs;
            }
            p.alive = false;
            sounds.powerup.play();
        }
    }
    powerUps.erase(remove_if(powerUps.begin(), powerUps.end(), [](const PowerUp &p) { return !p.alive; }),
                   powerUps.end());

    if (activePowerUp != PowerUpType::None && nowTicks >= powerUpUntilTicks) {
        activePowerUp = PowerUpType::None;
    }
}

//*******************************
// SurpriseGame::handleCollisions
//*******************************
void SurpriseGame::handleCollisions(unsigned int nowTicks) {
    for (Bullet &b : playerBullets) {
        if (!b.alive)
            continue;
        for (Alien &a : aliens) {
            if (!a.alive)
                continue;
            if (overlaps(b.x, b.y, LaserW, LaserH, a.x, a.y, AlienW, AlienH)) {
                killAlien(a, nowTicks);
                bumpScore(a.diving ? 150 : 100);
                sounds.explosion.play();
                maybeDropPowerUp(a.x + AlienW / 2.0f - PowerUpSize / 2.0f, a.y);
                if (b.pierceLeft > 0) {
                    b.pierceLeft--;
                } else {
                    b.alive = false;
                }
                break;
            }
        }
    }

    // a POWER bolt also shoots enemy bullets down (only the POWER shot - the owner, 2026-10-02): both pop, and the
    // bolt goes on through as it does through aliens
    for (Bullet &b : playerBullets) {
        if (!b.alive || !b.pierceShot)
            continue;
        for (Bullet &e : alienBullets) {
            if (!e.alive || !overlaps(b.x, b.y, LaserW, LaserH, e.x, e.y, LaserW, LaserH))
                continue;
            e.alive = false;
            addExplosion(e.x + LaserW / 2.0f, e.y + LaserH / 2.0f, nowTicks, surprise::PopScale);
            sounds.explosion.play();
            if (b.pierceLeft > 0) {
                b.pierceLeft--;
            } else {
                b.alive = false;
                break;
            }
        }
    }

    float shipY = ShipY;
    bool invulnerable = nowTicks < hitInvulnUntil || demo_; // the demo pilot is never hit

    if (!invulnerable && !gameOver()) {
        // at most one hit lands per frame - an alien bullet and a diving alien could otherwise both overlap
        // the ship on the same frame and take two lives before hitInvulnUntil has a chance to guard the second
        bool hitThisFrame = false;
        for (Bullet &b : alienBullets) {
            if (!b.alive)
                continue;
            if (overlaps(b.x, b.y, LaserW, LaserH, shipX, shipY, ShipW, ShipH)) {
                b.alive = false;
                loseLife();
                hitInvulnUntil = nowTicks + HitInvulnMs;
                sounds.playerHit.play();
                hitThisFrame = true;
                break;
            }
        }
        for (Alien &a : aliens) {
            if (hitThisFrame)
                break;
            if (!a.alive || !a.diving)
                continue;
            if (overlaps(a.x, a.y, AlienW, AlienH, shipX, shipY, ShipW, ShipH)) {
                killAlien(a, nowTicks);
                loseLife();
                hitInvulnUntil = nowTicks + HitInvulnMs;
                sounds.playerHit.play();
                hitThisFrame = true;
                break;
            }
        }

        if (hitThisFrame) {
            playerBullets.clear();
            alienBullets.clear();
            if (gameOver()) {
                // the last life: no LIFE LOST - the world slows down, the ship shudders and blows up (updateAfterGame),
                // and GAME OVER's own clock starts after that
                deathTicks_ = nowTicks;
                gameOverTicks = nowTicks + surprise::DyingSlowMs + surprise::DyingBoomMs;
            } else {
                // clear every laser on screen and freeze play for a couple of seconds, with a "LIFE LOST" banner
                freezeUntilTicks = nowTicks + LifeLostFreezeMs;
                totalFrozenMs += LifeLostFreezeMs;
                freezeStatic = lastDtMs > SlowFrameMs;
            }
        }
    }

    bool anyAlive = false;
    for (const Alien &a : aliens)
        if (a.alive) {
            anyAlive = true;
            break;
        }
    if (!anyAlive && !gameOver()) {
        wave++;
        bumpScore(500);
        sounds.waveClear.play();
        spawnWave(nowTicks);
    }
}

//*******************************
// SurpriseGame::update
//*******************************
void SurpriseGame::update(unsigned int nowTicks, bool moveLeft, bool moveRight, bool fireHeld) {
    unsigned int dt = (lastTicks == 0 || nowTicks < lastTicks) ? 16 : (nowTicks - lastTicks);
    lastDtMs = dt;
    if (dt > 200)
        dt = 200;
    lastTicks = nowTicks;
    float dtFrames = dt / 16.0f;
    advanceLayers(dt);

    if (gameOver()) {
        updateAfterGame(nowTicks);
        return;
    }
    if (demo_ && nowTicks - launchTicks_ >= surprise::DemoMs) {
        showTitle(); // the demo is over: the attract loop starts over
        return;
    }
    if (nowTicks < freezeUntilTicks)
        return; // "life lost" hit-stun: hold everything in place

    if (demo_)
        demoPilot(nowTicks, moveLeft, moveRight, fireHeld);
    if (nowTicks - launchTicks_ < surprise::BoostMs) {
        // the launch: the ship flies to its row on its own, nothing fires
        moveLeft = moveRight = fireHeld = false;
    }

    const float shipSpeed = 7.0f;
    if (moveLeft)
        shipX -= shipSpeed * dtFrames;
    if (moveRight)
        shipX += shipSpeed * dtFrames;
    // the belt is a wall: the ship stops at the corridor's edges
    shipX = max(static_cast<float>(surprise::CorridorLeft), min(static_cast<float>(surprise::CorridorRight - ShipW), shipX));

    updateAliens(dtFrames, nowTicks);
    updateBullets(dtFrames);
    updatePowerUps(dtFrames, nowTicks);
    handleCollisions(nowTicks);

    bool autofiring = (activePowerUp == PowerUpType::Rapid) && nowTicks - launchTicks_ >= surprise::BoostMs;
    if (fireHeld || autofiring)
        tryFire(nowTicks);
}

//*******************************
// SurpriseGame::renderSky
//*******************************
bool SurpriseGame::renderSky(ableem::Renderer &renderer, const SurpriseSprites &sprites) const {
    if (!sprites.sky.valid())
        return false;
    const ableem::Size size = sprites.sky.size();
    if (size.w <= 0 || size.h < SCREEN_HEIGHT)
        return false;
    // flying forward: the picture moves down, so the row at the top comes from further and further up the loop (by
    // the current speed: a cruise on the title, a rush at the launch)
    unsigned long long scrolled = static_cast<unsigned long long>(skyY_);
    int top = static_cast<int>((size.h - scrolled % static_cast<unsigned long long>(size.h)) % size.h);
    int first = min(static_cast<int>(SCREEN_HEIGHT), size.h - top);
    ableem::Rect src(0, top, size.w, first);
    ableem::Rect dst(0, 0, SCREEN_WIDTH, first);
    renderer.copy(sprites.sky, &src, &dst);
    if (first < SCREEN_HEIGHT) {
        ableem::Rect src2(0, 0, size.w, SCREEN_HEIGHT - first);
        ableem::Rect dst2(0, first, SCREEN_WIDTH, SCREEN_HEIGHT - first);
        renderer.copy(sprites.sky, &src2, &dst2);
    }
    return true;
}

namespace {
ableem::Rect rectOf(const surprise::Box &b) {
    return ableem::Rect(static_cast<int>(lroundf(b.x)), static_cast<int>(lroundf(b.y)), static_cast<int>(lroundf(b.w)),
                        static_cast<int>(lroundf(b.h)));
}

// a frame of a strip of equal frames
ableem::Rect frameOf(int frame, int w, int h) {
    return ableem::Rect(frame * w, 0, w, h);
}

const ableem::Color LabelRed(235, 50, 60, 255);  // the HUD labels, #eb323c
const ableem::Color NeonCyan(40, 235, 255, 255); // the title's small lines
const ableem::Color TimerOff(60, 64, 90, 255);   // an unlit timer segment
} // namespace

//*******************************
// SurpriseGame::renderHud
//*******************************
void SurpriseGame::renderHud(ableem::Renderer &renderer, const SurpriseSprites &sprites, SurpriseHud &hud,
                             unsigned char alpha) {
    using ableem::Align;
    using Gradient = SurpriseHud::Gradient;
    using Slot = SurpriseHud::Slot;
    const SurpriseFonts &f = hud.fonts;
    // the HUD fades in at the launch: everything on it at `alpha` (the labels' shadow only once it is whole)
    SurpriseHud::Fx fx;
    fx.alpha = alpha;
    const bool shadow = alpha == 255;
    auto faded = [alpha](ableem::Color c) {
        return ableem::Color(c.r, c.g, c.b, static_cast<unsigned char>(c.a * alpha / 255));
    };
    const ableem::Color white(255, 255, 255, 255);
    const ableem::Color none(0, 0, 0, 0);

    // top: 1UP | HI-SCORE | WAVE (the hi-score is dimmed under GOD MODE - it is not being played for)
    hud.plate(renderer, 24, 12, 250, 70, fx);
    hud.plate(renderer, 505, 12, 270, 70, fx);
    hud.plate(renderer, 1066, 12, 190, 70, fx);
    hud.shadowText(renderer, f.label, "1UP", 44, 22, Align::Left, faded(LabelRed), shadow);
    hud.chrome(renderer, Slot::Score, f.number, surprise::zeroPad(score, 7), Gradient::Gold, 3, 44, 42, Align::Left,
               none, 0.0f, white, fx);
    hud.shadowText(renderer, f.label, "HI-SCORE", 640, 22, Align::Center, faded(LabelRed), shadow);
    hud.chrome(renderer, Slot::HiScore, f.number, surprise::zeroPad(highScore, 7), Gradient::Ice, 3, 640, 42,
               Align::Center, none, 0.0f, cheating || demo_ ? ableem::Color(128, 128, 128, 255) : white, fx);
    hud.shadowText(renderer, f.label, _("WAVE"), 1236, 22, Align::Right, faded(LabelRed), shadow);
    hud.chrome(renderer, Slot::Wave, f.number, surprise::zeroPad(wave, 2), Gradient::Ice, 3, 1236, 42, Align::Right,
               none, 0.0f, white, fx);

    // bottom left: the lives are collected, so one ship and "x N" (an infinity sign under GOD MODE)
    // the plates sit above the hint bar, whatever its height (dy moves everything on them with the plate)
    const int plateY = surprise::bottomPlateY(barTop_);
    const int dy = plateY - surprise::BottomPlateY;
    hud.plate(renderer, 24, plateY, 150, 58, fx);
    ableem::Rect shipIcon(44, 665 + dy, 36, 27);
    ableem::Texture shipTex = sprites.ship; // a shared handle: the alpha is put back for the ship itself
    shipTex.setAlphaMod(alpha);
    renderer.copy(shipTex, nullptr, &shipIcon);
    shipTex.setAlphaMod(255);
    hud.shadowText(renderer, f.semi20, "x", 90, 668 + dy, Align::Left, faded(ableem::Color(200, 205, 225, 255)),
                   shadow);
    // during the freeze the counter (already showing the new count) blinks red every other 125 ms
    const bool frozen = !gameOver() && lastTicks < freezeUntilTicks;
    const bool blinkRed = frozen && surprise::livesBlinkRed(sinceHit());
    hud.chrome(renderer, Slot::Lives, f.number, cheating ? string(InfinitySign) : to_string(max(0, lives)),
               blinkRed ? Gradient::Red : Gradient::Ice, 3, 110, 660 + dy, Align::Left, none, 0.0f, white, fx);

    // bottom right: the power-up in force - its icon, its name in its colour and the time left as ten segments
    if (activePowerUp != PowerUpType::None && !gameOver()) {
        const bool rapid = activePowerUp == PowerUpType::Rapid;
        const bool spread = activePowerUp == PowerUpType::Spread;
        const ableem::Texture &icon = rapid    ? sprites.powerupRapid
                                      : spread ? sprites.powerupSpread
                                               : sprites.powerupPower;
        const ableem::Color color = rapid    ? ableem::Color(255, 224, 96, 255)
                                    : spread ? ableem::Color(110, 230, 150, 255)
                                             : ableem::Color(255, 110, 215, 255);
        string name = rapid ? _("RAPID FIRE") : spread ? _("SPREAD SHOT") : _("POWER SHOT");
        unsigned int remainingMs = (powerUpUntilTicks > lastTicks) ? (powerUpUntilTicks - lastTicks) : 0;
        hud.plate(renderer, 996, plateY, 260, 58);
        ableem::Rect iconRect(1012, 664 + dy, PowerUpSize, PowerUpSize);
        renderer.copy(icon, nullptr, &iconRect);
        hud.shadowText(renderer, f.label, name, 1052, 660 + dy, Align::Left, color);
        hud.segments(renderer, 1054, 686 + dy, 10, surprise::timerSegments(remainingMs, PowerUpDurationMs, 10), color,
                     TimerOff);
    }
}

//*******************************
// SurpriseGame::renderTitle
//*******************************
void SurpriseGame::renderTitle(ableem::Renderer &renderer, const SurpriseSprites &sprites, SurpriseHud &hud) {
    using ableem::Align;
    using Gradient = SurpriseHud::Gradient;
    using Slot = SurpriseHud::Slot;
    const SurpriseFonts &f = hud.fonts;

    // the attract loop's table: the ten best, PUSH START BUTTON under them
    if (attract_ == Attract::Scores) {
        renderScores(renderer, hud);
        if ((lastTicks / 500) % 2 == 0)
            hud.chrome(renderer, Slot::TitlePush, f.push, _("PUSH START BUTTON"), Gradient::Ice, 3, 640, 600,
                       Align::Center);
        return;
    }

    hud.shadowText(renderer, f.semi20, _("AUTOBLEEM PRESENTS"), 640, 92, Align::Center, NeonCyan);
    // the logo: chrome with a blue glow, "RELOADED" in pink under it and to the right, both leaning
    hud.chrome(renderer, Slot::TitleMain, f.title, "BLEEMSTRIKE", Gradient::Chrome, 6, 640, 128, Align::Center,
               ableem::Color(40, 120, 255, 255), -0.18f);
    hud.chrome(renderer, Slot::TitleSub, f.subtitle, "RELOADED", Gradient::Pink, 4, 820, 250, Align::Center,
               ableem::Color(255, 60, 190, 255), -0.18f);

    // the ship between a manta and a jellyfish
    ableem::Rect ship(584, 360, 112, 84);
    renderer.copy(sprites.ship, nullptr, &ship);
    const int frame = surprise::alienFrame(lastTicks);
    ableem::Rect mantaSrc = frameOf(frame, surprise::AlienFrameW, surprise::AlienFrameH);
    ableem::Rect mantaDst(427, 377, surprise::AlienFrameW, surprise::AlienFrameH);
    renderer.copy(sprites.enemy1, &mantaSrc, &mantaDst);
    ableem::Rect jellyDst(799, 377, surprise::AlienFrameW, surprise::AlienFrameH);
    renderer.copy(sprites.enemy2, &mantaSrc, &jellyDst);

    hud.chrome(renderer, Slot::TitlePush, f.push, _("PUSH START BUTTON"), Gradient::Ice, 3, 640, 500, Align::Center);
    hud.shadowText(renderer, f.bold20, "HI-SCORE   " + surprise::zeroPad(highScore, 7), 640, 560, Align::Center,
                   NeonCyan);
    // GOD MODE armed: a steady pink line under the high score once its splash is over
    if (godArmed_ && lastTicks - godSplashTicks_ >= surprise::GodSplashMs)
        hud.shadowText(renderer, f.label, _("GOD MODE"), 640, 596, Align::Center, ableem::Color(255, 110, 215, 255));
    hud.shadowText(renderer, f.credit, "(C) 2026 AUTOBLEEM    " + _("MUSIC") + " CC0 SKETCHYLOGIC", 640, 660,
                   Align::Center, ableem::Color(150, 158, 185, 255), false);
    if (godArmed_ && lastTicks - godSplashTicks_ < surprise::GodSplashMs)
        renderGodSplash(renderer, hud);
}

//*******************************
// SurpriseGame::renderGodSplash
//*******************************
// the Konami code on the title: a pink flash and GOD MODE popping in on a plate, as LIFE LOST does
void SurpriseGame::renderGodSplash(ableem::Renderer &renderer, SurpriseHud &hud) {
    using Gradient = SurpriseHud::Gradient;
    using Slot = SurpriseHud::Slot;
    const unsigned int elapsed = lastTicks - godSplashTicks_;
    const int flash = surprise::lifeLostFlashAlpha(elapsed);
    if (flash > 0) {
        renderer.setBlendMode(ableem::BlendMode::Blend);
        renderer.setDrawColor(ableem::Color(255, 60, 190, static_cast<unsigned char>(flash)));
        renderer.fillRect();
    }
    const surprise::EndFx pop = surprise::lifeLostFx(elapsed, true, surprise::GodSplashMs);
    if (pop.alpha <= 0)
        return;
    const surprise::Box &p = surprise::LifeLostPlate;
    SurpriseHud::Fx fx;
    fx.alpha = static_cast<unsigned char>(pop.alpha);
    fx.scale = pop.scale;
    fx.pivotX = static_cast<int>(p.x + p.w / 2);
    fx.pivotY = static_cast<int>(p.y + p.h / 2);
    hud.plate(renderer, static_cast<int>(p.x), static_cast<int>(p.y), static_cast<int>(p.w), static_cast<int>(p.h), fx);
    hud.chrome(renderer, Slot::GodMode, hud.fonts.subtitle, _("GOD MODE"), Gradient::Gold, 4, surprise::CentreX,
               surprise::LifeLostTextTop, ableem::Align::Center, ableem::Color(255, 60, 190, 255), -0.18f,
               ableem::Color(255, 255, 255, 255), fx);
}

//*******************************
// SurpriseGame::renderScores
//*******************************
// the ten best on a plate between the walls: rank, initials, score; a new entry blinks
void SurpriseGame::renderScores(ableem::Renderer &renderer, SurpriseHud &hud) {
    using ableem::Align;
    using Gradient = SurpriseHud::Gradient;
    using Slot = SurpriseHud::Slot;
    const SurpriseFonts &f = hud.fonts;
    hud.plate(renderer, 390, 96, 500, 470);
    hud.chrome(renderer, Slot::Scores, f.push, _("HIGH SCORES"), Gradient::Chrome, 3, 640, 112, Align::Center,
               ableem::Color(40, 120, 255, 255), -0.18f);
    const bool blinkOn = (lastTicks / 250) % 2 == 0;
    for (size_t i = 0; i < table_.size(); i++) {
        const int y = 166 + static_cast<int>(i) * 38;
        const bool lit = static_cast<int>(i) == litRow_;
        const ableem::Color rankColor = LabelRed;
        const ableem::Color nameColor = lit ? (blinkOn ? ableem::Color(255, 110, 215, 255) : ableem::Color(255, 255, 255, 255))
                                            : ableem::Color(200, 225, 255, 255);
        const ableem::Color scoreColor = lit ? nameColor : ableem::Color(255, 210, 90, 255);
        hud.shadowText(renderer, f.bold20, to_string(i + 1) + ".", 470, y + 6, Align::Right, rankColor);
        hud.shadowText(renderer, f.number, table_[i].initials, 500, y, Align::Left, nameColor);
        hud.shadowText(renderer, f.number, surprise::zeroPad(table_[i].score, 7), 860, y, Align::Right, scoreColor);
    }
}

//*******************************
// SurpriseGame::renderInitials
//*******************************
// a new table score: ENTER YOUR INITIALS, the score, three big letters with the current one blinking under a bar
void SurpriseGame::renderInitials(ableem::Renderer &renderer, SurpriseHud &hud) {
    using ableem::Align;
    using Gradient = SurpriseHud::Gradient;
    using Slot = SurpriseHud::Slot;
    const SurpriseFonts &f = hud.fonts;
    renderer.setBlendMode(ableem::BlendMode::Blend);
    renderer.setDrawColor(ableem::Color(0, 0, 0, 150));
    renderer.fillRect();
    hud.plate(renderer, 390, 150, 500, 330);
    hud.shadowText(renderer, f.label, _("ENTER YOUR INITIALS"), 640, 172, Align::Center, LabelRed);
    hud.chrome(renderer, Slot::EntryScore, f.number, surprise::zeroPad(score, 7), Gradient::Gold, 3, 640, 206,
               Align::Center);
    hud.shadowText(renderer, f.bold20, to_string(entryRank_ + 1) + ".", 640, 252, Align::Center, NeonCyan);
    const bool blinkOn = (lastTicks / 250) % 2 == 0;
    for (int i = 0; i < surprise::InitialsLength; i++) {
        const int cx = 560 + i * 80;
        const bool current = i == entryPos_;
        const string letter(1, entryName_[static_cast<size_t>(i)] == ' ' ? '_' : entryName_[static_cast<size_t>(i)]);
        const ableem::Color color = current ? (blinkOn ? ableem::Color(255, 255, 255, 255)
                                                       : ableem::Color(255, 110, 215, 255))
                                            : ableem::Color(200, 225, 255, 255);
        hud.shadowText(renderer, f.subtitle, letter, cx, 300, Align::Center, color);
        if (current) {
            renderer.setDrawColor(ableem::Color(40, 235, 255, 255));
            renderer.fillRect(ableem::Rect(cx - 28, 378, 56, 4));
        }
    }
}

//*******************************
// SurpriseGame::renderBelt
//*******************************
// the asteroid belt's two layers over the sky and the stars: each strip's left half is the left wall, its right half
// the right one, drawn twice so the loop has no gap
void SurpriseGame::renderBelt(ableem::Renderer &renderer, const SurpriseSprites &sprites) const {
    const struct {
        const ableem::Texture *tex;
        double offset;
    } layers[2] = {{&sprites.beltFar, beltFarY_}, {&sprites.beltNear, beltNearY_}};
    for (const auto &layer : layers) {
        if (!layer.tex->valid())
            continue;
        const int y0 = static_cast<int>(layer.offset);
        for (int y : {y0 - surprise::BeltStripH, y0}) {
            if (y >= SCREEN_HEIGHT || y + surprise::BeltStripH <= 0)
                continue;
            ableem::Rect leftSrc(0, 0, surprise::BeltHalfW, surprise::BeltStripH);
            ableem::Rect leftDst(0, y, surprise::BeltHalfW, surprise::BeltStripH);
            renderer.copy(*layer.tex, &leftSrc, &leftDst);
            ableem::Rect rightSrc(surprise::BeltHalfW, 0, surprise::BeltHalfW, surprise::BeltStripH);
            ableem::Rect rightDst(surprise::BeltRightX, y, surprise::BeltHalfW, surprise::BeltStripH);
            renderer.copy(*layer.tex, &rightSrc, &rightDst);
        }
    }
}

//*******************************
// SurpriseGame::renderShip
//*******************************
// the launch flies the ship from its title spot to its row; the last life shudders it until it blows up; otherwise
// it blinks while briefly invulnerable after a hit
void SurpriseGame::renderShip(ableem::Renderer &renderer, const SurpriseSprites &sprites) {
    if (gameOver()) {
        if (!dying() || bigBoom_)
            return;
        const float shake = 3.0f * (lastTicks - deathTicks_) / surprise::DyingSlowMs;
        uniform_real_distribution<float> jitter(-shake, shake);
        ableem::Rect dst(static_cast<int>(shipX + jitter(rng)), static_cast<int>(ShipY + jitter(rng) / 2), ShipW, ShipH);
        renderer.copy(sprites.ship, nullptr, &dst);
        return;
    }
    const unsigned int sinceLaunch = lastTicks - launchTicks_;
    if (sinceLaunch < surprise::BoostMs) {
        ableem::Rect dst = rectOf(surprise::launchShip(sinceLaunch, shipX, ShipY, ShipW, ShipH));
        renderer.copy(sprites.ship, nullptr, &dst);
        return;
    }
    bool blinkHidden = !demo_ && lastTicks < hitInvulnUntil && ((lastTicks / 100) % 2 == 0);
    if (!blinkHidden) {
        ableem::Rect dst(static_cast<int>(shipX), static_cast<int>(ShipY), ShipW, ShipH);
        renderer.copy(sprites.ship, nullptr, &dst);
    }
}

//*******************************
// SurpriseGame::renderLifeLost
//*******************************
// the 2 s freeze after a hit: a red flash over the frozen field, then the plate with LIFE LOST popping in over it
void SurpriseGame::renderLifeLost(ableem::Renderer &renderer, SurpriseHud &hud) {
    using Gradient = SurpriseHud::Gradient;
    using Slot = SurpriseHud::Slot;
    const unsigned int elapsed = sinceHit();

    // a slow frame leaves no animation: the plate and text stand for the whole freeze and the flash is dropped
    if (!freezeStatic) {
        int flash = surprise::lifeLostFlashAlpha(elapsed);
        if (flash > 0) {
            renderer.setBlendMode(ableem::BlendMode::Blend);
            renderer.setDrawColor(ableem::Color(235, 50, 60, static_cast<unsigned char>(flash)));
            renderer.fillRect();
        }
    }

    const surprise::EndFx pop = surprise::lifeLostFx(elapsed, !freezeStatic);
    if (pop.alpha <= 0)
        return;
    const surprise::Box &p = surprise::LifeLostPlate;
    SurpriseHud::Fx fx;
    fx.alpha = static_cast<unsigned char>(pop.alpha);
    fx.scale = pop.scale;
    fx.pivotX = static_cast<int>(p.x + p.w / 2);
    fx.pivotY = static_cast<int>(p.y + p.h / 2);
    hud.plate(renderer, static_cast<int>(p.x), static_cast<int>(p.y), static_cast<int>(p.w), static_cast<int>(p.h), fx);
    hud.chrome(renderer, Slot::LifeLost, hud.fonts.subtitle, _("LIFE LOST"), Gradient::Pink, 4, surprise::CentreX,
               surprise::LifeLostTextTop, ableem::Align::Center, ableem::Color(255, 60, 190, 255), -0.18f,
               ableem::Color(255, 255, 255, 255), fx);
}

//*******************************
// SurpriseGame::renderGameOver
//*******************************
// the end: the field and the HUD dimmed, GAME OVER dropping in, the final score plate, PUSH START BUTTON blinking
void SurpriseGame::renderGameOver(ableem::Renderer &renderer, SurpriseHud &hud) {
    using ableem::Align;
    using Gradient = SurpriseHud::Gradient;
    using Slot = SurpriseHud::Slot;
    const SurpriseFonts &f = hud.fonts;
    const unsigned int elapsed = lastTicks - gameOverTicks;
    const bool record = newRecord();

    // the dim covers what is drawn so far: the sky, the field, the HUD
    const int dim = surprise::gameOverDimAlpha(elapsed);
    if (dim > 0) {
        renderer.setBlendMode(ableem::BlendMode::Blend);
        renderer.setDrawColor(ableem::Color(0, 0, 0, static_cast<unsigned char>(dim)));
        renderer.fillRect();
    }

    hud.chrome(renderer, Slot::GameOver, f.title, _("GAME OVER"), Gradient::Chrome, 6, surprise::CentreX,
               surprise::gameOverTitleY(elapsed), Align::Center, LabelRed, -0.18f);

    const int plateAlpha = surprise::finalPlateAlpha(elapsed);
    if (plateAlpha > 0) {
        const unsigned char a = static_cast<unsigned char>(plateAlpha);
        const surprise::Box &p = surprise::FinalPlate;
        SurpriseHud::Fx fx;
        fx.alpha = a;
        hud.plate(renderer, static_cast<int>(p.x), static_cast<int>(p.y), static_cast<int>(p.w), static_cast<int>(p.h),
                  fx);

        auto faded = [a](ableem::Color c) {
            return ableem::Color(c.r, c.g, c.b, static_cast<unsigned char>(c.a * a / 255));
        };
        hud.shadowText(renderer, f.label, _("Final score"), surprise::CentreX, surprise::FinalLabelTop, Align::Center,
                       faded(LabelRed));

        const string shown = surprise::zeroPad(surprise::finalScoreShown(elapsed, score, CountUpFinalScore), 7);
        if (record)
            hud.glowLayer(renderer, Slot::FinalScore, f.subtitle, shown, ableem::Color(255, 60, 190, 255),
                          surprise::CentreX, surprise::FinalScoreTop, Align::Center,
                          static_cast<unsigned char>(surprise::recordGlowAlpha(elapsed) * a / 255));
        hud.chrome(renderer, Slot::FinalScore, f.subtitle, shown, Gradient::Gold, 4, surprise::CentreX,
                   surprise::FinalScoreTop, Align::Center, ableem::Color(0, 0, 0, 0), 0.0f,
                   ableem::Color(255, 255, 255, 255), fx);

        renderer.setBlendMode(ableem::BlendMode::Blend);
        renderer.setDrawColor(faded(ableem::Color(40, 235, 255, surprise::FinalDividerAlpha)));
        renderer.fillRect(ableem::Rect(surprise::FinalDividerX0, surprise::FinalDividerY,
                                       surprise::FinalDividerX1 - surprise::FinalDividerX0, 1));

        hud.shadowText(renderer, f.label, "HI-SCORE", surprise::FinalDividerX0, surprise::FinalHiLabelTop, Align::Left,
                       faded(LabelRed));
        hud.chrome(renderer, Slot::FinalHi, f.number, surprise::zeroPad(highScore, 7),
                   record ? Gradient::Gold : Gradient::Ice, 3, surprise::FinalDividerX1, surprise::FinalHiNumberTop,
                   Align::Right, ableem::Color(0, 0, 0, 0), 0.0f, ableem::Color(255, 255, 255, 255), fx);
    }

    if (surprise::pushVisible(elapsed))
        hud.chrome(renderer, Slot::Push, f.push, _("PUSH START BUTTON"), Gradient::Ice, 3, surprise::CentreX,
                   surprise::PushTop, Align::Center);
}

//*******************************
// SurpriseGame::render
//*******************************
void SurpriseGame::render(ableem::Renderer &renderer, TextRenderer &, const ableem::Font &font,
                          const SurpriseSprites &sprites, SurpriseHud &hud) {
    hud.fallback = font;

    // the belt runs under everything, on the title too
    renderBelt(renderer, sprites);

    if (onTitle_) {
        renderTitle(renderer, sprites, hud);
        return;
    }
    if (showingScores_) {
        renderScores(renderer, hud);
        return;
    }

    const int alienFrame = surprise::alienFrame(lastTicks);

    // every halo'd frame is drawn at its README offset over the unchanged hitbox, with the normal blend
    for (const Alien &a : aliens) {
        if (!a.alive)
            continue;
        if (a.diving) {
            ableem::Rect dst = rectOf(surprise::ufoDraw(a.x, a.y));
            renderer.copy(sprites.ufo, nullptr, &dst);
        } else {
            ableem::Rect src = frameOf(alienFrame, surprise::AlienFrameW, surprise::AlienFrameH);
            ableem::Rect dst = rectOf(surprise::alienDraw(a.x, a.y));
            renderer.copy(a.kind == 0 ? sprites.enemy1 : sprites.enemy2, &src, &dst);
        }
    }

    // the explosions of the aliens that died, over where they were (the ship's chained ones may not have started yet)
    for (const Explosion &e : explosions) {
        if (lastTicks < e.startTicks)
            continue;
        int frame = surprise::explosionFrame(lastTicks - e.startTicks);
        if (frame < 0)
            continue;
        ableem::Rect src = frameOf(frame, surprise::ExplosionFrameW, surprise::ExplosionFrameH);
        surprise::Box box = surprise::explosionDraw(e.x, e.y);
        if (e.scale != 1.0f) {
            const float cx = box.x + box.w / 2, cy = box.y + box.h / 2;
            box = {cx - box.w * e.scale / 2, cy - box.h * e.scale / 2, box.w * e.scale, box.h * e.scale};
        }
        ableem::Rect dst = rectOf(box);
        renderer.copy(sprites.explosion, &src, &dst);
    }
    explosions.erase(remove_if(explosions.begin(), explosions.end(),
                               [this](const Explosion &e) {
                                   return lastTicks >= e.startTicks &&
                                          surprise::explosionFrame(lastTicks - e.startTicks) < 0;
                               }),
                     explosions.end());

    for (const PowerUp &p : powerUps) {
        if (!p.alive)
            continue;
        const ableem::Texture &tex = p.type == PowerUpType::Rapid    ? sprites.powerupRapid
                                     : p.type == PowerUpType::Spread ? sprites.powerupSpread
                                     : p.type == PowerUpType::Power  ? sprites.powerupPower
                                                                     : sprites.powerupLife;
        ableem::Rect dst = rectOf(surprise::powerUpDraw(p.x, p.y));
        renderer.copy(tex, nullptr, &dst);
    }

    for (const Bullet &b : playerBullets) {
        if (!b.alive)
            continue;
        ableem::Rect dst =
            rectOf(b.pierceShot ? surprise::pierceLaserDraw(b.x, b.y) : surprise::playerLaserDraw(b.x, b.y));
        renderer.copy(b.pierceShot ? sprites.laserPierce : sprites.laserPlayer, nullptr, &dst);
    }
    const int plasmaFrame = surprise::enemyLaserFrame(lastTicks);
    for (const Bullet &b : alienBullets) {
        if (!b.alive)
            continue;
        ableem::Rect src = frameOf(plasmaFrame, surprise::EnemyLaserFrameW, surprise::EnemyLaserFrameH);
        ableem::Rect dst = rectOf(surprise::enemyLaserDraw(b.x, b.y));
        renderer.copy(sprites.laserEnemy, &src, &dst);
    }

    renderShip(renderer, sprites);

    const unsigned int sinceLaunch = lastTicks - launchTicks_;
    if (!gameOver() && surprise::getReadyVisible(sinceLaunch))
        hud.shadowText(renderer, hud.fonts.push, _("GET READY"), surprise::CentreX, surprise::GetReadyTop,
                       ableem::Align::Center, ableem::Color(255, 255, 255, 255));

    renderHud(renderer, sprites, hud, static_cast<unsigned char>(surprise::hudAlpha(sinceLaunch)));

    if (demo_ && (lastTicks / 500) % 2 == 0)
        hud.chrome(renderer, SurpriseHud::Slot::Push, hud.fonts.push, _("PUSH START BUTTON"),
                   SurpriseHud::Gradient::Ice, 3, surprise::CentreX, surprise::PushTop, ableem::Align::Center);
    if (demo_)
        hud.shadowText(renderer, hud.fonts.label, _("DEMO"), surprise::CentreX, surprise::PushTop - 30,
                       ableem::Align::Center, NeonCyan);

    if (entering_)
        renderInitials(renderer, hud);
    else if (gameOver() && !dying())
        renderGameOver(renderer, hud);
    else if (!gameOver() && lastTicks < freezeUntilTicks)
        renderLifeLost(renderer, hud);
}
