//
// ThemeSpec: theme.json in and out, a partial theme merged over a full one, file resolution.
//
#include "doctest/doctest.h"

#include "../support/string_maker.h"
#include "../support/temp_dir.h"

#include <ableem/engine/theme_spec.h>

#include <map>
#include <string>
#include <vector>

using ableem::ThemeColor;
using ableem::ThemeSpec;
using std::string;

namespace {

// a theme that sets everything, the way a converted full theme.ini does
ThemeSpec fullSpec() {
    ThemeSpec s;
    s.music.set = true;
    s.music.file = "mel.ogg";
    s.music.loop = true;

    s.classic.background = "background.jpg";
    s.classic.logo.set = true;
    s.classic.logo.file = "ab.png";
    s.classic.logo.x = 520;
    s.classic.logo.y = 0;
    s.classic.logo.w = 240;
    s.classic.logo.h = 180;
    s.classic.font.file = "zrnic.ttf";
    s.classic.font.size = 24;
    s.classic.menuLines = 13;
    s.classic.menuPanel.set = true;
    s.classic.menuPanel.x = 30;
    s.classic.menuPanel.y = 10;
    s.classic.menuPanel.w = 1220;
    s.classic.menuPanel.h = 530;
    s.classic.menuPanel.color = ThemeColor(0, 0, 0);
    s.classic.menuPanel.alpha = 170;
    s.classic.statusBar.set = true;
    s.classic.statusBar.x = 0;
    s.classic.statusBar.y = -670;
    s.classic.statusBar.w = 1280;
    s.classic.statusBar.h = 30;
    s.classic.statusBar.color = ThemeColor(0, 0, 0);
    s.classic.statusBar.alpha = 170;
    s.classic.statusBar.textY = 662;
    s.classic.textColor = ThemeColor(255, 255, 255);
    s.classic.textShadow = true;
    s.classic.keyboardKey.color = ThemeColor(120, 120, 120);
    s.classic.keyboardKey.alpha = 170;
    s.classic.labelColor = ThemeColor(180, 180, 180);
    s.classic.freeSpaceText.set = true;
    s.classic.freeSpaceText.x = 180;
    s.classic.freeSpaceText.y = 35;
    s.classic.editorCover.set = true;
    s.classic.editorCover.x = 95;
    s.classic.editorCover.y = 130;
    auto &b = s.classic.buttons;
    b.cross = "cross.png";
    b.circle = "circle.png";
    b.square = "square.png";
    b.triangle = "triangle.png";
    b.start = "start.png";
    b.select = "select.png";
    b.l1 = "l1.png";
    b.r1 = "r1.png";
    b.l2 = "l2.png";
    b.r2 = "r2.png";
    b.check = "on.png";
    b.uncheck = "off.png";
    b.esc = "esc.png";
    b.enter = "enter.png";
    b.tab = "tab.png";

    auto &l = s.launcher;
    l.background = "images/launcher_background.png";
    l.footer = "images/launcher_footer.png";
    l.playButton = "images/play_button.png";
    l.playText = "images/play_text.png";
    l.settingsPanel = "images/settings_panel.png";
    l.metaPanel = "images/meta_panel.png";
    l.metaPanelSlides = true;
    l.textShadow = false;
    l.arrow = "images/arrow.png";
    l.hints.cross = "images/hint_cross.png";
    l.hints.circle = "images/hint_circle.png";
    l.hints.triangle = "images/hint_triangle.png";
    l.menuIcons.settings = "images/menu_settings.png";
    l.menuIcons.guide = "images/menu_guide.png";
    l.menuIcons.memcard = "images/menu_memcard.png";
    l.menuIcons.resume = "images/menu_resume.png";
    l.memcardManager.grid = "images/memcard_grid.png";
    l.memcardManager.pencil = "images/memcard_pencil.png";
    l.fonts.medium = "font/SST-Medium.ttf";
    l.fonts.bold = "font/SST-Bold.ttf";
    l.colors.text = ThemeColor(255, 255, 255);
    l.colors.secondary = ThemeColor(100, 100, 100);
    l.colors.hint = ThemeColor(255, 255, 255);
    l.snapPanel.x = 260;
    l.snapPanel.y = 225;
    l.snapPanel.w = 240;
    l.snapPanel.h = 180;
    l.snapPanel.set = true;
    l.menuIcons.resumePicture.x = 25;
    l.menuIcons.resumePicture.y = 22;
    l.menuIcons.resumePicture.w = 68;
    l.menuIcons.resumePicture.h = 52;
    l.menuIcons.resumePicture.set = true;
    l.menuIcons.resumeSlotLabel.x = 30;
    l.menuIcons.resumeSlotLabel.y = 78;
    l.menuIcons.resumeSlotLabel.set = true;

    s.sounds.cursor = "sounds/cursor.wav";
    s.sounds.cancel = "sounds/cancel.wav";
    s.sounds.homeUp = "sounds/home_up.wav";
    s.sounds.homeDown = "sounds/home_down.wav";
    s.sounds.resume = "sounds/resume_new.wav";
    return s;
}

} // namespace

