//
// CoverLight (core/model/cover_light.h): the selected cover's glow and shine as geometry - pure, no fixture.
//
#include "doctest/doctest.h"

#include "core/model/cover_light.h"

using CoverLight::Box;
using CoverLight::glowBox;
using CoverLight::shineSlice;
using CoverLight::ShineSlice;

namespace {
Box box(float x, float y, float w, float h) {
    Box b;
    b.x = x;
    b.y = y;
    b.w = w;
    b.h = h;
    return b;
}

// the faces the carousel shows at rest in the middle slot: a jewel case, a tall (NES) and a wide (SNES) big box
const Box Square = box(529, 182, 222, 222);
const Box Tall = box(562.5f, 182, 155, 222);
const Box Wide = box(529, 213.5f, 222, 159);
const int TexSide = 256;
} // namespace

TEST_CASE("glowBox grows the face by the margin at the cover's scale, width and height each their own") {
    const Box g = glowBox(Square, 1.0f);
    CHECK(g.x == doctest::Approx(485));
    CHECK(g.y == doctest::Approx(138));
    CHECK(g.w == doctest::Approx(310));
    CHECK(g.h == doctest::Approx(310));

    const Box tall = glowBox(Tall, 1.0f);
    CHECK(tall.w == doctest::Approx(155 + 88));
    CHECK(tall.h == doctest::Approx(222 + 88));
    CHECK(tall.x + tall.w / 2 == doctest::Approx(Tall.x + Tall.w / 2)); // centred on the face
    CHECK(tall.y + tall.h / 2 == doctest::Approx(Tall.y + Tall.h / 2));

    const Box wide = glowBox(Wide, 1.0f);
    CHECK(wide.w == doctest::Approx(222 + 88));
    CHECK(wide.h == doctest::Approx(159 + 88));

    const Box half = glowBox(box(0, 0, 111, 111), 0.5f); // a cover on its way out of the middle, half size
    CHECK(half.x == doctest::Approx(-22));
    CHECK(half.w == doctest::Approx(111 + 44));
}

TEST_CASE("shineSlice stays inside the face and is drawn at the face's height, on every aspect") {
    for (const Box &face : {Square, Tall, Wide}) {
        const float column = face.h / TexSide;
        for (int i = 0; i <= 100; i++) {
            const float t = i / 100.0f;
            const ShineSlice s = shineSlice(face, t, TexSide);
            if (!s.visible)
                continue;
            CHECK(s.srcX >= 0);
            CHECK(s.srcW > 0);
            CHECK(s.srcX + s.srcW <= TexSide);
            CHECK(s.dst.x >= face.x - 0.01f);
            CHECK(s.dst.x + s.dst.w <= face.x + face.w + 0.01f);
            CHECK(s.dst.y == face.y);
            CHECK(s.dst.h == face.h);
            CHECK(s.dst.w == doctest::Approx(s.srcW * column)); // uniform: a column is face.h / texSide wide
        }
    }
}

TEST_CASE("shineSlice: the band is off the face at both ends of the crossing and on it half-way") {
    for (const Box &face : {Square, Tall, Wide}) {
        // t = 0: whatever part of the texture is over the face is past the band (the band is still to the left)
        const ShineSlice start = shineSlice(face, 0.0f, TexSide);
        if (start.visible) {
            CHECK(start.srcX >= CoverLight::SheenBandTo * TexSide - 1);
        }
        // t = 1: only the part before the band is still over the face
        const ShineSlice end = shineSlice(face, 1.0f, TexSide);
        if (end.visible) {
            CHECK(end.srcX + end.srcW <= CoverLight::SheenBandFrom * TexSide + 1);
        }
        // half-way the band's own columns are on the face
        const ShineSlice mid = shineSlice(face, 0.5f, TexSide);
        REQUIRE(mid.visible);
        CHECK(mid.srcX < CoverLight::SheenBandTo * TexSide);
        CHECK(mid.srcX + mid.srcW > CoverLight::SheenBandFrom * TexSide);
    }
}

TEST_CASE("shineSlice moves left to right, one texture column always where it was put") {
    // the texture's left edge (dst.x - srcX columns) only grows with t
    float previous = -1e9f;
    for (int i = 0; i <= 50; i++) {
        const ShineSlice s = shineSlice(Wide, i / 50.0f, TexSide);
        if (!s.visible)
            continue;
        const float left = s.dst.x - s.srcX * (Wide.h / TexSide);
        CHECK(left > previous);
        previous = left;
    }
}

TEST_CASE("shineSlice: a jewel case's texture is as wide as the face, a tall box's wider, a wide box's narrower") {
    // at the moment the texture's left edge meets the face's, a square face shows the whole texture
    const float t = CoverLight::SheenBandTo / (1.0f + CoverLight::SheenBandTo - CoverLight::SheenBandFrom);
    const ShineSlice s = shineSlice(Square, t, TexSide);
    REQUIRE(s.visible);
    CHECK(s.srcX <= 1);
    CHECK(s.srcX + s.srcW >= TexSide - 1);
    // a tall face never shows more than its own width's worth of columns
    for (int i = 0; i <= 20; i++) {
        const ShineSlice tall = shineSlice(Tall, i / 20.0f, TexSide);
        if (tall.visible) {
            CHECK(tall.srcW <= static_cast<int>(TexSide * Tall.w / Tall.h) + 1);
        }
    }
}

TEST_CASE("shineSlice: nothing for an empty face or texture") {
    CHECK_FALSE(shineSlice(box(0, 0, 0, 222), 0.5f, TexSide).visible);
    CHECK_FALSE(shineSlice(box(0, 0, 222, 0), 0.5f, TexSide).visible);
    CHECK_FALSE(shineSlice(Square, 0.5f, 0).visible);
}
