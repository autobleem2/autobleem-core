//
// ThemeZipCache: themes left as <name>.zip are listed without unpacking and unpacked into themes/.cache/<name>/
// only while picked. Every zip here is made by the test itself.
//
#include "doctest/doctest.h"

#include "../support/env_fixture.h"
#include "../support/temp_dir.h"

#include "core/services/theme.h"
#include "core/services/theme_converter.h"
#include "core/services/theme_zip_cache.h"

#include <ableem/engine/zip_archive.h>
#include <ableem/engine/zip_writer.h>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include <map>
#include <string>
#include <vector>

using std::map;
using std::string;
using std::vector;

namespace {

// a zip of `files` (name -> bytes) at `path`
void makeZip(const string &path, const map<string, string> &files) {
    ableem::ZipWriter writer;
    REQUIRE(writer.open(path));
    for (const auto &file : files)
        REQUIRE(writer.addBytes(file.first, file.second));
    REQUIRE(writer.close());
}

const char *NEW_JSON = "{ \"classic\": { \"background\": \"bg.png\", \"menuLines\": 7 } }";

void makeNewTheme(const TempDir &tmp, const string &name) {
    tmp.makeSubDir("themes");
    makeZip(tmp.at("themes/" + name + ".zip"), {{"theme.json", NEW_JSON}, {"bg.png", string(300, 'x')}});
}

// a 1.0 theme: theme.ini at the root of a folder inside the zip, one stock image to rename
void makeOldTheme(const TempDir &tmp, const string &name) {
    tmp.makeSubDir("themes");
    makeZip(tmp.at("themes/" + name + ".zip"), {{"mytheme/theme.ini", "[Theme]\nBackground=old.jpg\nLines=9\n"},
                                                {"mytheme/colors.ini", "[Colors]\n"},
                                                {"mytheme/images/GR/JP_US_BG.png", "stock bg"},
                                                {"mytheme/old.jpg", "background"},
                                                {"__MACOSX/mytheme/theme.ini", "junk"}});
}

// puts the real free-space probe back, whatever the test did
struct ProbeGuard {
    ~ProbeGuard() { ThemeZipCache::setFreeSpaceProbe(nullptr); }
};

} // namespace

//*******************************
// listing
//*******************************
TEST_CASE("listZipThemes reads the zips' directories only: nothing is unpacked, renamed or created") {
    TempDir tmp("zipcache");
    tmp.makeSubDir("themes");
    makeZip(tmp.at("themes/Beta.zip"), {{"theme.json", NEW_JSON}});
    makeZip(tmp.at("themes/alpha.zip"), {{"nice/theme.ini", "[Theme]\n"}, {"nice/images/a.png", "x"}});
    makeZip(tmp.at("themes/junk.zip"), {{"__MACOSX/x/theme.ini", "x"}, {"real/theme.json", NEW_JSON}});
    makeZip(tmp.at("themes/pics.zip"), {{"a.png", "x"}, {"b.png", "y"}}); // no theme in it
    makeZip(tmp.at("themes/two.zip"), {{"one/theme.json", NEW_JSON}, {"two/theme.json", NEW_JSON}});
    tmp.writeFile("themes/text.zip", "not an archive");
    tmp.writeFile("themes/old.zip.bad", "an earlier verdict");
    tmp.makeSubDir("themes/folder");

    CHECK(ThemeZipCache::listZipThemes(tmp.at("themes")) == vector<string>{"Beta", "alpha", "junk"});

    // as it was: every zip still a zip, no verdict written, no cache made
    for (const char *zip : {"Beta", "alpha", "junk", "pics", "two", "text"})
        CHECK(DirEntry::exists(tmp.at(string("themes/") + zip + ".zip")));
    CHECK_FALSE(DirEntry::exists(tmp.at("themes/text.zip.bad")));
    CHECK_FALSE(DirEntry::exists(tmp.at("themes/.cache")));
}