TEST_CASE("a colour is #rrggbb in the file and r,g,b in the old ini") {
    ThemeColor c;
    CHECK(ThemeColor::parseHex("#78A0ff", c));
    CHECK(c.r == 120);
    CHECK(c.g == 160);
    CHECK(c.b == 255);
    CHECK(c.set);
    CHECK(c.toHex() == "#78a0ff");

    CHECK(ThemeColor::parseRgb("255, 0,17", c));
    CHECK(c.toHex() == "#ff0011");

    ThemeColor bad;
    CHECK_FALSE(ThemeColor::parseHex("ffffff", bad)); // no '#'
    CHECK_FALSE(ThemeColor::parseHex("#fff", bad));   // short form is not accepted
    CHECK_FALSE(ThemeColor::parseHex("#gg0000", bad));
    CHECK_FALSE(ThemeColor::parseRgb("255,255", bad));
    CHECK_FALSE(ThemeColor::parseRgb("256,0,0", bad));
    CHECK_FALSE(ThemeColor::parseRgb("1,2,3,4", bad));
    CHECK_FALSE(bad.set);
}

TEST_CASE("a full theme survives a save/load round trip") {
    TempDir tmp("theme_spec");
    ThemeSpec out = fullSpec();
    REQUIRE(out.save(tmp.at("theme.json")));

    ThemeSpec in;
    REQUIRE(in.load(tmp.at("theme.json")));

    CHECK(in.format == ThemeSpec::currentFormat);
    CHECK(in.music.set);
    CHECK_FALSE(in.music.none);
    CHECK(in.music.file == "mel.ogg");
    CHECK(in.music.loop);
    CHECK(in.classic.logo.x == 520);
    CHECK(in.classic.logo.h == 180);
    CHECK(in.classic.font.size == 24);
    CHECK(int(in.classic.menuLines) == 13);
    CHECK(in.classic.statusBar.y == -670);
    CHECK(in.classic.statusBar.textY == 662);
    CHECK(in.classic.statusBar.alpha == 170);
    CHECK(in.classic.textShadow.set);
    CHECK(bool(in.classic.textShadow));
    CHECK(in.classic.keyboardKey.color.toHex() == "#787878");
    CHECK(in.classic.labelColor.toHex() == "#b4b4b4");
    CHECK(in.classic.editorCover.y == 130);
    CHECK(in.classic.buttons.tab == "tab.png");
    CHECK(in.launcher.metaPanel == "images/meta_panel.png");
    CHECK(in.launcher.metaPanelSlides.set);
    CHECK(bool(in.launcher.metaPanelSlides));
    CHECK(in.launcher.textShadow.set);
    CHECK_FALSE(bool(in.launcher.textShadow));
    CHECK(in.launcher.menuIcons.resume == "images/menu_resume.png");
    CHECK(in.launcher.fonts.bold == "font/SST-Bold.ttf");
    CHECK(in.launcher.colors.secondary.toHex() == "#646464");
    CHECK(in.launcher.colors.hint.toHex() == "#ffffff");
    CHECK(in.launcher.snapPanel.set);
    CHECK(in.launcher.snapPanel.w == 240);
    CHECK(in.launcher.menuIcons.resumePicture.set);
    CHECK(in.launcher.menuIcons.resumePicture.y == 22);
    CHECK(in.launcher.menuIcons.resumeSlotLabel.set);
    CHECK(in.launcher.menuIcons.resumeSlotLabel.y == 78);
    CHECK(in.sounds.resume == "sounds/resume_new.wav");

    // every file field made the trip - the one list in fileFields() is what everything else iterates
    CHECK(in.referencedFiles() == out.referencedFiles());
    CHECK(in.referencedFiles().size() == 42);

    // the file reads in section order, not alphabetically
    string text = tmp.readFile("theme.json");
    CHECK(text.find("\"format\"") < text.find("\"music\""));
    CHECK(text.find("\"music\"") < text.find("\"classic\""));
    CHECK(text.find("\"classic\"") < text.find("\"launcher\""));
    CHECK(text.find("\"launcher\"") < text.find("\"sounds\""));
    CHECK(text.find("\"#000000\"") != string::npos);
}

TEST_CASE("a partial theme writes only what it sets and reads back as partial") {
    TempDir tmp("theme_spec");
    ThemeSpec out;
    out.classic.background = "bg.png";
    out.classic.menuLines = 12;
    out.launcher.colors.text = ThemeColor(1, 2, 3);
    REQUIRE(out.save(tmp.at("theme.json")));

    string text = tmp.readFile("theme.json");
    CHECK(text.find("\"sounds\"") == string::npos); // an empty section is not written
    CHECK(text.find("\"logo\"") == string::npos);
    CHECK(text.find("\"music\"") == string::npos);

    ThemeSpec in;
    REQUIRE(in.load(tmp.at("theme.json")));
    CHECK(in.classic.background == "bg.png");
    CHECK(in.classic.menuLines.set);
    CHECK_FALSE(in.classic.logo.set);
    CHECK_FALSE(in.classic.font.size.set);
    CHECK_FALSE(in.music.set);
    CHECK(in.launcher.background.empty());
    CHECK(in.launcher.colors.text.set);
    CHECK_FALSE(in.launcher.colors.secondary.set);
    CHECK_FALSE(in.launcher.colors.hint.set);             // stays unset: the launcher falls back to secondary
    CHECK_FALSE(in.launcher.snapPanel.set);               // no pane unless a theme asks
    CHECK_FALSE(in.launcher.menuIcons.resumePicture.set); // the launcher's own default window then
    CHECK_FALSE(in.launcher.textShadow.set);              // a theme that says nothing gets the default
    CHECK_FALSE(in.classic.textShadow.set);
}

