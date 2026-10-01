//
// SurpriseLayout: the pure numbers of "BleemStrike: Reloaded" (UIREV-39, autobleem-design launcher/surprise/README.md)
// - where a halo'd sprite frame is drawn over the game's UNCHANGED hitbox, which frame of a strip shows when, how the
// score is padded and how many segments of the power-up timer are lit. No drawing, no SDL: tested on their own.
//
#pragma once

#include "core/model/timing.h"

#include <ab_gui/tween.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace surprise {

struct Box {
    float x = 0, y = 0, w = 0, h = 0;
};

// the game's hitboxes (surprise_game.cpp keeps its own copies; the layout tests pin the two together)
const int AlienHitW = 48, AlienHitH = 42;
const int LaserHitW = 8, LaserHitH = 32;
const int PowerUpSize = 30;

// the frames, in px at 720p (the code scales nothing: the renderer's output scale does)
const int AlienFrameW = 54, AlienFrameH = 48; // enemy1/enemy2: a strip of two
const int UfoFrameW = 69, UfoFrameH = 33;     // one frame
const int PlayerLaserFrameW = 15, PlayerLaserFrameH = 39;
const int EnemyLaserFrameW = 27, EnemyLaserFrameH = 45; // a strip of two
const int ExplosionFrameW = 48, ExplosionFrameH = 48;   // a strip of four
const int ExplosionFrames = 4;

// the alien frames alternate on a beat, and the enemy plasma flickers at about 8 fps
const unsigned int AlienBeatMs = 450;
const unsigned int EnemyLaserFlickerMs = 125;
const unsigned int ExplosionFrameMs = 70;

// a halo'd alien frame over its 48x42 hitbox: the README's x-3, y-3
inline Box alienDraw(float hitX, float hitY) {
    return {hitX - 3.0f, hitY - 3.0f, static_cast<float>(AlienFrameW), static_cast<float>(AlienFrameH)};
}

// the diving alien (the UFO). The README draws it at x-3, y-3 for a 63x27 hitbox, but the game's diving alien keeps
// the 48x42 alien hitbox, so the 69x33 frame is centred on that one instead
inline Box ufoDraw(float hitX, float hitY) {
    return {hitX + (AlienHitW - UfoFrameW) / 2.0f, hitY + (AlienHitH - UfoFrameH) / 2.0f, static_cast<float>(UfoFrameW),
            static_cast<float>(UfoFrameH)};
}

// the player's bolt: the README's x-3(.5), y-3 (the 15 px frame over the 8 px hitbox); the piercing bolt x-3, y-3
inline Box playerLaserDraw(float hitX, float hitY) {
    return {hitX - 3.5f, hitY - 3.0f, static_cast<float>(PlayerLaserFrameW), static_cast<float>(PlayerLaserFrameH)};
}
inline Box pierceLaserDraw(float hitX, float hitY) {
    return {hitX - 3.0f, hitY - 3.0f, static_cast<float>(PlayerLaserFrameW), static_cast<float>(PlayerLaserFrameH)};
}

// the enemy plasma: x-10, y-6
inline Box enemyLaserDraw(float hitX, float hitY) {
    return {hitX - 10.0f, hitY - 6.0f, static_cast<float>(EnemyLaserFrameW), static_cast<float>(EnemyLaserFrameH)};
}

// a power-up is drawn on its hitbox
inline Box powerUpDraw(float hitX, float hitY) {
    return {hitX, hitY, static_cast<float>(PowerUpSize), static_cast<float>(PowerUpSize)};
}

// the explosion, centred on the dead alien's hitbox
inline Box explosionDraw(float alienX, float alienY) {
    return {alienX + (AlienHitW - ExplosionFrameW) / 2.0f, alienY + (AlienHitH - ExplosionFrameH) / 2.0f,
            static_cast<float>(ExplosionFrameW), static_cast<float>(ExplosionFrameH)};
}

// which frame of the explosion strip shows `elapsedMs` after the kill, -1 once it is over
inline int explosionFrame(unsigned int elapsedMs) {
    int frame = static_cast<int>(elapsedMs / ExplosionFrameMs);
    return frame < ExplosionFrames ? frame : -1;
}

