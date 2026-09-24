//
// AppManifest: an App's app.ini (or an extension's extension.ini) resolved for this machine - which of the
// folder's binaries is this platform's, with what arguments, libraries and environment. The one rule every
// launch path uses (the launcher, rc/app_env.sh's copy of it, Windows' direct start); docs/app-format-plan.md.
//
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

//******************
// AppManifest
//******************
// The folder carries a binary per platform key (bin/<key>/..., by convention). The rule: for each key of
// the machine's ordered list (Env::appPlatformKeys), the ini's Exec.<key>= if it has one, else Exec= with
// {key} replaced by that key; the first candidate that is an existing file is the program. A name that is
// not a file as given is tried with each of Options::extensions appended (".exe" on Windows, ".so"/".dll"
// for a plugin). Args=, Lib= and Env= resolve the same way for the key that matched: .<key> first, then the
// plain key with {key} replaced. An ini with no Exec at all is an App of the old kind: Startup= names its
// script, which is the program (legacyStartup).
struct AppManifest {
    struct Options {
        std::string programKey = "exec";     // the ini key naming the binary; "plugin" for an extension
        std::vector<std::string> extensions; // tried after the name as given, in order
        bool allowStartup = true;            // an App's Startup= fallback; an extension has none
    };

    std::string folder;                        // the App's folder, as given
    std::map<std::string, std::string> values; // the ini: keys lower-cased and trimmed, values trimmed

    std::string program;        // absolute (folder-based) path of the program or library; "" = none here
    std::string key;            // the platform key that matched; "" for a Startup script
    bool legacyStartup = false; // the program is the old Startup= script
    std::string args;           // Args(.<key>), {key} replaced; "" = none
    std::string libDir;         // Lib(.<key>), under the folder; "" = none
    std::vector<std::pair<std::string, std::string>> env; // Env(.<key>)= "A=1;B=2", the key's own last
    std::string problem;        // why there is no program, for the log and the UI

    bool runnable() const { return !program.empty(); }
    // the ini's value for a key ("" when absent) - title, author, version, ...
    std::string value(const std::string &key) const;

    // reads <folder>/<iniName> and resolves it for `keys`; a missing ini is a manifest with a problem
    static AppManifest load(const std::string &folder, const std::string &iniName,
                            const std::vector<std::string> &keys, const Options &options);
    static AppManifest load(const std::string &folder, const std::string &iniName,
                            const std::vector<std::string> &keys);
    // the resolution alone, over values already read (keys lower-cased)
    static AppManifest resolve(const std::string &folder, const std::map<std::string, std::string> &values,
                               const std::vector<std::string> &keys, const Options &options);

    // what an App's program may be missing on this build: {".exe"} on Windows, nothing elsewhere
    static std::vector<std::string> programExtensions();
    // a plugin's library suffix on this build: ".dll" on Windows, ".so" elsewhere
    static std::string pluginExtension();
    // "A=1;B=x=y" -> {A,1}, {B,x=y}; entries without a name are dropped
    static std::vector<std::pair<std::string, std::string>> parseEnv(const std::string &value);
    // an Args= line as an argv: split at blanks, "double quotes" keep a blank inside one argument
    static std::vector<std::string> splitArgs(const std::string &value);
    // the program as the folder names it ("bin/psc/tyrian"), or the whole path when it is not under it
    std::string programInFolder() const;
};
