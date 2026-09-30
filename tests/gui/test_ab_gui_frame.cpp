//
// ab_gui frames (G4a of docs/ab-gui-plan.md): the 9-slice pieces - corners 1:1, edges and centre stretched, the bleed
// round the box, an @2x image's pixels for the same logical numbers, a box too small for its corners, an image too
// small for its slices - the 1x/@2x pick, the Style's colours by name (the tint) - all pure; then on a real renderer (a
// headless GuiBase, those cases skip themselves without one) the FrameSet loading the test theme's panel frame, and
// Style::drawFrame / sheet() through a Context with and without a frame provider.
//
#include "doctest/doctest.h"

#include <ab_gui/context.h>
#include <ab_gui/frame.h>
#include <ab_gui/style.h>

#include <ableem/ui/gui_base.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace std;
using abgui::FramePiece;
using abgui::FrameSet;
using abgui::FrameSpec;
using abgui::Insets;
using ableem::Color;
using ableem::Rect;
using ableem::Size;

namespace {

Size sizeOf(int w, int h) {
    Size s;
    s.w = w;
    s.h = h;
    return s;
}

bool same(const Rect &a, int x, int y, int w, int h) {
    return a.x == x && a.y == y && a.w == w && a.h == h;
}

// the test theme's frame (tests/data/frame-test-theme): 64x64, cut 24 all round, 8 of it the glow outside the box
const Size TestImage = sizeOf(64, 64);
const Insets TestSlice = Insets::all(24);
const Insets TestBleed = Insets::all(8);

struct MaybeGui {
    unique_ptr<ableem::GuiBase> gui;

    MaybeGui() {
#ifdef _WIN32
        _putenv_s("AB_HEADLESS", "1");
#else
        setenv("AB_HEADLESS", "1", 1);
#endif
        try {
            gui = make_unique<ableem::GuiBase>("ab_gui_test_frame", 320, 240);
        } catch (const exception &e) {
            const string why = e.what();
            MESSAGE("test_ab_gui_frame: skipping - no usable renderer here (" << why << ")");
        }
    }

    bool available() const { return gui != nullptr; }
};

string testFrame(const string &file) {
    return string(AB_TEST_DATA_DIR) + "/frame-test-theme/frames/" + file;
}

} // namespace

TEST_CASE(
    "framePieces: a 1x frame - corners 1:1, the edges and the centre stretched, drawn over the box and its bleed") {
    const vector<FramePiece> p =
        abgui::framePieces(TestImage, 1.0f, TestSlice, TestBleed, true, Rect(100, 50, 400, 200));
    REQUIRE(p.size() == 9);
    // the box grown by the bleed: (92, 42) 416 x 216
    CHECK(same(p[0].src, 0, 0, 24, 24));
    CHECK(same(p[0].dst, 92, 42, 24, 24));
    CHECK(same(p[1].src, 24, 0, 16, 24));
    CHECK(same(p[1].dst, 116, 42, 368, 24));
    CHECK(same(p[2].src, 40, 0, 24, 24));
    CHECK(same(p[2].dst, 484, 42, 24, 24));
    CHECK(same(p[3].src, 0, 24, 24, 16));
    CHECK(same(p[3].dst, 92, 66, 24, 168));
    CHECK(same(p[4].src, 24, 24, 16, 16));
    CHECK(same(p[4].dst, 116, 66, 368, 168));
    CHECK(same(p[5].src, 40, 24, 24, 16));
    CHECK(same(p[5].dst, 484, 66, 24, 168));
    CHECK(same(p[6].src, 0, 40, 24, 24));
    CHECK(same(p[6].dst, 92, 234, 24, 24));
    CHECK(same(p[7].src, 24, 40, 16, 24));
    CHECK(same(p[7].dst, 116, 234, 368, 24));
    CHECK(same(p[8].src, 40, 40, 24, 24));
    CHECK(same(p[8].dst, 484, 234, 24, 24));
    // the pieces tile the grown box exactly
    CHECK(p[8].dst.x + p[8].dst.w == 100 + 400 + 8);
    CHECK(p[8].dst.y + p[8].dst.h == 50 + 200 + 8);
}

TEST_CASE("framePieces: an @2x image has twice the pixels for the same logical numbers") {
    const vector<FramePiece> one =
        abgui::framePieces(TestImage, 1.0f, TestSlice, TestBleed, true, Rect(10, 20, 300, 90));
    const vector<FramePiece> two =
        abgui::framePieces(sizeOf(128, 128), 2.0f, TestSlice, TestBleed, true, Rect(10, 20, 300, 90));
    REQUIRE(one.size() == two.size());
    for (size_t i = 0; i < one.size(); i++) {
        CHECK(same(two[i].dst, one[i].dst.x, one[i].dst.y, one[i].dst.w, one[i].dst.h));
        CHECK(same(two[i].src, 2 * one[i].src.x, 2 * one[i].src.y, 2 * one[i].src.w, 2 * one[i].src.h));
    }
}

