//
// ThemeAssets: the asset half of what Gui used to be.
//
#include "theme_assets.h"
#include "gui.h" // Gui::tickBusy, the spinner between the loads
#include "../core/services/environment.h"
#include "../core/main.h"

#include <ableem/engine/theme_spec.h> // themeImageFile, the @2x choice (ab_gui G4f); resolveThemeIcons (G5a)

using namespace std;
using ableem::Texture;

//*******************************
// ThemeAssets::ThemeAssets
//*******************************
ThemeAssets::ThemeAssets(ableem::Renderer &renderer, Theme &theme, Config &config)
    : renderer_(renderer), theme_(theme), config_(config) {}

//*******************************
// ThemeAssets::load
//*******************************
void ThemeAssets::unload() {
    themeFonts.closeAll();
    fixedFonts().closeAll();
    themeFont = ableem::Font();
    backgroundImg = Texture();
    logo = Texture();
    cdJewel = Texture();
    bigBoxFrame = Texture();
    buttonTextureMap.clear();
    hintCross = hintCircle = hintTriangle = hintSquare = Texture();
    dpadUp = dpadDown = dpadLeft = dpadRight = Texture();
    dpadUpOutline = dpadDownOutline = dpadLeftOutline = dpadRightOutline = Texture();
}

//*******************************
// ThemeAssets::load
//*******************************
void ThemeAssets::load() {
    theme_.load(); // (re)reads theme.json, merged over themes/default, every file resolved
    const ClassicTheme &classic = theme_.classic();
    const LauncherTheme &launcher = theme_.launcher();

    backgroundImg = Texture(); // release the previous theme's textures before loading the new ones

    logoRect.x = classic.logo.x;
    logoRect.y = classic.logo.y;
    logoRect.w = classic.logo.w;
    logoRect.h = classic.logo.h;

    backgroundImg = loadImage(renderer_, classic.background);
    Gui::tickBusy();
    // drawn at its own size from the top-left corner (the theme's is the screen's); the splash used to be
    // the only place setting this, which left every program without a splash with no background at all.
    // Logical: an @2x background's size() is its pixels over 2, the 1x one's size
    ableem::Size backgroundSize = backgroundImg.size();
    backgroundRect = ableem::Rect(0, 0, backgroundSize.w, backgroundSize.h);
    logo = loadImage(renderer_, classic.logo.file);
    if (renderer_.fourByThreeOutput()) {
        // a 4:3 (CRT) output: the classic screens have the Gui's 4:3 canvas (Gui::CrtCanvasW x H), not the theme's
        // 1280x720 one - the theme's 4:3 picture (layout4x3 classicBackground, else the launcher's background) fills
        // it and the logo keeps its shape in the same place relative to it
        const int cw = renderer_.restCanvasWidth(), ch = renderer_.restCanvasHeight();
        const ableem::ThemeLayout4x3 layout4x3 = ableem::loadThemeLayout4x3(theme_.loadedPath());
        string picture = layout4x3.image("classicBackground");
        if (picture.empty())
            picture = layout4x3.image("background");
        if (layout4x3.set && !picture.empty()) {
            Texture fourByThree = loadImage(renderer_, picture);
            if (fourByThree.valid())
                backgroundImg = fourByThree;
        }
        backgroundRect = ableem::Rect(0, 0, cw, ch);
        const double sx = cw / 1280.0, sy = ch / 720.0, s = min(sx, sy);
        const double centreX = (classic.logo.x + classic.logo.w / 2.0) * sx;
        const double centreY = (classic.logo.y + classic.logo.h / 2.0) * sy;
        logoRect.w = static_cast<int>(classic.logo.w * s + 0.5);
        logoRect.h = static_cast<int>(classic.logo.h * s + 0.5);
        logoRect.x = static_cast<int>(centreX - logoRect.w / 2.0 + 0.5);
        logoRect.y = static_cast<int>(centreY - logoRect.h / 2.0 + 0.5);
    }
    bigBoxFrame = loadImage(renderer_, Env::getWorkingPath() + sep + "evoimg/bigbox.png");
    if (config_.inifile.values["jewel"] != "none") {
        if (config_.inifile.values["jewel"] == "default") {
            cdJewel = loadImage(renderer_, Env::getWorkingPath() + sep + "evoimg/nofilter.png");
        } else {
            cdJewel =
                loadImage(renderer_, Env::getWorkingPath() + sep + "evoimg/frames/" + config_.inifile.values["jewel"]);
        }
    } else {
        cdJewel = Texture();
    }

    Gui::tickBusy();
    const auto &b = classic.buttons;
    buttonTextureMap["O"] = loadImage(renderer_, b.circle);
    buttonTextureMap["X"] = loadImage(renderer_, b.cross);
    buttonTextureMap["T"] = loadImage(renderer_, b.triangle);
    buttonTextureMap["S"] = loadImage(renderer_, b.square);
    buttonTextureMap["Select"] = loadImage(renderer_, b.select);
    buttonTextureMap["Start"] = loadImage(renderer_, b.start);
    buttonTextureMap["L1"] = loadImage(renderer_, b.l1);
    buttonTextureMap["R1"] = loadImage(renderer_, b.r1);
    buttonTextureMap["L2"] = loadImage(renderer_, b.l2);
    buttonTextureMap["R2"] = loadImage(renderer_, b.r2);
    buttonTextureMap["Check"] = loadImage(renderer_, b.check);
    buttonTextureMap["Uncheck"] = loadImage(renderer_, b.uncheck);
    checkIconRightMargin = 0;
    // measured on the 1x file (`path`), whichever one is drawn: its pixels are logical, like the texture's size()
    auto rightMargin = [](const string &path, const Texture &texture) {
        ableem::Rect bounds = Texture::opaqueBounds(path);
        ableem::Size whole = texture.size();
        return (bounds.w > 0 && whole.w > 0) ? whole.w - (bounds.x + bounds.w) : 0;
    };
    checkIconRightMargin =
        max(rightMargin(b.check, buttonTextureMap["Check"]), rightMargin(b.uncheck, buttonTextureMap["Uncheck"]));
    buttonTextureMap["Esc"] = loadImage(renderer_, b.esc);
    buttonTextureMap["Enter"] = loadImage(renderer_, b.enter);
    buttonTextureMap["Tab"] = loadImage(renderer_, b.tab);
    hintCross = loadImage(renderer_, launcher.hints.cross);
    hintCircle = loadImage(renderer_, launcher.hints.circle);
    hintTriangle = loadImage(renderer_, launcher.hints.triangle);
    hintSquare = loadImage(renderer_, launcher.hints.square);
    // the d-pad arrows from the icon table (ab_gui G5a): the theme's launcher.icons dpadUp..., else the default's, else
    // the built-in evoimg/dpad_*.png - on a theme without the block the very files and calls of before (abgui::loadIcon
    // loads a 1x file exactly as loadImage did, an @2x next to it above scale 1 the same way too)
    const map<string, abgui::IconSpec> icons = iconSpecs(theme_);
    const bool halo = iconHalo(theme_);
    auto arrow = [&](const char *name, Texture &texture, Texture &outline) {
        auto it = icons.find(name);
        const abgui::IconSpec spec = it == icons.end() ? abgui::IconSpec() : it->second;
        texture = abgui::loadIcon(renderer_, spec);
        // the arrow's own dark halo (UIREV-2, L1), made once here rather than per frame from the 1x file even when an
        // @2x arrow is drawn: the outline is placed by the arrow's logical size (Style::button). None with
        // "iconHalo": false - the theme's arrows carry their own glow
        outline = halo ? abgui::loadIconHalo(renderer_, spec) : Texture();
    };
    arrow("dpadUp", dpadUp, dpadUpOutline);
    arrow("dpadDown", dpadDown, dpadDownOutline);
    arrow("dpadLeft", dpadLeft, dpadLeftOutline);
    arrow("dpadRight", dpadRight, dpadRightOutline);

    // a theme without launcher fonts gets the shipped pair - Red Hat Text Medium/SemiBold (OFL), since UIREV-31
    // (it was Open Sans Medium/Bold, the stand-in for the console's SST, from 2026-09-21)
    // the classic screens' font is one with the launcher's (UIREV-31): the theme's launcher.fonts medium - Red Hat Text
    // Medium on a theme that sets none - or the user's own when Options says so, or the CJK font for a language that
    // needs it; a theme's classic.font is not read (2026-09-29, the owner) - so it is never opened either
    const Fonts::Pick pick =
        Fonts::pickFonts(config_.inifile.values["themefont"], config_.inifile.values["font"],
                         config_.inifile.values["language"], launcher.fonts.medium, launcher.fonts.bold);
    const string &medium = pick.medium;
    const string &bold = pick.bold;
    string classicFont = pick.classic;
    const bool cjk = Fonts::cjkFontFor(config_.inifile.values["language"]) != "";
    if (cjk) {
        PLOG_INFO << "Language " << config_.inifile.values["language"] << ": every font is " << classicFont;
    }
    classicFontFile_ = classicFont;
    PLOG_INFO << "Classic font: " << classicFont;
    Gui::tickBusy();
    themeFont = Fonts::openNewSharedCachedFont(classicFont, Fonts::ClassicFontSize, renderer_);
    bool userFont = pick.userFont;
    if (!themeFont.valid() && classicFont != medium) {
        // a file that is no font (an empty one crashed every screen drawing with it) - the default instead
        PLOG_WARNING << "Cannot open the font " << classicFont << ", using the default";
        classicFont = classicFontFile_ = medium;
        userFont = false;
        themeFont = Fonts::openNewSharedCachedFont(classicFont, Fonts::ClassicFontSize, renderer_);
    }
    fixedFonts().openAllFonts(medium, bold, renderer_);
    if (userFont) {
        PLOG_INFO << "UI font: " << classicFont;
        themeFonts.openAllFonts(classicFont, classicFont, renderer_); // a user's font has no bold of its own
    } else {
        themeFonts = fixedFonts(); // the same pair: shared handles, nothing opened twice
    }
    Gui::tickBusy();
}

