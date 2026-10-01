//
// UIREV-39: the pure numbers of "BleemStrike: Reloaded" - the halo'd frames over the unchanged hitboxes, the strips'
// frames, the zero-padded score and the power-up timer's segments (gui/surprise_layout.h).
//
#include "doctest/doctest.h"

#include "gui/surprise_layout.h"

using namespace surprise;

TEST_CASE("an alien frame is its 48x42 hitbox grown by the 3 px halo") {
    Box b = alienDraw(100.0f, 50.0f);
    CHECK(b.x == doctest::Approx(97.0f));
    CHECK(b.y == doctest::Approx(47.0f));
    CHECK(b.w == doctest::Approx(54.0f));
    CHECK(b.h == doctest::Approx(48.0f));
    // the frame covers the whole hitbox
    CHECK(b.x + b.w >= 100.0f + AlienHitW);
    CHECK(b.y + b.h >= 50.0f + AlienHitH);
}

TEST_CASE("the diving alien's frame is centred on the alien hitbox") {
    Box b = ufoDraw(200.0f, 80.0f);
    CHECK(b.w == doctest::Approx(69.0f));
    CHECK(b.h == doctest::Approx(33.0f));
    CHECK(b.x + b.w / 2 == doctest::Approx(200.0f + AlienHitW / 2.0f));
    CHECK(b.y + b.h / 2 == doctest::Approx(80.0f + AlienHitH / 2.0f));
}

TEST_CASE("the shots' frames over the 8x32 hitbox") {
    Box p = playerLaserDraw(10.0f, 20.0f);
    CHECK(p.x == doctest::Approx(6.5f));
    CHECK(p.y == doctest::Approx(17.0f));
    Box q = pierceLaserDraw(10.0f, 20.0f);
    CHECK(q.x == doctest::Approx(7.0f));
    CHECK(q.y == doctest::Approx(17.0f));
    CHECK(q.w == doctest::Approx(15.0f));
    CHECK(q.h == doctest::Approx(39.0f));
    Box e = enemyLaserDraw(10.0f, 20.0f);
    CHECK(e.x == doctest::Approx(0.0f));
    CHECK(e.y == doctest::Approx(14.0f));
    CHECK(e.w == doctest::Approx(27.0f));
    CHECK(e.h == doctest::Approx(45.0f));
}

TEST_CASE("a power-up is drawn on its hitbox, the explosion centred on the dead alien") {
    Box u = powerUpDraw(5.0f, 6.0f);
    CHECK(u.x == doctest::Approx(5.0f));
    CHECK(u.y == doctest::Approx(6.0f));
    CHECK(u.w == doctest::Approx(30.0f));
    Box x = explosionDraw(100.0f, 100.0f);
    CHECK(x.x + x.w / 2 == doctest::Approx(100.0f + AlienHitW / 2.0f));
    CHECK(x.y + x.h / 2 == doctest::Approx(100.0f + AlienHitH / 2.0f));
}

TEST_CASE("the explosion plays its four frames and then is over") {
    CHECK(explosionFrame(0) == 0);
    CHECK(explosionFrame(ExplosionFrameMs - 1) == 0);
    CHECK(explosionFrame(ExplosionFrameMs) == 1);
    CHECK(explosionFrame(ExplosionFrameMs * 3) == 3);
    CHECK(explosionFrame(ExplosionFrameMs * 4) == -1);
    CHECK(explosionFrame(100000) == -1);
}

TEST_CASE("the two-frame strips alternate") {
    CHECK(alienFrame(0) == 0);
    CHECK(alienFrame(AlienBeatMs) == 1);
    CHECK(alienFrame(AlienBeatMs * 2) == 0);
    CHECK(enemyLaserFrame(0) == 0);
    CHECK(enemyLaserFrame(EnemyLaserFlickerMs) == 1);
    CHECK(enemyLaserFrame(EnemyLaserFlickerMs * 2) == 0);
}

TEST_CASE("the score is seven digits, the wave two, zero-padded") {
    CHECK(zeroPad(0, 7) == "0000000");
    CHECK(zeroPad(12340, 7) == "0012340");
    CHECK(zeroPad(50000, 7) == "0050000");
    CHECK(zeroPad(3, 2) == "03");
    CHECK(zeroPad(123, 2) == "123"); // never cut
    CHECK(zeroPad(-5, 2) == "00");
}

TEST_CASE("the bottom plates never reach into the hint bar") {
    // a bare canvas (bar top at 720) keeps the design's y 650; a taller bar lifts the plates above it
    CHECK(bottomPlateY(720) == 650);
    CHECK(bottomPlateY(666) + BottomPlateH <= 666);
    // 720p and 1080p (logical px: the renderer scales), the 54 px footer band, the 64 px hint grid and a taller one
    const int barHeights[] = {0, 54, 64, 80, 120};
    for (int h : barHeights) {
        int top = 720 - h;
        int y = bottomPlateY(top);
        CHECK(y + BottomPlateH + BottomPlateGap <= top);
        CHECK(y <= BottomPlateY); // never lower than the design
        CHECK(y >= 0);
    }
}