TEST_CASE("framePieces: fill false leaves the centre out") {
    const vector<FramePiece> p =
        abgui::framePieces(TestImage, 1.0f, TestSlice, TestBleed, false, Rect(100, 50, 400, 200));
    REQUIRE(p.size() == 8);
    for (const FramePiece &piece : p)
        CHECK_FALSE(same(piece.src, 24, 24, 16, 16));
}

TEST_CASE("framePieces: without a bleed the image is drawn at the box; different slices per side") {
    const vector<FramePiece> p =
        abgui::framePieces(sizeOf(40, 24), 1.0f, Insets(12, 6, 10, 4), Insets(), true, Rect(0, 100, 500, 24));
    REQUIRE(p.size() == 9);
    CHECK(same(p[0].dst, 0, 100, 12, 6));
    CHECK(same(p[4].src, 12, 6, 18, 14));
    CHECK(same(p[4].dst, 12, 106, 478, 14));
    CHECK(same(p[8].src, 30, 20, 10, 4));
    CHECK(same(p[8].dst, 490, 120, 10, 4));
}

TEST_CASE("framePieces: no slices - the whole image stretched over the box") {
    const vector<FramePiece> p = abgui::framePieces(sizeOf(10, 10), 1.0f, Insets(), Insets(), true, Rect(5, 6, 70, 80));
    REQUIRE(p.size() == 1);
    CHECK(same(p[0].src, 0, 0, 10, 10));
    CHECK(same(p[0].dst, 5, 6, 70, 80));
}

TEST_CASE("framePieces: a box too small for the corners shrinks them in proportion, never past the box") {
    // 20x20 + 8 bleed each side = 36: the two 24 px corners of a side share it 18/18, nothing between them
    const vector<FramePiece> p = abgui::framePieces(TestImage, 1.0f, TestSlice, TestBleed, true, Rect(0, 0, 20, 20));
    REQUIRE(p.size() == 4);
    CHECK(same(p[0].dst, -8, -8, 18, 18));
    CHECK(same(p[1].dst, 10, -8, 18, 18));
    CHECK(same(p[2].dst, -8, 10, 18, 18));
    CHECK(same(p[3].dst, 10, 10, 18, 18));
    // uneven corners keep their ratio: 30 px for a 20 + 10 cut
    const vector<FramePiece> q =
        abgui::framePieces(sizeOf(40, 40), 1.0f, Insets(20, 0, 10, 0), Insets(), true, Rect(0, 0, 15, 40));
    REQUIRE(q.size() == 2);
    CHECK(q[0].dst.w == 10);
    CHECK(q[1].dst.w == 5);
    // an empty box draws nothing
    CHECK(abgui::framePieces(TestImage, 1.0f, TestSlice, Insets(), true, Rect(0, 0, 0, 30)).empty());
}

TEST_CASE("frameFits: an image needs a pixel between its cut lines both ways") {
    CHECK(abgui::frameFits(TestImage, 1.0f, TestSlice));
    CHECK(abgui::frameFits(sizeOf(49, 49), 1.0f, TestSlice));
    CHECK_FALSE(abgui::frameFits(sizeOf(48, 64), 1.0f, TestSlice));
    CHECK_FALSE(abgui::frameFits(sizeOf(64, 40), 1.0f, TestSlice));
    // the @2x image of the same frame: 97 px is the least for 24 + 24 logical
    CHECK(abgui::frameFits(sizeOf(128, 128), 2.0f, TestSlice));
    CHECK_FALSE(abgui::frameFits(sizeOf(64, 64), 2.0f, TestSlice)); // a 1x image called @2x
    CHECK_FALSE(abgui::frameFits(TestImage, 1.0f, Insets(-1, 0, 0, 0)));
    // and a frame that does not fit gives no pieces
    CHECK(abgui::framePieces(sizeOf(40, 40), 1.0f, TestSlice, Insets(), true, Rect(0, 0, 400, 400)).empty());
}

