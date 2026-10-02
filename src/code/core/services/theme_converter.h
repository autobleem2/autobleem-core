//
// ThemeConverter: an old theme folder (theme.ini + a copy of the console's data tree) becomes a new one
// (theme.json + only the files the UI uses).
//
#pragma once

#include "../main.h"
#include "theme_color_deriver.h"

#include <string>
#include <vector>

//******************
// ThemeConverter
//******************
// A theme used to be theme.ini next to a full copy of /usr/sony/share/data (images/, sounds/, font/ - some
// 330 files) of which autobleem-gui reads 17 images, 2 fonts and 5 sounds by hard-coded PSC name. The
// new folder is theme.json (ThemeSpec) naming every file by its role, the launcher images renamed to
// those roles, and nothing else under images/, sounds/ and font/.
//
// convert() runs in an order that never leaves the folder unreadable: theme.json is written first (naming
// the role files), then the launcher images are renamed, then theme.ini, colors.ini and every file under
// images/, sounds/ and font/ that theme.json does not name are deleted, empty directories with them.
// Files at the theme's root that theme.json does not name (credit.txt, a readme) are left alone.
//
// Theme::load() calls it on a theme whose folder still has the old layout; tools/theme_convert does it
// for a whole themes/ directory ahead of time.
//
// The bridge (G6c2): a 1.0 theme has a background and little else, so on the 2.0 screens it fell back to default's
// pieces and code-drawn panels in default colours. convert() also writes what makes it tidy, derived once, here, never
// at run time: the colour roles of launcher.colors (ThemeColorDeriver, from the background and whatever colours the
// 1.0 files held), the dark sheet under the panels (launcher.colors.sheet) and the disabled veil, launcher.frames
// pointing at the ONE shared bridge set ("bridge:" paths, shipped in the resources), launcher.logo from the 1.0
// Logo/Lposition keys, hints.square from GR/Squere_Btn_ICN.png, and a stamp ("converter": {"stamp", "sum"}). A theme
// whose stamp is below StampVersion is re-derived once (upgrade()) - but only when the blocks the converter wrote are
// still what it wrote (the sum); a theme with no stamp, or one the user edited, is never touched.
class ThemeConverter {
public:
    // the number the converter's derivation is at: raise it, on purpose, to have stamped themes derived again
    static constexpr int StampVersion = 1;

    // a folder with no theme.json that has a theme.ini, or launcher images under the old names
    static bool needsConversion(const std::string &themeDir);

    // a folder that is a theme in either layout: theme.json, or needsConversion()
    static bool isThemeFolder(const std::string &themeDir);

    // converts in place. False when theme.json could not be written - the folder is then untouched.
    static bool convert(const std::string &themeDir);

    // the theme.json contents an old folder gives, without touching it: an existing theme.json, then
    // theme.ini and colors.ini over it, then whichever launcher images, fonts and sounds are on disk. Files
    // are named by their new role names whether or not the rename has happened (convert() does both).
    static ThemeSpec specFor(const std::string &themeDir);

    // the old theme.ini's keys as a ThemeSpec (the classic UI's values and the music); nothing about files
    // on disk. A group of keys (a rect, a font) is taken only when every key of the group is there.
    static ThemeSpec specFromIni(const IniFile &ini);

    // what a picture must be to stand for a role (G6c2). A file that cannot be decoded cannot be judged: it is used.
    enum class Require {
        Any,
        Square,  // a button glyph: not more than twice as tall as wide, or the other way round (1.0 shipped 30x80 and
                 // 30x200 strips - sprite sheets the footer would draw whole, glyphs all over the place)
        Visible, // a picture with something in it (the stock Play button is 200x68 of nothing, which 2.0 draws as a
                 // black box)
    };

    // one launcher image role: the old names tried in order, and the new name
    struct Role {
        std::vector<std::string> oldNames;  // relative to <theme>/images, first one found wins
        std::string newName;                // relative to <theme>/images
        std::string *(*field)(ThemeSpec &); // the ThemeSpec field the role names
        Require require = Require::Any;     // a picture that fails it is left out: the default theme's is used
    };
    static const std::vector<Role> &launcherRoles();

    // the colour roles of the theme as convert() derives them: the picture is spec.classic.background (read from
    // `themeDir`), the 1.0 colours are the spec's (launcher.colors text/secondary, else classic.textColor; the menu
    // panel's colour as Main_bg). A picture that cannot be read gives the fixed neutral sheet (a note says so).
    static ThemeColorRoles rolesFor(const std::string &themeDir, const ThemeSpec &spec);

    // a converted theme.json whose stamp is there and below `stamp` (the tests pass a later one than StampVersion)
    static bool needsUpgrade(const std::string &themeDir, int stamp = StampVersion);

    // derives the roles again from the theme.json (its own text/secondary, menu panel colour and background) and
    // writes them with the sheet, the veil, the frames and the new stamp over the old ones. False, and nothing
    // written, when the theme needs no upgrade or the blocks the converter wrote were edited since. The new stamp is
    // `stamp`.
    static bool upgrade(const std::string &themeDir, int stamp = StampVersion);
};
