//
// ThemeConverter: an old theme folder becomes theme.json + role-named files, and nothing else.
//
#include "doctest/doctest.h"

#include "../support/string_maker.h"
#include "../support/temp_dir.h"

#include "core/services/theme_converter.h"

#include <fstream>
#include <map>
#include <sstream>
#include <string>

using std::string;

namespace {

// the shipped aergb theme.ini, complete with the dead keys, plus default's extra ones
const char *FULL_INI = "[Theme]\n"
                       "Music=mel.ogg\n"
                       "Loop=1\n"
                       "\n"
                       "Logo=ab.png\n"
                       "Font=zrnic.ttf\n"
                       "Background=background.jpg\n"
                       "\n"
                       "# button icons\n"
                       "Circle=circle.png\n"
                       "Cross=cross.png\n"
                       "Square=square.png\n"
                       "Triangle=triangle.png\n"
                       "Start=start.png\n"
                       "Select=select.png\n"
                       "L1=l1.png\n"
                       "R1=r1.png\n"
                       "\n"
                       "Iconw=30\n"
                       "Iconh=30\n"
                       "IconRescan=705\n"
                       "IconExit=600\n"
                       "\n"
                       "Lpositionx=520\n"
                       "Lpositiony=0\n"
                       "Lw=240\n"
                       "Lh=180\n"
                       "\n"
                       "Textx=-0\n"
                       "Texty=-670\n"
                       "Textw=1280\n"
                       "Texth=30\n"
                       "\n"
                       "Textalpha=170\n"
                       "Text_fg=255,255,255\n"
                       "Text_bg=0,0,0\n"
                       "Key_bg=120,120,120\n"
                       "Keyalpha=170\n"
                       "Label_bg=180,180,180\n"
                       "\n"
                       "Lspositionx=400\n"
                       "Lspositiony=20\n"
                       "Lsw=240\n"
                       "Lsh=180\n"
                       "\n"
                       "Opscreenx=30\n"
                       "Opscreeny=10\n"
                       "Opscreenw=1220\n"
                       "Opscreenh=530\n"
                       "\n"
                       "Fsize=24\n"
                       "Ttop=662\n"
                       "Maxw=1140\n"
                       "\n"
                       "Fsposx=180\n"
                       "Fsposy=35\n"
                       "\n"
                       "Lines=13\n"
                       "\n"
                       "Ecoverx=95;\n"
                       "Ecovery=130;\n";

// an old theme folder: theme.ini, the launcher images under their PSC names among the ~300 others,
// the fonts and sounds, and a credit file at the root
struct OldTheme {
    explicit OldTheme(TempDir &tmp, const string &name, bool withAbVariants = false) : dir(tmp.at(name)) {
        tmp.makeSubDir(name + "/images/GR");
        tmp.makeSubDir(name + "/images/CB");
        tmp.makeSubDir(name + "/images/MC");
        tmp.makeSubDir(name + "/images/BMP_Text");
        tmp.makeSubDir(name + "/images/BMP_TXT_SST");
        tmp.makeSubDir(name + "/images/SET");
        tmp.makeSubDir(name + "/font");
        tmp.makeSubDir(name + "/sounds");
        tmp.writeFile(name + "/theme.ini", FULL_INI);
        tmp.writeFile(name + "/credit.txt", "Theme by someone\n");
        tmp.writeFile(name + "/mel.ogg", "ogg");
        tmp.writeFile(name + "/ab.png", "png");
        tmp.writeFile(name + "/background.jpg", "jpg");
        tmp.writeFile(name + "/zrnic.ttf", "ttf");
        for (const char *b : {"circle", "cross", "square", "triangle", "start", "select", "l1", "r1"})
            tmp.writeFile(name + "/" + b + ".png", "png");

        const char *used[] = {
            "GR/JP_US_BG.png",       "GR/Footer.png",      "GR/Acid_C_Btn.png",  "BMP_Text/Play_Text.png",
            "CB/Function_BG.png",    "CB/PlayerOne.png",   "GR/arrow.png",       "GR/X_Btn_ICN.png",
            "GR/Circle_Btn_ICN.png", "GR/Tri_Btn_ICN.png", "CB/Setting_ICN.png", "CB/Manual_ICN.png",
            "CB/MemoryCard_ICN.png", "CB/Resume.png",      "MC/Dot_Matrix.png",  "MC/Pencil_Carsor.png"};
        for (const char *f : used)
            tmp.writeFile(name + "/images/" + f, string("stock ") + f);
        if (withAbVariants) {
            tmp.writeFile(name + "/images/GR/AB_BG.png", "ab bg");
            tmp.writeFile(name + "/images/GR/Footer_AB.png", "ab footer");
            tmp.writeFile(name + "/images/CB/Function_AB.png", "ab function");
        }
        // the SonyUI-only leftovers
        tmp.writeFile(name + "/images/BMP_TXT_SST/msg_cant_change_discs_now_E.png", "x");
        tmp.writeFile(name + "/images/SET/Check.png", "x");
        tmp.writeFile(name + "/images/GR/Squere_Btn_ICN.png", "x");
        tmp.writeFile(name + "/images/Rectangle.png", "x");
        tmp.writeFile(name + "/images/Game_Guide_QR.png", "x");

        tmp.writeFile(name + "/font/SST-Medium.ttf", "ttf");
        tmp.writeFile(name + "/font/SST-Bold.ttf", "ttf");
        tmp.writeFile(name + "/font/SSTJapanese-Bold.ttf", "ttf");
        for (const char *s :
             {"cursor", "cancel", "home_up", "home_down", "resume_new", "resume_old", "decide", "end", "error"})
            tmp.writeFile(name + "/sounds/" + s + ".wav", "wav");
    }