TEST_CASE("a folder of the same name wins: the zip is not listed and not used") {
    TempDir tmp("zipcache");
    makeNewTheme(tmp, "Neon");
    tmp.makeSubDir("themes/Neon");
    tmp.writeFile("themes/Neon/theme.json", "{ \"classic\": { \"background\": \"folder.png\" } }");
    makeNewTheme(tmp, "Solo");

    CHECK(ThemeZipCache::listZipThemes(tmp.at("themes")) == vector<string>{"Solo"});
    CHECK(ThemeZipCache::prepare(tmp.at("themes"), "Neon").empty());
    CHECK_FALSE(DirEntry::exists(tmp.at("themes/.cache")));
    CHECK(DirEntry::exists(tmp.at("themes/Neon.zip")));
}

//*******************************
// unpack on pick
//*******************************
TEST_CASE("picking a zip theme unpacks it into .cache/<name>/ and leaves the zip") {
    TempDir tmp("zipcache");
    makeNewTheme(tmp, "Neon");

    const string dir = ThemeZipCache::prepare(tmp.at("themes"), "Neon");

    CHECK(dir == tmp.at("themes/.cache/Neon"));
    CHECK(tmp.readFile("themes/.cache/Neon/theme.json") == NEW_JSON);
    CHECK(tmp.readFile("themes/.cache/Neon/bg.png").size() == 300);
    CHECK(DirEntry::exists(tmp.at("themes/Neon.zip")));
    CHECK_FALSE(DirEntry::exists(tmp.at("themes/Neon"))); // not installed as a folder
    CHECK_FALSE(DirEntry::exists(tmp.at("themes/.cache/.Neon.unzip")));
}

TEST_CASE("a zip with one folder inside unpacks that folder as the theme") {
    TempDir tmp("zipcache");
    makeOldTheme(tmp, "retro");

    const string dir = ThemeZipCache::prepare(tmp.at("themes"), "retro");

    CHECK(dir == tmp.at("themes/.cache/retro"));
    CHECK_FALSE(DirEntry::exists(tmp.at("themes/.cache/retro/mytheme")));
    CHECK_FALSE(DirEntry::exists(tmp.at("themes/.cache/__MACOSX")));
    CHECK(ThemeConverter::isThemeFolder(dir));
}

TEST_CASE("nothing is written for a theme that is not a zip: no .cache on a quiet stick") {
    TempDir tmp("zipcache");
    makeNewTheme(tmp, "Neon");
    tmp.makeSubDir("themes/default");
    tmp.writeFile("themes/default/theme.json", NEW_JSON);

    CHECK(ThemeZipCache::prepare(tmp.at("themes"), "default").empty());
    CHECK(ThemeZipCache::prepare(tmp.at("themes"), "missing").empty());
    CHECK(ThemeZipCache::prepare(tmp.at("themes"), "").empty());
    CHECK(ThemeZipCache::prepare(tmp.at("themes"), "../evil").empty());
    CHECK_FALSE(DirEntry::exists(tmp.at("themes/.cache")));
}

TEST_CASE("picking another theme deletes the previous cache; a folder theme empties .cache") {
    TempDir tmp("zipcache");
    makeNewTheme(tmp, "one");
    makeNewTheme(tmp, "two");
    tmp.makeSubDir("themes/default");
    tmp.writeFile("themes/default/theme.json", NEW_JSON);

    REQUIRE(!ThemeZipCache::prepare(tmp.at("themes"), "one").empty());
    CHECK(DirEntry::isDirectory(tmp.at("themes/.cache/one")));

    REQUIRE(!ThemeZipCache::prepare(tmp.at("themes"), "two").empty());
    CHECK_FALSE(DirEntry::exists(tmp.at("themes/.cache/one")));
    CHECK(DirEntry::isDirectory(tmp.at("themes/.cache/two")));

    CHECK(ThemeZipCache::prepare(tmp.at("themes"), "default").empty());
    CHECK_FALSE(DirEntry::exists(tmp.at("themes/.cache")));
    CHECK(DirEntry::exists(tmp.at("themes/one.zip")));
    CHECK(DirEntry::exists(tmp.at("themes/two.zip")));
}

