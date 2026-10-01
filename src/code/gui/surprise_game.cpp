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

const float SkyScrollPxPerSecond = 24.0f; // the far layer: slow, the star streaks over it are faster

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
// SurpriseGame::enableInfiniteLives
//*******************************
void SurpriseGame::enableInfiniteLives() {
    if (cheating || gameOver())
        return;
    cheating = true;
    sounds.powerup.play();
    sounds.waveClear.play();
}

//*******************************
// SurpriseGame::reset
//*******************************
void SurpriseGame::reset(unsigned int nowTicks) {
    shipX = SCREEN_WIDTH / 2.0f - ShipW / 2.0f;
    lives = 3;
    score = 0;
    wave = 1;
    cheating = false;
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
    nextDiveAtTicks = nowTicks + 2500;
    freezeUntilTicks = 0;
    totalFrozenMs = 0;
    freezeStatic = false;
    lastDtMs = 0;
    gameOverTicks = 0;
    startHighScore = highScore;
    spawnWave(nowTicks);
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

    float shipY = SCREEN_HEIGHT - 90.0f;
    bool invulnerable = nowTicks < hitInvulnUntil;

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
            // clear every laser on screen and freeze play for a couple of seconds, with a "LIFE LOST" banner
            playerBullets.clear();
            alienBullets.clear();
            freezeUntilTicks = nowTicks + LifeLostFreezeMs;
            totalFrozenMs += LifeLostFreezeMs;
            freezeStatic = lastDtMs > SlowFrameMs;
            if (gameOver())
                gameOverTicks = nowTicks;
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
    unsigned int dt = (lastTicks == 0) ? 16 : (nowTicks - lastTicks);
    lastDtMs = dt;
    if (dt > 200)
        dt = 200;
    lastTicks = nowTicks;
    float dtFrames = dt / 16.0f;

    if (gameOver())
        return;
    if (nowTicks < freezeUntilTicks)
        return; // "life lost" hit-stun: hold everything in place

    const float shipSpeed = 7.0f;
    if (moveLeft)
        shipX -= shipSpeed * dtFrames;
    if (moveRight)
        shipX += shipSpeed * dtFrames;
    shipX = max(10.0f, min(static_cast<float>(SCREEN_WIDTH) - 10.0f - ShipW, shipX));

    updateAliens(dtFrames, nowTicks);
    updateBullets(dtFrames);
    updatePowerUps(dtFrames, nowTicks);
    handleCollisions(nowTicks);

    bool autofiring = (activePowerUp == PowerUpType::Rapid);
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
    // flying forward: the picture moves down, so the row at the top comes from further and further up the loop
    unsigned long long scrolled = static_cast<unsigned long long>(lastTicks * SkyScrollPxPerSecond / 1000.0f);
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
void SurpriseGame::renderHud(ableem::Renderer &renderer, const SurpriseSprites &sprites, SurpriseHud &hud) {
    using ableem::Align;
    using Gradient = SurpriseHud::Gradient;
    using Slot = SurpriseHud::Slot;
    const SurpriseFonts &f = hud.fonts;

    // top: 1UP | HI-SCORE | WAVE (the hi-score is dimmed under the Konami code - it is not being played for)
    hud.plate(renderer, 24, 12, 250, 70);
    hud.plate(renderer, 505, 12, 270, 70);
    hud.plate(renderer, 1066, 12, 190, 70);
    hud.shadowText(renderer, f.label, "1UP", 44, 22, Align::Left, LabelRed);
    hud.chrome(renderer, Slot::Score, f.number, surprise::zeroPad(score, 7), Gradient::Gold, 3, 44, 42, Align::Left);
    hud.shadowText(renderer, f.label, "HI-SCORE", 640, 22, Align::Center, LabelRed);
    hud.chrome(renderer, Slot::HiScore, f.number, surprise::zeroPad(highScore, 7), Gradient::Ice, 3, 640, 42,
               Align::Center, ableem::Color(0, 0, 0, 0), 0.0f,
               cheating ? ableem::Color(128, 128, 128, 255) : ableem::Color(255, 255, 255, 255));
    hud.shadowText(renderer, f.label, _("WAVE"), 1236, 22, Align::Right, LabelRed);
    hud.chrome(renderer, Slot::Wave, f.number, surprise::zeroPad(wave, 2), Gradient::Ice, 3, 1236, 42, Align::Right);

    // bottom left: the lives are collected, so one ship and "x N" (an infinity sign under the cheat)
    // the plates sit above the hint bar, whatever its height (dy moves everything on them with the plate)
    const int plateY = surprise::bottomPlateY(barTop_);
    const int dy = plateY - surprise::BottomPlateY;
    hud.plate(renderer, 24, plateY, 150, 58);
    ableem::Rect shipIcon(44, 665 + dy, 36, 27);
    renderer.copy(sprites.ship, nullptr, &shipIcon);
    hud.shadowText(renderer, f.semi20, "x", 90, 668 + dy, Align::Left, ableem::Color(200, 205, 225, 255));
    // during the freeze the counter (already showing the new count) blinks red every other 125 ms
    const bool frozen = !gameOver() && lastTicks < freezeUntilTicks;
    const ableem::Color livesTint = frozen && surprise::livesBlinkRed(sinceHit()) ? ableem::Color(255, 90, 90, 255)
                                                                                  : ableem::Color(255, 255, 255, 255);
    hud.chrome(renderer, Slot::Lives, f.number, cheating ? string(InfinitySign) : to_string(max(0, lives)),
               Gradient::Ice, 3, 110, 660 + dy, Align::Left, ableem::Color(0, 0, 0, 0), 0.0f, livesTint);

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
    hud.shadowText(renderer, f.credit, "(C) 2026 AUTOBLEEM    " + _("MUSIC") + " CC0 SKETCHYLOGIC", 640, 660,
                   Align::Center, ableem::Color(150, 158, 185, 255), false);
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

    if (onTitle_) {
        renderTitle(renderer, sprites, hud);
        return;
    }

    float shipY = SCREEN_HEIGHT - 90.0f;
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

    // the explosions of the aliens that died, over where they were
    for (const Explosion &e : explosions) {
        int frame = surprise::explosionFrame(lastTicks - e.startTicks);
        if (frame < 0)
            continue;
        ableem::Rect src = frameOf(frame, surprise::ExplosionFrameW, surprise::ExplosionFrameH);
        ableem::Rect dst = rectOf(surprise::explosionDraw(e.x, e.y));
        renderer.copy(sprites.explosion, &src, &dst);
    }
    explosions.erase(
        remove_if(explosions.begin(), explosions.end(),
                  [this](const Explosion &e) { return surprise::explosionFrame(lastTicks - e.startTicks) < 0; }),
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

    // blink the ship while briefly invulnerable after a hit, instead of drawing it solid
    bool blinkHidden = lastTicks < hitInvulnUntil && ((lastTicks / 100) % 2 == 0);
    if (!gameOver() && !blinkHidden) {
        ableem::Rect dst(static_cast<int>(shipX), static_cast<int>(shipY), ShipW, ShipH);
        renderer.copy(sprites.ship, nullptr, &dst);
    }

    renderHud(renderer, sprites, hud);

    if (gameOver())
        renderGameOver(renderer, hud);
    else if (lastTicks < freezeUntilTicks)
        renderLifeLost(renderer, hud);
}
