//
// ThemeConverter: theme.ini + the PSC data tree -> theme.json + role-named files.
//

#include "theme_converter.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <set>
#include <sstream>
#include <ableem/engine/image_pixels.h>
#include <ableem/engine/log.h>

using namespace std;

namespace {

const char *THEME_JSON = "theme.json";
const char *THEME_INI = "theme.ini";
const char *COLORS_INI = "colors.ini";

// the sub-directories the cleanup is confined to
const char *CLEANED_DIRS[] = {"images", "sounds", "font"};

// launcher.frames of every converted theme: the ONE shared bridge set (autobleem-design bridge/frames/frames.json),
// every image a "bridge:" path the frame reader resolves under the launcher's resources (bridge/). Cut lines and bleed
// are the designer's; `tint` names the derived role each frame is multiplied by.
const char *BRIDGE_FRAMES = R"json({
"panel":{"image":"bridge:frames/panel.png","slice":36,"bleed":12,"fill":false,"tint":"edge"},
"selection":{"image":"bridge:frames/selection.png","slice":{"left":12,"top":10,"right":12,"bottom":10},"bleed":4,"tint":"selectionBand"},
"heading":{"image":"bridge:frames/heading.png","slice":{"left":12,"top":6,"right":12,"bottom":6},"tint":"edge"},
"key":{"image":"bridge:frames/key.png","slice":16,"bleed":4,"tint":"edge"},
"keyFunction":{"image":"bridge:frames/key_function.png","slice":16,"bleed":4,"tint":"edge"},
"keyLit":{"image":"bridge:frames/key_lit.png","slice":16,"bleed":4,"tint":"edge"},
"keySelected":{"image":"bridge:frames/key_selected.png","slice":16,"bleed":4,"tint":"selectionBand"},
"field":{"image":"bridge:frames/field.png","slice":16,"bleed":4,"tint":"edge"},
"chip":{"image":"bridge:frames/chip.png","slice":10,"bleed":2,"tint":"edge"},
"badge":{"image":"bridge:frames/badge.png","slice":12,"bleed":4,"tint":"edge"}
})json";

// the blocks of a theme.json the converter owns: the stamp's sum covers them, so a theme whose blocks changed since is
// one somebody edited and is never derived again
const vector<string> OWNED_BLOCKS = {"/launcher/colors", "/launcher/frames", "/launcher/logo", "/launcher/hints"};

//*******************************
// ini helpers
//*******************************
bool has(const IniFile &ini, const string &key) {
    return ini.values.find(key) != ini.values.end();
}

bool hasAll(const IniFile &ini, const vector<string> &keys) {
    for (const string &key : keys)
        if (!has(ini, key))
            return false;
    return true;
}

// atoi's leniency is what the old code had: "95;" is 95
int intOf(const IniFile &ini, const string &key) {
    return atoi(ini.values.at(key).c_str());
}

string strOf(const IniFile &ini, const string &key) {
    return Strings::trim(ini.values.at(key));
}

void colorOf(const IniFile &ini, const string &key, ThemeColor &out) {
    if (!has(ini, key))
        return;
    if (!ThemeColor::parseRgb(ini.values.at(key), out)) {
        PLOG_INFO << "theme.ini: " << key << "=" << ini.values.at(key) << " is not r,g,b - ignored";
    }
}

// an ini rect: the four keys together or not at all
bool rectOf(const IniFile &ini, const string &x, const string &y, const string &w, const string &h, int &ox, int &oy,
            int &ow, int &oh) {
    if (!hasAll(ini, {x, y, w, h})) {
        if (has(ini, x) || has(ini, y) || has(ini, w) || has(ini, h)) {
            PLOG_INFO << "theme.ini: " << x << "/" << y << "/" << w << "/" << h << " incomplete - ignored";
        }
        return false;
    }
    ox = intOf(ini, x);
    oy = intOf(ini, y);
    ow = intOf(ini, w);
    oh = intOf(ini, h);
    return true;
}

//*******************************
// file helpers
//*******************************
// every file under `dir`, as paths relative to `dir`, depth first
void listFiles(const string &dir, const string &prefix, vector<string> &out) {
    for (const DirEntry &entry : DirEntry::diru(dir)) {
        string rel = prefix.empty() ? entry.name : prefix + sep + entry.name;
        if (entry.isDir)
            listFiles(dir + sep + entry.name, rel, out);
        else
            out.push_back(rel);
    }
}