    bool has(const string &rel) const { return DirEntry::exists(dir + sep + rel); }

    string dir;
};

} // namespace

TEST_CASE("specFromIni: every live key lands in its typed place, the dead ones are dropped") {
    TempDir tmp("theme_conv");
    tmp.writeFile("theme.ini", FULL_INI);
    IniFile ini;
    ini.load(tmp.at("theme.ini"));

    ThemeSpec s = ThemeConverter::specFromIni(ini);

    CHECK(s.music.set);
    CHECK(s.music.file == "mel.ogg");
    CHECK(s.music.loop);
    CHECK_FALSE(s.music.none);
    CHECK(s.classic.background == "background.jpg");
    CHECK(s.classic.logo.file == "ab.png");
    CHECK(s.classic.logo.set);
    CHECK(s.classic.logo.x == 520);
    CHECK(s.classic.logo.h == 180);
    CHECK(s.classic.font.file == "zrnic.ttf");
    CHECK(int(s.classic.font.size) == 24);
    CHECK(int(s.classic.menuLines) == 13);
    CHECK(s.classic.menuPanel.set);
    CHECK(s.classic.menuPanel.w == 1220);
    CHECK_FALSE(s.classic.menuPanel.color.set); // no Main_bg in this ini: inherits the default's
    CHECK_FALSE(s.classic.menuPanel.alpha.set);
    CHECK(s.classic.statusBar.set);
    CHECK(s.classic.statusBar.y == -670);
    CHECK(s.classic.statusBar.color.toHex() == "#000000");
    CHECK(int(s.classic.statusBar.alpha) == 170);
    CHECK(int(s.classic.statusBar.textY) == 662);
    CHECK(s.classic.textColor.toHex() == "#ffffff");
    CHECK(s.classic.keyboardKey.color.toHex() == "#787878");
    CHECK(int(s.classic.keyboardKey.alpha) == 170);
    CHECK(s.classic.labelColor.toHex() == "#b4b4b4");
    CHECK(s.classic.freeSpaceText.x == 180);
    CHECK(s.classic.editorCover.x == 95); // "95;" the way atoi read it
    CHECK(s.classic.editorCover.y == 130);
    CHECK(s.classic.buttons.cross == "cross.png");
    CHECK(s.classic.buttons.r1 == "r1.png");
    CHECK(s.classic.buttons.l2.empty()); // not in this ini
    CHECK(s.classic.buttons.tab.empty());
    CHECK(s.launcher.background.empty()); // the ini knows nothing about the launcher
    CHECK(s.sounds.cursor.empty());
}

TEST_CASE("specFromIni: Loop=-1 is no music; a group with a key missing is left to the default") {
    TempDir tmp("theme_conv");
    tmp.writeFile("theme.ini", "[Theme]\nMusic=x.ogg\nLoop=-1\nLpositionx=1\nLpositiony=2\nLw=3\nFsposx=5\n");
    IniFile ini;
    ini.load(tmp.at("theme.ini"));

    ThemeSpec s = ThemeConverter::specFromIni(ini);
    CHECK(s.music.set);
    CHECK(s.music.none);
    CHECK_FALSE(s.classic.logo.set);
    CHECK_FALSE(s.classic.freeSpaceText.set);

    tmp.writeFile("once.ini", "[Theme]\nMusic=x.ogg\nLoop=0\n");
    IniFile once;
    once.load(tmp.at("once.ini"));
    ThemeSpec o = ThemeConverter::specFromIni(once);
    CHECK_FALSE(o.music.loop);
    CHECK_FALSE(o.music.none);
}