// the two-frame strips: the aliens on the beat, the plasma on its flicker. `ticksMs` is the game's own clock
inline int alienFrame(unsigned int ticksMs) {
    return static_cast<int>((ticksMs / AlienBeatMs) % 2);
}
inline int enemyLaserFrame(unsigned int ticksMs) {
    return static_cast<int>((ticksMs / EnemyLaserFlickerMs) % 2);
}

// a number with leading zeros, at least `digits` wide (the score is 7, the wave 2); a negative one counts as 0
inline std::string zeroPad(int value, int digits) {
    std::string s = std::to_string(std::max(0, value));
    if (static_cast<int>(s.size()) < digits)
        s.insert(0, static_cast<size_t>(digits) - s.size(), '0');
    return s;
}

// the bottom HUD plates (lives, power-up timer): 58 px tall, at y 650 on the bare 720p canvas, but never reaching into
// the hint bar - `barTop` is the real top of the bar the screen draws (Gui::classicFooter().y), `gap` the air between
const int BottomPlateH = 58, BottomPlateY = 650, BottomPlateGap = 4;
inline int bottomPlateY(int barTop, int gap = BottomPlateGap) {
    return std::min(BottomPlateY, barTop - BottomPlateH - gap);
}

// the power-up timer's lit segments out of `segments`: the ones its remaining share still covers (at least one while
// anything is left, none at 0)
inline int timerSegments(unsigned int remainingMs, unsigned int totalMs, int segments = 10) {
    if (totalMs == 0 || remainingMs == 0)
        return 0;
    // whole numbers, so a share of exactly 9/10 never rounds up on a float
    unsigned long long left = std::min(remainingMs, totalMs);
    unsigned long long lit = (left * static_cast<unsigned long long>(segments) + totalMs - 1) / totalMs;
    return std::max(1, std::min(segments, static_cast<int>(lit)));
}

// ---------------------------------------------------------------------------------------------------------------------
// Life lost and Game over (README "Life lost and Game over (follow-up)"; 1280x720, the renderer scales)
// ---------------------------------------------------------------------------------------------------------------------

// the Life lost freeze (surprise_game.cpp keeps its own copy; the tests pin the two together) and its parts
const unsigned int LifeLostFreezeMs = 2000;
const unsigned int LifeLostFlashMs = 250; // the red flash fades out over this
const int LifeLostFlashAlpha = 110;
const unsigned int LifeLostPopMs = 160;     // plate and text pop in
const unsigned int LifeLostFadeOutMs = 250; // ...and fade out over the freeze's last stretch
const float LifeLostPopScale = 1.25f;
const unsigned int LifeBlinkMs = 125; // the lives counter blinks red every other 125 ms

const Box LifeLostPlate = {410, 300, 460, 110};
const int LifeLostTextTop = 318, CentreX = 640;

const unsigned int GameOverDimMs = 400;
const int GameOverDimAlpha = 150;
const unsigned int GameOverDropMs = 450;
const int GameOverTitleFromY = -130, GameOverTitleTop = 150;
const Box FinalPlate = {380, 320, 520, 190};
const unsigned int FinalPlateFadeMs = 200; // starts when the title has landed
const unsigned int FinalCountMs = 900;     // the score counts up over this, from the plate's arrival
const unsigned int FinalCountStepMs =
    50; // ...in steps of this, so the lettering is rebuilt about 18 times, not each frame
const int FinalLabelTop = 338, FinalScoreTop = 362;
const int FinalDividerX0 = 404, FinalDividerX1 = 876, FinalDividerY = 444, FinalDividerAlpha = 140;
const int FinalHiLabelTop = 458, FinalHiNumberTop = 452;
const int PushTop = 560;
const unsigned int PushOnMs = 600, PushOffMs = 400;
const unsigned int RecordGlowCycleMs = 1200;

// what a pop or a fade does to a plate and its text: alpha 0..255 and the scale about the plate's centre
struct EndFx {
    int alpha = 255;
    float scale = 1.0f;
};

inline float clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

// the red flash's alpha `elapsedMs` after the hit: 110 -> 0 over 250 ms, ease out cubic
inline int lifeLostFlashAlpha(unsigned int elapsedMs) {
    if (elapsedMs >= LifeLostFlashMs)
        return 0;
    float t = static_cast<float>(elapsedMs) / static_cast<float>(LifeLostFlashMs);
    return static_cast<int>(std::lround(LifeLostFlashAlpha * (1.0f - easeOutCubic(t))));
}