// removes the directories under `dir` that are left empty, deepest first, and `dir` itself if it ends up empty
void removeEmptyDirs(const string &dir) {
    for (const DirEntry &entry : DirEntry::diru_DirsOnly(dir))
        removeEmptyDirs(dir + sep + entry.name);
    if (DirEntry::diru(dir).empty())
        DirEntry::removeDirAndContents(dir);
}

//*******************************
// which picture stands for a role
//*******************************
// the file a role is taken from, `images` being <theme>/images/: the renamed one when the folder is already converted,
// else the first old name that is there; "" when the theme has none
string roleSource(const string &images, const ThemeConverter::Role &role) {
    if (DirEntry::exists(images + role.newName))
        return images + role.newName;
    for (const string &oldName : role.oldNames)
        if (DirEntry::exists(images + oldName))
            return images + oldName;
    return string();
}

// whether the picture at `file` can stand for the role (Role::Require); one that cannot be decoded is used as it is
bool fitsRole(const ThemeConverter::Role &role, const string &file) {
    using Require = ThemeConverter::Require;
    if (role.require == Require::Square) {
        int w = 0;
        int h = 0;
        if (ableem::readImageSize(file, w, h) && (h > 2 * w || w > 2 * h)) {
            PLOG_INFO << "Theme image " << file << " is " << w << "x" << h << ", not a button glyph - not used";
            return false;
        }
    } else if (role.require == Require::Visible) {
        ableem::ImagePixels picture;
        if (ableem::readImagePixels(file, picture)) {
            bool visible = false;
            for (size_t i = 3; i < picture.rgba.size() && !visible; i += 4)
                visible = picture.rgba[i] != 0;
            if (!visible) {
                PLOG_INFO << "Theme image " << file << " is fully transparent - not used";
                return false;
            }
        }
    }
    return true;
}

//*******************************
// the bridge's helpers
//*******************************
// `text` as a JSON string literal
string quoted(const string &text) {
    string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
            out += buf;
        } else {
            out += c;
        }
    }
    return out + "\"";
}

// `name` in `dir`'s root: as written, else whichever file differs only in case (1.0 themes were made on Windows); "" none
string findInRoot(const string &dir, const string &name) {
    if (name.empty())
        return string();
    if (DirEntry::exists(dir + sep + name))
        return dir + sep + name;
    if (name.find_first_of("/\\") != string::npos)
        return string();
    auto lower = [](string s) {
        for (char &c : s)
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const string wanted = lower(name);
    for (const DirEntry &entry : DirEntry::diru(dir))
        if (!entry.isDir && lower(entry.name) == wanted)
            return dir + sep + entry.name;
    return string();
}

// the picture the colours are read from: the theme's background, else the 1.0 default names
string backgroundFile(const string &dir, const string &named) {
    string file = findInRoot(dir, named);
    for (const char *fallback : {"background.png", "background.jpg"})
        if (file.empty())
            file = findInRoot(dir, fallback);
    return file;
}

void putRole(ostringstream &o, const char *name, const ThemeRgb &c) {
    o << quoted(name) << ":" << quoted(c.hex()) << ",";
}

// The JSON the bridge adds to a written theme.json (mergeThemeJson): every colour role, the sheet and the veil, the
// shared frames, and - for a theme that has one - its logo. `logo` is the launcher.logo object's text, "" for none.
string bridgePatch(const ThemeColorRoles &r, const string &logo) {
    ostringstream o;
    o << "{\"launcher\":{\"colors\":{";
    putRole(o, "text", r.text);
    putRole(o, "secondary", r.secondary);
    putRole(o, "hint", r.hint);
    putRole(o, "row", r.row);
    putRole(o, "rowSelected", r.rowSelected);
    putRole(o, "heading", r.heading);
    putRole(o, "value", r.value);
    putRole(o, "description", r.description);
    putRole(o, "footer", r.footer);
    putRole(o, "selectionBand", r.selectionBand);
    putRole(o, "edge", r.edge);
    o << "\"sheet\":{\"color\":" << quoted(r.sheet.hex()) << ",\"alpha\":" << r.sheetAlpha << "},"
      << "\"disabled\":{\"color\":" << quoted(r.disabled.hex()) << ",\"alpha\":" << r.disabledAlpha << "}},"
      << "\"frames\":" << BRIDGE_FRAMES;
    if (!logo.empty())
        o << ",\"logo\":" << logo;
    o << "}}";
    return o.str();
}

// the stamp: the version, and the sum of the blocks as they are now in the file
bool writeStamp(const string &themeJson, int stamp) {
    const string sum = ableem::digestThemeJson(themeJson, OWNED_BLOCKS);
    return ableem::mergeThemeJson(themeJson, "{\"converter\":{\"stamp\":" + to_string(stamp) + ",\"sum\":" + quoted(sum) +
                                                 "}}");
}

} // namespace