TEST_CASE("convert: theme.json names every used file by role, the rest is gone, the root is left alone") {
    TempDir tmp("theme_conv");
    OldTheme theme(tmp, "aergb");
    REQUIRE(ThemeConverter::needsConversion(theme.dir));

    REQUIRE(ThemeConverter::convert(theme.dir));

    CHECK(theme.has("theme.json"));
    CHECK_FALSE(theme.has("theme.ini"));
    CHECK_FALSE(ThemeConverter::needsConversion(theme.dir));

    ThemeSpec s;
    REQUIRE(s.load(theme.dir + sep + "theme.json"));
    CHECK(s.classic.background == "background.jpg");
    CHECK(s.classic.buttons.l1 == "l1.png");
    CHECK(s.launcher.background == "images/launcher_background.png");
    CHECK(s.launcher.footer == "images/launcher_footer.png");
    CHECK(s.launcher.playText == "images/play_text.png");
    CHECK(s.launcher.settingsPanel == "images/settings_panel.png");
    CHECK(s.launcher.metaPanel == "images/meta_panel.png");
    CHECK(s.launcher.metaPanelSlides.set);
    CHECK(bool(s.launcher.metaPanelSlides));
    CHECK(s.launcher.hints.triangle == "images/hint_triangle.png");
    CHECK(s.launcher.menuIcons.memcard == "images/menu_memcard.png");
    CHECK(s.launcher.memcardManager.pencil == "images/memcard_pencil.png");
    CHECK(s.launcher.fonts.medium == "font/SST-Medium.ttf");
    CHECK(s.launcher.fonts.bold == "font/SST-Bold.ttf");
    CHECK(s.sounds.resume == "sounds/resume_new.wav");
    CHECK(s.referencedFiles().size() ==
          1 + 3 + 8 + 17 + 2 + 5); // music, bg/logo/font, 8 buttons, launcher, fonts, sounds

    // renamed, with the right content, and the old names gone
    CHECK(tmp.readFile("aergb/images/launcher_background.png") == "stock GR/JP_US_BG.png");
    CHECK(tmp.readFile("aergb/images/memcard_pencil.png") == "stock MC/Pencil_Carsor.png");
    CHECK_FALSE(theme.has("images/GR/JP_US_BG.png"));
    CHECK_FALSE(theme.has("images/MC/Pencil_Carsor.png"));

    // the leftovers and their directories
    CHECK_FALSE(theme.has("images/BMP_TXT_SST/msg_cant_change_discs_now_E.png"));
    CHECK_FALSE(theme.has("images/BMP_TXT_SST"));
    CHECK_FALSE(theme.has("images/SET"));
    CHECK_FALSE(theme.has("images/GR"));
    CHECK_FALSE(theme.has("images/CB"));
    CHECK_FALSE(theme.has("images/Rectangle.png"));
    CHECK_FALSE(theme.has("images/Game_Guide_QR.png"));
    CHECK_FALSE(theme.has("font/SSTJapanese-Bold.ttf"));
    CHECK_FALSE(theme.has("sounds/decide.wav"));
    CHECK_FALSE(theme.has("sounds/resume_old.wav"));
    CHECK(theme.has("font/SST-Bold.ttf"));
    CHECK(theme.has("sounds/home_down.wav"));

    // the root: everything named is there, and so is what is not
    CHECK(theme.has("credit.txt"));
    CHECK(theme.has("mel.ogg"));
    CHECK(theme.has("cross.png"));

    // exactly what the new layout says
    std::vector<string> files;
    for (const DirEntry &e : DirEntry::diru(theme.dir + sep + "images"))
        files.push_back(e.name);
    CHECK(files.size() == 17);
    CHECK(DirEntry::diru(theme.dir + sep + "font").size() == 2);
    CHECK(DirEntry::diru(theme.dir + sep + "sounds").size() == 5);
}