//*******************************
// 1.0 zips: converted inside the cache
//*******************************
TEST_CASE("a 1.0 zip is converted inside the cache, and the zip is not touched") {
    TempDir tmp("zipcache");
    makeOldTheme(tmp, "retro");
    const long long zipSize = DirEntry::fileSize(tmp.at("themes/retro.zip"));

    const string dir = ThemeZipCache::prepare(tmp.at("themes"), "retro");

    REQUIRE(!dir.empty());
    CHECK(DirEntry::exists(dir + "/theme.json"));
    CHECK(DirEntry::exists(dir + "/images/launcher_background.png"));
    CHECK_FALSE(DirEntry::exists(dir + "/theme.ini")); // deleted by the converter - in the cache only
    CHECK_FALSE(DirEntry::exists(dir + "/colors.ini"));
    CHECK_FALSE(ThemeConverter::needsConversion(dir));
    CHECK(DirEntry::fileSize(tmp.at("themes/retro.zip")) == zipSize);
    CHECK(ThemeZipCache::holdsTheme(tmp.at("themes/retro.zip"))); // still a 1.0 theme inside
}

TEST_CASE("a restart finds the converted cache and does no work") {
    TempDir tmp("zipcache");
    makeOldTheme(tmp, "retro");
    const string dir = ThemeZipCache::prepare(tmp.at("themes"), "retro");
    REQUIRE(!dir.empty());
    const string json = tmp.readFile("themes/.cache/retro/theme.json");
    tmp.writeFile("themes/.cache/retro/sentinel", "still here"); // gone if it were unpacked again

    CHECK(ThemeZipCache::prepare(tmp.at("themes"), "retro") == dir);
    CHECK(ThemeZipCache::prepare(tmp.at("themes"), "retro") == dir);

    CHECK(tmp.readFile("themes/.cache/retro/sentinel") == "still here");
    CHECK(tmp.readFile("themes/.cache/retro/theme.json") == json);
    CHECK_FALSE(DirEntry::exists(tmp.at("themes/.cache/retro/theme.ini")));
}

TEST_CASE("a zip replaced by a different one is unpacked again") {
    TempDir tmp("zipcache");
    makeNewTheme(tmp, "Neon");
    REQUIRE(!ThemeZipCache::prepare(tmp.at("themes"), "Neon").empty());

    makeZip(tmp.at("themes/Neon.zip"), {{"theme.json", "{ \"classic\": { \"menuLines\": 11 } }"}, {"new.png", "n"}});

    CHECK(ThemeZipCache::prepare(tmp.at("themes"), "Neon") == tmp.at("themes/.cache/Neon"));
    CHECK(DirEntry::exists(tmp.at("themes/.cache/Neon/new.png")));
    CHECK_FALSE(DirEntry::exists(tmp.at("themes/.cache/Neon/bg.png")));
}

//*******************************
// power cut
//*******************************
TEST_CASE("a leftover after a power cut is cleaned at start only when it does not match the picked theme") {
    TempDir tmp("zipcache");
    makeNewTheme(tmp, "Neon");
    tmp.makeSubDir("themes/default");
    tmp.writeFile("themes/default/theme.json", NEW_JSON);
    REQUIRE(!ThemeZipCache::prepare(tmp.at("themes"), "Neon").empty());
    tmp.writeFile("themes/.cache/Neon/sentinel", "complete");
    tmp.writeFile("themes/.cache/.Neon.unzip/theme.json", "half"); // cut during the next unpack
    tmp.writeFile("themes/.cache/other/theme.json", "an old pick");
    tmp.writeFile("themes/.cache/.other.unzip/x", "half");

    // picked Neon: its complete cache stays as it is, the rest goes
    CHECK(ThemeZipCache::prepare(tmp.at("themes"), "Neon") == tmp.at("themes/.cache/Neon"));
    CHECK(tmp.readFile("themes/.cache/Neon/sentinel") == "complete");
    CHECK_FALSE(DirEntry::exists(tmp.at("themes/.cache/.Neon.unzip")));
    CHECK_FALSE(DirEntry::exists(tmp.at("themes/.cache/other")));
    CHECK_FALSE(DirEntry::exists(tmp.at("themes/.cache/.other.unzip")));

    // picked a folder theme: all of it goes
    tmp.writeFile("themes/.cache/.Neon.unzip/theme.json", "half");
    CHECK(ThemeZipCache::prepare(tmp.at("themes"), "default").empty());
    CHECK_FALSE(DirEntry::exists(tmp.at("themes/.cache")));
}