TEST_CASE("FrameSet::pickFile: the @2x image above scale 1, else the 1x one, else the @2x one everywhere") {
    FrameSpec both;
    both.file = "a.png";
    both.file2x = "a@2x.png";
    float scale = 0;
    CHECK(FrameSet::pickFile(both, 1.0f, scale) == "a.png");
    CHECK(scale == 1.0f);
    CHECK(FrameSet::pickFile(both, 1.5f, scale) == "a@2x.png");
    CHECK(scale == 2.0f);
    CHECK(FrameSet::pickFile(both, 2.0f, scale) == "a@2x.png");

    FrameSpec only1x;
    only1x.file = "a.png";
    CHECK(FrameSet::pickFile(only1x, 1.5f, scale) == "a.png");
    CHECK(scale == 1.0f);

    FrameSpec only2x;
    only2x.file2x = "a@2x.png";
    CHECK(FrameSet::pickFile(only2x, 1.0f, scale) == "a@2x.png");
    CHECK(scale == 2.0f);

    CHECK(FrameSet::pickFile(FrameSpec(), 1.5f, scale).empty());
}

TEST_CASE("FrameSet: the specs by name; assign replaces them") {
    FrameSet set;
    CHECK(set.empty());
    map<string, FrameSpec> specs;
    specs["panel"].file = "p.png";
    specs["key"].file = "k.png";
    set.assign(specs);
    CHECK_FALSE(set.empty());
    CHECK(set.has("panel"));
    CHECK(set.has("key"));
    CHECK_FALSE(set.has("selection"));
    set.release(); // the specs stay
    CHECK(set.has("panel"));
    set.assign(map<string, FrameSpec>());
    CHECK(set.empty());
}

TEST_CASE("Style::colorByName: the tint's colour names are the theme's") {
    abgui::ColorRoles roles;
    roles.text = abgui::OptionalColor(Color(1, 2, 3));
    roles.secondary = abgui::OptionalColor(Color(4, 5, 6));
    roles.selectionBand.color = abgui::OptionalColor(Color(7, 8, 9));
    const abgui::Style style = abgui::Style::fromColors(roles);
    Color c;
    REQUIRE(style.colorByName("text", c));
    CHECK((c.r == 1 && c.g == 2 && c.b == 3));
    REQUIRE(style.colorByName("edge", c)); // edge falls back to secondary
    CHECK((c.r == 4 && c.g == 5 && c.b == 6));
    REQUIRE(style.colorByName("selectionBand", c));
    CHECK((c.r == 7 && c.g == 8 && c.b == 9));
    REQUIRE(style.colorByName("footer", c)); // the role `footer` is the member footerText
    CHECK((c.r == 1 && c.g == 2 && c.b == 3));
    for (const char *name : {"hint", "row", "rowSelected", "heading", "value", "description", "secondary"})
        CHECK(style.colorByName(name, c));
    const Color before = c;
    CHECK_FALSE(style.colorByName("selection", c)); // not a Style colour (the launcher's resume-slot halo)
    CHECK_FALSE(style.colorByName("Edge", c));
    CHECK_FALSE(style.colorByName("", c));
    CHECK((c.r == before.r && c.g == before.g && c.b == before.b)); // untouched
}

TEST_CASE("FrameSet on a renderer: the test theme's panel loads, 1x at scale 1; a bad frame is no frame") {
    MaybeGui maybe;
    if (!maybe.available())
        return;
    ableem::Renderer &renderer = maybe.gui->renderer();
    REQUIRE(renderer.outputScale() == 1.0f);

    map<string, FrameSpec> specs;
    FrameSpec panel;
    panel.file = testFrame("panel.png");
    panel.file2x = testFrame("panel@2x.png");
    panel.slice = TestSlice;
    panel.bleed = TestBleed;
    panel.tint = "edge";
    specs["panel"] = panel;
    FrameSpec only2x;
    only2x.file2x = testFrame("panel@2x.png");
    only2x.slice = TestSlice;
    specs["only2x"] = only2x;
    FrameSpec tooSmall; // the 1x image cut wider than it is
    tooSmall.file = testFrame("panel.png");
    tooSmall.slice = Insets::all(32);
    specs["tooSmall"] = tooSmall;
    FrameSpec missing;
    missing.file = testFrame("no-such.png");
    specs["missing"] = missing;

    FrameSet set;
    set.assign(specs);
    const abgui::Frame f = set.frame(renderer, "panel");
    REQUIRE(f.valid());
    CHECK(f.imageScale == 1.0f);
    CHECK(f.texture.size().w == 64);
    CHECK(f.texture.size().h == 64);
    CHECK(f.slice.left == 24);
    CHECK(f.bleed.bottom == 8);
    CHECK(f.tint == "edge");

    const abgui::Frame two = set.frame(renderer, "only2x");
    REQUIRE(two.valid());
    CHECK(two.imageScale == 2.0f);
    CHECK(two.texture.size().w == 128);

    CHECK_FALSE(set.frame(renderer, "tooSmall").valid());
    CHECK_FALSE(set.frame(renderer, "tooSmall").valid()); // remembered, not retried
    CHECK_FALSE(set.frame(renderer, "missing").valid());
    CHECK_FALSE(set.frame(renderer, "selection").valid());

    set.release();
    CHECK(set.frame(renderer, "panel").valid()); // loaded again after a release
}