TEST_CASE("convert: the _AB variants win, and make the meta panel static") {
    TempDir tmp("theme_conv");
    OldTheme theme(tmp, "evolution", true);

    REQUIRE(ThemeConverter::convert(theme.dir));

    ThemeSpec s;
    REQUIRE(s.load(theme.dir + sep + "theme.json"));
    CHECK_FALSE(bool(s.launcher.metaPanelSlides));
    CHECK(tmp.readFile("evolution/images/launcher_background.png") == "ab bg");
    CHECK(tmp.readFile("evolution/images/launcher_footer.png") == "ab footer");
    CHECK(tmp.readFile("evolution/images/settings_panel.png") == "ab function");
    CHECK(tmp.readFile("evolution/images/play_button.png") == "stock GR/Acid_C_Btn.png");
    CHECK_FALSE(theme.has("images/GR")); // the stock JP_US_BG.png etc. went with the directory
}

TEST_CASE("convert: a folder with launcher images but no theme.ini is a theme too") {
    TempDir tmp("theme_conv");
    OldTheme theme(tmp, "autobleem");
    DirEntry::removeFile(theme.dir + sep + "theme.ini");
    REQUIRE(ThemeConverter::needsConversion(theme.dir));

    REQUIRE(ThemeConverter::convert(theme.dir));

    ThemeSpec s;
    REQUIRE(s.load(theme.dir + sep + "theme.json"));
    CHECK_FALSE(s.music.set);
    CHECK(s.classic.background.empty());
    CHECK_FALSE(s.classic.logo.set);
    CHECK(s.launcher.metaPanel == "images/meta_panel.png");
    CHECK(theme.has("images/meta_panel.png"));
    CHECK(theme.has("ab.png")); // a root file the json does not name is not the converter's business
}

TEST_CASE("convert: colors.ini feeds launcher.colors (the derived roles) and is removed") {
    TempDir tmp("theme_conv");
    OldTheme theme(tmp, "sony");
    tmp.writeFile("sony/colors.ini", "[colors]\nfg=255,255,255\nsec=60,60,60\n");

    REQUIRE(ThemeConverter::convert(theme.dir));

    ThemeSpec s;
    REQUIRE(s.load(theme.dir + sep + "theme.json"));
    CHECK(s.launcher.colors.text.toHex() == "#ffffff");
    // sec is a neutral grey, so it is no accent: the secondary is the derived one (readable on the sheet), not sec itself
    CHECK(s.launcher.colors.secondary.toHex() == "#9a9a9a");
    CHECK_FALSE(theme.has("colors.ini"));
}

TEST_CASE("convert: a partial theme stays partial, and a launcher image that is missing is not named") {
    TempDir tmp("theme_conv");
    tmp.makeSubDir("mini/images/GR");
    tmp.writeFile("mini/theme.ini", "[Theme]\nBackground=bg.png\nFsize=30\n");
    tmp.writeFile("mini/bg.png", "x");
    tmp.writeFile("mini/images/GR/JP_US_BG.png", "x");

    REQUIRE(ThemeConverter::convert(tmp.at("mini")));

    ThemeSpec s;
    REQUIRE(s.load(tmp.at("mini/theme.json")));
    CHECK(s.classic.background == "bg.png");
    CHECK(int(s.classic.font.size) == 30);
    CHECK(s.classic.font.file.empty());
    CHECK_FALSE(s.classic.menuPanel.set);
    CHECK(s.launcher.background == "images/launcher_background.png");
    CHECK(s.launcher.footer.empty());
    CHECK(s.launcher.fonts.bold.empty());
    CHECK(s.sounds.cursor.empty());
    CHECK(DirEntry::exists(tmp.at("mini/images/launcher_background.png")));
    CHECK_FALSE(DirEntry::exists(tmp.at("mini/font"))); // never there, not created either
}

TEST_CASE("convert twice: the second run has nothing to do and changes nothing") {
    TempDir tmp("theme_conv");
    OldTheme theme(tmp, "twice");
    REQUIRE(ThemeConverter::convert(theme.dir));
    string json = tmp.readFile("twice/theme.json");

    CHECK_FALSE(ThemeConverter::needsConversion(theme.dir));
    REQUIRE(ThemeConverter::convert(theme.dir)); // forced anyway: still fine
    CHECK(tmp.readFile("twice/theme.json") == json);
    CHECK(theme.has("images/launcher_background.png"));
    CHECK(theme.has("credit.txt"));
}

TEST_CASE("a folder that is neither is not a theme") {
    TempDir tmp("theme_conv");
    tmp.makeSubDir("junk/images");
    tmp.writeFile("junk/readme.txt", "x");
    CHECK_FALSE(ThemeConverter::needsConversion(tmp.at("junk")));

    tmp.makeSubDir("done");
    tmp.writeFile("done/theme.json", "{}");
    tmp.writeFile("done/theme.ini", "[Theme]\n"); // a leftover next to a theme.json is not a reason to convert again
    CHECK_FALSE(ThemeConverter::needsConversion(tmp.at("done")));
}

