//
// ThemeColorDeriver (core/services/theme_color_deriver.h): the colour roles of a 1.0 theme.
//
// The fixtures are our own pictures (tests/data/color-fixtures, drawn by make_color_fixtures.py); the exact roles
// below are what the Python prototype gives for the same pixels and 1.0 data, so a change in the numbers is a
// change in the heuristic and must be made on purpose.
//
#include "doctest/doctest.h"

#include "core/services/theme_color_deriver.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Picture {
    int w = 0;
    int h = 0;
    std::vector<unsigned char> rgb;
};

// a binary PPM (P6, maxval 255)
Picture loadPpm(const std::string &name) {
    Picture p;
    std::ifstream f(std::string(AB_TEST_DATA_DIR) + "/color-fixtures/" + name, std::ios::binary);
    std::string magic;
    int maxval = 0;
    f >> magic >> p.w >> p.h >> maxval;
    f.get();
    p.rgb.resize(static_cast<size_t>(p.w) * p.h * 3);
    f.read(reinterpret_cast<char *>(p.rgb.data()), static_cast<std::streamsize>(p.rgb.size()));
    REQUIRE(magic == "P6");
    REQUIRE(maxval == 255);
    REQUIRE(f.gcount() == static_cast<std::streamsize>(p.rgb.size()));
    return p;
}

ThemeColorInput inputOf(const Picture &p) {
    ThemeColorInput in;
    in.pixels = p.rgb.data();
    in.width = p.w;
    in.height = p.h;
    in.channels = 3;
    return in;
}

struct Expected {
    const char *sheet;
    const char *text;
    const char *secondary;
    const char *row;
    const char *heading;
    const char *edge;
};

void checkRoles(const ThemeColorRoles &r, const Expected &e) {
    CHECK(r.sheet.hex() == e.sheet);
    CHECK(r.sheetAlpha == 200);
    CHECK(r.disabled == r.sheet);
    CHECK(r.disabledAlpha == 120);
    CHECK(r.text.hex() == e.text);
    CHECK(r.rowSelected == r.text);
    CHECK(r.footer == r.text);
    CHECK(r.secondary.hex() == e.secondary);
    CHECK(r.description == r.secondary);
    CHECK(r.hint == r.secondary);
    CHECK(r.row.hex() == e.row);
    CHECK(r.value == r.row);
    CHECK(r.heading.hex() == e.heading);
    CHECK(r.edge.hex() == e.edge);
    CHECK(r.selectionBand == r.edge);
    CHECK(r.accent == r.edge);
}

// every role passes its ratio against the sheet
std::string invariantFailure(const ThemeColorRoles &r) {
    const ThemeRgb text[] = {r.text,        r.rowSelected, r.footer, r.row,    r.value,
                             r.description, r.secondary,   r.hint,   r.heading};
    const char *names[] = {"text",        "rowSelected", "footer", "row",    "value",
                           "description", "secondary",   "hint",   "heading"};
    for (int i = 0; i < 9; i++)
        if (ThemeColorDeriver::contrast(text[i], r.sheet) < themecolor::TextMinContrast)
            return std::string(names[i]) + " " + text[i].hex() + " on " + r.sheet.hex();
    if (ThemeColorDeriver::contrast(r.accent, r.sheet) < themecolor::EdgeMinContrast)
        return "accent " + r.accent.hex() + " on " + r.sheet.hex();
    if (ThemeColorDeriver::contrast(r.edge, r.sheet) < themecolor::EdgeMinContrast)
        return "edge " + r.edge.hex() + " on " + r.sheet.hex();
    if (ThemeColorDeriver::contrast(r.selectionBand, r.sheet) < themecolor::EdgeMinContrast)
        return "selectionBand " + r.selectionBand.hex() + " on " + r.sheet.hex();
    for (const std::string &f : r.fallbacks)
        if (f.find("still below") != std::string::npos || f.find("cannot reach") != std::string::npos)
            return f;
    return "";
}