TEST_CASE("Style::drawFrame and sheet() through a Context: the frame when the provider has one, else the old sheet") {
    MaybeGui maybe;
    if (!maybe.available())
        return;
    ableem::Renderer &renderer = maybe.gui->renderer();
    abgui::Context ctx(renderer);
    const abgui::Style style;
    const Rect box(20, 20, 200, 120);

    // no provider: no frame, and sheet() draws the code-drawn one (nothing to see here but that it runs)
    CHECK_FALSE(ctx.frame("panel").valid());
    CHECK_FALSE(style.drawFrame(ctx, "panel", box));
    style.sheet(ctx, box);

    FrameSet set;
    FrameSpec panel;
    panel.file = testFrame("panel.png");
    panel.slice = TestSlice;
    panel.bleed = TestBleed;
    map<string, FrameSpec> specs;
    specs["panel"] = panel;
    set.assign(specs);
    vector<string> asked;
    ctx.frameProvider = [&](const string &name) {
        asked.push_back(name);
        return set.frame(renderer, name);
    };
    CHECK(style.drawFrame(ctx, "panel", box));
    CHECK_FALSE(style.drawFrame(ctx, "selection", box));
    asked.clear();
    style.sheet(ctx, box);
    REQUIRE(asked.size() == 1);
    CHECK(asked[0] == "panel");
}

TEST_CASE("Style::selection through a Context: the selection frame when the provider has one, else the band and bar") {
    MaybeGui maybe;
    if (!maybe.available())
        return;
    ableem::Renderer &renderer = maybe.gui->renderer();
    abgui::Context ctx(renderer);
    const abgui::Style style;
    const Rect row(20, 20, 400, 28);

    // no provider: not framed, the code-drawn band (nothing to see here but that it runs)
    CHECK_FALSE(style.selectionFramed(ctx));
    style.selection(ctx, row);

    // a provider with a panel frame only: still not framed - a theme with no `selection` keeps the old look
    FrameSet set;
    FrameSpec panel;
    panel.file = testFrame("panel.png");
    panel.slice = TestSlice;
    panel.bleed = TestBleed;
    map<string, FrameSpec> specs;
    specs["panel"] = panel;
    set.assign(specs);
    vector<string> asked;
    ctx.frameProvider = [&](const string &name) {
        asked.push_back(name);
        return set.frame(renderer, name);
    };
    CHECK_FALSE(style.selectionFramed(ctx));
    asked.clear();
    style.selection(ctx, row);
    REQUIRE(asked.size() == 1);
    CHECK(asked[0] == "selection");

    // the test theme's selection frame (48x40, slice 12/10, bleed 4)
    FrameSpec selection;
    selection.file = testFrame("selection.png");
    selection.file2x = testFrame("selection@2x.png");
    selection.slice = Insets{12, 10, 12, 10};
    selection.bleed = Insets::all(4);
    specs["selection"] = selection;
    set.assign(specs);
    const abgui::Frame f = set.frame(renderer, "selection");
    REQUIRE(f.valid());
    CHECK(f.texture.size().w == 48);
    CHECK(f.texture.size().h == 40);
    CHECK(style.selectionFramed(ctx));
    CHECK(style.drawFrame(ctx, "selection", row));
    style.selection(ctx, row);
}

TEST_CASE("Style::label through a Context: the heading frame when the provider has one, else the faint band") {
    MaybeGui maybe;
    if (!maybe.available())
        return;
    ableem::Renderer &renderer = maybe.gui->renderer();
    abgui::Context ctx(renderer);
    const abgui::Style style;
    const Rect band(20, 20, 400, 24);

    // no provider: the code-drawn band (nothing to see here but that it runs)
    style.label(ctx, band);

    // a provider with a panel frame only: the heading is asked for, not found, and nothing else is drawn as a frame
    FrameSet set;
    FrameSpec panel;
    panel.file = testFrame("panel.png");
    panel.slice = TestSlice;
    panel.bleed = TestBleed;
    map<string, FrameSpec> specs;
    specs["panel"] = panel;
    set.assign(specs);
    vector<string> asked;
    ctx.frameProvider = [&](const string &name) {
        asked.push_back(name);
        return set.frame(renderer, name);
    };
    style.label(ctx, band);
    REQUIRE(asked.size() == 1);
    CHECK(asked[0] == "heading");
    CHECK_FALSE(style.drawFrame(ctx, "heading", band));

    // the test theme's heading frame (40x24, slice 12/6, no bleed)
    FrameSpec heading;
    heading.file = testFrame("heading.png");
    heading.file2x = testFrame("heading@2x.png");
    heading.slice = Insets{12, 6, 12, 6};
    heading.bleed = Insets::all(0);
    specs["heading"] = heading;
    set.assign(specs);
    const abgui::Frame f = set.frame(renderer, "heading");
    REQUIRE(f.valid());
    CHECK(f.texture.size().w == 40);
    CHECK(f.texture.size().h == 24);
    CHECK(style.drawFrame(ctx, "heading", band));
    asked.clear();
    style.label(ctx, band);
    REQUIRE(asked.size() == 1);
    CHECK(asked[0] == "heading");
}