TEST_CASE("\"music\": null is a theme with no music") {
    TempDir tmp("theme_spec");
    ThemeSpec out;
    out.music.set = true;
    out.music.none = true;
    REQUIRE(out.save(tmp.at("theme.json")));
    CHECK(tmp.readFile("theme.json").find("\"music\": null") != string::npos);

    ThemeSpec in;
    REQUIRE(in.load(tmp.at("theme.json")));
    CHECK(in.music.set);
    CHECK(in.music.none);

    // merging over a theme with music keeps it silent
    in.mergeOver(fullSpec());
    CHECK(in.music.none);
    CHECK(in.music.file.empty());
}

TEST_CASE("mergeOver takes the base's value for everything the theme leaves out") {
    ThemeSpec partial;
    partial.classic.background = "aergb.png";
    partial.classic.font.file = "other.ttf";
    partial.classic.font.size = 30;
    partial.classic.buttons.cross = "x.png";
    partial.launcher.metaPanelSlides = false;
    partial.classic.textShadow = false;

    partial.mergeOver(fullSpec());

    CHECK(partial.classic.background == "aergb.png"); // its own
    CHECK(partial.classic.font.size == 30);
    CHECK(partial.classic.buttons.cross == "x.png");
    CHECK_FALSE(bool(partial.launcher.metaPanelSlides));
    CHECK_FALSE(bool(partial.classic.textShadow)); // its own false survives the base's true
    CHECK(partial.launcher.textShadow.set);        // the base's
    CHECK_FALSE(bool(partial.launcher.textShadow));
    CHECK(partial.classic.logo.file == "ab.png"); // the base's
    CHECK(partial.classic.logo.w == 240);
    CHECK(int(partial.classic.menuLines) == 13);
    CHECK(partial.classic.buttons.circle == "circle.png");
    CHECK(partial.classic.textColor.toHex() == "#ffffff");
    CHECK(partial.music.file == "mel.ogg");
    CHECK(partial.launcher.metaPanel == "images/meta_panel.png");
    CHECK(partial.sounds.cursor == "sounds/cursor.wav");
    CHECK(partial.launcher.colors.hint.toHex() == "#ffffff"); // the base's, like any other colour
    CHECK(partial.referencedFiles().size() == 42);
}

TEST_CASE("a hint colour the theme and the base both leave out stays unset after the merge") {
    // the launcher then draws the footer labels in the secondary colour - that fallback is the
    // screen's, not the spec's, so the spec must not invent a value here
    ThemeSpec base = fullSpec();
    base.launcher.colors.hint = ThemeColor();
    ThemeSpec partial;
    partial.launcher.colors.secondary = ThemeColor(10, 20, 30);
    partial.mergeOver(base);
    CHECK_FALSE(partial.launcher.colors.hint.set);
    CHECK(partial.launcher.colors.secondary.toHex() == "#0a141e");
}

TEST_CASE("mergeOver keeps a rect, a colour and an alpha apart: a theme with only the rect inherits the fill") {
    ThemeSpec partial;
    partial.classic.menuPanel.set = true;
    partial.classic.menuPanel.x = 1;
    partial.classic.menuPanel.y = 2;
    partial.classic.menuPanel.w = 3;
    partial.classic.menuPanel.h = 4;
    partial.classic.statusBar.color = ThemeColor(9, 9, 9); // a colour without a rect
    partial.classic.font.file = "mine.ttf";                // a file without a size
    partial.classic.logo.file = "mylogo.png";              // a file without a rect

    partial.mergeOver(fullSpec());

    CHECK(partial.classic.menuPanel.x == 1);
    CHECK(partial.classic.menuPanel.h == 4);
    CHECK(partial.classic.menuPanel.color.toHex() == "#000000");
    CHECK(int(partial.classic.menuPanel.alpha) == 170);
    CHECK(partial.classic.statusBar.y == -670);
    CHECK(partial.classic.statusBar.color.toHex() == "#090909");
    CHECK(int(partial.classic.statusBar.textY) == 662);
    CHECK(partial.classic.font.file == "mine.ttf");
    CHECK(int(partial.classic.font.size) == 24);
    CHECK(partial.classic.logo.file == "mylogo.png");
    CHECK(partial.classic.logo.w == 240);
}

TEST_CASE("a file that is not valid JSON, or not there, is reported and leaves the spec alone") {
    TempDir tmp("theme_spec");
    tmp.writeFile("bad.json", "{ \"classic\": { \"background\": ");
    tmp.writeFile("array.json", "[1, 2, 3]");

    ThemeSpec spec;
    spec.classic.background = "keep.png";
    CHECK_FALSE(spec.load(tmp.at("bad.json")));
    CHECK_FALSE(spec.load(tmp.at("array.json")));
    CHECK_FALSE(spec.load(tmp.at("missing.json")));
    CHECK(spec.classic.background == "keep.png");
}