//*******************************
// the bridge (G6c2)
//*******************************
namespace {

// the bytes of one of our own fixture pictures (tests/data/color-fixtures)
string fixture(const string &name) {
    std::ifstream in(string(AB_TEST_DATA_DIR) + "/color-fixtures/" + name, std::ios::binary);
    std::ostringstream all;
    all << in.rdbuf();
    REQUIRE_FALSE(all.str().empty());
    return all.str();
}

// a small old theme whose background is one of the fixtures: the ini given, the stock Footer so it is a theme
struct PictureTheme {
    PictureTheme(TempDir &tmp, const string &name, const string &picture, const string &ini,
                 const string &colors = string())
        : dir(tmp.at(name)) {
        tmp.writeFile(name + "/background.png", fixture(picture));
        tmp.writeFile(name + "/theme.ini", ini);
        tmp.writeFile(name + "/ab.png", "logo");
        tmp.writeFile(name + "/images/GR/Footer.png", "footer");
        tmp.writeFile(name + "/images/GR/Squere_Btn_ICN.png", "square");
        if (!colors.empty())
            tmp.writeFile(name + "/colors.ini", colors);
    }

    string json() const { return dir + sep + "theme.json"; }
    bool hasFile(const string &rel) const { return DirEntry::exists(dir + sep + rel); }

    string dir;
};

const char *PLAIN_INI = "[Theme]\nBackground=background.png\nLogo=ab.png\nLpositionx=520\nLpositiony=0\nLw=240\nLh=180\n";

} // namespace

TEST_CASE("convert: the bridge - derived roles, the dark sheet and veil, the shared frames, the logo, the stamp") {
    TempDir tmp("theme_bridge");
    PictureTheme theme(tmp, "dark", "dark.png", PLAIN_INI);

    REQUIRE(ThemeConverter::convert(theme.dir));

    // the roles (the same numbers test_theme_color_deriver holds for this picture)
    ThemeSpec s;
    REQUIRE(s.load(theme.json()));
    const auto &c = s.launcher.colors;
    CHECK(c.text.toHex() == "#fceee4");
    CHECK(c.secondary.toHex() == "#909195");
    CHECK(c.hint.toHex() == "#909195");
    CHECK(c.row.color.toHex() == "#b9b0ac");
    CHECK(c.value.color.toHex() == "#b9b0ac");
    CHECK(c.rowSelected.color.toHex() == "#fceee4");
    CHECK(c.footer.color.toHex() == "#fceee4");
    CHECK(c.description.color.toHex() == "#909195");
    CHECK(c.heading.color.toHex() == "#e2701e");
    CHECK(c.edge.color.toHex() == "#e2701e");
    CHECK(c.selectionBand.color.toHex() == "#e2701e");

    // the sheet is dark, the veil is the sheet's colour at 120
    const ableem::ThemeSheet sheet = ableem::readThemeSheet(theme.json());
    CHECK(sheet.set);
    CHECK(sheet.color.toHex() == "#1d1f28");
    CHECK(sheet.alpha == 200);
    const ableem::ThemeDisabledVeil veil = ableem::readThemeDisabledVeil(theme.json());
    CHECK(veil.set);
    CHECK(veil.color.toHex() == "#1d1f28");
    CHECK(veil.alpha == 120);

    // ONE shared frame set, by the bridge: marker; the panel is a rim only, so the sheet shows through
    std::map<string, ableem::ThemeFrame> frames;
    for (const ableem::ThemeFrame &f : ableem::readThemeFrames(theme.json()))
        frames[f.name] = f;
    CHECK(frames.size() == 10);
    for (const char *name : {"panel", "selection", "heading", "key", "keyFunction", "keyLit", "keySelected", "field",
                             "chip", "badge"}) {
        REQUIRE(frames.count(name) == 1);
        CHECK(frames[name].image.compare(0, 7, "bridge:") == 0);
        CHECK_FALSE(frames[name].tint.empty());
    }
    CHECK(frames["panel"].image == "bridge:frames/panel.png");
    CHECK_FALSE(frames["panel"].fill);
    CHECK(frames["panel"].slice.left == 36);
    CHECK(frames["panel"].bleed.left == 12);
    CHECK(frames["panel"].tint == "edge");
    CHECK(frames["selection"].tint == "selectionBand");
    CHECK(frames["selection"].slice.top == 10);
    CHECK(frames["key"].image == "bridge:frames/key.png");
    CHECK(frames["keyFunction"].image == "bridge:frames/key_function.png");
    CHECK(frames["keySelected"].tint == "selectionBand");
    CHECK(frames["chip"].slice.left == 10);
    CHECK(frames["badge"].bleed.left == 4);
    CHECK_FALSE(DirEntry::exists(theme.dir + sep + "frames")); // nothing copied into the theme

    // the logo from the 1.0 keys, the Square hint renamed in
    const ableem::ThemeLauncherLogo logo = ableem::readThemeLogo(theme.json());
    REQUIRE(logo.set);
    CHECK(logo.file == "ab.png");
    CHECK(logo.x == 520);
    CHECK(logo.y == 0);
    CHECK(logo.w == 240);
    CHECK(logo.h == 180);
    CHECK(s.launcher.hints.square == "images/hint_square.png");
    CHECK(tmp.readFile("dark/images/hint_square.png") == "square");

    // the stamp, and its sum covers the blocks as they are in the file
    CHECK(ableem::readThemeJsonInt(theme.json(), "/converter/stamp", 0) == ThemeConverter::StampVersion);
    const string sum = ableem::readThemeJsonString(theme.json(), "/converter/sum");
    CHECK(sum.size() == 16);
    CHECK(sum == ableem::digestThemeJson(theme.json(), {"/launcher/colors", "/launcher/frames", "/launcher/logo",
                                                        "/launcher/hints"}));

    // the picture the colours came from is still the theme's background
    CHECK(DirEntry::exists(theme.dir + sep + "background.png"));
    CHECK_FALSE(DirEntry::exists(theme.dir + sep + "theme.ini"));
}