TEST_CASE("Style::key and field through a Context: the state's frame, else key, else the old drawing") {
    MaybeGui maybe;
    if (!maybe.available())
        return;
    ableem::Renderer &renderer = maybe.gui->renderer();
    abgui::Context ctx(renderer);
    const abgui::Style style;
    const Rect box(20, 20, 96, 64);
    using abgui::KeyState;

    // no provider: the code-drawn keys and field (nothing to see here but that they run)
    style.key(ctx, box);
    style.key(ctx, box, KeyState::Normal, true);
    style.key(ctx, box, KeyState::Lit);
    style.key(ctx, box, KeyState::Selected);
    style.field(ctx, box);

    // a provider with a panel frame only: every state asks for its own frame, then `key`, and finds neither
    FrameSet set;
    FrameSpec panel;
    panel.file = testFrame("panel.png");
    panel.slice = TestSlice;
    panel.bleed = TestBleed;
    map<string, FrameSpec> specs;
    specs["panel"] = panel;
    set.assign(specs);
    vector<string> asked;
    ctx.frameProvider = [&](const string &name) {
        asked.push_back(name);
        return set.frame(renderer, name);
    };
    style.key(ctx, box);
    CHECK(asked == vector<string>{"key"});
    asked.clear();
    style.key(ctx, box, KeyState::Normal, true);
    CHECK(asked == vector<string>{"keyFunction", "key"});
    asked.clear();
    style.key(ctx, box, KeyState::Lit, true);
    CHECK(asked == vector<string>{"keyLit", "key"});
    asked.clear();
    style.key(ctx, box, KeyState::Selected);
    CHECK(asked == vector<string>{"keySelected", "key"});
    asked.clear();
    style.field(ctx, box);
    CHECK(asked == vector<string>{"field"});

    // the test theme's frames: 48x48 keys (slice 16, bleed 4), a 56x56 field
    auto spec = [&](const char *file) {
        FrameSpec s;
        s.file = testFrame(string(file) + ".png");
        s.file2x = testFrame(string(file) + "@2x.png");
        s.slice = Insets::all(16);
        s.bleed = Insets::all(4);
        return s;
    };
    specs["key"] = spec("key");
    specs["field"] = spec("field");
    set.assign(specs);
    REQUIRE(set.frame(renderer, "key").valid());
    CHECK(set.frame(renderer, "key").texture.size().w == 48);
    REQUIRE(set.frame(renderer, "field").valid());
    CHECK(set.frame(renderer, "field").texture.size().w == 56);

    // only `key` and `field`: function, lit and selected keys fall back to it (the selected one gets its outline)
    asked.clear();
    style.key(ctx, box, KeyState::Normal, true);
    CHECK(asked == vector<string>{"keyFunction", "key"});
    asked.clear();
    style.key(ctx, box, KeyState::Selected);
    CHECK(asked == vector<string>{"keySelected", "key"});
    asked.clear();
    style.key(ctx, box);
    CHECK(asked == vector<string>{"key"});
    asked.clear();
    style.field(ctx, box);
    CHECK(asked == vector<string>{"field"});

    // all of them: one frame asked for each state
    specs["keyFunction"] = spec("key_function");
    specs["keyLit"] = spec("key_lit");
    specs["keySelected"] = spec("key_selected");
    set.assign(specs);
    for (const auto &c : vector<pair<KeyState, bool>>{{KeyState::Normal, true}, {KeyState::Lit, false},
                                                       {KeyState::Selected, false}}) {
        asked.clear();
        style.key(ctx, box, c.first, c.second);
        CHECK(asked.size() == 1);
    }
}