TEST_CASE("a style role is a colour or the name of another colour, and round-trips as written (UIREV-29)") {
    TempDir tmp("theme_spec");
    tmp.writeFile("theme.json", "{ \"launcher\": { \"colors\": { \"row\": \"secondary\", \"value\": \"#0a141e\","
                                " \"edge\": \"not a name\", \"heading\": 5 } } }");
    ThemeSpec spec;
    REQUIRE(spec.load(tmp.at("theme.json")));
    CHECK(spec.launcher.colors.row.ref == "secondary");
    CHECK_FALSE(spec.launcher.colors.row.color.set);
    CHECK(spec.launcher.colors.value.color.toHex() == "#0a141e");
    CHECK_FALSE(spec.launcher.colors.edge.isSet());    // spaces: neither a colour nor a name
    CHECK_FALSE(spec.launcher.colors.heading.isSet()); // a number
    CHECK_FALSE(spec.launcher.colors.footer.isSet());

    REQUIRE(spec.save(tmp.at("out.json")));
    ThemeSpec back;
    REQUIRE(back.load(tmp.at("out.json")));
    CHECK(back.launcher.colors.row.ref == "secondary");
    CHECK(back.launcher.colors.value.color.toHex() == "#0a141e");

    ThemeSpec partial;
    partial.launcher.colors.value.color = ThemeColor(1, 2, 3);
    partial.mergeOver(spec);
    CHECK(partial.launcher.colors.row.ref == "secondary");           // the base's name, still a name
    CHECK(partial.launcher.colors.value.color.toHex() == "#010203"); // its own
}

TEST_CASE("a key of the wrong type is ignored, not an error") {
    TempDir tmp("theme_spec");
    tmp.writeFile("theme.json", "{ \"classic\": { \"background\": 7, \"menuLines\": \"twelve\", \"textColor\": \"red\","
                                " \"logo\": { \"file\": \"ab.png\", \"x\": \"1\" } }, \"launcher\": \"none\" }");

    ThemeSpec spec;
    REQUIRE(spec.load(tmp.at("theme.json")));
    CHECK(spec.classic.background.empty());
    CHECK_FALSE(spec.classic.menuLines.set);
    CHECK_FALSE(spec.classic.textColor.set);
    CHECK(spec.classic.logo.file == "ab.png");
    CHECK_FALSE(spec.classic.logo.set); // "x" is a string, so the rect is not set
    CHECK(spec.classic.logo.x == 0);
    CHECK(spec.launcher.background.empty());
}

TEST_CASE("resolveFiles: the theme's own file, else the fallback's, else nothing") {
    TempDir tmp("theme_spec");
    tmp.makeSubDir("mine/images");
    tmp.makeSubDir("base/images");
    tmp.writeFile("mine/background.jpg", "x");
    tmp.writeFile("base/background.jpg", "x");
    tmp.writeFile("base/ab.png", "x");
    tmp.writeFile("base/images/arrow.png", "x");

    ThemeSpec base;
    base.classic.background = "background.jpg";
    base.classic.logo.set = true;
    base.classic.logo.file = "ab.png";
    base.launcher.arrow = "images/arrow.png";
    base.launcher.footer = "images/footer.png"; // named but not on disk

    ThemeSpec mine;
    mine.classic.background = "background.jpg"; // has its own
    mine.classic.logo.set = true;
    mine.classic.logo.file = "logo.png"; // names a file it does not have -> base's
    mine.mergeOver(base);                // arrow inherited by name, resolved against `mine` first
    mine.resolveFiles(tmp.at("mine"), base, tmp.at("base"));

    CHECK(mine.classic.background == tmp.at("mine/background.jpg"));
    CHECK(mine.classic.logo.file == tmp.at("base/ab.png"));
    CHECK(mine.launcher.arrow == tmp.at("base/images/arrow.png"));
    CHECK(mine.launcher.footer.empty());
    CHECK(mine.sounds.cursor.empty());
}

//*******************************
// launcher.frames (ab_gui G4a)
//*******************************
namespace {
const ableem::ThemeFrame *frameNamed(const std::vector<ableem::ThemeFrame> &frames, const string &name) {
    for (const ableem::ThemeFrame &f : frames)
        if (f.name == name)
            return &f;
    return nullptr;
}
} // namespace

TEST_CASE("readThemeFrames: launcher.frames by name - slice and bleed a number or four sides, fill, tint") {
    TempDir tmp("theme_spec");
    tmp.writeFile(
        "theme.json",
        "{ \"launcher\": { \"colors\": { \"text\": \"#ffffff\" }, \"frames\": {"
        " \"panel\": { \"image\": \"frames/panel.png\", \"slice\": 36, \"bleed\": 12 },"
        " \"selection\": { \"image\": \"frames/sel.png\", \"image2x\": \"hi/sel.png\","
        "   \"slice\": { \"left\": 12, \"top\": 10, \"right\": 11, \"bottom\": 9 }, \"bleed\": { \"left\": 4 },"
        "   \"fill\": false, \"tint\": \"selectionBand\" },"
        " \"heading\": { \"slice\": 6 },"
        " \"key\": \"frames/key.png\","
        " \"field\": { \"image\": \"frames/field.png\", \"slice\": \"wide\", \"fill\": 0 } } } }");
    const std::vector<ableem::ThemeFrame> frames = ableem::readThemeFrames(tmp.at("theme.json"));
    REQUIRE(frames.size() == 3); // heading has no image, key is not an object

    const ableem::ThemeFrame *panel = frameNamed(frames, "panel");
    REQUIRE(panel != nullptr);
    CHECK(panel->image == "frames/panel.png");
    CHECK(panel->image2x.empty());
    CHECK(panel->slice.left == 36);
    CHECK(panel->slice.top == 36);
    CHECK(panel->slice.right == 36);
    CHECK(panel->slice.bottom == 36);
    CHECK(panel->bleed.left == 12);
    CHECK(panel->bleed.bottom == 12);
    CHECK(panel->fill);
    CHECK(panel->tint.empty());

    const ableem::ThemeFrame *sel = frameNamed(frames, "selection");
    REQUIRE(sel != nullptr);
    CHECK(sel->image2x == "hi/sel.png");
    CHECK(sel->slice.left == 12);
    CHECK(sel->slice.top == 10);
    CHECK(sel->slice.right == 11);
    CHECK(sel->slice.bottom == 9);
    CHECK(sel->bleed.left == 4);
    CHECK(sel->bleed.top == 0);
    CHECK_FALSE(sel->fill);
    CHECK(sel->tint == "selectionBand");

    const ableem::ThemeFrame *field = frameNamed(frames, "field");
    REQUIRE(field != nullptr);
    CHECK(field->slice.left == 0); // a slice of the wrong type is not set
    CHECK(field->fill);            // nor a fill that is not a boolean
}