TEST_CASE("a half-done unpack is never taken for the theme: the folder in .cache appears complete or not at all") {
    TempDir tmp("zipcache");
    makeNewTheme(tmp, "Neon");
    tmp.writeFile("themes/.cache/.Neon.unzip/bg.png", "half, no theme.json yet"); // the cut left only this

    const string dir = ThemeZipCache::prepare(tmp.at("themes"), "Neon");

    CHECK(dir == tmp.at("themes/.cache/Neon"));
    CHECK(tmp.readFile("themes/.cache/Neon/theme.json") == NEW_JSON);
    CHECK(tmp.readFile("themes/.cache/Neon/bg.png").size() == 300);
}

//*******************************
// bad zips and a full stick
//*******************************
TEST_CASE("a corrupt zip is renamed .bad when picked, and a theme-less one too") {
    TempDir tmp("zipcache");
    tmp.makeSubDir("themes");
    tmp.writeFile("themes/text.zip", "not an archive");
    makeZip(tmp.at("themes/pics.zip"), {{"a.png", "x"}});
    makeNewTheme(tmp, "cut");
    // a real archive cut short: the directory at its end is gone
    const string whole = tmp.readFile("themes/cut.zip");
    tmp.writeFile("themes/cut.zip", whole.substr(0, whole.size() / 2));

    for (const char *name : {"text", "pics", "cut"}) {
        CHECK(ThemeZipCache::prepare(tmp.at("themes"), name).empty());
        CHECK_FALSE(DirEntry::exists(tmp.at(string("themes/") + name + ".zip")));
        CHECK(DirEntry::exists(tmp.at(string("themes/") + name + ".zip.bad")));
        CHECK_FALSE(DirEntry::exists(tmp.at(string("themes/.cache/") + name)));
    }
    CHECK_FALSE(DirEntry::exists(tmp.at("themes/.cache")));
    CHECK(ThemeZipCache::listZipThemes(tmp.at("themes")).empty()); // .bad is not offered again
}

TEST_CASE("an archive with a name that escapes the folder is refused and renamed .bad") {
    TempDir tmp("zipcache");
    tmp.makeSubDir("themes");
    makeZip(tmp.at("themes/evil.zip"), {{"theme.json", NEW_JSON}, {"../../evil.txt", "x"}});

    CHECK(ThemeZipCache::prepare(tmp.at("themes"), "evil").empty());
    CHECK(DirEntry::exists(tmp.at("themes/evil.zip.bad")));
    CHECK_FALSE(DirEntry::exists(tmp.at("evil.txt")));
    CHECK_FALSE(DirEntry::exists(tmp.at("themes/.cache/evil")));
}