TEST_CASE("Style::button and footer through a Context: a named key's chip asks for the chip frame, the width is the same") {
    MaybeGui maybe;
    if (!maybe.available())
        return;
    ableem::Renderer &renderer = maybe.gui->renderer();
    abgui::Context ctx(renderer);
    const abgui::Style style;

    // no provider: the code-drawn chip
    const int plain = style.button(ctx, "Start", 20, 20, 30);
    CHECK(plain == style.buttonWidth(ctx, "Start", 30));

    // a provider with a panel frame only: the chip is asked for, not found, the old box is drawn, the width is as before
    FrameSet set;
    FrameSpec panel;
    panel.file = testFrame("panel.png");
    panel.slice = TestSlice;
    panel.bleed = TestBleed;
    map<string, FrameSpec> specs;
    specs["panel"] = panel;
    set.assign(specs);
    vector<string> asked;
    ctx.frameProvider = [&](const string &name) {
        asked.push_back(name);
        return set.frame(renderer, name);
    };
    CHECK(style.button(ctx, "Start", 20, 20, 30) == plain);
    CHECK(asked == vector<string>{"chip"});

    // the test theme's chip (32x32, slice 10, bleed 2): the same width, the frame drawn under the name
    FrameSpec chip;
    chip.file = testFrame("chip.png");
    chip.file2x = testFrame("chip@2x.png");
    chip.slice = Insets::all(10);
    chip.bleed = Insets::all(2);
    specs["chip"] = chip;
    set.assign(specs);
    REQUIRE(set.frame(renderer, "chip").valid());
    CHECK(set.frame(renderer, "chip").texture.size().w == 32);
    asked.clear();
    CHECK(style.button(ctx, "Start", 20, 20, 30) == plain);
    CHECK(asked == vector<string>{"chip"});

    // the buttons line and the footer's hints take the chip too, and their widths do not change
    asked.clear();
    style.buttons(ctx, "|@L2|+|@R2|", 20, 20, 30);
    CHECK(asked == vector<string>{"chip", "chip"});
    asked.clear();
    style.footer(ctx, Rect(0, 600, 1280, 54), "|@Start| Menu", false);
    CHECK(asked == vector<string>{"chip"});
}

TEST_CASE("Style::footer through a Context (G5r8): the footer frame replaces the rule, only with the rule on") {
    MaybeGui maybe;
    if (!maybe.available())
        return;
    ableem::Renderer &renderer = maybe.gui->renderer();
    abgui::Context ctx(renderer);
    const abgui::Style style;
    const Rect band(0, 600, 1280, 54);

    // no provider (every shipped theme): the rule and the hints as always, nothing asked
    style.footer(ctx, band, "|@X| Select", true);

    FrameSet set;
    FrameSpec footer;
    footer.file = testFrame("footer.png");
    footer.file2x = testFrame("footer@2x.png");
    footer.slice = Insets::all(16);
    footer.bleed = Insets::all(4);
    map<string, FrameSpec> specs;
    specs["footer"] = footer;
    set.assign(specs);
    REQUIRE(set.frame(renderer, "footer").valid());
    // 56x54 body + a 4 px bleed: 64 x 62
    CHECK(set.frame(renderer, "footer").texture.size().w == 64);
    CHECK(set.frame(renderer, "footer").texture.size().h == 62);
    // only the footer frame is counted: a hint's key with no glyph in this bare Context asks for the `chip` frame
    // (G5d) as well, which is not what this case is about
    vector<string> asked;
    ctx.frameProvider = [&](const string &name) {
        if (name == "footer")
            asked.push_back(name);
        return set.frame(renderer, name);
    };
    style.footer(ctx, band, "|@X| Select", true);
    CHECK(asked == vector<string>{"footer"});
    // without the rule (About's surprise game) the band is left alone
    asked.clear();
    style.footer(ctx, band, "|@X| Select", false);
    CHECK(asked.empty());
    // a theme with no footer frame: asked, not found, the rule is drawn
    set.assign(map<string, FrameSpec>());
    asked.clear();
    style.footer(ctx, band, "|@X| Select", true);
    CHECK(asked == vector<string>{"footer"});
}

