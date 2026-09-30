//
// Fonts::pickFonts (UIREV-31, one font): which file draws the classic screens and the launcher pair -
// the user's font > the CJK font > the theme's launcher.fonts > the built-in Open Sans. Static and file-only
// (the fonts are never opened), so the real class is tested over a scratch resources tree.
//
#include "doctest/doctest.h"

#include "gui/gui_font.h"
#include "support/env_fixture.h"
#include "support/temp_dir.h"

#include <ableem/engine/filesystem.h>

using ableem::sep;

namespace {
// a resources tree with the shipped fonts and a user's font in it
struct FontTree {
    TempDir tmp{"font_pick"};
    EnvFixture env;
    FontTree() {
        env.setWorkingPath(tmp.path());
        env.setRetroarchDir(tmp.path() + sep + "ra");
        tmp.makeSubDir("fonts");
        tmp.writeFile("fonts/OpenSans-Medium.ttf", "x");
        tmp.writeFile("fonts/OpenSans-Bold.ttf", "x");
        tmp.writeFile("fonts/NotoSansSC-Regular.otf", "x");
        tmp.writeFile("fonts/Mine.ttf", "x");
    }
    std::string fonts(const std::string &file) const { return ableem::Environment::getPathToFontsDir() + sep + file; }
};
} // namespace

TEST_CASE("no theme fonts: Open Sans for the launcher pair and the classic screens (today's look)") {
    FontTree tree;
    const Fonts::Pick pick = Fonts::pickFonts("true", "--", "English", "", "");
    CHECK(pick.medium == tree.fonts("OpenSans-Medium.ttf"));
    CHECK(pick.bold == tree.fonts("OpenSans-Bold.ttf"));
    CHECK(pick.classic == tree.fonts("OpenSans-Medium.ttf"));
    CHECK_FALSE(pick.userFont);
}

TEST_CASE("a theme's launcher.fonts is the classic default: 'Use default font' on means its medium") {
    FontTree tree;
    const Fonts::Pick pick = Fonts::pickFonts("true", "Mine.ttf", "English", "themes/x/Red-Medium.ttf", "themes/x/Red-Bold.ttf");
    CHECK(pick.medium == "themes/x/Red-Medium.ttf");
    CHECK(pick.bold == "themes/x/Red-Bold.ttf");
    CHECK(pick.classic == "themes/x/Red-Medium.ttf"); // "font" is ignored while the default is on
    CHECK_FALSE(pick.userFont);
}

TEST_CASE("a theme with only a medium keeps the built-in bold") {
    FontTree tree;
    const Fonts::Pick pick = Fonts::pickFonts("true", "--", "English", "themes/x/Red-Medium.ttf", "");
    CHECK(pick.classic == "themes/x/Red-Medium.ttf");
    CHECK(pick.bold == tree.fonts("OpenSans-Bold.ttf"));
}

TEST_CASE("a user's font still wins over the theme's, and leaves the launcher pair alone") {
    FontTree tree;
    const Fonts::Pick pick = Fonts::pickFonts("false", "Mine.ttf", "English", "themes/x/Red-Medium.ttf", "themes/x/Red-Bold.ttf");
    CHECK(pick.classic == tree.fonts("Mine.ttf"));
    CHECK(pick.userFont);
    CHECK(pick.medium == "themes/x/Red-Medium.ttf");
    CHECK(pick.bold == "themes/x/Red-Bold.ttf");
}

TEST_CASE("a user's font that is not found falls back to the theme's medium") {
    FontTree tree;
    const Fonts::Pick pick = Fonts::pickFonts("false", "Gone.ttf", "English", "themes/x/Red-Medium.ttf", "");
    CHECK(pick.classic == "themes/x/Red-Medium.ttf");
    CHECK_FALSE(pick.userFont);
}

TEST_CASE("a CJK language overrides everything, the user's font and the theme's included") {
    FontTree tree;
    const Fonts::Pick pick = Fonts::pickFonts("false", "Mine.ttf", "Chinese (Simplified)", "themes/x/Red-Medium.ttf", "themes/x/Red-Bold.ttf");
    const std::string cjk = tree.fonts("NotoSansSC-Regular.otf");
    CHECK(pick.classic == cjk);
    CHECK(pick.medium == cjk);
    CHECK(pick.bold == cjk);
    CHECK_FALSE(pick.userFont);
}

TEST_CASE("the CJK font not shipped: the usual order applies") {
    FontTree tree;
    ableem::DirEntry::removeFile(tree.fonts("NotoSansSC-Regular.otf"));
    const Fonts::Pick pick = Fonts::pickFonts("true", "--", "Chinese", "themes/x/Red-Medium.ttf", "");
    CHECK(pick.classic == "themes/x/Red-Medium.ttf");
}
