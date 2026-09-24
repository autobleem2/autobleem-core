//
// AppManifest and Env::appPlatformKeys: which of a multi-platform App's binaries this machine runs
// (docs/app-format-plan.md in the launcher).
//
#include "doctest/doctest.h"

#include "../support/temp_dir.h"
#include "core/services/app_manifest.h"
#include "core/services/environment.h"
#include "core/services/platform_config.h"
#include "core/main.h"

using namespace std;

namespace {
// an App folder with the given ini and (empty) files
void makeApp(const TempDir &tmp, const string &ini, const vector<string> &files) {
    tmp.writeFile("app.ini", ini);
    for (const string &f : files) {
        string dir = DirEntry::getDirNameFromPath(f);
        if (!dir.empty() && dir != f)
            tmp.makeSubDir(dir);
        tmp.writeFile(f, "x");
    }
}

AppManifest::Options appOptions(vector<string> extensions = {}) {
    AppManifest::Options o;
    o.extensions = std::move(extensions);
    return o;
}
} // namespace

TEST_CASE("Env::appPlatformKeysFor: the table") {
    CHECK(Env::appPlatformKeysFor("psc", "linux", "armhf") == vector<string>{"psc"});
    CHECK(Env::appPlatformKeysFor("rpi", "linux", "armhf") == vector<string>{"rpi", "linux-armhf"});
    CHECK(Env::appPlatformKeysFor("rpi64", "linux", "arm64") == vector<string>{"rpi64", "linux-arm64"});
    CHECK(Env::appPlatformKeysFor("pcusb", "linux", "i386") == vector<string>{"pcusb", "linux-i386"});
    CHECK(Env::appPlatformKeysFor("win", "windows", "x86_64") == vector<string>{"win", "windows-x86_64"});
    CHECK(Env::appPlatformKeysFor("dev", "windows", "x86_64") ==
          vector<string>{"dev", "win", "windows-x86_64"});
    CHECK(Env::appPlatformKeysFor("dev", "linux", "x86_64") == vector<string>{"dev", "linux-x86_64"});
    // a future target needs no change to an App built for its generic key
    CHECK(Env::appPlatformKeysFor("vcs", "linux", "x86_64") == vector<string>{"vcs", "linux-x86_64"});
}

TEST_CASE("Env::appPlatformKeys: the build's list, then the platform ini's extras (added, never removed)") {
    Env::setExtraAppPlatformKeys({});
    vector<string> builtIn = Env::appPlatformKeys();
    REQUIRE(!builtIn.empty());
    CHECK(builtIn[0] == Env::buildTargetKey());

    Env::setExtraAppPlatformKeys({" Linux-Extra ", builtIn[0], ""});
    vector<string> keys = Env::appPlatformKeys();
    CHECK(keys.size() == builtIn.size() + 1); // the duplicate and the empty one are not added again
    CHECK(keys.back() == "linux-extra");
    Env::setExtraAppPlatformKeys({});
}

TEST_CASE("PlatformConfig reads app_platform_keys") {
    TempDir tmp("appkeys");
    tmp.makeSubDir("platform");
    tmp.writeFile("platform/odd.ini", "app_platform_keys=linux-x86_64; odd\n");
    PlatformConfig cfg = PlatformConfig::load(PlatformConfig::pathFor(tmp.path(), "odd"));
    CHECK(cfg.appPlatformKeys == vector<string>{"linux-x86_64", "odd"});
}

TEST_CASE("AppManifest: Exec=bin/{key}/... takes the first key whose binary is there") {
    TempDir tmp("manifest");
    makeApp(tmp, "[app]\nTitle=Tyrian\nExec=bin/{key}/tyrian\n", {"bin/linux-arm64/tyrian", "bin/psc/tyrian"});

    AppManifest m = AppManifest::load(tmp.path(), "app.ini", {"rpi64", "linux-arm64"}, appOptions());
    REQUIRE(m.runnable());
    CHECK(m.key == "linux-arm64");
    CHECK(m.program == tmp.at("bin/linux-arm64/tyrian"));
    CHECK_FALSE(m.legacyStartup);
    CHECK(m.value("title") == "Tyrian");

    // the specific key wins when both are there
    tmp.makeSubDir("bin/rpi64");
    tmp.writeFile("bin/rpi64/tyrian", "x");
    m = AppManifest::load(tmp.path(), "app.ini", {"rpi64", "linux-arm64"}, appOptions());
    CHECK(m.key == "rpi64");

    // nothing for this machine
    m = AppManifest::load(tmp.path(), "app.ini", {"win", "windows-x86_64"}, appOptions());
    CHECK_FALSE(m.runnable());
    CHECK(m.problem.find("win") != string::npos);
}

TEST_CASE("AppManifest: Exec.<key> beats the pattern, and a key without its own line uses the pattern") {
    TempDir tmp("manifest");
    makeApp(tmp,
            "[app]\n"
            "Exec.psc = odd/place/tyrian-psc\n"
            "Exec=bin/{key}/tyrian\n",
            {"odd/place/tyrian-psc", "bin/psc/tyrian", "bin/rpi/tyrian"});
    CHECK(AppManifest::load(tmp.path(), "app.ini", {"psc"}, appOptions()).program == tmp.at("odd/place/tyrian-psc"));
    CHECK(AppManifest::load(tmp.path(), "app.ini", {"rpi", "linux-armhf"}, appOptions()).program ==
          tmp.at("bin/rpi/tyrian"));
}