TEST_CASE("convert: 1.0 colour data comes first - Main_bg the sheet, colors.ini sec the accent, Text_fg the text") {
    TempDir tmp("theme_bridge");
    PictureTheme theme(tmp, "own", "dark.png",
                       "[Theme]\nBackground=background.png\nMain_bg=40,30,90\nText_fg=230,230,200\n",
                       "[colors]\nsec=20,200,120\n");

    REQUIRE(ThemeConverter::convert(theme.dir));

    ThemeSpec s;
    REQUIRE(s.load(theme.json()));
    CHECK(ableem::readThemeSheet(theme.json()).color.toHex() == "#2b243d");
    CHECK(s.launcher.colors.text.toHex() == "#e6e6c8");
    CHECK(s.launcher.colors.edge.color.toHex() == "#14c878");
    CHECK(s.launcher.colors.selectionBand.color.toHex() == "#14c878");
    CHECK(ableem::readThemeDisabledVeil(theme.json()).color.toHex() == "#2b243d");
}

TEST_CASE("convert: every derived text role is readable on the sheet, whatever the picture") {
    TempDir tmp("theme_bridge");
    int n = 0;
    for (const char *picture : {"dark.png", "light.png", "mono.png"}) {
        PictureTheme theme(tmp, string("t") + std::to_string(n++), picture, PLAIN_INI);
        REQUIRE(ThemeConverter::convert(theme.dir));
        ThemeSpec s;
        REQUIRE(s.load(theme.json()));
        const ableem::ThemeSheet sheet = ableem::readThemeSheet(theme.json());
        REQUIRE(sheet.set);
        const ThemeRgb under(sheet.color.r, sheet.color.g, sheet.color.b);
        const auto &c = s.launcher.colors;
        for (const ableem::ThemeColor *text : {&c.text, &c.secondary, &c.hint, &c.row.color, &c.rowSelected.color,
                                               &c.footer.color, &c.value.color, &c.description.color,
                                               &c.heading.color}) {
            INFO(picture << " " << text->toHex());
            CHECK(ThemeColorDeriver::contrast(ThemeRgb(text->r, text->g, text->b), under) >= 4.5);
        }
        INFO(picture << " edge " << c.edge.color.toHex());
        CHECK(ThemeColorDeriver::contrast(ThemeRgb(c.edge.color.r, c.edge.color.g, c.edge.color.b), under) >= 3.0);
    }
}

