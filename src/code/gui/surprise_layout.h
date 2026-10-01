//
// SurpriseLayout: the pure numbers of "BleemStrike: Reloaded" (UIREV-39, autobleem-design launcher/surprise/README.md)
// - where a halo'd sprite frame is drawn over the game's UNCHANGED hitbox, which frame of a strip shows when, how the
// score is padded and how many segments of the power-up timer are lit. No drawing, no SDL: tested on their own.
//
#pragma once

#include <algorithm>
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

} // namespace surprise
