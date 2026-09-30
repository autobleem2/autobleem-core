// lib_ableem - engine: a UI theme's description, theme.json. What a theme consists of - its music, the
// classic UI's colours, rects and button markers, the launcher's images and fonts, the UI sounds - as a
// typed struct with JSON load/save. The library knows nothing about which UI reads which field; it only
// keeps the file honest (a bad file is reported, never thrown) and merges a partial theme over a base one.
#pragma once

#include <string>
#include <vector>

namespace ableem {

//******************
// Opt
//******************
// A scalar a theme may leave out. Reads convert to T; an assignment marks it set. mergeOver() takes the
// base theme's value for anything not set.
template <class T> struct Opt {
    T value{};
    bool set = false;

    Opt() = default;
    Opt(const T &v) : value(v), set(true) {} // NOLINT: implicit on purpose, `spec.menuLines = 13`
    Opt &operator=(const T &v) {
        value = v;
        set = true;
        return *this;
    }
    operator const T &() const { return value; } // NOLINT: implicit on purpose, `int n = spec.menuLines`
};

//******************
// ThemeColor
//******************
// "#rrggbb" in theme.json. fromRgb() reads the old theme.ini "r,g,b" form.
struct ThemeColor {
    int r = 0, g = 0, b = 0;
    bool set = false;

    ThemeColor() = default;
    ThemeColor(int r_, int g_, int b_) : r(r_), g(g_), b(b_), set(true) {}

    static bool parseHex(const std::string &hex, ThemeColor &out); // "#rrggbb", case-insensitive
    static bool parseRgb(const std::string &rgb, ThemeColor &out); // "255,255,255"
    std::string toHex() const;
};

//******************
// ThemeColorRole
//******************
// A style role's colour in launcher.colors (UIREV-29): "#rrggbb", or the name of another colour in the same
// block ("secondary", "text", "row", ...) that it takes its value from, like a CSS variable - so the default
// theme can say "rows are the secondary colour", and a theme that sets only `secondary` moves every row with
// it. Neither set means the role's own fallback (PanelStyle::fromTheme resolves them).
struct ThemeColorRole {
    ThemeColor color;
    std::string ref;

    bool isSet() const { return color.set || !ref.empty(); }
};

//******************
// ThemePoint / ThemeRect
//******************
struct ThemePoint {
    int x = 0, y = 0;
    bool set = false;
};

struct ThemeRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool set = false;
};

//******************
// ThemeMusic
//******************
// "music": null is a theme that plays no music at all (theme.ini's Loop=-1): set with none.
struct ThemeMusic {
    std::string file;
    bool loop = true;
    bool none = false;
    bool set = false;
};

//******************
// ThemeLogo / ThemeFont / ThemeFill / ThemePanel
//******************
// Each part of these is optional on its own (the file, the rect, the colour, the alpha), so a theme that
// gives only a rect still inherits the default's fill. `set` is the rect's flag.
struct ThemeLogo { // the classic logo and where it is drawn
    std::string file;
    int x = 0, y = 0, w = 0, h = 0;
    bool set = false;
};

struct ThemeFont { // a ttf and a point size
    std::string file;
    Opt<int> size;
};

struct ThemeFill { // a translucent fill: colour + alpha
    ThemeColor color;
    Opt<int> alpha;
};

struct ThemePanel { // a filled rect
    int x = 0, y = 0, w = 0, h = 0;
    bool set = false;
    ThemeColor color;
    Opt<int> alpha;
};

struct ThemeStatusBar : ThemePanel { // the classic status line: its bar, and the y the text is drawn at
    Opt<int> textY;
};

//******************
// ClassicTheme
//******************
// The classic UI (menus, splash, dialogs, keyboard). Every file is relative to the theme dir until
// resolveFiles() makes it absolute.
struct ClassicTheme {
    std::string background;
    ThemeLogo logo;
    ThemeFont font;
    Opt<int> menuLines;   // visible rows in the list menus
    ThemePanel menuPanel; // the translucent panel behind a menu
    ThemeStatusBar statusBar;
    ThemeColor textColor;
    Opt<bool> textShadow;     // false: no dark halo under the classic UI's text (unset counts as true)
    ThemeFill keyboardKey;    // on-screen keyboard key
    ThemeColor labelColor;    // label box fill
    ThemePoint freeSpaceText; // where "Free space: ..." is drawn
    ThemePoint editorCover;   // the game editor's cover art

    // the |@X| marker textures, by marker name
    struct Buttons {
        std::string cross, circle, square, triangle, start, select, l1, r1, l2, r2;
        std::string check, uncheck, esc, enter, tab;
    } buttons;
};

//******************
// LauncherTheme
//******************
// The EvolutionUI launcher.
struct LauncherTheme {
    std::string background;
    std::string footer;
    std::string playButton;
    std::string playText;
    std::string settingsPanel;
    std::string metaPanel;
    Opt<bool> metaPanelSlides; // false: the meta panel stays put when the menu opens (a static layout)
    Opt<bool> textShadow;      // false: no dark halo under the launcher's text (unset counts as true)
    ThemeRect snapPanel;       // where the selected game's screenshot is drawn (aspect-fit); unset: not drawn
    std::string arrow;