//*******************************
// ThemeAssets::loadImage
//*******************************
Texture ThemeAssets::loadImage(ableem::Renderer &renderer, const string &file) {
    float pixelScale = 1.0f;
    const string picked = ableem::themeImageFile(file, renderer.outputScale(), pixelScale);
    if (pixelScale == 1.0f)
        return Texture::loadFile(renderer, picked); // the 1x file: the very call it always was
    return Texture::loadFile(renderer, picked, pixelScale);
}

//*******************************
// ThemeAssets::builtInIcons / iconSpecs / iconHalo
//*******************************
map<string, string> ThemeAssets::builtInIcons(const Theme &theme) {
    static const struct {
        const char *name;
        const char *file;
    } evoimgIcons[] = {
        // the meta row (G5b)
        {"disc", "cd.png"},
        {"usb", "usb.png"},
        {"internal", "ps1.png"},
        {"hd", "hd.png"},
        {"sd", "sd.png"},
        {"lock", "lock.png"},
        {"unlock", "unlock.png"},
        {"favorite", "favorite.png"},
        {"retroarch", "ra.png"},
        {"lightgun", "lightgun.png"},
        {"lightgun2", "lightgun2.png"},
        // the hint lines and footers (G5a)
        {"dpadUp", "dpad_up.png"},
        {"dpadDown", "dpad_down.png"},
        {"dpadLeft", "dpad_left.png"},
        {"dpadRight", "dpad_right.png"},
        // the set picker's tabs, the missing-art covers, the big box's edge (G5c)
        {"tabPlayStation", "tab_playstation.png"},
        {"tabRetroArch", "tab_retroarch.png"},
        {"tabApps", "tab_apps.png"},
        {"raCover", "ra-cover.png"},
        {"appCover", "app-cover.png"},
        {"bigBox", "bigbox.png"},
    };
    map<string, string> table;
    const string evoimg = Env::getWorkingPath() + sep + "evoimg" + sep;
    for (const auto &icon : evoimgIcons)
        table[icon.name] = evoimg + icon.file;
    table["players"] = theme.launcher().metaPanel; // resolved: the theme's own, else the default's
    return table;
}

map<string, abgui::IconSpec> ThemeAssets::iconSpecs(const Theme &theme) {
    map<string, abgui::IconSpec> specs;
    for (const ableem::ThemeIcon &icon :
         ableem::resolveThemeIcons(theme.loadedPath(), Theme::defaultsPath(), builtInIcons(theme))) {
        abgui::IconSpec spec;
        spec.file = icon.image;
        spec.file2x = icon.image2x;
        specs[icon.name] = spec;
    }
    return specs;
}

bool ThemeAssets::iconHalo(const Theme &theme) {
    return ableem::resolveThemeIconHalo(theme.loadedPath(), Theme::defaultsPath());
}

//*******************************
// ThemeAssets::fixedFonts
//*******************************
Fonts &ThemeAssets::fixedFonts() {
    static Fonts fonts; // closed by unload() before the renderer goes
    return fonts;
}

//*******************************
// ThemeAssets::classicFontAtSize
//*******************************
ableem::Font ThemeAssets::classicFontAtSize(int size) {
    if (classicFontFile_.empty())
        return themeFont;
    return Fonts::openNewSharedCachedFont(classicFontFile_, size, renderer_);
}
