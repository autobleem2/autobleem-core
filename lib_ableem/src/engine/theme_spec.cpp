#include "ableem/engine/theme_spec.h"
#include "ableem/engine/filesystem.h"
#include "ableem/engine/strings.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <json.h>
#include <fifo_map.h>
#include "ableem/engine/log.h"

using namespace std;
using namespace nlohmann;

namespace ableem {

namespace {

// A workaround to use fifo_map as the json map so keys keep their insertion order; the 'less' compare is ignored
template <class K, class V, class dummy_compare, class A>
using fifo_map_workaround = fifo_map<K, V, fifo_map_compare<K>, A>;
using ordered_json = basic_json<fifo_map_workaround>;

//*******************************
// reading: each helper sets `out` only when the key is there with the right type
//*******************************
const json *child(const json &j, const char *key) {
    if (!j.is_object())
        return nullptr;
    auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}

void readStr(const json &j, const char *key, string &out) {
    const json *v = child(j, key);
    if (v && v->is_string())
        out = v->get<string>();
}

void readInt(const json &j, const char *key, int &out, bool &set) {
    const json *v = child(j, key);
    if (v && v->is_number_integer()) {
        out = v->get<int>();
        set = true;
    }
}

void readInt(const json &j, const char *key, int &out) {
    bool set = false;
    readInt(j, key, out, set);
}

void readOptInt(const json &j, const char *key, Opt<int> &out) {
    readInt(j, key, out.value, out.set);
}

void readOptBool(const json &j, const char *key, Opt<bool> &out) {
    const json *v = child(j, key);
    if (v && v->is_boolean())
        out = v->get<bool>();
}

void readColor(const json &j, const char *key, ThemeColor &out) {
    const json *v = child(j, key);
    if (v && v->is_string())
        ThemeColor::parseHex(v->get<string>(), out);
}

// a style role: "#rrggbb", or a bare name (letters only) of another colour in the block
void readRole(const json &j, const char *key, ThemeColorRole &out) {
    const json *v = child(j, key);
    if (!v || !v->is_string())
        return;
    const string s = v->get<string>();
    if (ThemeColor::parseHex(s, out.color))
        return;
    if (s.empty())
        return;
    for (char c : s)
        if (!isalpha(static_cast<unsigned char>(c)))
            return;
    out.ref = s;
}

// x/y/w/h (set when x is there), colour and alpha, each optional on its own
void readRect(const json &j, int &x, int &y, int &w, int &h, bool &set) {
    readInt(j, "x", x, set);
    readInt(j, "y", y);
    readInt(j, "w", w);
    readInt(j, "h", h);
}

void readPanel(const json &j, ThemePanel &out) {
    readRect(j, out.x, out.y, out.w, out.h, out.set);
    readColor(j, "color", out.color);
    readOptInt(j, "alpha", out.alpha);
}

//*******************************
// writing
//*******************************
ordered_json rectJson(int x, int y, int w, int h) {
    ordered_json o = ordered_json::object();
    o["x"] = x;
    o["y"] = y;
    o["w"] = w;
    o["h"] = h;
    return o;
}

void putStr(ordered_json &o, const char *key, const string &s) {
    if (!s.empty())
        o[key] = s;
}

void putColor(ordered_json &o, const char *key, const ThemeColor &c) {
    if (c.set)
        o[key] = c.toHex();
}

void putRole(ordered_json &o, const char *key, const ThemeColorRole &r) {
    if (r.color.set)
        o[key] = r.color.toHex();
    else if (!r.ref.empty())
        o[key] = r.ref;
}

void putOptInt(ordered_json &o, const char *key, const Opt<int> &v) {
    if (v.set)
        o[key] = v.value;
}

ordered_json panelJson(const ThemePanel &p) {
    ordered_json o = ordered_json::object();
    if (p.set)
        o = rectJson(p.x, p.y, p.w, p.h);
    putColor(o, "color", p.color);
    putOptInt(o, "alpha", p.alpha);
    return o;
}

void putPoint(ordered_json &o, const char *key, const ThemePoint &p) {
    if (!p.set)
        return;
    ordered_json pt = ordered_json::object();
    pt["x"] = p.x;
    pt["y"] = p.y;
    o[key] = pt;
}

// an object is written only if something in it is set, so an untouched section stays out of the file
void putObject(ordered_json &o, const char *key, const ordered_json &obj) {
    if (!obj.empty())
        o[key] = obj;
}

//*******************************
// merging
//*******************************
void mergeStr(string &mine, const string &base) {
    if (mine.empty())
        mine = base;
}

void mergeColor(ThemeColor &mine, const ThemeColor &base) {
    if (!mine.set)
        mine = base;
}

void mergeRole(ThemeColorRole &mine, const ThemeColorRole &base) {
    if (!mine.isSet())
        mine = base;
}

template <class T>
void mergeSet(T &mine, const T &base) { // anything with a `set` member and nothing else optional in it
    if (!mine.set)
        mine = base;
}

void mergePanel(ThemePanel &mine, const ThemePanel &base) {
    if (!mine.set) {
        mine.x = base.x;
        mine.y = base.y;
        mine.w = base.w;
        mine.h = base.h;
        mine.set = base.set;
    }
    mergeColor(mine.color, base.color);
    mergeSet(mine.alpha, base.alpha);
}

} // namespace

//*******************************
// ThemeColor::parseHex / parseRgb / toHex
//*******************************
bool ThemeColor::parseHex(const string &hex, ThemeColor &out) {
    if (hex.size() != 7 || hex[0] != '#')
        return false;
    for (size_t i = 1; i < hex.size(); i++)
        if (!isxdigit(static_cast<unsigned char>(hex[i])))
            return false;
    unsigned int rgb = static_cast<unsigned int>(strtoul(hex.c_str() + 1, nullptr, 16));
    out = ThemeColor((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
    return true;
}

bool ThemeColor::parseRgb(const string &rgb, ThemeColor &out) {
    int r, g, b;
    char extra;
    if (sscanf(rgb.c_str(), " %d , %d , %d %c", &r, &g, &b, &extra) != 3)
        return false;
    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255)
        return false;
    out = ThemeColor(r, g, b);
    return true;
}

string ThemeColor::toHex() const {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02x%02x%02x", r & 0xff, g & 0xff, b & 0xff);
    return buf;
}

//*******************************
// ThemeSpec::supports4x3
//*******************************
bool ThemeSpec::supports4x3(const string &path) {
    ifstream in(path, ifstream::binary);
    if (!in.is_open())
        return false;
    json j;
    try {
        in >> j;
    } catch (const json::exception &) {
        return false;
    }
    const json *layout = child(j, "layout4x3");
    return layout && layout->is_object();
}

//*******************************
// ThemeSpec::load
//*******************************
bool ThemeSpec::load(const string &path) {
    ifstream in(path, ifstream::binary);
    if (!in.is_open()) {
        PLOG_WARNING << "Could not open theme file: " << path;
        return false;
    }

    // a hand-edited theme must not take the whole UI down (nlohmann throws on bad input)
    json j;
    try {
        in >> j;
    } catch (const json::exception &e) {
        PLOG_INFO << "Theme " << path << " is not valid JSON: " << e.what();
        return false;
    }
    if (!j.is_object()) {
        PLOG_INFO << "Theme " << path << " is not a JSON object";
        return false;
    }

    readInt(j, "format", format);

    if (const json *m = child(j, "music")) {
        music.set = true;
        if (m->is_null()) {
            music.none = true;
        } else {
            readStr(*m, "file", music.file);
            const json *loop = child(*m, "loop");
            if (loop && loop->is_boolean())
                music.loop = loop->get<bool>();
            const json *languages = child(*m, "languages");
            if (languages && languages->is_object()) {
                for (auto it = languages->begin(); it != languages->end(); ++it)
                    if (it.value().is_string() && !it.value().get<string>().empty())
                        music.languages[it.key()] = it.value().get<string>();
            }
        }
    }

    if (const json *c = child(j, "classic")) {
        readStr(*c, "background", classic.background);
        if (const json *l = child(*c, "logo")) {
            readStr(*l, "file", classic.logo.file);
            readRect(*l, classic.logo.x, classic.logo.y, classic.logo.w, classic.logo.h, classic.logo.set);
        }
        if (const json *f = child(*c, "font")) {
            readStr(*f, "file", classic.font.file);
            readOptInt(*f, "size", classic.font.size);
        }
        readOptInt(*c, "menuLines", classic.menuLines);
        if (const json *p = child(*c, "menuPanel"))
            readPanel(*p, classic.menuPanel);
        if (const json *s = child(*c, "statusBar")) {
            readPanel(*s, classic.statusBar);
            readOptInt(*s, "textY", classic.statusBar.textY);
        }
        readColor(*c, "textColor", classic.textColor);
        readOptBool(*c, "textShadow", classic.textShadow);
        if (const json *k = child(*c, "keyboardKey")) {
            readColor(*k, "color", classic.keyboardKey.color);
            readOptInt(*k, "alpha", classic.keyboardKey.alpha);
        }
        readColor(*c, "labelColor", classic.labelColor);
        if (const json *p = child(*c, "freeSpaceText")) {
            readInt(*p, "x", classic.freeSpaceText.x);
            readInt(*p, "y", classic.freeSpaceText.y);
            classic.freeSpaceText.set = true;
        }
        if (const json *p = child(*c, "editorCover")) {
            readInt(*p, "x", classic.editorCover.x);
            readInt(*p, "y", classic.editorCover.y);
            classic.editorCover.set = true;
        }
        if (const json *b = child(*c, "buttons")) {
            auto &bt = classic.buttons;
            readStr(*b, "cross", bt.cross);
            readStr(*b, "circle", bt.circle);
            readStr(*b, "square", bt.square);
            readStr(*b, "triangle", bt.triangle);
            readStr(*b, "start", bt.start);
            readStr(*b, "select", bt.select);
            readStr(*b, "l1", bt.l1);
            readStr(*b, "r1", bt.r1);
            readStr(*b, "l2", bt.l2);
            readStr(*b, "r2", bt.r2);
            readStr(*b, "check", bt.check);
            readStr(*b, "uncheck", bt.uncheck);
            readStr(*b, "esc", bt.esc);
            readStr(*b, "enter", bt.enter);
            readStr(*b, "tab", bt.tab);
        }
    }

    if (const json *l = child(j, "launcher")) {
        readStr(*l, "background", launcher.background);
        readStr(*l, "footer", launcher.footer);
        readStr(*l, "playButton", launcher.playButton);
        readStr(*l, "playText", launcher.playText);
        readStr(*l, "settingsPanel", launcher.settingsPanel);
        readStr(*l, "metaPanel", launcher.metaPanel);
        readOptBool(*l, "metaPanelSlides", launcher.metaPanelSlides);
        readOptBool(*l, "textShadow", launcher.textShadow);
        if (const json *p = child(*l, "snapPanel"))
            readRect(*p, launcher.snapPanel.x, launcher.snapPanel.y, launcher.snapPanel.w, launcher.snapPanel.h,
                     launcher.snapPanel.set);
        readStr(*l, "arrow", launcher.arrow);
        if (const json *p = child(*l, "hintBar"))
            readRect(*p, launcher.hintBar.x, launcher.hintBar.y, launcher.hintBar.w, launcher.hintBar.h,
                     launcher.hintBar.set);
        if (const json *h = child(*l, "hints")) {
            readStr(*h, "cross", launcher.hints.cross);
            readStr(*h, "circle", launcher.hints.circle);
            readStr(*h, "triangle", launcher.hints.triangle);
            readStr(*h, "square", launcher.hints.square);
        }
        if (const json *m = child(*l, "menuIcons")) {
            readStr(*m, "settings", launcher.menuIcons.settings);
            readStr(*m, "guide", launcher.menuIcons.guide);
            readStr(*m, "memcard", launcher.menuIcons.memcard);
            readStr(*m, "resume", launcher.menuIcons.resume);
            if (const json *p = child(*m, "resumePicture"))
                readRect(*p, launcher.menuIcons.resumePicture.x, launcher.menuIcons.resumePicture.y,
                         launcher.menuIcons.resumePicture.w, launcher.menuIcons.resumePicture.h,
                         launcher.menuIcons.resumePicture.set);
            if (const json *p = child(*m, "resumeSlotLabel")) {
                readInt(*p, "x", launcher.menuIcons.resumeSlotLabel.x);
                readInt(*p, "y", launcher.menuIcons.resumeSlotLabel.y);
                launcher.menuIcons.resumeSlotLabel.set = true;
            }
        }
        if (const json *m = child(*l, "memcardManager")) {
            readStr(*m, "grid", launcher.memcardManager.grid);
            readStr(*m, "pencil", launcher.memcardManager.pencil);
        }
        if (const json *f = child(*l, "fonts")) {
            readStr(*f, "medium", launcher.fonts.medium);
            readStr(*f, "bold", launcher.fonts.bold);
        }
        if (const json *c = child(*l, "colors")) {
            readColor(*c, "text", launcher.colors.text);
            readColor(*c, "secondary", launcher.colors.secondary);
            readColor(*c, "hint", launcher.colors.hint);
            readColor(*c, "selection", launcher.colors.selection);
            auto &cl = launcher.colors;
            readRole(*c, "row", cl.row);
            readRole(*c, "rowSelected", cl.rowSelected);
            readRole(*c, "heading", cl.heading);
            readRole(*c, "value", cl.value);
            readRole(*c, "description", cl.description);
            readRole(*c, "footer", cl.footer);
            readRole(*c, "selectionBand", cl.selectionBand);
            readRole(*c, "edge", cl.edge);
        }
    }

    if (const json *s = child(j, "sounds")) {
        readStr(*s, "cursor", sounds.cursor);
        readStr(*s, "cancel", sounds.cancel);
        readStr(*s, "homeUp", sounds.homeUp);
        readStr(*s, "homeDown", sounds.homeDown);
        readStr(*s, "resume", sounds.resume);
    }
    return true;
}

//*******************************
// ThemeSpec::save
//*******************************
bool ThemeSpec::save(const string &path) const {
    ordered_json j = ordered_json::object();
    j["format"] = format;

    if (music.set) {
        if (music.none) {
            j["music"] = nullptr;
        } else {
            ordered_json m = ordered_json::object();
            m["file"] = music.file;
            m["loop"] = music.loop;
            if (!music.languages.empty()) {
                ordered_json l = ordered_json::object();
                for (const auto &entry : music.languages)
                    l[entry.first] = entry.second;
                m["languages"] = l;
            }
            j["music"] = m;
        }
    }

    {
        ordered_json c = ordered_json::object();
        putStr(c, "background", classic.background);
        {
            ordered_json l = ordered_json::object();
            putStr(l, "file", classic.logo.file);
            if (classic.logo.set) {
                l["x"] = classic.logo.x;
                l["y"] = classic.logo.y;
                l["w"] = classic.logo.w;
                l["h"] = classic.logo.h;
            }
            putObject(c, "logo", l);
        }
        {
            ordered_json f = ordered_json::object();
            putStr(f, "file", classic.font.file);
            putOptInt(f, "size", classic.font.size);
            putObject(c, "font", f);
        }
        putOptInt(c, "menuLines", classic.menuLines);
        putObject(c, "menuPanel", panelJson(classic.menuPanel));
        {
            ordered_json s = panelJson(classic.statusBar);
            putOptInt(s, "textY", classic.statusBar.textY);
            putObject(c, "statusBar", s);
        }
        putColor(c, "textColor", classic.textColor);
        if (classic.textShadow.set)
            c["textShadow"] = classic.textShadow.value;
        {
            ordered_json k = ordered_json::object();
            putColor(k, "color", classic.keyboardKey.color);
            putOptInt(k, "alpha", classic.keyboardKey.alpha);
            putObject(c, "keyboardKey", k);
        }
        putColor(c, "labelColor", classic.labelColor);
        putPoint(c, "freeSpaceText", classic.freeSpaceText);
        putPoint(c, "editorCover", classic.editorCover);
        {
            const auto &bt = classic.buttons;
            ordered_json b = ordered_json::object();
            putStr(b, "cross", bt.cross);
            putStr(b, "circle", bt.circle);
            putStr(b, "square", bt.square);
            putStr(b, "triangle", bt.triangle);
            putStr(b, "start", bt.start);
            putStr(b, "select", bt.select);
            putStr(b, "l1", bt.l1);
            putStr(b, "r1", bt.r1);
            putStr(b, "l2", bt.l2);
            putStr(b, "r2", bt.r2);
            putStr(b, "check", bt.check);
            putStr(b, "uncheck", bt.uncheck);
            putStr(b, "esc", bt.esc);
            putStr(b, "enter", bt.enter);
            putStr(b, "tab", bt.tab);
            putObject(c, "buttons", b);
        }
        putObject(j, "classic", c);
    }

    {
        ordered_json l = ordered_json::object();
        putStr(l, "background", launcher.background);
        putStr(l, "footer", launcher.footer);
        putStr(l, "playButton", launcher.playButton);
        putStr(l, "playText", launcher.playText);
        putStr(l, "settingsPanel", launcher.settingsPanel);
        putStr(l, "metaPanel", launcher.metaPanel);
        if (launcher.metaPanelSlides.set)
            l["metaPanelSlides"] = launcher.metaPanelSlides.value;
        if (launcher.textShadow.set)
            l["textShadow"] = launcher.textShadow.value;
        if (launcher.snapPanel.set) {
            ordered_json p = ordered_json::object();
            p["x"] = launcher.snapPanel.x;
            p["y"] = launcher.snapPanel.y;
            p["w"] = launcher.snapPanel.w;
            p["h"] = launcher.snapPanel.h;
            l["snapPanel"] = p;
        }
        putStr(l, "arrow", launcher.arrow);
        if (launcher.hintBar.set) {
            ordered_json p = ordered_json::object();
            p["x"] = launcher.hintBar.x;
            p["y"] = launcher.hintBar.y;
            p["w"] = launcher.hintBar.w;
            p["h"] = launcher.hintBar.h;
            l["hintBar"] = p;
        }
        {
            ordered_json h = ordered_json::object();
            putStr(h, "cross", launcher.hints.cross);
            putStr(h, "circle", launcher.hints.circle);
            putStr(h, "triangle", launcher.hints.triangle);
            putStr(h, "square", launcher.hints.square);
            putObject(l, "hints", h);
        }
        {
            ordered_json m = ordered_json::object();
            putStr(m, "settings", launcher.menuIcons.settings);
            putStr(m, "guide", launcher.menuIcons.guide);
            putStr(m, "memcard", launcher.menuIcons.memcard);
            putStr(m, "resume", launcher.menuIcons.resume);
            if (launcher.menuIcons.resumePicture.set) {
                ordered_json p = ordered_json::object();
                p["x"] = launcher.menuIcons.resumePicture.x;
                p["y"] = launcher.menuIcons.resumePicture.y;
                p["w"] = launcher.menuIcons.resumePicture.w;
                p["h"] = launcher.menuIcons.resumePicture.h;
                m["resumePicture"] = p;
            }
            putPoint(m, "resumeSlotLabel", launcher.menuIcons.resumeSlotLabel);
            putObject(l, "menuIcons", m);
        }
        {
            ordered_json m = ordered_json::object();
            putStr(m, "grid", launcher.memcardManager.grid);
            putStr(m, "pencil", launcher.memcardManager.pencil);
            putObject(l, "memcardManager", m);
        }
        {
            ordered_json f = ordered_json::object();
            putStr(f, "medium", launcher.fonts.medium);
            putStr(f, "bold", launcher.fonts.bold);
            putObject(l, "fonts", f);
        }
        {
            ordered_json c = ordered_json::object();
            putColor(c, "text", launcher.colors.text);
            putColor(c, "secondary", launcher.colors.secondary);
            putColor(c, "hint", launcher.colors.hint);
            putColor(c, "selection", launcher.colors.selection);
            const auto &cl = launcher.colors;
            putRole(c, "row", cl.row);
            putRole(c, "rowSelected", cl.rowSelected);
            putRole(c, "heading", cl.heading);
            putRole(c, "value", cl.value);
            putRole(c, "description", cl.description);
            putRole(c, "footer", cl.footer);
            putRole(c, "selectionBand", cl.selectionBand);
            putRole(c, "edge", cl.edge);
            putObject(l, "colors", c);
        }
        putObject(j, "launcher", l);
    }

    {
        ordered_json s = ordered_json::object();
        putStr(s, "cursor", sounds.cursor);
        putStr(s, "cancel", sounds.cancel);
        putStr(s, "homeUp", sounds.homeUp);
        putStr(s, "homeDown", sounds.homeDown);
        putStr(s, "resume", sounds.resume);
        putObject(j, "sounds", s);
    }

    ofstream o(path, ofstream::binary);
    if (!DirEntry::checkWritable(o, path))
        return false;
    o << setw(2) << j << "\n";
    o.flush();
    o.close();
    return o.good();
}

//*******************************
// ThemeSpec::mergeOver
//*******************************
void ThemeSpec::mergeOver(const ThemeSpec &base) {
    mergeSet(music, base.music);

    if (!classic.logo.set) { // the rect; the file is merged with the other files below
        classic.logo.x = base.classic.logo.x;
        classic.logo.y = base.classic.logo.y;
        classic.logo.w = base.classic.logo.w;
        classic.logo.h = base.classic.logo.h;
        classic.logo.set = base.classic.logo.set;
    }
    mergeSet(classic.font.size, base.classic.font.size);
    mergeSet(classic.menuLines, base.classic.menuLines);
    mergePanel(classic.menuPanel, base.classic.menuPanel);
    mergePanel(classic.statusBar, base.classic.statusBar);
    mergeSet(classic.statusBar.textY, base.classic.statusBar.textY);
    mergeColor(classic.textColor, base.classic.textColor);
    mergeSet(classic.textShadow, base.classic.textShadow);
    mergeColor(classic.keyboardKey.color, base.classic.keyboardKey.color);
    mergeSet(classic.keyboardKey.alpha, base.classic.keyboardKey.alpha);
    mergeColor(classic.labelColor, base.classic.labelColor);
    mergeSet(classic.freeSpaceText, base.classic.freeSpaceText);
    mergeSet(classic.editorCover, base.classic.editorCover);

    mergeSet(launcher.metaPanelSlides, base.launcher.metaPanelSlides);
    mergeSet(launcher.textShadow, base.launcher.textShadow);
    mergeSet(launcher.snapPanel, base.launcher.snapPanel);
    mergeSet(launcher.hintBar, base.launcher.hintBar);
    mergeSet(launcher.menuIcons.resumePicture, base.launcher.menuIcons.resumePicture);
    mergeSet(launcher.menuIcons.resumeSlotLabel, base.launcher.menuIcons.resumeSlotLabel);
    mergeColor(launcher.colors.text, base.launcher.colors.text);
    mergeColor(launcher.colors.secondary, base.launcher.colors.secondary);
    mergeColor(launcher.colors.hint, base.launcher.colors.hint);
    mergeColor(launcher.colors.selection, base.launcher.colors.selection);
    // a role inherits the default's as it is written - a name stays a name, resolved against this theme's
    // own colours (PanelStyle::fromTheme), so a theme that sets only `secondary` moves every role naming it
    mergeRole(launcher.colors.row, base.launcher.colors.row);
    mergeRole(launcher.colors.rowSelected, base.launcher.colors.rowSelected);
    mergeRole(launcher.colors.heading, base.launcher.colors.heading);
    mergeRole(launcher.colors.value, base.launcher.colors.value);
    mergeRole(launcher.colors.description, base.launcher.colors.description);
    mergeRole(launcher.colors.footer, base.launcher.colors.footer);
    mergeRole(launcher.colors.selectionBand, base.launcher.colors.selectionBand);
    mergeRole(launcher.colors.edge, base.launcher.colors.edge);

    // every file field, in one go
    vector<string *> mine = fileFields();
    vector<const string *> theirs = base.fileFields();
    for (size_t i = 0; i < mine.size(); i++)
        mergeStr(*mine[i], *theirs[i]);
    if (music.none) {
        music.file = ""; // "music": null inherits no file
        music.languages.clear();
    }
}

//*******************************
// ThemeSpec::resolveFiles
//*******************************
void ThemeSpec::resolveFiles(const string &dir, const ThemeSpec &fallback, const string &fallbackDir) {
    vector<string *> mine = fileFields();
    vector<const string *> theirs = fallback.fileFields();
    for (size_t i = 0; i < mine.size(); i++) {
        string &file = *mine[i];
        if (!file.empty() && DirEntry::exists(dir + sep + file)) {
            file = dir + sep + file;
        } else if (!theirs[i]->empty() && DirEntry::exists(fallbackDir + sep + *theirs[i])) {
            file = fallbackDir + sep + *theirs[i];
        } else {
            file = "";
        }
    }
    // the per-language tracks: the theme's own folder only (the default theme has none to stand in); a missing
    // file drops the entry, so that language plays the theme's `file` (logged once, here, per theme load)
    for (auto it = music.languages.begin(); it != music.languages.end();) {
        if (DirEntry::exists(dir + sep + it->second)) {
            it->second = dir + sep + it->second;
            ++it;
        } else {
            PLOG_INFO << "Theme music for language " << it->first << ": " << it->second << " is missing - using "
                      << (music.file.empty() ? string("the default track") : music.file);
            it = music.languages.erase(it);
        }
    }
}

//*******************************
// ThemeSpec::referencedFiles
//*******************************
vector<string> ThemeSpec::referencedFiles() const {
    vector<string> files;
    for (const string *f : fileFields())
        if (!f->empty())
            files.push_back(*f);
    for (const auto &entry : music.languages)
        if (!entry.second.empty())
            files.push_back(entry.second);
    return files;
}

//*******************************
// ThemeSpec::fileFields
//*******************************
// The one list of file fields. Both overloads go through the mutable one so they cannot drift apart.
vector<string *> ThemeSpec::fileFields() {
    auto &b = classic.buttons;
    auto &l = launcher;
    return {
        &music.file,
        &classic.background,
        &classic.logo.file,
        &classic.font.file,
        &b.cross,
        &b.circle,
        &b.square,
        &b.triangle,
        &b.start,
        &b.select,
        &b.l1,
        &b.r1,
        &b.l2,
        &b.r2,
        &b.check,
        &b.uncheck,
        &b.esc,
        &b.enter,
        &b.tab,
        &l.background,
        &l.footer,
        &l.playButton,
        &l.playText,
        &l.settingsPanel,
        &l.metaPanel,
        &l.arrow,
        &l.hints.cross,
        &l.hints.circle,
        &l.hints.triangle,
        &l.hints.square,
        &l.menuIcons.settings,
        &l.menuIcons.guide,
        &l.menuIcons.memcard,
        &l.menuIcons.resume,
        &l.memcardManager.grid,
        &l.memcardManager.pencil,
        &l.fonts.medium,
        &l.fonts.bold,
        &sounds.cursor,
        &sounds.cancel,
        &sounds.homeUp,
        &sounds.homeDown,
        &sounds.resume,
    };
}

vector<const string *> ThemeSpec::fileFields() const {
    vector<string *> mutableFields = const_cast<ThemeSpec *>(this)->fileFields();
    return vector<const string *>(mutableFields.begin(), mutableFields.end());
}

//*******************************
// readThemeFrames / loadThemeFrames
//*******************************
namespace {

// a number (all four) or { "left", "top", "right", "bottom" } (each optional); anything else leaves `out` alone
void readInsets(const json &j, const char *key, ThemeInsets &out) {
    const json *v = child(j, key);
    if (!v)
        return;
    if (v->is_number_integer()) {
        const int n = v->get<int>();
        out.left = out.top = out.right = out.bottom = n;
        return;
    }
    readInt(*v, "left", out.left);
    readInt(*v, "top", out.top);
    readInt(*v, "right", out.right);
    readInt(*v, "bottom", out.bottom);
}

// "frames/panel.png" -> "frames/panel@2x.png" (the dot of the file name, not of a folder)
string at2x(const string &file) {
    const size_t slash = file.find_last_of("/\\");
    const size_t dot = file.find_last_of('.');
    if (dot == string::npos || (slash != string::npos && dot < slash))
        return file + "@2x";
    return file.substr(0, dot) + "@2x" + file.substr(dot);
}

} // namespace

vector<ThemeFrame> readThemeFrames(const string &path) {
    vector<ThemeFrame> frames;
    ifstream in(path, ifstream::binary);
    if (!in.is_open())
        return frames;
    json j;
    try {
        in >> j;
    } catch (const json::exception &) {
        return frames; // ThemeSpec::load has logged it
    }
    const json *launcher = child(j, "launcher");
    const json *block = launcher ? child(*launcher, "frames") : nullptr;
    if (!block || !block->is_object())
        return frames;
    for (auto it = block->begin(); it != block->end(); ++it) {
        if (!it->is_object())
            continue;
        ThemeFrame f;
        f.name = it.key();
        readStr(*it, "image", f.image);
        readStr(*it, "image2x", f.image2x);
        if (f.image.empty() && f.image2x.empty())
            continue;
        readInsets(*it, "slice", f.slice);
        readInsets(*it, "bleed", f.bleed);
        const json *fill = child(*it, "fill");
        if (fill && fill->is_boolean())
            f.fill = fill->get<bool>();
        readStr(*it, "tint", f.tint);
        frames.push_back(f);
    }
    return frames;
}

namespace {

const char *BridgeMarker = "bridge:";

// `file` as written in a theme.json -> the path it names: under `bridgeDir` for a "bridge:" file ("" when there is no
// such dir), else under the theme's `dir`; "" for no file
string framePath(const string &dir, const string &bridgeDir, const string &file) {
    if (file.empty())
        return string();
    const string marker = BridgeMarker;
    if (file.compare(0, marker.size(), marker) != 0)
        return dir + sep + file;
    return bridgeDir.empty() ? string() : bridgeDir + sep + file.substr(marker.size());
}

} // namespace

vector<ThemeFrame> loadThemeFrames(const string &dir, const string &bridgeDir) {
    vector<ThemeFrame> frames;
    for (ThemeFrame f : readThemeFrames(dir + sep + "theme.json")) {
        const string named = framePath(dir, bridgeDir, f.image);
        const string image = !named.empty() && DirEntry::exists(named) ? named : string();
        string image2x;
        if (!f.image2x.empty()) {
            const string named2x = framePath(dir, bridgeDir, f.image2x);
            if (!named2x.empty() && DirEntry::exists(named2x))
                image2x = named2x;
        } else if (!named.empty() && DirEntry::exists(at2x(named))) {
            image2x = at2x(named);
        }
        if (image.empty() && image2x.empty()) {
            PLOG_WARNING << "Theme frame '" << f.name << "': no image in " << dir << " - drawn by the code instead";
            continue;
        }
        f.image = image;
        f.image2x = image2x;
        frames.push_back(f);
    }
    return frames;
}

//*******************************
// readThemeIcons / loadThemeIcons / resolveThemeIcons
//*******************************
namespace {

// the "launcher" object of the theme.json at `path` into `out`; false when the file is missing, invalid or has none
bool readLauncher(const string &path, json &out) {
    ifstream in(path, ifstream::binary);
    if (!in.is_open())
        return false;
    json j;
    try {
        in >> j;
    } catch (const json::exception &) {
        return false; // ThemeSpec::load has logged it
    }
    const json *launcher = child(j, "launcher");
    if (!launcher || !launcher->is_object())
        return false;
    out = *launcher;
    return true;
}

// `file` (relative to `dir`) as an absolute path when it is there, else ""
string existing(const string &dir, const string &file) {
    if (file.empty())
        return string();
    const string path = dir + sep + file;
    return DirEntry::exists(path) ? path : string();
}

} // namespace

vector<ThemeIcon> readThemeIcons(const string &path) {
    vector<ThemeIcon> icons;
    json launcher;
    if (!readLauncher(path, launcher))
        return icons;
    const json *block = child(launcher, "icons");
    if (!block || !block->is_object())
        return icons;
    for (auto it = block->begin(); it != block->end(); ++it) {
        ThemeIcon icon;
        icon.name = it.key();
        if (it->is_string()) {
            icon.image = it->get<string>();
        } else {
            readStr(*it, "image", icon.image);
            readStr(*it, "image2x", icon.image2x);
        }
        if (icon.image.empty() && icon.image2x.empty())
            continue;
        icons.push_back(icon);
    }
    return icons;
}

bool readThemeIconHalo(const string &path, bool &halo) {
    json launcher;
    if (!readLauncher(path, launcher))
        return false;
    const json *v = child(launcher, "iconHalo");
    if (!v || !v->is_boolean())
        return false;
    halo = v->get<bool>();
    return true;
}

vector<ThemeIcon> loadThemeIcons(const string &dir) {
    vector<ThemeIcon> icons;
    for (ThemeIcon icon : readThemeIcons(dir + sep + "theme.json")) {
        const string image = existing(dir, icon.image);
        string image2x;
        if (!icon.image2x.empty())
            image2x = existing(dir, icon.image2x);
        else if (!image.empty() && DirEntry::exists(at2x(image)))
            image2x = at2x(image);
        if (image.empty() && image2x.empty()) {
            PLOG_WARNING << "Theme icon '" << icon.name << "': no image in " << dir << " - it falls back";
            continue;
        }
        icon.image = image;
        icon.image2x = image2x;
        icons.push_back(icon);
    }
    return icons;
}

vector<ThemeIcon> resolveThemeIcons(const string &themeDir, const string &defaultDir,
                                    const map<string, string> &builtIn) {
    map<string, ThemeIcon> table;
    // the built-in files first, then the default theme's entries over them, then the theme's own over those
    for (const auto &b : builtIn) {
        if (b.second.empty() || !DirEntry::exists(b.second))
            continue;
        ThemeIcon icon;
        icon.name = b.first;
        icon.image = b.second;
        if (DirEntry::exists(at2x(b.second)))
            icon.image2x = at2x(b.second);
        table[b.first] = icon;
    }
    for (const ThemeIcon &icon : loadThemeIcons(defaultDir))
        table[icon.name] = icon;
    if (themeDir != defaultDir)
        for (const ThemeIcon &icon : loadThemeIcons(themeDir))
            table[icon.name] = icon;
    vector<ThemeIcon> icons;
    for (const auto &t : table)
        icons.push_back(t.second);
    return icons;
}

bool resolveThemeIconHalo(const string &themeDir, const string &defaultDir) {
    bool halo = true;
    if (!readThemeIconHalo(themeDir + sep + "theme.json", halo))
        readThemeIconHalo(defaultDir + sep + "theme.json", halo);
    return halo;
}

//*******************************
// readThemeLogo / loadThemeLogo / readThemeResumeMask / loadThemeResumeMask
//*******************************
ThemeLauncherLogo readThemeLogo(const string &path) {
    ThemeLauncherLogo logo;
    json launcher;
    if (!readLauncher(path, launcher))
        return logo;
    const json *block = child(launcher, "logo");
    if (!block || !block->is_object())
        return logo;
    readStr(*block, "file", logo.file);
    readInt(*block, "x", logo.x);
    readInt(*block, "y", logo.y);
    readInt(*block, "w", logo.w);
    readInt(*block, "h", logo.h);
    logo.set = !logo.file.empty() && logo.w > 0 && logo.h > 0;
    return logo;
}

ThemeLauncherLogo loadThemeLogo(const string &dir) {
    ThemeLauncherLogo logo = readThemeLogo(dir + sep + "theme.json");
    if (!logo.set)
        return logo;
    const string file = existing(dir, logo.file);
    if (file.empty()) {
        PLOG_WARNING << "Theme logo '" << logo.file << "': not in " << dir << " - no logo drawn";
        return ThemeLauncherLogo();
    }
    logo.file = file;
    return logo;
}

string readThemeResumeMask(const string &path) {
    string file;
    json launcher;
    if (!readLauncher(path, launcher))
        return file;
    const json *icons = child(launcher, "menuIcons");
    if (icons)
        readStr(*icons, "resumePictureMask", file);
    return file;
}

string loadThemeResumeMask(const string &dir) {
    const string file = readThemeResumeMask(dir + sep + "theme.json");
    if (file.empty())
        return file;
    const string path = existing(dir, file);
    if (path.empty()) {
        PLOG_WARNING << "Theme resume picture mask '" << file << "': not in " << dir << " - a plain rectangle";
    }
    return path;
}

//*******************************
// readThemeDisabledVeil / loadThemeDisabledVeil
//*******************************
constexpr int ThemeDisabledVeil::DefaultAlpha;

ThemeDisabledVeil readThemeDisabledVeil(const string &path) {
    ThemeDisabledVeil veil;
    json launcher;
    if (!readLauncher(path, launcher))
        return veil;
    const json *colors = child(launcher, "colors");
    const json *value = colors ? child(*colors, "disabled") : nullptr;
    if (!value)
        return veil;
    ThemeColor color;
    if (value->is_string()) {
        if (!ThemeColor::parseHex(value->get<string>(), color))
            return veil;
    } else if (value->is_object()) {
        const json *c = child(*value, "color");
        if (c) {
            if (!c->is_string() || !ThemeColor::parseHex(c->get<string>(), color))
                return veil;
        } else {
            color = ThemeColor(0, 0, 0);
        }
        const json *a = child(*value, "alpha");
        if (a) {
            if (!a->is_number_integer())
                return veil;
            const long long alpha = a->get<long long>();
            veil.alpha = alpha < 0 ? 0 : alpha > 255 ? 255 : static_cast<int>(alpha);
        }
    } else {
        return veil;
    }
    veil.color = color;
    veil.set = true;
    return veil;
}

ThemeDisabledVeil loadThemeDisabledVeil(const string &dir) {
    return readThemeDisabledVeil(dir + sep + "theme.json");
}

//*******************************
// readThemeSheet / loadThemeSheet
//*******************************
constexpr int ThemeSheet::DefaultAlpha;

ThemeSheet readThemeSheet(const string &path) {
    ThemeSheet sheet;
    json launcher;
    if (!readLauncher(path, launcher))
        return sheet;
    const json *colors = child(launcher, "colors");
    const json *value = colors ? child(*colors, "sheet") : nullptr;
    if (!value)
        return sheet;
    ThemeColor color;
    if (value->is_string()) {
        if (!ThemeColor::parseHex(value->get<string>(), color))
            return sheet;
    } else if (value->is_object()) {
        const json *c = child(*value, "color");
        if (c) {
            if (!c->is_string() || !ThemeColor::parseHex(c->get<string>(), color))
                return sheet;
        } else {
            color = ThemeColor(0, 0, 0);
        }
        const json *a = child(*value, "alpha");
        if (a) {
            if (!a->is_number_integer())
                return sheet;
            const long long alpha = a->get<long long>();
            sheet.alpha = alpha < 0 ? 0 : alpha > 255 ? 255 : static_cast<int>(alpha);
        }
    } else {
        return sheet;
    }
    sheet.color = color;
    sheet.set = true;
    return sheet;
}

ThemeSheet loadThemeSheet(const string &dir) {
    return readThemeSheet(dir + sep + "theme.json");
}

//*******************************
// readThemeInactiveAlphas / loadThemeInactiveAlphas
//*******************************
ThemeInactiveAlphas readThemeInactiveAlphas(const string &path) {
    ThemeInactiveAlphas alphas;
    json launcher;
    if (!readLauncher(path, launcher))
        return alphas;
    const json *block = child(launcher, "inactive");
    if (!block || !block->is_object())
        return alphas;
    auto alphaOf = [&](const char *key, int &out) {
        const json *value = child(*block, key);
        if (!value || !value->is_number_integer())
            return;
        const long long alpha = value->get<long long>();
        out = alpha < 0 ? 0 : alpha > 255 ? 255 : static_cast<int>(alpha);
    };
    alphaOf("resume", alphas.resume);
    alphaOf("tab", alphas.tab);
    alphaOf("barTrack", alphas.barTrack);
    return alphas;
}

ThemeInactiveAlphas loadThemeInactiveAlphas(const string &dir) {
    return readThemeInactiveAlphas(dir + sep + "theme.json");
}

string themeImageFile(const string &file, float outputScale, float &pixelScale) {
    pixelScale = 1.0f;
    if (outputScale <= 1.0f || file.empty())
        return file; // at scale 1 the disk is not even asked
    const string hiRes = at2x(file);
    if (!DirEntry::exists(hiRes))
        return file;
    pixelScale = 2.0f;
    return hiRes;
}

//*******************************
// readThemeSpinner / loadThemeSpinner
//*******************************
constexpr int ThemeSpinner::DefaultFps;

bool readThemeSpinner(const string &path, ThemeSpinner &out) {
    json launcher;
    if (!readLauncher(path, launcher))
        return false;
    const json *block = child(launcher, "spinner");
    if (!block || !block->is_object())
        return false;
    ThemeSpinner s;
    readStr(*block, "image", s.image);
    readStr(*block, "image2x", s.image2x);
    if (s.image.empty() && s.image2x.empty())
        return false;
    const json *frames = child(*block, "frames");
    if (!frames || !frames->is_number_integer() || frames->get<int>() < 1)
        return false;
    s.frames = frames->get<int>();
    const json *fps = child(*block, "fps");
    s.fps = fps && fps->is_number_integer() && fps->get<int>() >= 1 ? fps->get<int>() : ThemeSpinner::DefaultFps;
    out = s;
    return true;
}

bool loadThemeSpinner(const string &dir, ThemeSpinner &out) {
    ThemeSpinner s;
    if (!readThemeSpinner(dir + sep + "theme.json", s))
        return false;
    const string image = existing(dir, s.image);
    string image2x;
    if (!s.image2x.empty())
        image2x = existing(dir, s.image2x);
    else if (DirEntry::exists(at2x(dir + sep + s.image)))
        image2x = at2x(dir + sep + s.image); // found next to the 1x's name even when the 1x itself is not there
    if (image.empty() && image2x.empty()) {
        PLOG_WARNING << "Theme spinner: no image in " << dir << " - the ring of dots is drawn instead";
        return false;
    }
    s.image = image;
    s.image2x = image2x;
    out = s;
    return true;
}

//*******************************
// readThemeLayout4x3 / loadThemeLayout4x3
//*******************************
namespace {
// the numbers of `j` into `out` as "<prefix><key>" (nested objects "<prefix><key>.<inner>")
void flattenNumbers(const json &j, const string &prefix, map<string, double> &out) {
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (it->is_number())
            out[prefix + it.key()] = it->get<double>();
        else if (it->is_object())
            flattenNumbers(*it, prefix + it.key() + ".", out);
    }
}
} // namespace

ThemeLayout4x3 readThemeLayout4x3(const string &path) {
    ThemeLayout4x3 layout;
    ifstream in(path, ifstream::binary);
    if (!in.is_open())
        return layout;
    json j;
    try {
        in >> j;
    } catch (const json::exception &) {
        return layout;
    }
    const json *block = child(j, "layout4x3");
    if (!block || !block->is_object())
        return layout;
    layout.set = true;
    for (auto it = block->begin(); it != block->end(); ++it) {
        if (it.key() == "images") {
            if (!it->is_object())
                continue;
            for (auto image = it->begin(); image != it->end(); ++image)
                if (image->is_string() && !image->get<string>().empty())
                    layout.images[image.key()] = image->get<string>();
        } else if (it->is_number()) {
            layout.values[it.key()] = it->get<double>();
        } else if (it->is_object()) {
            flattenNumbers(*it, it.key() + ".", layout.values);
        }
    }
    return layout;
}

ThemeLayout4x3 loadThemeLayout4x3(const string &dir) {
    ThemeLayout4x3 layout = readThemeLayout4x3(dir + sep + "theme.json");
    for (auto it = layout.images.begin(); it != layout.images.end();) {
        const string file = existing(dir, it->second);
        if (file.empty()) {
            PLOG_WARNING << "Theme layout4x3 image '" << it->second << "': not in " << dir << " - the 16:9 one is used";
            it = layout.images.erase(it);
        } else {
            it->second = file;
            ++it;
        }
    }
    return layout;
}

//*******************************
// readThemeHidden / loadThemeHidden
//*******************************
bool readThemeHidden(const string &path) {
    ifstream in(path, ifstream::binary);
    if (!in.is_open())
        return false;
    json j;
    try {
        in >> j;
    } catch (const json::exception &) {
        return false;
    }
    const json *hidden = child(j, "hidden");
    return hidden && hidden->is_boolean() && hidden->get<bool>();
}

bool loadThemeHidden(const string &dir) {
    return readThemeHidden(dir + sep + "theme.json");
}

//*******************************
// mergeThemeJson / readThemeJsonInt / digestThemeJson
//*******************************
namespace {

// `into` gets `patch`'s keys: objects on both sides merge key by key, anything else is replaced
void mergeInto(ordered_json &into, const ordered_json &patch) {
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (it->is_object() && into.contains(it.key()) && into[it.key()].is_object())
            mergeInto(into[it.key()], *it);
        else
            into[it.key()] = *it;
    }
}

// the theme.json at `path` parsed into `out`; false when missing, invalid or not an object
template <class Json> bool parseObjectFile(const string &path, Json &out) {
    ifstream in(path, ifstream::binary);
    if (!in.is_open())
        return false;
    try {
        in >> out;
    } catch (const typename Json::exception &) {
        return false;
    }
    return out.is_object();
}

} // namespace