constexpr int ThemeConverter::StampVersion;

//*******************************
// ThemeConverter::launcherRoles
//*******************************
// The PSC names GuiLauncher, PsMenu and GuiMcManager used to load, in the order they preferred them: an
// "_AB" variant, when a theme had one, won over the stock file.
const vector<ThemeConverter::Role> &ThemeConverter::launcherRoles() {
    static const vector<Role> roles = {
        {{"GR/AB_BG.png", "GR/JP_US_BG.png"},
         "launcher_background.png",
         [](ThemeSpec &s) { return &s.launcher.background; }},
        {{"GR/Footer_AB.png", "GR/Footer.png"}, "launcher_footer.png", [](ThemeSpec &s) { return &s.launcher.footer; }},
        {{"GR/Acid_C_Btn.png"},
         "play_button.png",
         [](ThemeSpec &s) { return &s.launcher.playButton; },
         Require::Visible},
        {{"BMP_Text/Play_Text.png"}, "play_text.png", [](ThemeSpec &s) { return &s.launcher.playText; }},
        {{"CB/Function_AB.png", "CB/Function_BG.png"},
         "settings_panel.png",
         [](ThemeSpec &s) { return &s.launcher.settingsPanel; }},
        {{"CB/PlayerOne.png"}, "meta_panel.png", [](ThemeSpec &s) { return &s.launcher.metaPanel; }},
        {{"GR/arrow.png"}, "arrow.png", [](ThemeSpec &s) { return &s.launcher.arrow; }},
        {{"GR/X_Btn_ICN.png"},
         "hint_cross.png",
         [](ThemeSpec &s) { return &s.launcher.hints.cross; },
         Require::Square},
        {{"GR/Circle_Btn_ICN.png"},
         "hint_circle.png",
         [](ThemeSpec &s) { return &s.launcher.hints.circle; },
         Require::Square},
        {{"GR/Tri_Btn_ICN.png"},
         "hint_triangle.png",
         [](ThemeSpec &s) { return &s.launcher.hints.triangle; },
         Require::Square},
        // 1.0 spelled it "Squere"; 50 of the pack's 51 themes ship one, and the footer's hints have a Square (G6b3)
        {{"GR/Squere_Btn_ICN.png", "GR/Square_Btn_ICN.png"},
         "hint_square.png",
         [](ThemeSpec &s) { return &s.launcher.hints.square; },
         Require::Square},
        {{"CB/Setting_ICN.png"}, "menu_settings.png", [](ThemeSpec &s) { return &s.launcher.menuIcons.settings; }},
        {{"CB/Manual_ICN.png"}, "menu_guide.png", [](ThemeSpec &s) { return &s.launcher.menuIcons.guide; }},
        {{"CB/MemoryCard_ICN.png"}, "menu_memcard.png", [](ThemeSpec &s) { return &s.launcher.menuIcons.memcard; }},
        {{"CB/Resume.png"}, "menu_resume.png", [](ThemeSpec &s) { return &s.launcher.menuIcons.resume; }},
        {{"MC/Dot_Matrix.png"}, "memcard_grid.png", [](ThemeSpec &s) { return &s.launcher.memcardManager.grid; }},
        {{"MC/Pencil_Carsor.png"},
         "memcard_pencil.png",
         [](ThemeSpec &s) { return &s.launcher.memcardManager.pencil; }},
    };
    return roles;
}