    struct Hints {
        std::string cross, circle, triangle;
    } hints; // the button hints in the footer
    // the frame the footer's hint row is laid out in (centred, at the largest font that fits); unset means
    // the pill most themes paint at the bottom right, x 560..1240, y 624..696
    ThemeRect hintBar;
    struct MenuIcons {
        std::string settings, guide, memcard, resume;
        // where the save state's picture is pasted on the resume icon, in the icon's own pixels (the icon
        // is 118x118, drawn scaled); unset means the launcher's default of (25, 33) 68x52
        ThemeRect resumePicture;
        // where the resume-slot picker writes "Slot n" on its copy of the icon, in the icon's pixels (the
        // text is left-aligned there); unset means the original spot, (22, 18), above the original window
        ThemePoint resumeSlotLabel;
    } menuIcons; // the launcher's menu row
    struct MemcardManager {
        std::string grid, pencil;
    } memcardManager;
    struct Fonts {
        std::string medium, bold;
    } fonts;
    // hint: the launcher's own footer labels (evoui_launcher_screen.cpp, next to the button icons) and its
    // low-battery pad fill; unset means they take the secondary colour. PanelStyle's classic-screen footer
    // (Options, Game Manager, the system menu, ...) draws its hint labels in the theme's text colour instead
    // (UIREV-7) - hint stayed too close to the panel's own dim background there.
    // selection: the resume-slot picker's colour for the selected slot (a halo around its tile, the others
    // dimmed); unset means the original red tint of the slot's tile, invisible on a tile that is not white
    // The style roles (UIREV-29, appended after the four above - SDK layout): the one place every menu, list
    // and dialog takes its colours from (PanelStyle). row: an unselected row's text; rowSelected: the
    // selected row's text and value; heading: the text on a heading band; value: an unselected row's
    // right-hand value; description: a row's second line, a subtitle, the description strip, the footer's
    // counter; footer: the footer's hint labels; selectionBand: the selected row's band and bar; edge: the
    // panel's edge, its rules, the heading band. Fallbacks: row, heading, description, edge -> secondary;
    // rowSelected, footer, selectionBand -> text; value -> row. docs/theme-format.md has the table.
    struct Colors {
        ThemeColor text, secondary, hint, selection;
        ThemeColorRole row, rowSelected, heading, value, description, footer, selectionBand, edge;
    } colors;
};

//******************
// ThemeSounds
//******************
struct ThemeSounds {
    std::string cursor, cancel, homeUp, homeDown, resume;
};

//******************
// ThemeSpec
//******************
struct ThemeSpec {
    enum { currentFormat = 1 };

    int format = currentFormat;
    ThemeMusic music;
    ClassicTheme classic;
    LauncherTheme launcher;
    ThemeSounds sounds;

    // reads theme.json. False (and *this untouched) when the file is missing or not valid JSON; a key that
    // is missing or of the wrong type is simply not set.
    bool load(const std::string &path);
    // writes theme.json; only what is set is written, so a partial theme stays partial
    bool save(const std::string &path) const;

    // every value this theme does not set is taken from `base` - a partial theme over the default one
    void mergeOver(const ThemeSpec &base);

    // Turns every relative file name into an absolute path: `dir`'s file when it exists, otherwise
    // `fallback`'s file for the same role under `fallbackDir`, otherwise "". After this the spec is what
    // a UI loads from.
    void resolveFiles(const std::string &dir, const ThemeSpec &fallback, const std::string &fallbackDir);

    // the (relative or resolved) file names the theme sets, in field order; "" entries are skipped
    std::vector<std::string> referencedFiles() const;

    // a mutable view of every file field, for the loops above. Kept public for the same reason as
    // referencedFiles(): a converter enumerates them too.
    std::vector<std::string *> fileFields();
    std::vector<const std::string *> fileFields() const;
};

//******************
// ThemeFrame (launcher.frames, ab_gui G4)
//******************
// A 9-slice frame a theme draws a panel, a selection, a key... with instead of the code-drawn box
// (autobleem-core docs/ab-gui-frames-spec.md). Kept out of ThemeSpec on purpose: ThemeSpec and LauncherTheme are
// laid out in the extensions' SDK, and frames are the theme's own - never merged over the default theme's.
struct ThemeInsets { // logical px: a number in theme.json sets all four
    int left = 0, top = 0, right = 0, bottom = 0;
};

struct ThemeFrame {
    std::string name;    // "panel", "selection", "heading", "key", "keyFunction", "keyLit", "keySelected", "field"
    std::string image;   // the 1x PNG
    std::string image2x; // the @2x PNG ("" none)
    ThemeInsets slice;   // the cut lines from the image's outer edge
    ThemeInsets bleed;   // how far the image reaches outside the box
    bool fill = true;    // false: the centre is not drawn
    std::string tint;    // a launcher.colors role the image is multiplied by; "" its own colours
};

// launcher.frames of the theme.json at `path`, by name, the file names as written. An entry with neither image, or
// not an object, is skipped; a missing or invalid file gives none. Never throws.
std::vector<ThemeFrame> readThemeFrames(const std::string &path);
// the frames of the theme in `dir` (its own theme.json only), each image an absolute path or "": `image` when it is in
// the folder; `image2x` when given and in the folder, else "<image's stem>@2x<ext>" when that is next to the image. A
// frame with neither file is dropped (logged).
std::vector<ThemeFrame> loadThemeFrames(const std::string &dir);

//******************
// High-resolution theme images (ab_gui G4f)
//******************
// Which file to load for the theme image `file` (a resolved path) at `outputScale`, and its pixels per logical pixel
// in `pixelScale` (for Texture::loadFile): above scale 1 its "<stem>@2x<ext>" when that is next to it - the same
// picture at twice the pixels, pixelScale 2 - else `file` itself, pixelScale 1. At scale 1 (and for an empty
// `file`) it is `file` and the disk is not asked. The 1x file stays what a theme must ship: its size is the image's
// logical size, and whatever measures the picture's pixels (Texture::opaqueBounds, an outline) reads the 1x file.
std::string themeImageFile(const std::string &file, float outputScale, float &pixelScale);

} // namespace ableem