// a small deterministic generator - the tests never use rand()
struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed) {}
    uint32_t next() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    }
    int below(int n) { return static_cast<int>(next() % static_cast<uint32_t>(n)); }
    ThemeRgb colour() { return ThemeRgb(below(256), below(256), below(256)); }
};

bool hasNote(const std::vector<std::string> &notes, const std::string &start) {
    for (const std::string &n : notes)
        if (n.compare(0, start.size(), start) == 0)
            return true;
    return false;
}

} // namespace

TEST_CASE("the dark fixture: an orange emblem gives the accent, the darkest cluster the sheet") {
    const Picture p = loadPpm("dark.ppm");
    const ThemeColorRoles r = ThemeColorDeriver::derive(inputOf(p));
    checkRoles(r, {"#1d1f28", "#fceee4", "#909195", "#b9b0ac", "#e2701e", "#e2701e"});
    CHECK(r.fallbacks.empty());
    CHECK_FALSE(r.monochrome);
    CHECK(hasNote(r.notes, "sheet <- background: darkest dominant cluster"));
    CHECK(hasNote(r.notes, "accent <- background: most saturated frequent cluster"));
    CHECK(hasNote(r.notes, "text <- derived"));
    CHECK(invariantFailure(r).empty());
}

TEST_CASE("the light fixture: a light picture still gets a dark sheet, the blue band the accent (lifted to 3:1)") {
    const Picture p = loadPpm("light.ppm");
    const ThemeColorRoles r = ThemeColorDeriver::derive(inputOf(p));
    checkRoles(r, {"#222b45", "#e9eef8", "#9397a3", "#adb4c2", "#6e90e8", "#4a72c6"});
    CHECK(r.fallbacks.empty());
    CHECK(ThemeColorDeriver::contrast(r.edge, r.sheet) >= themecolor::EdgeMinContrast);
    CHECK(invariantFailure(r).empty());
}

TEST_CASE("the palette of a fixture: twelve clusters, shares of a whole, biggest first") {
    const Picture p = loadPpm("dark.ppm");
    const auto pal = ThemeColorDeriver::palette(p.rgb.data(), p.w, p.h, 3);
    REQUIRE(pal.size() == 12);
    double sum = 0.0;
    for (size_t i = 0; i < pal.size(); i++) {
        sum += pal[i].share;
        if (i > 0)
            CHECK(pal[i].share <= pal[i - 1].share);
    }
    CHECK(sum == doctest::Approx(1.0).epsilon(1e-9));
    // the prototype's biggest and smallest clusters
    CHECK(pal[0].color.hex() == "#e2701e");
    CHECK(pal[0].share == doctest::Approx(0.166).epsilon(0.005));
    CHECK(pal[1].color.hex() == "#0f1223");
    CHECK(pal[11].color.hex() == "#8c4f23");
    CHECK(pal[11].share == doctest::Approx(0.014).epsilon(0.02));
}

TEST_CASE("1.0 colour data comes first: Main_bg is the sheet, sec the accent, Text_fg the text") {
    const Picture p = loadPpm("dark.ppm");
    ThemeColorInput in = inputOf(p);
    in.hasMainBg = true;
    in.mainBg = ThemeRgb(40, 30, 90);
    in.hasSecondary = true;
    in.secondary = ThemeRgb(20, 200, 120);
    in.hasText = true;
    in.text = ThemeRgb(230, 230, 200);
    const ThemeColorRoles r = ThemeColorDeriver::derive(in);
    checkRoles(r, {"#2b243d", "#e6e6c8", "#918e99", "#aeac9e", "#14c878", "#14c878"});
    CHECK(r.text == ThemeRgb(230, 230, 200));
    CHECK(r.fallbacks.empty());
    CHECK(hasNote(r.notes, "sheet <- Main_bg"));
    CHECK(hasNote(r.notes, "accent <- colors.ini sec"));
    CHECK(hasNote(r.notes, "text <- 1.0 colors.ini fg"));
}