TEST_CASE("readThemeFrames: no block, a bad file or no file - no frames") {
    TempDir tmp("theme_spec");
    tmp.writeFile("plain.json", "{ \"launcher\": { \"colors\": { \"text\": \"#ffffff\" } } }");
    tmp.writeFile("bad.json", "{ \"launcher\": { \"frames\": ");
    tmp.writeFile("array.json", "{ \"launcher\": { \"frames\": [ { \"image\": \"a.png\" } ] } }");
    CHECK(ableem::readThemeFrames(tmp.at("plain.json")).empty());
    CHECK(ableem::readThemeFrames(tmp.at("bad.json")).empty());
    CHECK(ableem::readThemeFrames(tmp.at("array.json")).empty());
    CHECK(ableem::readThemeFrames(tmp.at("none.json")).empty());
}

TEST_CASE("loadThemeFrames: the images resolved in the theme's folder, the @2x found next to the 1x") {
    TempDir tmp("theme_spec");
    tmp.writeFile("t/theme.json",
                  "{ \"launcher\": { \"frames\": {"
                  " \"panel\": { \"image\": \"frames/panel.png\", \"slice\": 24 },"
                  " \"key\": { \"image\": \"frames/key.png\" },"
                  " \"field\": { \"image\": \"frames/field.png\", \"image2x\": \"frames/big-field.png\" },"
                  " \"heading\": { \"image\": \"frames/heading.png\" },"
                  " \"selection\": { \"image\": \"frames/selection.png\" },"
                  " \"keyLit\": { \"image2x\": \"frames/lit.png\" },"
                  " \"keySelected\": { \"image\": \"frames/gone.png\", \"image2x\": \"frames/gone2.png\" } } } }");
    tmp.writeFile("t/frames/panel.png", "x");
    tmp.writeFile("t/frames/panel@2x.png", "x");
    tmp.writeFile("t/frames/key.png", "x"); // no @2x
    tmp.writeFile("t/frames/field.png", "x");
    tmp.writeFile("t/frames/field@2x.png", "x"); // not the one named
    tmp.writeFile("t/frames/big-field.png", "x");
    tmp.writeFile("t/frames/selection@2x.png", "x"); // only the @2x of a named 1x
    tmp.writeFile("t/frames/lit.png", "x");
    const string dir = tmp.at("t");
    const std::vector<ableem::ThemeFrame> frames = ableem::loadThemeFrames(dir);

    const ableem::ThemeFrame *panel = frameNamed(frames, "panel");
    REQUIRE(panel != nullptr);
    CHECK(panel->image == dir + "/frames/panel.png");
    CHECK(panel->image2x == dir + "/frames/panel@2x.png");
    CHECK(panel->slice.left == 24);

    const ableem::ThemeFrame *key = frameNamed(frames, "key");
    REQUIRE(key != nullptr);
    CHECK(key->image == dir + "/frames/key.png");
    CHECK(key->image2x.empty());

    const ableem::ThemeFrame *field = frameNamed(frames, "field");
    REQUIRE(field != nullptr);
    CHECK(field->image2x == dir + "/frames/big-field.png");

    CHECK(frameNamed(frames, "heading") == nullptr); // its image is not there
    const ableem::ThemeFrame *selection = frameNamed(frames, "selection");
    REQUIRE(selection != nullptr);
    CHECK(selection->image.empty());
    CHECK(selection->image2x == dir + "/frames/selection@2x.png");
    const ableem::ThemeFrame *lit = frameNamed(frames, "keyLit");
    REQUIRE(lit != nullptr);
    CHECK(lit->image.empty());
    CHECK(lit->image2x == dir + "/frames/lit.png");
    CHECK(frameNamed(frames, "keySelected") == nullptr);
    CHECK(frames.size() == 5);

    // a theme without the block (every shipped theme today) has none, and neither has a folder without theme.json
    tmp.writeFile("plain/theme.json", "{ \"format\": 1 }");
    CHECK(ableem::loadThemeFrames(tmp.at("plain")).empty());
    CHECK(ableem::loadThemeFrames(tmp.at("nothing")).empty());
}