//*******************************
// ThemeConverter::needsConversion
//*******************************
bool ThemeConverter::needsConversion(const string &themeDir) {
    if (DirEntry::exists(themeDir + sep + THEME_JSON))
        return false;
    if (DirEntry::exists(themeDir + sep + THEME_INI))
        return true;
    const string images = themeDir + sep + "images" + sep;
    for (const string &name : launcherRoles().front().oldNames)
        if (DirEntry::exists(images + name))
            return true;
    return false;
}

//*******************************
// ThemeConverter::isThemeFolder
//*******************************
bool ThemeConverter::isThemeFolder(const string &themeDir) {
    return DirEntry::exists(themeDir + sep + THEME_JSON) || needsConversion(themeDir);
}

//*******************************
// ThemeConverter::specFromIni
//*******************************
ThemeSpec ThemeConverter::specFromIni(const IniFile &ini) {
    ThemeSpec spec;
    ClassicTheme &c = spec.classic;

    if (has(ini, "loop") && strOf(ini, "loop") == "-1") {
        spec.music.set = true;
        spec.music.none = true;
    } else if (has(ini, "music")) {
        spec.music.set = true;
        spec.music.file = strOf(ini, "music");
        spec.music.loop = !has(ini, "loop") || strOf(ini, "loop") == "1";
    }

    if (has(ini, "background"))
        c.background = strOf(ini, "background");
    if (has(ini, "logo"))
        c.logo.file = strOf(ini, "logo");
    c.logo.set = rectOf(ini, "lpositionx", "lpositiony", "lw", "lh", c.logo.x, c.logo.y, c.logo.w, c.logo.h);
    if (has(ini, "font"))
        c.font.file = strOf(ini, "font");
    if (has(ini, "fsize"))
        c.font.size = intOf(ini, "fsize");
    if (has(ini, "lines"))
        c.menuLines = intOf(ini, "lines");

    c.menuPanel.set = rectOf(ini, "opscreenx", "opscreeny", "opscreenw", "opscreenh", c.menuPanel.x, c.menuPanel.y,
                             c.menuPanel.w, c.menuPanel.h);
    colorOf(ini, "main_bg", c.menuPanel.color);
    if (has(ini, "mainalpha"))
        c.menuPanel.alpha = intOf(ini, "mainalpha");

    c.statusBar.set =
        rectOf(ini, "textx", "texty", "textw", "texth", c.statusBar.x, c.statusBar.y, c.statusBar.w, c.statusBar.h);
    colorOf(ini, "text_bg", c.statusBar.color);
    if (has(ini, "textalpha"))
        c.statusBar.alpha = intOf(ini, "textalpha");
    if (has(ini, "ttop"))
        c.statusBar.textY = intOf(ini, "ttop");

    colorOf(ini, "text_fg", c.textColor);
    colorOf(ini, "key_bg", c.keyboardKey.color);
    if (has(ini, "keyalpha"))
        c.keyboardKey.alpha = intOf(ini, "keyalpha");
    colorOf(ini, "label_bg", c.labelColor);

    if (hasAll(ini, {"fsposx", "fsposy"})) {
        c.freeSpaceText.set = true;
        c.freeSpaceText.x = intOf(ini, "fsposx");
        c.freeSpaceText.y = intOf(ini, "fsposy");
    }
    if (hasAll(ini, {"ecoverx", "ecovery"})) {
        c.editorCover.set = true;
        c.editorCover.x = intOf(ini, "ecoverx");
        c.editorCover.y = intOf(ini, "ecovery");
    }

    struct {
        const char *key;
        string *field;
    } buttons[] = {
        {"cross", &c.buttons.cross},   {"circle", &c.buttons.circle},
        {"square", &c.buttons.square}, {"triangle", &c.buttons.triangle},
        {"start", &c.buttons.start},   {"select", &c.buttons.select},
        {"l1", &c.buttons.l1},         {"r1", &c.buttons.r1},
        {"l2", &c.buttons.l2},         {"r2", &c.buttons.r2},
        {"check", &c.buttons.check},   {"uncheck", &c.buttons.uncheck},
        {"esc", &c.buttons.esc},       {"enter", &c.buttons.enter},
        {"tab", &c.buttons.tab},
    };
    for (const auto &b : buttons)
        if (has(ini, b.key))
            *b.field = strOf(ini, b.key);

    return spec;
}