// the plate and its text during the freeze: popping in (alpha 0 -> 255, scale 1.25 -> 1.0 over 160 ms, outBack), held,
// then fading out over the last 250 ms; `animated` false (a slow frame) = static for the whole freeze
inline EndFx lifeLostFx(unsigned int elapsedMs, bool animated = true, unsigned int freezeMs = LifeLostFreezeMs) {
    EndFx fx;
    if (!animated || elapsedMs >= freezeMs)
        return fx;
    if (elapsedMs < LifeLostPopMs) {
        float k = abgui::ease::outBack(static_cast<float>(elapsedMs) / static_cast<float>(LifeLostPopMs));
        fx.alpha = static_cast<int>(std::lround(255.0f * clamp01(k)));
        fx.scale = LifeLostPopScale + (1.0f - LifeLostPopScale) * k;
    }
    if (elapsedMs + LifeLostFadeOutMs > freezeMs) {
        float left = static_cast<float>(freezeMs - elapsedMs) / static_cast<float>(LifeLostFadeOutMs);
        fx.alpha = static_cast<int>(std::lround(255.0f * clamp01(left)));
    }
    return fx;
}

// the lives counter shows its red frame on the even 125 ms stretches of the freeze
inline bool livesBlinkRed(unsigned int elapsedMs) {
    return (elapsedMs / LifeBlinkMs) % 2 == 0;
}

// Game over: the black dim over the field and the HUD, alpha 0 -> 150 over 400 ms
inline int gameOverDimAlpha(unsigned int elapsedMs) {
    if (elapsedMs >= GameOverDimMs)
        return GameOverDimAlpha;
    return static_cast<int>(std::lround(GameOverDimAlpha * static_cast<float>(elapsedMs) / GameOverDimMs));
}

// GAME OVER drops from top -130 to 150 over 450 ms, outBack (it passes 150 a little on the way)
inline int gameOverTitleY(unsigned int elapsedMs) {
    float k = abgui::ease::outBack(static_cast<float>(elapsedMs) / static_cast<float>(GameOverDropMs));
    return static_cast<int>(std::lround(GameOverTitleFromY + (GameOverTitleTop - GameOverTitleFromY) * k));
}

// the score plate fades in (alpha 0 -> 255, 200 ms) once the title has landed
inline int finalPlateAlpha(unsigned int elapsedMs) {
    if (elapsedMs <= GameOverDropMs)
        return 0;
    unsigned int in = elapsedMs - GameOverDropMs;
    if (in >= FinalPlateFadeMs)
        return 255;
    return static_cast<int>(std::lround(255.0f * static_cast<float>(in) / FinalPlateFadeMs));
}

// the score as shown `elapsedMs` after the game ended: counting up 0 -> score over 900 ms from the plate's arrival,
// ease out cubic, in 50 ms steps; `countUp` false shows the final number at once
inline int finalScoreShown(unsigned int elapsedMs, int score, bool countUp = true) {
    if (!countUp)
        return score;
    if (elapsedMs <= GameOverDropMs)
        return 0;
    unsigned int in = elapsedMs - GameOverDropMs;
    if (in >= FinalCountMs)
        return score;
    in -= in % FinalCountStepMs;
    float t = static_cast<float>(in) / static_cast<float>(FinalCountMs);
    return static_cast<int>(std::lround(static_cast<float>(std::max(0, score)) * easeOutCubic(t)));
}

// PUSH START BUTTON: shown once the plate is in, 600 ms on, 400 ms off
inline bool pushVisible(unsigned int elapsedMs) {
    unsigned int shownFrom = GameOverDropMs + FinalPlateFadeMs;
    if (elapsedMs < shownFrom)
        return false;
    return (elapsedMs - shownFrom) % (PushOnMs + PushOffMs) < PushOnMs;
}

// a new record's pink glow pulses between 40 % and 100 %, 1200 ms a cycle (yoyo)
inline int recordGlowAlpha(unsigned int elapsedMs) {
    float k = pulseWave(static_cast<long>(elapsedMs), static_cast<long>(RecordGlowCycleMs));
    return static_cast<int>(std::lround(255.0f * (0.4f + 0.6f * k)));
}

} // namespace surprise