TEST_CASE("not enough free space: nothing is unpacked and the zip is left alone, not marked bad") {
    ProbeGuard guard;
    TempDir tmp("zipcache");
    makeNewTheme(tmp, "Neon");
    uint64_t asked = 0;
    ThemeZipCache::setFreeSpaceProbe([&](const string &) {
        asked++;
        return uint64_t(1000); // less than the 300 bytes of the theme plus the margin
    });

    CHECK(ThemeZipCache::prepare(tmp.at("themes"), "Neon").empty());

    CHECK(asked > 0);
    CHECK(DirEntry::exists(tmp.at("themes/Neon.zip")));
    CHECK_FALSE(DirEntry::exists(tmp.at("themes/Neon.zip.bad")));
    CHECK_FALSE(DirEntry::exists(tmp.at("themes/.cache/Neon")));
    CHECK_FALSE(DirEntry::exists(tmp.at("themes/.cache/.Neon.unzip")));

    // room again: the same zip unpacks
    ThemeZipCache::setFreeSpaceProbe([](const string &) { return uint64_t(1) << 40; });
    CHECK(ThemeZipCache::prepare(tmp.at("themes"), "Neon") == tmp.at("themes/.cache/Neon"));
}

TEST_CASE("a cache that cannot be written is not the zip's fault: a good zip is never renamed .bad") {
    TempDir tmp("zipcache");
    makeNewTheme(tmp, "Old10");
    makeOldTheme(tmp, "Old11");

    // .cache is in the way as a plain file: it can neither be cleaned as a folder nor created
    tmp.writeFile("themes/.cache", "in the way");
    for (const char *name : {"Old10", "Old11"}) {
        CHECK(ThemeZipCache::prepare(tmp.at("themes"), name).empty());
        CHECK(DirEntry::exists(tmp.at(string("themes/") + name + ".zip")));
        CHECK_FALSE(DirEntry::exists(tmp.at(string("themes/") + name + ".zip.bad")));
    }

#ifndef _WIN32
    // .cache is a folder that may not be written to: the unpack folder cannot be created
    DirEntry::removeFile(tmp.at("themes/.cache"));
    tmp.makeSubDir("themes/.cache");
    tmp.makeSubDir("themes/.cache/Old10"); // a stale entry of the picked name, so the folder is kept, not removed
    REQUIRE(chmod(tmp.at("themes/.cache").c_str(), 0555) == 0);
    DirEntry::createDir(tmp.at("themes/.cache/.probe"));
    const bool enforced = !DirEntry::isDirectory(tmp.at("themes/.cache/.probe")); // false when running as root
    if (enforced) {
        CHECK(ThemeZipCache::prepare(tmp.at("themes"), "Old10").empty());
        CHECK(DirEntry::exists(tmp.at("themes/Old10.zip")));
        CHECK_FALSE(DirEntry::exists(tmp.at("themes/Old10.zip.bad")));
    }
    chmod(tmp.at("themes/.cache").c_str(), 0755); // so the TempDir can delete it
#endif

    // once it can be written, the same zip unpacks
    DirEntry::removeDirAndContents(tmp.at("themes/.cache"));
    DirEntry::removeFile(tmp.at("themes/.cache"));
    CHECK(ThemeZipCache::prepare(tmp.at("themes"), "Old10") == tmp.at("themes/.cache/Old10"));
    CHECK(DirEntry::exists(tmp.at("themes/.cache/Old10/theme.json")));
}

TEST_CASE("a zip whose entry is damaged inside is still renamed .bad: only a sound archive is spared") {
    TempDir tmp("zipcache");
    makeNewTheme(tmp, "rot");
    string bytes = tmp.readFile("themes/rot.zip");
    // the first entry's stored bytes sit right after its local header (30 bytes + the name "theme.json")
    bytes[30 + 10 + 3] ^= 0x55;
    tmp.writeFile("themes/rot.zip", bytes);

    CHECK_FALSE(ableem::ZipArchive::verify(tmp.at("themes/rot.zip")));
    CHECK(ThemeZipCache::prepare(tmp.at("themes"), "rot").empty());
    CHECK(DirEntry::exists(tmp.at("themes/rot.zip.bad")));
}

TEST_CASE("ZipArchive::verify accepts a sound archive and writes nothing") {
    TempDir tmp("zipcache");
    makeNewTheme(tmp, "fine");
    CHECK(ableem::ZipArchive::verify(tmp.at("themes/fine.zip")));
    CHECK_FALSE(ableem::ZipArchive::verify(tmp.at("themes/missing.zip")));
}