//*******************************
// ThemeConverter::specFor
//*******************************
ThemeSpec ThemeConverter::specFor(const string &themeDir) {
    ThemeSpec spec;
    // a theme.json already there (a conversion that did not get to the end) is the starting point
    if (DirEntry::exists(themeDir + sep + THEME_JSON))
        spec.load(themeDir + sep + THEME_JSON);
    if (DirEntry::exists(themeDir + sep + THEME_INI)) {
        IniFile ini;
        ini.load(themeDir + sep + THEME_INI);
        spec = specFromIni(ini);
    }

    if (DirEntry::exists(themeDir + sep + COLORS_INI)) {
        IniFile colors;
        colors.load(themeDir + sep + COLORS_INI);
        colorOf(colors, "fg", spec.launcher.colors.text);
        colorOf(colors, "sec", spec.launcher.colors.secondary);
    }

    // the launcher images: whichever old name the theme has, or the new name if it is already renamed
    const string images = themeDir + sep + "images" + sep;
    bool hasLauncherImages = false;
    for (const Role &role : launcherRoles()) {
        const string source = roleSource(images, role);
        if (!source.empty() && fitsRole(role, source)) {
            *role.field(spec) = string("images") + sep + role.newName;
            hasLauncherImages = true;
        }
    }
    // GuiLauncher took an AB_BG.png as "the meta panel does not move"
    if (hasLauncherImages)
        spec.launcher.metaPanelSlides = !DirEntry::exists(images + "GR/AB_BG.png");

    struct {
        const char *rel;
        string *field;
    } named[] = {
        {"font/SST-Medium.ttf", &spec.launcher.fonts.medium}, {"font/SST-Bold.ttf", &spec.launcher.fonts.bold},
        {"sounds/cursor.wav", &spec.sounds.cursor},           {"sounds/cancel.wav", &spec.sounds.cancel},
        {"sounds/home_up.wav", &spec.sounds.homeUp},          {"sounds/home_down.wav", &spec.sounds.homeDown},
        {"sounds/resume_new.wav", &spec.sounds.resume},
    };
    for (const auto &n : named)
        if (DirEntry::exists(themeDir + sep + n.rel))
            *n.field = n.rel;

    return spec;
}

//*******************************
// ThemeConverter::rolesFor
//*******************************
ThemeColorRoles ThemeConverter::rolesFor(const string &themeDir, const ThemeSpec &spec) {
    ThemeColorInput in;
    ableem::ImagePixels picture;
    const string file = backgroundFile(themeDir, spec.classic.background);
    if (!file.empty() && ableem::readImagePixels(file, picture)) {
        in.pixels = picture.rgba.data();
        in.width = picture.width;
        in.height = picture.height;
        in.channels = 4;
    }
    // colors.ini's fg first, then theme.ini's Text_fg (the order the prototype read them in)
    const ThemeColor &text = spec.launcher.colors.text.set ? spec.launcher.colors.text : spec.classic.textColor;
    if (text.set) {
        in.hasText = true;
        in.text = ThemeRgb(text.r, text.g, text.b);
    }
    const ThemeColor &secondary = spec.launcher.colors.secondary;
    if (secondary.set) {
        in.hasSecondary = true;
        in.secondary = ThemeRgb(secondary.r, secondary.g, secondary.b);
    }
    const ThemeColor &mainBg = spec.classic.menuPanel.color;
    if (mainBg.set) {
        in.hasMainBg = true;
        in.mainBg = ThemeRgb(mainBg.r, mainBg.g, mainBg.b);
    }
    ThemeColorRoles roles = ThemeColorDeriver::derive(in);
    for (const string &note : roles.notes) {
        PLOG_INFO << "Theme colours: " << note;
    }
    for (const string &why : roles.fallbacks) {
        PLOG_INFO << "Theme colours, fallback: " << why;
    }
    return roles;
}

//*******************************
// ThemeConverter::needsUpgrade / upgrade
//*******************************
bool ThemeConverter::needsUpgrade(const string &themeDir, int stamp) {
    const int has = ableem::readThemeJsonInt(themeDir + sep + THEME_JSON, "/converter/stamp", 0);
    return has > 0 && has < stamp;
}

