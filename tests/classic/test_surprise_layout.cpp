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