TEST_CASE("Style::toast (G5f): the toast frame, else the panel frame, else the code-drawn sheet") {
    MaybeGui maybe;
    if (!maybe.available())
        return;
    ableem::Renderer &renderer = maybe.gui->renderer();
    abgui::Context ctx(renderer);
    const abgui::Style style;
    const Rect box(200, 16, 440, 48);

    // no provider (every shipped theme): the sheet and its edge, nothing asked
    style.toast(ctx, box);

    FrameSet set;
    FrameSpec panel;
    panel.file = testFrame("panel.png");
    panel.slice = TestSlice;
    panel.bleed = TestBleed;
    map<string, FrameSpec> specs;
    specs["panel"] = panel;
    set.assign(specs);
    vector<string> asked;
    ctx.frameProvider = [&](const string &name) {
        asked.push_back(name);
        return set.frame(renderer, name);
    };

    // a theme with a panel frame only: the toast is asked for first, the panel drawn - what G4b gave the bubble
    style.toast(ctx, box);
    CHECK(asked == vector<string>{"toast", "panel"});

    // the test theme's toast (64x64, slice 20, bleed 8): the first frame there is drawn, the panel not asked
    FrameSpec toast;
    toast.file = testFrame("toast.png");
    toast.file2x = testFrame("toast@2x.png");
    toast.slice = Insets::all(20);
    toast.bleed = Insets::all(8);
    specs["toast"] = toast;
    set.assign(specs);
    REQUIRE(set.frame(renderer, "toast").valid());
    CHECK(set.frame(renderer, "toast").texture.size().w == 64);
    CHECK(set.frame(renderer, "toast").texture.size().h == 64);
    asked.clear();
    style.toast(ctx, box);
    CHECK(asked == vector<string>{"toast"});

    // a theme with neither: both asked, the code-drawn sheet follows
    set.assign(map<string, FrameSpec>());
    asked.clear();
    style.toast(ctx, box);
    CHECK(asked == vector<string>{"toast", "panel"});
}

TEST_CASE("Style::tabCell through a Context (G5h): the tab frame when the provider has one, else the band and bar") {
    MaybeGui maybe;
    if (!maybe.available())
        return;
    ableem::Renderer &renderer = maybe.gui->renderer();
    abgui::Context ctx(renderer);
    const abgui::Style style;
    const Rect cell(100, 40, 150, 102);

    // no provider (every shipped theme): the code-drawn band and bar - both overloads run
    style.tabCell(ctx, cell);
    style.tabCell(renderer, cell);

    // a provider with a panel frame only: `tab` is asked for, not found, the old look is drawn
    FrameSet set;
    FrameSpec panel;
    panel.file = testFrame("panel.png");
    panel.slice = TestSlice;
    panel.bleed = TestBleed;
    map<string, FrameSpec> specs;
    specs["panel"] = panel;
    set.assign(specs);
    vector<string> asked;
    ctx.frameProvider = [&](const string &name) {
        asked.push_back(name);
        return set.frame(renderer, name);
    };
    style.tabCell(ctx, cell);
    CHECK(asked == vector<string>{"tab"});

    // the test theme's tab (48x48, slice 16, no bleed): drawn, and one ask
    FrameSpec tab;
    tab.file = testFrame("tab.png");
    tab.file2x = testFrame("tab@2x.png");
    tab.slice = Insets::all(16);
    specs["tab"] = tab;
    set.assign(specs);
    const abgui::Frame f = set.frame(renderer, "tab");
    REQUIRE(f.valid());
    CHECK(f.texture.size().w == 48);
    CHECK(f.texture.size().h == 48);
    asked.clear();
    style.tabCell(ctx, cell);
    CHECK(asked == vector<string>{"tab"});
    CHECK(style.drawFrame(ctx, "tab", cell));
}

TEST_CASE(
    "Style::progress and progressBox through a Context (G5g): the track and fill frames, each falling back alone") {
    MaybeGui maybe;
    if (!maybe.available())
        return;
    ableem::Renderer &renderer = maybe.gui->renderer();
    abgui::Context ctx(renderer);
    const abgui::Style style;
    const Rect bar(100, 300, 400, 6);

    // no provider (every shipped theme): the two fills as always, nothing to ask
    style.progress(ctx, bar, 5, 10);
    style.progressBox(ctx, Rect(100, 300, 700, 22), 0.5);

    // a provider with a panel frame only: both progress frames are asked for, neither is found, the old drawing runs
    FrameSet set;
    FrameSpec panel;
    panel.file = testFrame("panel.png");
    panel.slice = TestSlice;
    panel.bleed = TestBleed;
    map<string, FrameSpec> specs;
    specs["panel"] = panel;
    set.assign(specs);
    vector<string> asked;
    ctx.frameProvider = [&](const string &name) {
        asked.push_back(name);
        return set.frame(renderer, name);
    };
    style.progress(ctx, bar, 5, 10);
    CHECK(asked == vector<string>{"progressTrack", "progressFill"});
    asked.clear();
    style.progressBox(ctx, Rect(100, 300, 700, 22), 0.5);
    CHECK(asked == vector<string>{"progressTrack", "progressFill"});

    // the test theme's two frames (16x8, slice 4/2, no bleed)
    FrameSpec track;
    track.file = testFrame("progress_track.png");
    track.file2x = testFrame("progress_track@2x.png");
    track.slice = Insets(4, 2, 4, 2);
    FrameSpec fill;
    fill.file = testFrame("progress_fill.png");
    fill.file2x = testFrame("progress_fill@2x.png");
    fill.slice = Insets(4, 2, 4, 2);
    specs["progressTrack"] = track;
    specs["progressFill"] = fill;
    set.assign(specs);
    REQUIRE(set.frame(renderer, "progressTrack").valid());
    REQUIRE(set.frame(renderer, "progressFill").valid());
    CHECK(set.frame(renderer, "progressTrack").texture.size().w == 16);
    CHECK(set.frame(renderer, "progressTrack").texture.size().h == 8);

    // half done: the track, then the fill (each is asked for once to see it is there, once more by the drawing)
    const auto count = [&](const string &name) {
        return static_cast<int>(std::count(asked.begin(), asked.end(), name));
    };
    asked.clear();
    style.progress(ctx, bar, 5, 10);
    CHECK(count("progressTrack") == 2);
    CHECK(count("progressFill") == 2);
    // nothing done: the fill is 0 px wide and not drawn; the track is
    asked.clear();
    style.progress(ctx, bar, 0, 10);
    CHECK(count("progressTrack") == 2);
    CHECK(count("progressFill") == 1);
    // no total: the track only
    asked.clear();
    style.progress(ctx, bar, 0, 0);
    CHECK(count("progressFill") == 1);
    // the prompt's bar: the same, the fill over the bar's whole height
    asked.clear();
    style.progressBox(ctx, Rect(100, 300, 700, 22), 0.5);
    CHECK(count("progressTrack") == 2);
    CHECK(count("progressFill") == 2);
    asked.clear();
    style.progressBox(ctx, Rect(100, 300, 700, 22), 0.0);
    CHECK(count("progressFill") == 1);

    // a theme with only the fill frame keeps the code-drawn track
    specs.erase("progressTrack");
    set.assign(specs);
    asked.clear();
    style.progress(ctx, bar, 5, 10);
    CHECK(count("progressTrack") == 1);
    CHECK(count("progressFill") == 2);
}