TEST_CASE("a Main_bg that is the stock black is not a choice") {
    const Picture p = loadPpm("dark.ppm");
    ThemeColorInput in = inputOf(p);
    in.hasMainBg = true;
    in.mainBg = ThemeRgb(0, 0, 0);
    const ThemeColorRoles stock = ThemeColorDeriver::derive(in);
    const ThemeColorRoles plain = ThemeColorDeriver::derive(inputOf(p));
    CHECK(stock.sheet == plain.sheet);
    CHECK(hasNote(stock.notes, "sheet <- background"));
}

TEST_CASE("a Main_bg over a light picture is the sheet's tint, its lightness clamped") {
    const Picture p = loadPpm("light.ppm");
    ThemeColorInput in = inputOf(p);
    in.hasMainBg = true;
    in.mainBg = ThemeRgb(30, 60, 50);
    const ThemeColorRoles r = ThemeColorDeriver::derive(in);
    checkRoles(r, {"#133128", "#e9eef8", "#8d9a96", "#a9b5ba", "#6e90e8", "#4a72c6"});
}

TEST_CASE("own text below 4.5:1 becomes the accent, the text is the derived near-white (owner decision 3)") {
    const Picture p = loadPpm("dark.ppm");
    ThemeColorInput in = inputOf(p);
    in.hasText = true;
    in.text = ThemeRgb(205, 20, 34); // #cd1422: 2.9:1 on the sheet
    const ThemeColorRoles r = ThemeColorDeriver::derive(in);
    checkRoles(r, {"#1d1f28", "#f9e4e5", "#8e8f93", "#b7a9ac", "#f7463f", "#d01a24"});
    CHECK(ThemeColorDeriver::contrast(ThemeRgb(205, 20, 34), r.sheet) < themecolor::TextMinContrast);
    CHECK(ThemeColorDeriver::contrast(r.text, r.sheet) >= 12.0); // near-white
    CHECK(hasNote(r.notes, "accent <- colors.ini fg / Text_fg"));
    REQUIRE(r.fallbacks.size() == 1);
    CHECK(r.fallbacks[0].find("#cd1422 is 2.9:1") != std::string::npos);
    CHECK(invariantFailure(r).empty());
}

TEST_CASE("a monochrome picture takes the grey accent (owner decision 2) and the default theme's text") {
    const Picture p = loadPpm("mono.ppm");
    const ThemeColorRoles r = ThemeColorDeriver::derive(inputOf(p));
    checkRoles(r, {"#222222", "#ffffff", "#9c9c9c", "#bdbdbd", "#aaaaaa", "#6e6e6e"});
    CHECK(r.monochrome);
    REQUIRE(r.fallbacks.size() == 1);
    CHECK(r.fallbacks[0].find("monochrome") != std::string::npos);
    CHECK(hasNote(r.notes, "accent <- default theme's secondary"));
    CHECK(invariantFailure(r).empty());
}

TEST_CASE("the monochrome accent is the default secondary #646464 lifted to 3:1 - #6b6b6b on a dark neutral sheet") {
    // a sheet from Main_bg #1f2021 (what the pack's Batman gives)
    std::vector<unsigned char> grey(16 * 9 * 3, 120);
    ThemeColorInput in;
    in.pixels = grey.data();
    in.width = 16;
    in.height = 9;
    in.hasMainBg = true;
    in.mainBg = ThemeRgb(0x1f, 0x20, 0x21);
    const ThemeColorRoles r = ThemeColorDeriver::derive(in);
    CHECK(r.monochrome);
    CHECK(r.sheet.hex() == "#1f2021");
    CHECK(r.accent.hex() == "#6b6b6b");
    CHECK(invariantFailure(r).empty());
}