TEST_CASE("convert: a background that cannot be read gives the neutral sheet, still a complete bridge block") {
    TempDir tmp("theme_bridge");
    OldTheme theme(tmp, "nopicture"); // its background.jpg is three bytes of "jpg"

    REQUIRE(ThemeConverter::convert(theme.dir));

    const string json = theme.dir + sep + "theme.json";
    CHECK(ableem::readThemeSheet(json).set);
    CHECK(ableem::readThemeDisabledVeil(json).set);
    CHECK(ableem::readThemeFrames(json).size() == 10);
    ThemeSpec s;
    REQUIRE(s.load(json));
    CHECK(s.launcher.colors.edge.color.set);
    CHECK(s.launcher.colors.text.set);
}

TEST_CASE("convert: no launcher.logo for a theme with no logo file, an empty rect or no Logo at all") {
    TempDir tmp("theme_bridge");
    {
        PictureTheme zero(tmp, "zero", "dark.png", "[Theme]\nBackground=background.png\nLogo=ab.png\n"
                                                   "Lpositionx=1\nLpositiony=2\nLw=0\nLh=0\n");
        REQUIRE(ThemeConverter::convert(zero.dir));
        CHECK_FALSE(ableem::readThemeLogo(zero.json()).set); // shelves: a rect of 0 x 0
    }
    {
        PictureTheme empty(tmp, "empty", "dark.png", "[Theme]\nBackground=background.png\nLogo=\n"
                                                     "Lpositionx=1\nLpositiony=2\nLw=3\nLh=4\n");
        REQUIRE(ThemeConverter::convert(empty.dir));
        CHECK_FALSE(ableem::readThemeLogo(empty.json()).set); // strangerbleem: Logo= with nothing after it
    }
    {
        PictureTheme gone(tmp, "gone", "dark.png", PLAIN_INI);
        DirEntry::removeFile(gone.dir + sep + "ab.png");
        REQUIRE(ThemeConverter::convert(gone.dir));
        CHECK_FALSE(ableem::readThemeLogo(gone.json()).set); // named, but the file is not there
    }
    {
        PictureTheme none(tmp, "none", "dark.png", "[Theme]\nBackground=background.png\n");
        REQUIRE(ThemeConverter::convert(none.dir));
        CHECK_FALSE(ableem::readThemeLogo(none.json()).set);
    }
}

TEST_CASE("convert: a background named in another case is found (1.0 themes came from Windows)") {
    TempDir tmp("theme_bridge");
    PictureTheme theme(tmp, "cased", "dark.png", "[Theme]\nBackground=Background.PNG\n");

    REQUIRE(ThemeConverter::convert(theme.dir));

    CHECK(ableem::readThemeSheet(theme.json()).color.toHex() == "#1d1f28"); // read from the real picture
}