TEST_CASE("the test theme's frames (tests/data/frame-test-theme) load as the G4a check expects") {
    const string dir = string(AB_TEST_DATA_DIR) + "/frame-test-theme";
    const std::vector<ableem::ThemeFrame> frames = ableem::loadThemeFrames(dir);
    // panel (G4a), selection (G4c), heading (G4d), key/keyFunction/keyLit/keySelected/field (G4e)
    REQUIRE(frames.size() == 8);
    const ableem::ThemeFrame *panel = nullptr;
    const ableem::ThemeFrame *selection = nullptr;
    const ableem::ThemeFrame *heading = nullptr;
    for (const ableem::ThemeFrame &f : frames) {
        if (f.name == "panel")
            panel = &f;
        if (f.name == "selection")
            selection = &f;
        if (f.name == "heading")
            heading = &f;
    }
    REQUIRE(panel != nullptr);
    CHECK(panel->image == dir + "/frames/panel.png");
    CHECK(panel->image2x == dir + "/frames/panel@2x.png");
    CHECK(panel->slice.top == 24);
    CHECK(panel->bleed.right == 8);
    // G4c: the row selection - slice 12 at the sides and 10 top and bottom, a 4 px bleed, its own colours
    REQUIRE(selection != nullptr);
    CHECK(selection->image == dir + "/frames/selection.png");
    CHECK(selection->image2x == dir + "/frames/selection@2x.png");
    CHECK(selection->slice.left == 12);
    CHECK(selection->slice.right == 12);
    CHECK(selection->slice.top == 10);
    CHECK(selection->slice.bottom == 10);
    CHECK(selection->bleed.left == 4);
    CHECK(selection->tint.empty());
    // G4d: the heading band - slice 12 at the sides and 6 top and bottom, no bleed
    REQUIRE(heading != nullptr);
    CHECK(heading->image == dir + "/frames/heading.png");
    CHECK(heading->image2x == dir + "/frames/heading@2x.png");
    CHECK(heading->slice.left == 12);
    CHECK(heading->slice.right == 12);
    CHECK(heading->slice.top == 6);
    CHECK(heading->slice.bottom == 6);
    CHECK(heading->bleed.left == 0);
    CHECK(heading->tint.empty());
}

TEST_CASE("themeImageFile: the @2x next to a theme image above scale 1, else the image itself (G4f)") {
    TempDir tmp("theme_spec");
    tmp.writeFile("t.v1/bg.png", "x");
    tmp.writeFile("t.v1/bg@2x.png", "x");
    tmp.writeFile("t.v1/logo.png", "x"); // no @2x
    tmp.writeFile("t.v1/icons/cross", "x");
    tmp.writeFile("t.v1/icons/cross@2x", "x"); // no extension: "@2x" at the end
    const string dir = tmp.at("t.v1");
    float scale = 0.0f;

    // scale 1 (the console, a 720p window): always the 1x, even with an @2x there
    CHECK(ableem::themeImageFile(dir + "/bg.png", 1.0f, scale) == dir + "/bg.png");
    CHECK(scale == 1.0f);
    // above 1 (1080p is 1.5): the @2x when there is one, drawn at pixel scale 2
    scale = 0.0f;
    CHECK(ableem::themeImageFile(dir + "/bg.png", 1.5f, scale) == dir + "/bg@2x.png");
    CHECK(scale == 2.0f);
    // ...and the 1x itself when there is none - what every shipped theme gets, so it draws as before
    scale = 0.0f;
    CHECK(ableem::themeImageFile(dir + "/logo.png", 1.5f, scale) == dir + "/logo.png");
    CHECK(scale == 1.0f);
    // the dot of the file name, not of the folder
    CHECK(ableem::themeImageFile(dir + "/icons/cross", 2.0f, scale) == dir + "/icons/cross@2x");
    CHECK(scale == 2.0f);
    // no file is no file
    CHECK(ableem::themeImageFile("", 1.5f, scale).empty());
    CHECK(scale == 1.0f);
}

TEST_CASE("the high-resolution test theme (tests/data/hires-test-theme): every image has its @2x, picked above 1") {
    const string dir = string(AB_TEST_DATA_DIR) + "/hires-test-theme";
    for (const char *name : {"background.png", "cross.png", "on.png", "off.png"}) {
        const string file = dir + "/" + name;
        float scale = 0.0f;
        CHECK(ableem::themeImageFile(file, 1.0f, scale) == file);
        CHECK(scale == 1.0f);
        const string picked = ableem::themeImageFile(file, 1.5f, scale);
        CHECK(picked == file.substr(0, file.size() - 4) + "@2x.png");
        CHECK(scale == 2.0f);
    }
}

//*******************************
// launcher.icons (ab_gui G5a)
//*******************************
namespace {
const ableem::ThemeIcon *iconNamed(const std::vector<ableem::ThemeIcon> &icons, const string &name) {
    for (const ableem::ThemeIcon &i : icons)
        if (i.name == name)
            return &i;
    return nullptr;
}
} // namespace