TEST_CASE("scaledFrame (G5k): the slices and bleed follow the factor, the cut stays on the same image pixels") {
    abgui::Frame glow;
    glow.imageScale = 1.0f;
    glow.slice = Insets::all(64);
    glow.bleed = Insets::all(48);
    glow.fill = false;
    const Size image = sizeOf(160, 160);

    // a factor of 1 (and a factor that makes no sense) leaves the frame as it is
    for (float factor : {1.0f, 0.0f, -2.0f}) {
        const abgui::Frame same1 = abgui::scaledFrame(glow, factor);
        CHECK(same1.slice.left == 64);
        CHECK(same1.bleed.top == 48);
        CHECK(same1.imageScale == 1.0f);
    }

    // a cover at half size: 32 / 24 logical, the image cut where it always was (64 px corners)
    const abgui::Frame half = abgui::scaledFrame(glow, 0.5f);
    CHECK(half.slice.left == 32);
    CHECK(half.slice.bottom == 32);
    CHECK(half.bleed.right == 24);
    CHECK(half.imageScale == doctest::Approx(2.0f));
    CHECK_FALSE(half.fill);
    const vector<FramePiece> small =
        abgui::framePieces(image, half.imageScale, half.slice, half.bleed, half.fill, Rect(100, 100, 111, 111));
    REQUIRE(small.size() == 8); // no centre
    CHECK(same(small[0].src, 0, 0, 64, 64));
    CHECK(same(small[0].dst, 76, 76, 32, 32)); // the box grown by 24
    CHECK(same(small[7].src, 96, 96, 64, 64));

    // the full size draws the frame as it is: the same pieces as the unscaled one
    const vector<FramePiece> a =
        abgui::framePieces(image, glow.imageScale, glow.slice, glow.bleed, glow.fill, Rect(100, 100, 222, 222));
    const abgui::Frame full = abgui::scaledFrame(glow, 222.0f / 222.0f);
    const vector<FramePiece> b =
        abgui::framePieces(image, full.imageScale, full.slice, full.bleed, full.fill, Rect(100, 100, 222, 222));
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); i++)
        CHECK(same(b[i].dst, a[i].dst.x, a[i].dst.y, a[i].dst.w, a[i].dst.h));

    // a cover 1.2 times as big: the lengths are rounded, never negative
    const abgui::Frame big = abgui::scaledFrame(glow, 1.2f);
    CHECK(big.slice.left == 77); // 76.8
    CHECK(big.bleed.left == 58); // 57.6

    // the tint and its resolved colour travel with the copy
    glow.tint = "selection";
    glow.tintResolved = true;
    glow.tintColor = Color(10, 20, 30, 255);
    const abgui::Frame copy = abgui::scaledFrame(glow, 0.7f);
    CHECK(copy.tint == "selection");
    CHECK(copy.tintResolved);
    CHECK(copy.tintColor.g == 20);
}