TEST_CASE("the power-up timer lights ten segments, fewer as it runs out") {
    CHECK(timerSegments(10000, 10000) == 10);
    CHECK(timerSegments(9001, 10000) == 10);
    CHECK(timerSegments(9000, 10000) == 9);
    CHECK(timerSegments(5000, 10000) == 5);
    CHECK(timerSegments(1, 10000) == 1);
    CHECK(timerSegments(0, 10000) == 0);
    CHECK(timerSegments(20000, 10000) == 10); // never more than the row has
    CHECK(timerSegments(500, 0) == 0);
}

// ---- Life lost and Game over (the follow-up)
// -------------------------------------------------------------------------

namespace {
// a box lies on the 1280x720 canvas
bool onScreen(const Box &b) {
    return b.x >= 0 && b.y >= 0 && b.x + b.w <= 1280 && b.y + b.h <= 720;
}
bool inside(const Box &outer, const Box &inner) {
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w &&
           inner.y + inner.h <= outer.y + outer.h;
}
} // namespace

TEST_CASE("the end-screen plates sit on the 720p canvas, centred, clear of the bottom HUD") {
    CHECK(onScreen(LifeLostPlate));
    CHECK(onScreen(FinalPlate));
    CHECK(LifeLostPlate.x + LifeLostPlate.w / 2 == doctest::Approx(CentreX));
    CHECK(FinalPlate.x + FinalPlate.w / 2 == doctest::Approx(CentreX));
    // both end above the bottom HUD plates' top at the design's y (650)
    CHECK(LifeLostPlate.y + LifeLostPlate.h < BottomPlateY);
    CHECK(FinalPlate.y + FinalPlate.h < BottomPlateY);
    // the popped-in LIFE LOST plate at its biggest still fits the canvas
    Box big = {LifeLostPlate.x - LifeLostPlate.w * (LifeLostPopScale - 1) / 2,
               LifeLostPlate.y - LifeLostPlate.h * (LifeLostPopScale - 1) / 2, LifeLostPlate.w * LifeLostPopScale,
               LifeLostPlate.h * LifeLostPopScale};
    CHECK(onScreen(big));
}

TEST_CASE("the final score plate holds its rows; the divider and the hi-score line stay inside it") {
    const Box &p = FinalPlate;
    CHECK(FinalLabelTop > p.y);
    CHECK(FinalLabelTop < FinalScoreTop);
    CHECK(FinalScoreTop < FinalDividerY);
    CHECK(FinalDividerY < FinalHiLabelTop);
    CHECK(FinalHiLabelTop < p.y + p.h);
    CHECK(FinalHiNumberTop < p.y + p.h);
    // the divider and the hi-score row use the plate's inner width
    CHECK(inside(p, Box{static_cast<float>(FinalDividerX0), static_cast<float>(FinalDividerY),
                        static_cast<float>(FinalDividerX1 - FinalDividerX0), 1.0f}));
}

TEST_CASE("PUSH START BUTTON sits under the score plate and above the bottom HUD plates") {
    CHECK(PushTop > FinalPlate.y + FinalPlate.h);
    CHECK(PushTop + 40 < BottomPlateY); // the 30 px face's line is about 40 high
}

TEST_CASE("the GAME OVER title drops from -130 and lands on y 150") {
    CHECK(gameOverTitleY(0) == GameOverTitleFromY);
    CHECK(gameOverTitleY(GameOverDropMs) == GameOverTitleTop);
    CHECK(gameOverTitleY(GameOverDropMs + 1000) == GameOverTitleTop);
    // outBack: it passes the landing a little before it settles, but never by more than a fifth of the drop
    int lowest = gameOverTitleY(0);
    for (unsigned int t = 0; t <= GameOverDropMs; t += 10)
        lowest = std::max(lowest, gameOverTitleY(t));
    CHECK(lowest > GameOverTitleTop);
    CHECK(lowest < GameOverTitleTop + (GameOverTitleTop - GameOverTitleFromY) / 5);
}