TEST_CASE("readThemeIcons: launcher.icons by name - a file, or { image, image2x }; iconHalo") {
    TempDir tmp("theme_spec");
    tmp.writeFile("theme.json", "{ \"launcher\": { \"iconHalo\": false, \"icons\": {"
                                " \"dpadUp\": \"icons/up.png\","
                                " \"disc\": { \"image\": \"icons/cd.png\", \"image2x\": \"hi/cd.png\" },"
                                " \"tabApps\": { \"image2x\": \"icons/apps@2x.png\" },"
                                " \"lock\": { \"slice\": 4 },"
                                " \"sd\": 7,"
                                " \"hd\": \"\" } } }");
    const std::vector<ableem::ThemeIcon> icons = ableem::readThemeIcons(tmp.at("theme.json"));
    REQUIRE(icons.size() == 3); // lock has no image, sd is not a file name, hd is empty

    const ableem::ThemeIcon *up = iconNamed(icons, "dpadUp");
    REQUIRE(up != nullptr);
    CHECK(up->image == "icons/up.png");
    CHECK(up->image2x.empty());
    const ableem::ThemeIcon *disc = iconNamed(icons, "disc");
    REQUIRE(disc != nullptr);
    CHECK(disc->image == "icons/cd.png");
    CHECK(disc->image2x == "hi/cd.png");
    const ableem::ThemeIcon *apps = iconNamed(icons, "tabApps");
    REQUIRE(apps != nullptr);
    CHECK(apps->image.empty());
    CHECK(apps->image2x == "icons/apps@2x.png");

    bool halo = true;
    CHECK(ableem::readThemeIconHalo(tmp.at("theme.json"), halo));
    CHECK_FALSE(halo);

    // no block, a bad file, no file, an iconHalo that is not a boolean
    tmp.writeFile("plain.json", "{ \"launcher\": { \"colors\": { \"text\": \"#ffffff\" }, \"iconHalo\": \"no\" } }");
    tmp.writeFile("bad.json", "{ \"launcher\": { \"icons\": ");
    tmp.writeFile("array.json", "{ \"launcher\": { \"icons\": [ \"a.png\" ] } }");
    CHECK(ableem::readThemeIcons(tmp.at("plain.json")).empty());
    CHECK(ableem::readThemeIcons(tmp.at("bad.json")).empty());
    CHECK(ableem::readThemeIcons(tmp.at("array.json")).empty());
    CHECK(ableem::readThemeIcons(tmp.at("none.json")).empty());
    halo = true;
    CHECK_FALSE(ableem::readThemeIconHalo(tmp.at("plain.json"), halo));
    CHECK_FALSE(ableem::readThemeIconHalo(tmp.at("bad.json"), halo));
    CHECK_FALSE(ableem::readThemeIconHalo(tmp.at("none.json"), halo));
    CHECK(halo); // untouched
}

TEST_CASE("loadThemeIcons: the images resolved in the theme's folder, the @2x found next to the 1x") {
    TempDir tmp("theme_spec");
    tmp.writeFile("t/theme.json", "{ \"launcher\": { \"icons\": {"
                                  " \"dpadUp\": \"icons/up.png\","
                                  " \"dpadDown\": \"icons/down.png\","
                                  " \"disc\": { \"image\": \"icons/cd.png\", \"image2x\": \"icons/big-cd.png\" },"
                                  " \"tabApps\": { \"image2x\": \"icons/apps.png\" },"
                                  " \"lock\": \"icons/gone.png\" } } }");
    tmp.writeFile("t/icons/up.png", "x");
    tmp.writeFile("t/icons/up@2x.png", "x");
    tmp.writeFile("t/icons/down.png", "x"); // no @2x
    tmp.writeFile("t/icons/cd.png", "x");
    tmp.writeFile("t/icons/cd@2x.png", "x"); // not the one named
    tmp.writeFile("t/icons/big-cd.png", "x");
    tmp.writeFile("t/icons/apps.png", "x");
    const string dir = tmp.at("t");
    const std::vector<ableem::ThemeIcon> icons = ableem::loadThemeIcons(dir);
    REQUIRE(icons.size() == 4); // lock's file is not there

    const ableem::ThemeIcon *up = iconNamed(icons, "dpadUp");
    REQUIRE(up != nullptr);
    CHECK(up->image == dir + "/icons/up.png");
    CHECK(up->image2x == dir + "/icons/up@2x.png");
    const ableem::ThemeIcon *down = iconNamed(icons, "dpadDown");
    REQUIRE(down != nullptr);
    CHECK(down->image2x.empty());
    const ableem::ThemeIcon *disc = iconNamed(icons, "disc");
    REQUIRE(disc != nullptr);
    CHECK(disc->image2x == dir + "/icons/big-cd.png");
    const ableem::ThemeIcon *apps = iconNamed(icons, "tabApps");
    REQUIRE(apps != nullptr);
    CHECK(apps->image.empty());
    CHECK(apps->image2x == dir + "/icons/apps.png");
    CHECK(iconNamed(icons, "lock") == nullptr);
}