TEST_CASE("the previous cache goes before the room is checked: its space counts") {
    ProbeGuard guard;
    TempDir tmp("zipcache");
    makeNewTheme(tmp, "one");
    makeNewTheme(tmp, "two");
    REQUIRE(!ThemeZipCache::prepare(tmp.at("themes"), "one").empty());

    ThemeZipCache::setFreeSpaceProbe([&](const string &) {
        return DirEntry::exists(tmp.at("themes/.cache/one")) ? uint64_t(1000) : uint64_t(1) << 40;
    });

    CHECK(ThemeZipCache::prepare(tmp.at("themes"), "two") == tmp.at("themes/.cache/two"));
}

//*******************************
// Theme::load
//*******************************
TEST_CASE("Theme::load() unpacks the picked zip, converts a 1.0 one in the cache, and a restart reuses it") {
    EnvFixture env;
    TempDir tmp("zipcache");
    env.setWorkingPath(tmp.path());
    env.setThemesDir(tmp.makeSubDir("themes"));
    tmp.writeFile("themes/default/theme.json", "{ \"classic\": { \"background\": \"bg.png\", \"menuLines\": 12 } }");
    tmp.writeFile("themes/default/bg.png", "x");
    makeOldTheme(tmp, "retro");
    tmp.writeFile("config.ini", "Theme=retro\n");

    {
        Config config;
        Theme theme(config);
        theme.load();

        CHECK(theme.loadedPath() == tmp.at("themes/.cache/retro"));
        CHECK(theme.classic().background == tmp.at("themes/.cache/retro/old.jpg"));
        CHECK(int(theme.classic().menuLines) == 9);
        CHECK(DirEntry::exists(tmp.at("themes/.cache/retro/theme.json")));
        CHECK(DirEntry::exists(tmp.at("themes/retro.zip")));
        CHECK_FALSE(DirEntry::exists(tmp.at("themes/retro")));
        CHECK(config.inifile.values["theme"] == "retro");
    }

    tmp.writeFile("themes/.cache/retro/sentinel", "kept");
    Config again; // the next start
    Theme restarted(again);
    restarted.load();
    CHECK(restarted.loadedPath() == tmp.at("themes/.cache/retro"));
    CHECK(tmp.readFile("themes/.cache/retro/sentinel") == "kept");
}

TEST_CASE("Theme::load() with a folder theme behaves as before and clears a stale cache") {
    EnvFixture env;
    TempDir tmp("zipcache");
    env.setWorkingPath(tmp.path());
    env.setThemesDir(tmp.makeSubDir("themes"));
    tmp.writeFile("themes/default/theme.json", "{ \"classic\": { \"menuLines\": 12 } }");
    tmp.writeFile("themes/mine/theme.json", "{ \"classic\": { \"menuLines\": 5 } }");
    tmp.writeFile("themes/.cache/gone/theme.json", "{}");
    tmp.writeFile("config.ini", "Theme=mine\n");

    Config config;
    Theme theme(config);
    theme.load();

    CHECK(theme.loadedPath() == tmp.at("themes/mine"));
    CHECK(int(theme.classic().menuLines) == 5);
    CHECK_FALSE(DirEntry::exists(tmp.at("themes/.cache")));
}

TEST_CASE("Theme::load() falls back to default when the picked zip is corrupt") {
    EnvFixture env;
    TempDir tmp("zipcache");
    env.setWorkingPath(tmp.path());
    env.setThemesDir(tmp.makeSubDir("themes"));
    tmp.writeFile("themes/default/theme.json", "{ \"classic\": { \"menuLines\": 12 } }");
    tmp.writeFile("themes/broken.zip", "not an archive");
    tmp.writeFile("config.ini", "Theme=broken\n");

    Config config;
    Theme theme(config);
    theme.load();

    CHECK(theme.loadedPath() == tmp.at("themes/default"));
    CHECK(DirEntry::exists(tmp.at("themes/broken.zip.bad")));
}