TEST_CASE("no picture: the fixed neutral sheet and the monochrome fallback, still readable") {
    ThemeColorInput in;
    const ThemeColorRoles r = ThemeColorDeriver::derive(in);
    CHECK(r.monochrome);
    CHECK(hasNote(r.notes, "sheet <- fixed neutral"));
    bool noPicture = false;
    for (const std::string &f : r.fallbacks)
        noPicture = noPicture || f.find("no readable background") != std::string::npos;
    CHECK(noPicture);
    CHECK(invariantFailure(r).empty());
}

TEST_CASE("the contrast invariant holds over 10000 random palettes and 1.0 colour sets") {
    Lcg rng(0xAB1E3D);
    std::string firstFailure;
    int failures = 0;
    int monochrome = 0;
    for (int n = 0; n < 10000; n++) {
        std::vector<ThemeColorDeriver::PaletteEntry> pal;
        const int count = 1 + rng.below(12);
        const int kind = rng.below(4); // any colour / pastel / dark / greys
        double left = 1.0;
        for (int i = 0; i < count; i++) {
            ThemeColorDeriver::PaletteEntry e;
            ThemeRgb c = rng.colour();
            if (kind == 1)
                c = ThemeRgb(128 + c.r / 2, 128 + c.g / 2, 128 + c.b / 2);
            else if (kind == 2)
                c = ThemeRgb(c.r / 4, c.g / 4, c.b / 4);
            else if (kind == 3)
                c = ThemeRgb(c.r, c.r, c.r);
            e.color = c;
            e.share = i + 1 == count ? left : left * (rng.below(60) + 1) / 100.0;
            left -= e.share;
            pal.push_back(e);
        }
        ThemeColorInput in;
        if (rng.below(2) == 0) {
            in.hasText = true;
            in.text = rng.colour();
        }
        if (rng.below(3) == 0) {
            in.hasSecondary = true;
            in.secondary = rng.colour();
        }
        if (rng.below(3) == 0) {
            in.hasMainBg = true;
            in.mainBg = rng.colour();
        }
        const ThemeColorRoles r = ThemeColorDeriver::fromPalette(pal, true, in);
        monochrome += r.monochrome ? 1 : 0;
        const std::string bad = invariantFailure(r);
        if (!bad.empty()) {
            if (failures == 0)
                firstFailure = "case " + std::to_string(n) + ": " + bad;
            failures++;
        }
    }
    INFO(firstFailure);
    CHECK(failures == 0);
    CHECK(monochrome > 100);  // the random set does reach the fallback
    CHECK(monochrome < 9900); // ... and the derived path
}

TEST_CASE("the invariant holds end to end over random small pictures (the median cut included)") {
    Lcg rng(0x51DE);
    std::string firstFailure;
    int failures = 0;
    for (int n = 0; n < 150; n++) {
        const int w = 1 + rng.below(7);
        const int h = 1 + rng.below(5);
        const int channels = rng.below(2) == 0 ? 3 : 4;
        std::vector<unsigned char> px(static_cast<size_t>(w) * h * channels);
        const ThemeRgb a = rng.colour();
        const ThemeRgb b = rng.colour();
        for (size_t i = 0; i < static_cast<size_t>(w) * h; i++) {
            const ThemeRgb c = rng.below(3) == 0 ? b : a;
            px[i * channels] = static_cast<unsigned char>(c.r);
            px[i * channels + 1] = static_cast<unsigned char>(c.g);
            px[i * channels + 2] = static_cast<unsigned char>(c.b);
            if (channels == 4)
                px[i * channels + 3] = static_cast<unsigned char>(rng.below(256));
        }
        ThemeColorInput in;
        in.pixels = px.data();
        in.width = w;
        in.height = h;
        in.channels = channels;
        const std::string bad = invariantFailure(ThemeColorDeriver::derive(in));
        if (!bad.empty()) {
            if (failures == 0)
                firstFailure = "case " + std::to_string(n) + ": " + bad;
            failures++;
        }
    }
    INFO(firstFailure);
    CHECK(failures == 0);
}

