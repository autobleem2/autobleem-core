//
// ThemeAssets: the current theme's textures and fonts, loaded once per theme change.
//
#pragma once

#include "gui_font.h"
#include "../core/services/config.h"
#include "../core/services/theme.h"

#include <ab_gui/icon.h>
#include <ableem/ableem.h>

#include <map>
#include <string>

//********************
// ThemeAssets
//********************
// Was the asset half of Gui: the background, logo and jewel-case textures, the button-marker textures the
// text renderer draws for "|@X|", the classic screens' font, and the two font sets (the theme's
// and the console's own). load() reads them for whichever theme config.ini names, falling back to
// themes/default for anything missing, and is what an Options-menu theme change calls. Screens reach it as
// gui->assets().
class ThemeAssets {
public:
    ThemeAssets(ableem::Renderer &renderer, Theme &theme, Config &config);

    // (re)loads theme.ini and everything below for the theme it names; the previous textures are released first
    void load();
    // drops every texture and font: they belong to the renderer and must be gone before Gui::releaseDisplay()
    // destroys it. load() brings them back once the display is acquired again.
    void unload();

    // a theme image: its "<stem>@2x<ext>" when the output scale is above 1 and one is next to `file`, loaded with
    // pixel scale 2 so it draws in the 1x one's logical size (ableem::themeImageFile) - else `file` as it always was
    static ableem::Texture loadImage(ableem::Renderer &renderer, const std::string &file);

    // The launcher's icons (launcher.icons, ab_gui G5a - docs/theme-format.md): each name the theme's own entry, else
    // the default theme's, else the built-in file (builtInIcons()), with its @2x file when there is one - what Gui's
    // IconSet is filled with, and where the d-pad arrows below are loaded from. Statics: no layout change (AB_SDK_ABI)
    static std::map<std::string, abgui::IconSpec> iconSpecs(const Theme &theme);
    // whether the icons get their dark halo: the theme's launcher.iconHalo, else the default's, else true
    static bool iconHalo(const Theme &theme);
    // the built-in table: an icon's name -> its file, `evoimg/` in the resources (the meta row's badges, the d-pad
    // arrows, the set picker's tabs, the missing-art covers, the big box's edge) - and `players`, which has no built-in
    // file: the theme's launcher.metaPanel (merged over the default's)
    static std::map<std::string, std::string> builtInIcons(const Theme &theme);

    // the UI's font set (titles, rows, footers, the menus, the extensions' screens): the launcher's pair (the
    // theme's launcher.fonts, else Open Sans Medium/Bold) - or, with "Use Default Font" off, the user's font for
    // both (2026-09-29, the owner)
    Fonts themeFonts;
    // the launcher's pair whatever Options say: the parts with a fixed look draw with it - About and its game,
    // the launcher's game details, game menu and hints. A static on purpose: a new member here would move
    // Gui's layout, which the extensions are built against (AB_SDK_ABI)
    static Fonts &fixedFonts();
    // the classic screens' font - the name is historic: the theme's launcher medium (Open Sans on a theme that sets
    // none; or the user's own font, or the CJK one) at Fonts::ClassicFontSize; a theme's classic.font is not read
    // (2026-09-29; UIREV-31: one font with the launcher's)
    ableem::Font themeFont;
    // the classic font (the file themeFont was opened from - default, user or CJK) at another size, for a screen
    // whose rows will not fit at the usual size
    ableem::Font classicFontAtSize(int size);

    ableem::Rect backgroundRect;
    ableem::Rect logoRect;

    ableem::Texture backgroundImg;
    ableem::Texture logo;
    ableem::Texture cdJewel;
    // the edge of a printed cardboard box, a 9-slice (evoimg/bigbox.png, see tools/make_bigbox_frame.py):
    // what a RetroArch game's or an App's cover is composed into, where a PS1 game gets the jewel case
    ableem::Texture bigBoxFrame;
    std::map<std::string, ableem::Texture> buttonTextureMap; // "X", "O", "Start", "Check", ... -> its texture
    // the launcher's footer hints (theme.json launcher.hints), what PanelStyle draws in a panel's footer
    ableem::Texture hintCross, hintCircle, hintTriangle;
    // the d-pad hint chips: the icon table's dpadUp/dpadDown/dpadLeft/dpadRight (iconSpecs - the theme's
    // launcher.icons, else the default's, else the launcher's evoimg/dpad_*.png, tools/make_evoimg_icons.py; ab_gui
    // G5a). What PanelStyle::faceIcon draws for "|@Up|"/"|@Down|"/"|@Left|"/"|@Right|"
    ableem::Texture dpadUp, dpadDown, dpadLeft, dpadRight;
    // the transparent margin to the right of the check switch's art (the larger of on/off), measured
    // from the theme's files: an option row's value text lines up with the switch's visible edge
    int checkIconRightMargin = 0;

private:
    // the theme's own file for `texname`, or the default theme's when it has none
    ableem::Texture loadThemeTexture(const std::string &themePath, const std::string &defaultPath,
                                     const std::string &texname);

    ableem::Renderer &renderer_;
    Theme &theme_;
    Config &config_;
    std::string classicFontFile_;

public:
    // each arrow's dark outline (PanelStyle::outlineOf, UIREV-2), made once here so a white arrow still
    // reads on a light theme's hint bar (aergb, autobleem, default, evolution). Last in the class on purpose:
    // an extension built before them keeps every other member's offset (new members go at the end).
    ableem::Texture dpadUpOutline, dpadDownOutline, dpadLeftOutline, dpadRightOutline;
};