TEST_CASE("AppManifest: an extension is tried when the name as given is not a file") {
    TempDir tmp("manifest");
    makeApp(tmp, "[app]\nExec=bin/{key}/tyrian\n", {"bin/win/tyrian.exe"});
    AppManifest m = AppManifest::load(tmp.path(), "app.ini", {"win"}, appOptions({".exe"}));
    CHECK(m.program == tmp.at("bin/win/tyrian.exe"));
    // without the extension list it is not found
    CHECK_FALSE(AppManifest::load(tmp.path(), "app.ini", {"win"}, appOptions()).runnable());
}

TEST_CASE("AppManifest: a directory is not a program") {
    TempDir tmp("manifest");
    makeApp(tmp, "[app]\nExec=bin/{key}\n", {"bin/psc/something"});
    CHECK_FALSE(AppManifest::load(tmp.path(), "app.ini", {"psc"}, appOptions()).runnable());
}

TEST_CASE("AppManifest: Args, Lib and Env resolve for the key that matched") {
    TempDir tmp("manifest");
    makeApp(tmp,
            "[app]\n"
            "Exec=bin/{key}/tyrian\n"
            "Args=--data data --fullscreen\n"
            "Args.psc=--data data --wayland\n"
            "Lib=lib/{key}\n"
            "Env=SDL_AUDIODRIVER=alsa; GAME_DIR={key}; EMPTY=\n"
            "Env.rpi=SDL_AUDIODRIVER=pulse;EXTRA=a=b\n",
            {"bin/psc/tyrian", "bin/rpi/tyrian"});

    AppManifest psc = AppManifest::load(tmp.path(), "app.ini", {"psc"}, appOptions());
    CHECK(psc.args == "--data data --wayland");
    CHECK(psc.libDir == tmp.at("lib/psc"));
    REQUIRE(psc.env.size() == 3);
    CHECK(psc.env[0] == make_pair(string("SDL_AUDIODRIVER"), string("alsa")));
    CHECK(psc.env[1] == make_pair(string("GAME_DIR"), string("psc")));
    CHECK(psc.env[2] == make_pair(string("EMPTY"), string("")));

    AppManifest rpi = AppManifest::load(tmp.path(), "app.ini", {"rpi", "linux-armhf"}, appOptions());
    CHECK(rpi.args == "--data data --fullscreen");
    CHECK(rpi.libDir == tmp.at("lib/rpi"));
    REQUIRE(rpi.env.size() == 4);
    CHECK(rpi.env[0].second == "pulse"); // the key's own overrides, in place
    CHECK(rpi.env[3] == make_pair(string("EXTRA"), string("a=b")));
}

TEST_CASE("AppManifest: an App of the old kind starts through its Startup script") {
    TempDir tmp("manifest");
    makeApp(tmp, "[app]\nTitle=Old\nStartup=run.sh\n", {"run.sh"});
    AppManifest m = AppManifest::load(tmp.path(), "app.ini", {"psc"}, appOptions());
    REQUIRE(m.runnable());
    CHECK(m.legacyStartup);
    CHECK(m.key.empty());
    CHECK(m.program == tmp.at("run.sh"));

    // an extension has no such fallback
    AppManifest::Options plugin;
    plugin.programKey = "plugin";
    plugin.allowStartup = false;
    CHECK_FALSE(AppManifest::load(tmp.path(), "app.ini", {"psc"}, plugin).runnable());

    // a Startup naming a missing script
    TempDir broken("manifest");
    makeApp(broken, "[app]\nStartup=run.sh\n", {});
    AppManifest b = AppManifest::load(broken.path(), "app.ini", {"psc"}, appOptions());
    CHECK_FALSE(b.runnable());
    CHECK(b.problem.find("run.sh") != string::npos);
}

TEST_CASE("AppManifest: an extension's Plugin= with the platform's library suffix") {
    TempDir tmp("manifest");
    tmp.writeFile("extension.ini", "[extension]\nName=Store\nPlugin=bin/{key}/store\n");
    tmp.makeSubDir("bin/psc");
    tmp.writeFile("bin/psc/store.so", "x");
    AppManifest::Options plugin;
    plugin.programKey = "plugin";
    plugin.extensions = {".so"};
    plugin.allowStartup = false;
    AppManifest m = AppManifest::load(tmp.path(), "extension.ini", {"psc"}, plugin);
    CHECK(m.program == tmp.at("bin/psc/store.so"));
    CHECK(m.value("name") == "Store");
}

TEST_CASE("AppManifest: no ini") {
    TempDir tmp("manifest");
    AppManifest m = AppManifest::load(tmp.path(), "app.ini", {"psc"}, appOptions());
    CHECK_FALSE(m.runnable());
    CHECK(m.problem.find("app.ini") != string::npos);
}

TEST_CASE("AppManifest::parseEnv") {
    auto env = AppManifest::parseEnv("A=1; B = two ;=nameless;C;D=x=y;;");
    REQUIRE(env.size() == 4);
    CHECK(env[0] == make_pair(string("A"), string("1")));
    CHECK(env[1] == make_pair(string("B"), string("two")));
    CHECK(env[2] == make_pair(string("C"), string("")));
    CHECK(env[3] == make_pair(string("D"), string("x=y")));
}