TEST_CASE("stamp: no derivation again without a bump, one with it - and the edited or unstamped are never touched") {
    TempDir tmp("theme_bridge");
    PictureTheme theme(tmp, "stamped", "dark.png", PLAIN_INI);
    REQUIRE(ThemeConverter::convert(theme.dir));
    CHECK(ableem::readThemeSheet(theme.json()).color.toHex() == "#1d1f28");

    // the picture changes under it; the stamp is current, so nothing is derived again
    tmp.writeFile("stamped/background.png", fixture("light.png"));
    CHECK_FALSE(ThemeConverter::needsUpgrade(theme.dir));
    CHECK_FALSE(ThemeConverter::upgrade(theme.dir));
    CHECK(ableem::readThemeSheet(theme.json()).color.toHex() == "#1d1f28");
    const string sumBefore = ableem::readThemeJsonString(theme.json(), "/converter/sum");

    // the stamp is bumped on purpose: derived once more, from the theme.json and the picture
    CHECK(ThemeConverter::needsUpgrade(theme.dir, ThemeConverter::StampVersion + 1));
    REQUIRE(ThemeConverter::upgrade(theme.dir, ThemeConverter::StampVersion + 1));
    CHECK(ableem::readThemeSheet(theme.json()).color.toHex() == "#222b45"); // the light picture's sheet
    CHECK(ableem::readThemeJsonInt(theme.json(), "/converter/stamp", 0) == ThemeConverter::StampVersion + 1);
    CHECK(ableem::readThemeJsonString(theme.json(), "/converter/sum") != sumBefore);
    CHECK(ableem::readThemeFrames(theme.json()).size() == 10);
    CHECK(ableem::readThemeLogo(theme.json()).set); // what the upgrade does not own is left as it was
    ThemeSpec s;
    REQUIRE(s.load(theme.json()));
    CHECK(s.launcher.hints.square == "images/hint_square.png");
    CHECK(s.classic.background == "background.png");

    // and once: the stamp is now the new one
    CHECK_FALSE(ThemeConverter::needsUpgrade(theme.dir, ThemeConverter::StampVersion + 1));
    CHECK_FALSE(ThemeConverter::upgrade(theme.dir, ThemeConverter::StampVersion + 1));

    // a theme edited after the conversion keeps its edits
    PictureTheme edited(tmp, "edited", "dark.png", PLAIN_INI);
    REQUIRE(ThemeConverter::convert(edited.dir));
    REQUIRE(ableem::mergeThemeJson(edited.json(), "{\"launcher\":{\"colors\":{\"edge\":\"#ff00ff\"}}}"));
    tmp.writeFile("edited/background.png", fixture("light.png"));
    CHECK(ThemeConverter::needsUpgrade(edited.dir, 5));
    CHECK_FALSE(ThemeConverter::upgrade(edited.dir, 5));
    CHECK(ableem::readThemeSheet(edited.json()).color.toHex() == "#1d1f28");
    ThemeSpec e;
    REQUIRE(e.load(edited.json()));
    CHECK(e.launcher.colors.edge.color.toHex() == "#ff00ff");
    CHECK(ableem::readThemeJsonInt(edited.json(), "/converter/stamp", 0) == ThemeConverter::StampVersion);

    // a theme.json nobody stamped (written by hand, or by the converter before the bridge) is never touched
    tmp.writeFile("hand/theme.json", "{ \"format\": 1, \"launcher\": { \"colors\": { \"text\": \"#ffffff\" } } }");
    tmp.writeFile("hand/background.png", fixture("light.png"));
    const string handBefore = tmp.readFile("hand/theme.json");
    CHECK_FALSE(ThemeConverter::needsUpgrade(tmp.at("hand"), 9));
    CHECK_FALSE(ThemeConverter::upgrade(tmp.at("hand"), 9));
    CHECK(tmp.readFile("hand/theme.json") == handBefore);
}

TEST_CASE("convert: a button glyph that is a sprite strip, and a Play button with nothing in it, are left out") {
    TempDir tmp("theme_bridge");
    PictureTheme theme(tmp, "strips", "dark.png", PLAIN_INI);
    tmp.writeFile("strips/images/GR/X_Btn_ICN.png", fixture("strip30x200.png"));          // 30 x 200: a strip
    tmp.writeFile("strips/images/GR/Circle_Btn_ICN.png", fixture("glyph30x30.png"));      // 30 x 30: a glyph
    tmp.writeFile("strips/images/GR/Tri_Btn_ICN.png", "not a picture, so it cannot be judged"); // used as it is
    tmp.writeFile("strips/images/GR/Squere_Btn_ICN.png", fixture("strip30x200.png"));
    tmp.writeFile("strips/images/GR/Acid_C_Btn.png", fixture("blank200x68.png"));         // the stock empty button
    tmp.writeFile("strips/images/BMP_Text/Play_Text.png", "the word");

    REQUIRE(ThemeConverter::convert(theme.dir));

    ThemeSpec s;
    REQUIRE(s.load(theme.json()));
    CHECK(s.launcher.hints.cross.empty());
    CHECK(s.launcher.hints.square.empty());
    CHECK(s.launcher.hints.circle == "images/hint_circle.png");
    CHECK(s.launcher.hints.triangle == "images/hint_triangle.png");
    CHECK(s.launcher.playButton.empty());
    CHECK(s.launcher.playText == "images/play_text.png"); // the word is the theme's own and stays
    CHECK(theme.hasFile("images/hint_circle.png"));
    CHECK_FALSE(theme.hasFile("images/hint_cross.png"));
    CHECK_FALSE(theme.hasFile("images/play_button.png"));
    CHECK_FALSE(theme.hasFile("images/GR")); // the unused ones went with the rest

    // a Play button with something in it, a glyph twice as tall as wide: both are used
    PictureTheme fine(tmp, "fine", "dark.png", PLAIN_INI);
    tmp.writeFile("fine/images/GR/Acid_C_Btn.png", fixture("button200x68.png"));
    tmp.writeFile("fine/images/GR/X_Btn_ICN.png", fixture("glyph30x30.png"));
    REQUIRE(ThemeConverter::convert(fine.dir));
    ThemeSpec f;
    REQUIRE(f.load(fine.json()));
    CHECK(f.launcher.playButton == "images/play_button.png");
    CHECK(f.launcher.hints.cross == "images/hint_cross.png");
}
