//
// AppManifest - see the header.
//
#include "app_manifest.h"
#include "../main.h"

#include <ableem/engine/log.h>

using namespace std;

namespace {

bool isAbsolute(const string &path) {
    return !path.empty() && (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':'));
}

string under(const string &folder, const string &path) {
    if (path.empty() || isAbsolute(path))
        return path;
    return folder + sep + path;
}

bool isFile(const string &path) {
    return !path.empty() && DirEntry::exists(path) && !DirEntry::isDirectory(path);
}

string withKey(string value, const string &key) {
    Strings::replaceAll(value, "{key}", key);
    return value;
}

// .<key> first, then the plain key with {key} replaced; "" when neither
string valueFor(const map<string, string> &values, const string &name, const string &key) {
    auto own = values.find(name + "." + key);
    if (own != values.end())
        return withKey(own->second, key);
    auto plain = values.find(name);
    if (plain != values.end())
        return withKey(plain->second, key);
    return "";
}

// the ini names the program for some platform: Exec= or any Exec.<key>=
bool namesAProgram(const map<string, string> &values, const string &programKey) {
    for (const auto &kv : values)
        if (kv.first == programKey || kv.first.compare(0, programKey.size() + 1, programKey + ".") == 0)
            return true;
    return false;
}

} // namespace

//*******************************
// AppManifest::value
//*******************************
string AppManifest::value(const string &name) const {
    auto it = values.find(name);
    return it == values.end() ? string() : it->second;
}

//*******************************
// AppManifest::programExtensions / pluginExtension
//*******************************
vector<string> AppManifest::programExtensions() {
#ifdef _WIN32
    return {".exe"};
#else
    return {};
#endif
}

string AppManifest::pluginExtension() {
#ifdef _WIN32
    return ".dll";
#else
    return ".so";
#endif
}

//*******************************
// AppManifest::parseEnv
//*******************************
vector<pair<string, string>> AppManifest::parseEnv(const string &value) {
    vector<pair<string, string>> out;
    for (const string &item : Strings::getTokens(value, ';')) {
        string entry = Strings::trim(item);
        string::size_type eq = entry.find('=');
        string name = Strings::trim(entry.substr(0, eq));
        if (name.empty())
            continue;
        out.emplace_back(name, eq == string::npos ? string() : Strings::trim(entry.substr(eq + 1)));
    }
    return out;
}

//*******************************
// AppManifest::splitArgs
//*******************************
vector<string> AppManifest::splitArgs(const string &value) {
    vector<string> out;
    string current;
    bool quoted = false, any = false;
    for (char c : value) {
        if (c == '"') {
            quoted = !quoted;
            any = true;
        } else if ((c == ' ' || c == '\t') && !quoted) {
            if (any)
                out.push_back(current);
            current.clear();
            any = false;
        } else {
            current += c;
            any = true;
        }
    }
    if (any)
        out.push_back(current);
    return out;
}

//*******************************
// AppManifest::programInFolder
//*******************************
string AppManifest::programInFolder() const {
    string prefix = folder + sep;
    if (program.compare(0, prefix.size(), prefix) == 0)
        return program.substr(prefix.size());
    return program;
}

//*******************************
// AppManifest::resolve
//*******************************
AppManifest AppManifest::resolve(const string &folder, const map<string, string> &values, const vector<string> &keys,
                                 const Options &options) {
    AppManifest m;
    m.folder = folder;
    m.values = values;

    if (!namesAProgram(values, options.programKey)) {
        string startup = m.value("startup");
        if (options.allowStartup && !startup.empty()) {
            string script = under(folder, startup);
            if (isFile(script)) {
                m.program = script;
                m.legacyStartup = true;
            } else {
                m.problem = "its Startup script " + startup + " is missing";
            }
        } else {
            m.problem = "it names no " + options.programKey;
        }
        return m;
    }

    string tried;
    for (const string &key : keys) {
        auto own = values.find(options.programKey + "." + key);
        string name;
        if (own != values.end())
            name = withKey(own->second, key);
        else if (values.count(options.programKey))
            name = withKey(values.at(options.programKey), key);
        if (name.empty())
            continue;
        string candidate = under(folder, name);
        string found;
        if (isFile(candidate)) {
            found = candidate;
        } else {
            for (const string &ext : options.extensions) {
                if (isFile(candidate + ext)) {
                    found = candidate + ext;
                    break;
                }
            }
        }
        if (found.empty()) {
            tried += (tried.empty() ? "" : ", ") + key;
            continue;
        }
        m.program = found;
        m.key = key;
        break;
    }
    if (m.program.empty()) {
        m.problem = "no binary for this system (tried " + (tried.empty() ? string("no key") : tried) + ")";
        return m;
    }

    m.args = valueFor(values, "args", m.key);
    string lib = valueFor(values, "lib", m.key);
    m.libDir = lib.empty() ? "" : under(folder, lib);
    // the plain list first, then the key's own, so a platform can override one variable
    vector<pair<string, string>> env;
    auto plainEnv = values.find("env");
    if (plainEnv != values.end())
        for (auto &kv : parseEnv(withKey(plainEnv->second, m.key)))
            env.push_back(kv);
    auto ownEnv = values.find("env." + m.key);
    if (ownEnv != values.end()) {
        for (auto &kv : parseEnv(ownEnv->second)) {
            bool replaced = false;
            for (auto &existing : env) {
                if (existing.first == kv.first) {
                    existing.second = kv.second;
                    replaced = true;
                }
            }
            if (!replaced)
                env.push_back(kv);
        }
    }
    m.env = env;
    return m;
}

//*******************************
// AppManifest::load
//*******************************
AppManifest AppManifest::load(const string &folder, const string &iniName, const vector<string> &keys,
                              const Options &options) {
    string iniPath = folder + sep + iniName;
    map<string, string> values;
    if (DirEntry::exists(iniPath)) {
        IniFile ini;
        ini.load(iniPath);
        // IniFile lower-cases the keys but keeps the blanks around '='
        for (const auto &kv : ini.values)
            values[Strings::trim(kv.first)] = Strings::trim(kv.second);
    } else {
        AppManifest m;
        m.folder = folder;
        m.problem = "there is no " + iniName;
        return m;
    }
    AppManifest m = resolve(folder, values, keys, options);
    if (!m.runnable()) {
        PLOG_INFO << folder << ": not runnable here - " << m.problem;
    }
    return m;
}

AppManifest AppManifest::load(const string &folder, const string &iniName, const vector<string> &keys) {
    Options options;
    options.extensions = programExtensions();
    return load(folder, iniName, keys, options);
}