bool ThemeConverter::upgrade(const string &themeDir, int stamp) {
    const string json = themeDir + sep + THEME_JSON;
    if (!needsUpgrade(themeDir, stamp))
        return false;
    if (ableem::digestThemeJson(json, OWNED_BLOCKS) != ableem::readThemeJsonString(json, "/converter/sum")) {
        PLOG_INFO << "Theme " << themeDir << " was edited after it was converted - not derived again";
        return false;
    }
    ThemeSpec spec;
    if (!spec.load(json))
        return false;
    PLOG_INFO << "Deriving the colours of " << themeDir << " again (converter stamp " << stamp << ")";
    const ThemeColorRoles roles = rolesFor(themeDir, spec);
    if (!ableem::mergeThemeJson(json, bridgePatch(roles, string()))) {
        PLOG_WARNING << "Theme not upgraded, could not write " << THEME_JSON;
        return false;
    }
    return writeStamp(json, stamp);
}

//*******************************
// ThemeConverter::convert
//*******************************
bool ThemeConverter::convert(const string &themeDir) {
    PLOG_INFO << "Converting theme folder to theme.json: " << themeDir;
    ThemeSpec spec = specFor(themeDir);
    // the colours come from the 1.0 files and the background, so before theme.ini and colors.ini go
    const ThemeColorRoles roles = rolesFor(themeDir, spec);

    // launcher.logo from the 1.0 Logo and Lposition keys - not for a theme with no logo file or an empty rect (shelves)
    const auto &logo = spec.classic.logo;
    string logoJson;
    if (logo.set && logo.w > 0 && logo.h > 0 && !logo.file.empty() && DirEntry::exists(themeDir + sep + logo.file)) {
        logoJson = "{\"file\":" + quoted(logo.file) + ",\"x\":" + to_string(logo.x) + ",\"y\":" + to_string(logo.y) +
                   ",\"w\":" + to_string(logo.w) + ",\"h\":" + to_string(logo.h) + "}";
    }

    // 1. theme.json first: from here on the folder is readable by the new code whatever happens next
    const string json = themeDir + sep + THEME_JSON;
    if (!spec.save(json)) {
        PLOG_WARNING << "Theme not converted, could not write " << THEME_JSON;
        return false;
    }
    // the bridge: roles, sheet, veil, frames and logo (ThemeSpec holds none of the last four), then the stamp
    if (!ableem::mergeThemeJson(json, bridgePatch(roles, logoJson)) || !writeStamp(json, StampVersion)) {
        PLOG_WARNING << "Theme converted without its bridge block, could not extend " << THEME_JSON;
    }

    // 2. the launcher images to their role names
    const string images = themeDir + sep + "images" + sep;
    for (const Role &role : launcherRoles()) {
        if (DirEntry::exists(images + role.newName))
            continue;
        for (const string &oldName : role.oldNames) {
            if (!DirEntry::exists(images + oldName))
                continue;
            if (!fitsRole(role, images + oldName))
                break; // not named in the json: removed with the rest
            if (!DirEntry::renameFile(images + oldName, images + role.newName)) {
                PLOG_WARNING << "Could not rename " << images + oldName << " to " << role.newName;
            }
            break;
        }
    }

    // 3. everything the json does not name goes, but only under the three data directories
    set<string> keep;
    for (const string &file : spec.referencedFiles())
        keep.insert(file);
    DirEntry::removeFile(themeDir + sep + THEME_INI);
    DirEntry::removeFile(themeDir + sep + COLORS_INI);
    int removed = 0;
    for (const char *sub : CLEANED_DIRS) {
        const string dir = themeDir + sep + sub;
        if (!DirEntry::isDirectory(dir))
            continue;
        vector<string> files;
        listFiles(dir, sub, files);
        for (const string &rel : files) {
            if (keep.count(rel))
                continue;
            if (DirEntry::removeFile(themeDir + sep + rel))
                removed++;
        }
        removeEmptyDirs(dir);
    }
    PLOG_INFO << "Theme converted: " << keep.size() << " files kept, " << removed << " removed";
    return true;
}