TEST_CASE("resolveThemeIcons: the theme's own, else the default theme's, else the built-in file (UIREV-30)") {
    TempDir tmp("theme_spec");
    // the program's built-in files: up has an @2x twin next to it, left is missing on disk
    tmp.writeFile("evoimg/dpad_up.png", "x");
    tmp.writeFile("evoimg/dpad_up@2x.png", "x");
    tmp.writeFile("evoimg/dpad_down.png", "x");
    tmp.writeFile("evoimg/dpad_right.png", "x");
    tmp.writeFile("evoimg/cd.png", "x");
    std::map<string, string> builtIn;
    builtIn["dpadUp"] = tmp.at("evoimg/dpad_up.png");
    builtIn["dpadDown"] = tmp.at("evoimg/dpad_down.png");
    builtIn["dpadLeft"] = tmp.at("evoimg/dpad_left.png");
    builtIn["dpadRight"] = tmp.at("evoimg/dpad_right.png");
    builtIn["disc"] = tmp.at("evoimg/cd.png");
    builtIn["players"] = ""; // a theme without a meta panel: nothing
    // the default theme replaces the right arrow and the down arrow, the theme the down arrow and the disc and adds an
    // icon of its own; its up arrow's file is missing
    tmp.writeFile("default/theme.json", "{ \"launcher\": { \"icons\": { \"dpadRight\": \"r.png\","
                                        " \"dpadDown\": \"d.png\" } } }");
    tmp.writeFile("default/r.png", "x");
    tmp.writeFile("default/d.png", "x");
    tmp.writeFile("t/theme.json", "{ \"launcher\": { \"icons\": { \"dpadDown\": \"i/d.png\", \"disc\": \"i/cd.png\","
                                  " \"extension\": \"i/ext.png\", \"dpadUp\": \"i/missing.png\" } } }");
    tmp.writeFile("t/i/d.png", "x");
    tmp.writeFile("t/i/cd.png", "x");
    tmp.writeFile("t/i/cd@2x.png", "x");
    tmp.writeFile("t/i/ext.png", "x");
    const string def = tmp.at("default");
    const string theme = tmp.at("t");

    const std::vector<ableem::ThemeIcon> icons = ableem::resolveThemeIcons(theme, def, builtIn);
    const ableem::ThemeIcon *up = iconNamed(icons, "dpadUp"); // the theme's file is missing: the built-in one
    REQUIRE(up != nullptr);
    CHECK(up->image == tmp.at("evoimg/dpad_up.png"));
    CHECK(up->image2x == tmp.at("evoimg/dpad_up@2x.png"));
    const ableem::ThemeIcon *down = iconNamed(icons, "dpadDown"); // the theme's, over the default's
    REQUIRE(down != nullptr);
    CHECK(down->image == theme + "/i/d.png");
    const ableem::ThemeIcon *right = iconNamed(icons, "dpadRight"); // the default's, over the built-in
    REQUIRE(right != nullptr);
    CHECK(right->image == def + "/r.png");
    CHECK(right->image2x.empty());
    const ableem::ThemeIcon *disc = iconNamed(icons, "disc");
    REQUIRE(disc != nullptr);
    CHECK(disc->image == theme + "/i/cd.png");
    CHECK(disc->image2x == theme + "/i/cd@2x.png");
    CHECK(iconNamed(icons, "extension") != nullptr); // a name only the theme has
    CHECK(iconNamed(icons, "dpadLeft") == nullptr);  // its built-in file is not there
    CHECK(iconNamed(icons, "players") == nullptr);
    CHECK(icons.size() == 5);

    // the default theme itself: the default's over the built-in
    const std::vector<ableem::ThemeIcon> ofDefault = ableem::resolveThemeIcons(def, def, builtIn);
    REQUIRE(iconNamed(ofDefault, "dpadDown") != nullptr);
    CHECK(iconNamed(ofDefault, "dpadDown")->image == def + "/d.png");
    REQUIRE(iconNamed(ofDefault, "disc") != nullptr);
    CHECK(iconNamed(ofDefault, "disc")->image == tmp.at("evoimg/cd.png"));
    CHECK(iconNamed(ofDefault, "extension") == nullptr);

    // a theme and a default without the block (every shipped theme today): exactly the built-in files
    tmp.writeFile("plain/theme.json", "{ \"format\": 1 }");
    tmp.writeFile("plaindef/theme.json", "{ \"format\": 1 }");
    const std::vector<ableem::ThemeIcon> plain =
        ableem::resolveThemeIcons(tmp.at("plain"), tmp.at("plaindef"), builtIn);
    REQUIRE(plain.size() == 4);
    for (const ableem::ThemeIcon &i : plain) {
        CHECK(i.image == builtIn[i.name]);
        CHECK(i.image2x == (i.name == "dpadUp" ? tmp.at("evoimg/dpad_up@2x.png") : string()));
    }
}

TEST_CASE("resolveThemeIconHalo: the theme's iconHalo, else the default's, else on") {
    TempDir tmp("theme_spec");
    tmp.writeFile("off/theme.json", "{ \"launcher\": { \"iconHalo\": false } }");
    tmp.writeFile("on/theme.json", "{ \"launcher\": { \"iconHalo\": true } }");
    tmp.writeFile("plain/theme.json", "{ \"format\": 1 }");
    CHECK(ableem::resolveThemeIconHalo(tmp.at("plain"), tmp.at("plain")));
    CHECK_FALSE(ableem::resolveThemeIconHalo(tmp.at("off"), tmp.at("plain")));
    CHECK_FALSE(ableem::resolveThemeIconHalo(tmp.at("plain"), tmp.at("off")));
    CHECK(ableem::resolveThemeIconHalo(tmp.at("on"), tmp.at("off")));
    CHECK(ableem::resolveThemeIconHalo(tmp.at("nothing"), tmp.at("nothing")));
}

TEST_CASE("the test theme's icons (tests/data/frame-test-theme): every name of the art spec, each with its @2x") {
    const string dir = string(AB_TEST_DATA_DIR) + "/frame-test-theme";
    const std::vector<ableem::ThemeIcon> icons = ableem::loadThemeIcons(dir);
    CHECK(icons.size() == 27);
    for (const char *name :
         {"players",        "disc",         "usb",      "internal",  "hd",       "sd",       "lock",      "unlock",
          "favorite",       "retroarch",    "lightgun", "lightgun2", "dpadUp",   "dpadDown", "dpadLeft",  "dpadRight",
          "tabPlayStation", "tabRetroArch", "tabApps",  "raCover",   "appCover", "bigBox",   "extension", "battery",
          "play",           "switchOn",     "switchOff"}) {
        const ableem::ThemeIcon *icon = iconNamed(icons, name);
        REQUIRE_MESSAGE(icon != nullptr, name);
        CHECK(icon->image.find(dir + "/icons/") == 0);
        CHECK(icon->image2x == icon->image.substr(0, icon->image.size() - 4) + "@2x.png");
    }
    CHECK(ableem::resolveThemeIconHalo(dir, dir));   // the halo stays on: it shows under the test colours
    CHECK(ableem::loadThemeFrames(dir).size() == 8); // the frames are untouched by the block
}