TEST_CASE("derive is deterministic: the same input twice, and through a copy, gives the same roles") {
    const Picture p = loadPpm("light.ppm");
    ThemeColorInput in = inputOf(p);
    in.hasSecondary = true;
    in.secondary = ThemeRgb(200, 60, 40);
    const ThemeColorRoles a = ThemeColorDeriver::derive(in);
    const ThemeColorRoles b = ThemeColorDeriver::derive(in);
    const Picture copy = p;
    ThemeColorInput in2 = inputOf(copy);
    in2.hasSecondary = true;
    in2.secondary = ThemeRgb(200, 60, 40);
    const ThemeColorRoles c = ThemeColorDeriver::derive(in2);
    for (const ThemeColorRoles *o : {&b, &c}) {
        CHECK(o->sheet == a.sheet);
        CHECK(o->accent == a.accent);
        CHECK(o->text == a.text);
        CHECK(o->row == a.row);
        CHECK(o->description == a.description);
        CHECK(o->heading == a.heading);
        CHECK(o->notes == a.notes);
        CHECK(o->fallbacks == a.fallbacks);
    }
}

TEST_CASE("colour maths: contrast, Lab, lightness, mixing") {
    CHECK(ThemeColorDeriver::contrast(ThemeRgb(0, 0, 0), ThemeRgb(255, 255, 255)) == doctest::Approx(21.0));
    CHECK(ThemeColorDeriver::contrast(ThemeRgb(255, 255, 255), ThemeRgb(0, 0, 0)) == doctest::Approx(21.0));
    CHECK(ThemeColorDeriver::contrast(ThemeRgb(9, 9, 9), ThemeRgb(9, 9, 9)) == doctest::Approx(1.0));
    // #777777 on white is the textbook 4.48:1
    CHECK(ThemeColorDeriver::contrast(ThemeRgb(0x77, 0x77, 0x77), ThemeRgb(255, 255, 255)) ==
          doctest::Approx(4.48).epsilon(0.002));

    double L, a, b;
    ThemeColorDeriver::toLab(ThemeRgb(255, 255, 255), L, a, b);
    CHECK(L == doctest::Approx(100.0).epsilon(0.001));
    CHECK(a == doctest::Approx(0.0).epsilon(0.05));
    ThemeColorDeriver::toLab(ThemeRgb(0, 0, 0), L, a, b);
    CHECK(L == doctest::Approx(0.0).epsilon(0.001));
    ThemeColorDeriver::toLab(ThemeRgb(255, 0, 0), L, a, b);
    CHECK(L == doctest::Approx(53.24).epsilon(0.001));
    CHECK(ThemeColorDeriver::chroma(ThemeRgb(255, 0, 0)) == doctest::Approx(104.55).epsilon(0.002));
    CHECK(ThemeColorDeriver::chroma(ThemeRgb(90, 90, 90)) < 0.01);

    // Lab -> sRGB round trip (within one level) and the gamut test
    for (const ThemeRgb &c : {ThemeRgb(12, 200, 99), ThemeRgb(250, 3, 130), ThemeRgb(64, 64, 64), ThemeRgb(1, 2, 3)}) {
        ThemeColorDeriver::toLab(c, L, a, b);
        ThemeRgb back;
        REQUIRE(ThemeColorDeriver::fromLab(L, a, b, false, back));
        CHECK(std::abs(back.r - c.r) <= 1);
        CHECK(std::abs(back.g - c.g) <= 1);
        CHECK(std::abs(back.b - c.b) <= 1);
    }
    ThemeRgb out;
    CHECK_FALSE(ThemeColorDeriver::fromLab(50, 120, 120, false, out));
    CHECK(ThemeColorDeriver::fromLab(50, 120, 120, true, out));

    // withLightness keeps the hue and fits the gamut; a cap lowers the chroma
    const ThemeRgb teal = ThemeColorDeriver::withLightness(ThemeRgb(20, 150, 150), 80.0);
    ThemeColorDeriver::toLab(teal, L, a, b);
    CHECK(L == doctest::Approx(80.0).epsilon(0.01));
    CHECK(ThemeColorDeriver::chroma(ThemeColorDeriver::withLightness(ThemeRgb(20, 150, 150), 40.0, 10.0)) <= 10.5);

    // mix rounds half to even (like the prototype's round())
    CHECK(ThemeColorDeriver::mix(ThemeRgb(0, 0, 0), ThemeRgb(255, 255, 255), 0.0) == ThemeRgb(0, 0, 0));
    CHECK(ThemeColorDeriver::mix(ThemeRgb(0, 0, 0), ThemeRgb(255, 255, 255), 1.0) == ThemeRgb(255, 255, 255));
    CHECK(ThemeColorDeriver::mix(ThemeRgb(0, 0, 0), ThemeRgb(5, 7, 9), 0.5) == ThemeRgb(2, 4, 4));

    // liftUntil: reaches the ratio, or says it never will
    ThemeRgb lifted;
    REQUIRE(ThemeColorDeriver::liftUntil(ThemeRgb(40, 40, 60), ThemeRgb(20, 20, 20), 4.5, lifted));
    CHECK(ThemeColorDeriver::contrast(lifted, ThemeRgb(20, 20, 20)) >= 4.5);
    CHECK_FALSE(ThemeColorDeriver::liftUntil(ThemeRgb(200, 200, 200), ThemeRgb(255, 255, 255), 4.5, lifted));
}