bool mergeThemeJson(const string &path, const string &patch) {
    ordered_json patchJson;
    try {
        patchJson = ordered_json::parse(patch);
    } catch (const ordered_json::exception &) {
        return false;
    }
    if (!patchJson.is_object())
        return false;
    ordered_json j;
    if (!parseObjectFile(path, j))
        return false;
    mergeInto(j, patchJson);
    ofstream o(path, ofstream::binary);
    if (!DirEntry::checkWritable(o, path))
        return false;
    o << setw(2) << j << "\n";
    o.flush();
    o.close();
    return o.good();
}

int readThemeJsonInt(const string &path, const string &pointer, int fallback) {
    json j;
    if (!parseObjectFile(path, j))
        return fallback;
    try {
        const json &v = j.at(json::json_pointer(pointer));
        return v.is_number_integer() ? v.get<int>() : fallback;
    } catch (const json::exception &) {
        return fallback;
    }
}

string readThemeJsonString(const string &path, const string &pointer) {
    json j;
    if (!parseObjectFile(path, j))
        return string();
    try {
        const json &v = j.at(json::json_pointer(pointer));
        return v.is_string() ? v.get<string>() : string();
    } catch (const json::exception &) {
        return string();
    }
}

string digestThemeJson(const string &path, const vector<string> &pointers) {
    json j;
    if (!parseObjectFile(path, j))
        return string();
    string text;
    for (const string &pointer : pointers) {
        json value; // null when the pointer leads nowhere
        try {
            value = j.at(json::json_pointer(pointer));
        } catch (const json::exception &) {
        }
        text += pointer + "=" + value.dump() + "\n"; // json keeps its keys sorted: the same blocks, the same text
    }
    uint64_t hash = 1469598103934665603ULL; // FNV-1a, 64 bit
    for (const char c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    char hex[17];
    snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(hash));
    return hex;
}

} // namespace ableem