TEST_CASE("the dim, the plate's fade and the PUSH blink follow the title") {
    CHECK(gameOverDimAlpha(0) == 0);
    CHECK(gameOverDimAlpha(GameOverDimMs / 2) == GameOverDimAlpha / 2);
    CHECK(gameOverDimAlpha(GameOverDimMs) == GameOverDimAlpha);
    CHECK(gameOverDimAlpha(60000) == GameOverDimAlpha);

    CHECK(finalPlateAlpha(GameOverDropMs) == 0); // the plate waits for the landing
    CHECK(finalPlateAlpha(GameOverDropMs + FinalPlateFadeMs / 2) == doctest::Approx(128).epsilon(0.01));
    CHECK(finalPlateAlpha(GameOverDropMs + FinalPlateFadeMs) == 255);

    unsigned int in = GameOverDropMs + FinalPlateFadeMs; // PUSH shows once the plate is in: 600 on, 400 off
    CHECK_FALSE(pushVisible(in - 1));
    CHECK(pushVisible(in));
    CHECK(pushVisible(in + PushOnMs - 1));
    CHECK_FALSE(pushVisible(in + PushOnMs));
    CHECK_FALSE(pushVisible(in + PushOnMs + PushOffMs - 1));
    CHECK(pushVisible(in + PushOnMs + PushOffMs));
}

TEST_CASE("the final score counts up 0 -> score in steps and shows the final number after 900 ms") {
    const int score = 12340;
    CHECK(finalScoreShown(0, score) == 0);
    CHECK(finalScoreShown(GameOverDropMs, score) == 0);
    CHECK(finalScoreShown(GameOverDropMs + FinalCountMs, score) == score);
    CHECK(finalScoreShown(GameOverDropMs + FinalCountMs + 5000, score) == score);
    int last = 0;
    for (unsigned int t = GameOverDropMs; t <= GameOverDropMs + FinalCountMs; t += 7) {
        int v = finalScoreShown(t, score);
        CHECK(v >= last); // never goes back
        CHECK(v <= score);
        last = v;
    }
    // steps of 50 ms: two frames inside one step show the same number
    CHECK(finalScoreShown(GameOverDropMs + 101, score) == finalScoreShown(GameOverDropMs + 149, score));
    // the fallback: the final number at once
    CHECK(finalScoreShown(0, score, false) == score);
}

TEST_CASE("a new record's glow pulses between 40 % and 100 % over 1200 ms") {
    CHECK(recordGlowAlpha(0) == doctest::Approx(102).epsilon(0.02));
    CHECK(recordGlowAlpha(RecordGlowCycleMs / 2) == 255);
    CHECK(recordGlowAlpha(RecordGlowCycleMs) == doctest::Approx(102).epsilon(0.02));
    for (unsigned int t = 0; t < 3 * RecordGlowCycleMs; t += 13) {
        CHECK(recordGlowAlpha(t) >= 100);
        CHECK(recordGlowAlpha(t) <= 255);
    }
}

TEST_CASE("the Life lost plate pops in, holds and fades out over the freeze") {
    EndFx start = lifeLostFx(0);
    CHECK(start.alpha == 0);
    CHECK(start.scale == doctest::Approx(LifeLostPopScale));
    EndFx popped = lifeLostFx(LifeLostPopMs);
    CHECK(popped.alpha == 255);
    CHECK(popped.scale == doctest::Approx(1.0f));
    EndFx held = lifeLostFx(1000);
    CHECK(held.alpha == 255);
    CHECK(held.scale == doctest::Approx(1.0f));
    CHECK(lifeLostFx(LifeLostFreezeMs - LifeLostFadeOutMs).alpha == 255);
    CHECK(lifeLostFx(LifeLostFreezeMs - LifeLostFadeOutMs / 2).alpha == doctest::Approx(128).epsilon(0.01));
    CHECK(lifeLostFx(LifeLostFreezeMs - 1).alpha <= 2);
    // a slow frame: static - the plate at full for the whole freeze, no pop, no fade
    for (unsigned int t = 0; t < LifeLostFreezeMs; t += 50) {
        EndFx s = lifeLostFx(t, false);
        CHECK(s.alpha == 255);
        CHECK(s.scale == doctest::Approx(1.0f));
    }
}

TEST_CASE("the red flash fades 110 -> 0 over 250 ms; the lives counter blinks every other 125 ms") {
    CHECK(lifeLostFlashAlpha(0) == LifeLostFlashAlpha);
    CHECK(lifeLostFlashAlpha(LifeLostFlashMs - 1) <= 2);
    CHECK(lifeLostFlashAlpha(LifeLostFlashMs) == 0);
    CHECK(lifeLostFlashAlpha(5000) == 0);
    // ease out: it has lost more than half by half time
    CHECK(lifeLostFlashAlpha(LifeLostFlashMs / 2) < LifeLostFlashAlpha / 2);

    CHECK(livesBlinkRed(0));
    CHECK(livesBlinkRed(LifeBlinkMs - 1));
    CHECK_FALSE(livesBlinkRed(LifeBlinkMs));
    CHECK(livesBlinkRed(LifeBlinkMs * 2));
}