TEST_CASE("the palette reader: flat, two-tone and transparent pictures") {
    // one colour: one cluster with the whole share
    std::vector<unsigned char> flat(8 * 8 * 3, 0);
    for (size_t i = 0; i < flat.size(); i += 3) {
        flat[i] = 200;
        flat[i + 1] = 100;
        flat[i + 2] = 50;
    }
    auto pal = ThemeColorDeriver::palette(flat.data(), 8, 8, 3);
    REQUIRE(pal.size() == 1);
    CHECK(pal[0].color == ThemeRgb(200, 100, 50));
    CHECK(pal[0].share == doctest::Approx(1.0));

    // two halves: two clusters of half each
    std::vector<unsigned char> halves(8 * 8 * 3, 0);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            const size_t i = (static_cast<size_t>(y) * 8 + x) * 3;
            halves[i] = x < 4 ? 255 : 0;
            halves[i + 2] = x < 4 ? 0 : 255;
        }
    pal = ThemeColorDeriver::palette(halves.data(), 8, 8, 3);
    REQUIRE(pal.size() >= 2);
    CHECK(pal[0].share + pal[1].share > 0.6); // the soft edge between the halves is the rest

    // fully transparent RGBA is black
    std::vector<unsigned char> clear(4 * 4 * 4, 0);
    for (size_t i = 0; i < clear.size(); i += 4) {
        clear[i] = 250;
        clear[i + 1] = 250;
        clear[i + 2] = 250;
        clear[i + 3] = 0;
    }
    pal = ThemeColorDeriver::palette(clear.data(), 4, 4, 4);
    REQUIRE(pal.size() == 1);
    CHECK(pal[0].color == ThemeRgb(0, 0, 0));

    // no picture, no palette
    CHECK(ThemeColorDeriver::palette(nullptr, 0, 0, 3).empty());
}

TEST_CASE("downscale: a flat picture stays flat at any size, and the output has the asked size") {
    std::vector<unsigned char> flat(1280 * 720 * 3, 77);
    const std::vector<unsigned char> small = ThemeColorDeriver::downscale(flat.data(), 1280, 720, 3, 160, 90);
    REQUIRE(small.size() == 160u * 90u * 3u);
    for (unsigned char v : small)
        REQUIRE(int(v) == 77);
    const std::vector<unsigned char> up = ThemeColorDeriver::downscale(flat.data(), 3, 2, 3, 160, 90);
    REQUIRE(up.size() == 160u * 90u * 3u);
    CHECK(int(up[0]) == 77);
    CHECK(int(up.back()) == 77);
}
