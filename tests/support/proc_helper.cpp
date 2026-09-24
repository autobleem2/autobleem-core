//
// proc_helper: a scanner processor the tests can script (docs/scanner-processors-plan.md in the launcher).
//
// It picks a script by what it was asked - version.txt, ismine.txt, games.txt, roms.txt, ps1.txt or rom.txt,
// read from its working directory (the processor's folder) - and appends one line per call to calls.txt
// there: the arguments, then AB_PROCESSOR_PROTOCOL and AB_TMP. No script: "#Starting - helper" and "#DONE".
//
// A script line is printed to stdout as it is, except:
//   !err <text>            to stderr
//   !sleep <ms>
//   !exit <n>              exits at once with n (the default at the end is 0)
//   !write <path> <text>   writes a file; {target} in the path is the last argument
//   !move <from> <to>      renames; {target} as above
//   !remove <path>         deletes a file; {target} as above
//   !has <suffix>          exits 1 unless a file in the target folder (or the target file) ends with suffix
//
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

using namespace std;

namespace {

string replaceTarget(string s, const string &target) {
    size_t p;
    while ((p = s.find("{target}")) != string::npos)
        s.replace(p, 8, target);
    return s;
}

bool endsWith(const string &s, const string &suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

vector<string> listDir(const string &dir) {
    vector<string> names;
#ifdef _WIN32
    WIN32_FIND_DATAA data;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &data);
    if (h == INVALID_HANDLE_VALUE)
        return names;
    do {
        names.emplace_back(data.cFileName);
    } while (FindNextFileA(h, &data));
    FindClose(h);
#else
    if (DIR *d = opendir(dir.c_str())) {
        while (dirent *e = readdir(d))
            names.emplace_back(e->d_name);
        closedir(d);
    }
#endif
    return names;
}

string env(const char *name) {
    const char *v = getenv(name);
    return v ? v : "";
}

} // namespace

int main(int argc, char **argv) {
    vector<string> args(argv + 1, argv + argc);
    string target = args.empty() ? "" : args.back();

    {
        ofstream calls("calls.txt", ios::app);
        for (const string &a : args)
            calls << a << "|";
        calls << "protocol=" << env("AB_PROCESSOR_PROTOCOL") << "|tmp=" << (env("AB_TMP").empty() ? "no" : "yes")
              << "\n";
    }

    auto has = [&](const string &flag) {
        for (const string &a : args) {
            if (a == flag)
                return true;
        }
        return false;
    };
    string script;
    if (has("--version"))
        script = "version.txt";
    else if (has("--ismine"))
        script = "ismine.txt";
    else if (has("--games"))
        script = "games.txt";
    else if (has("--roms"))
        script = "roms.txt";
    else if (has("--ps1"))
        script = "ps1.txt";
    else if (has("--rom"))
        script = "rom.txt";

    ifstream in(script);
    if (!in.is_open()) {
        cout << "#Starting - helper" << endl;
        cout << "#DONE" << endl;
        return 0;
    }
    string line;
    while (getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.compare(0, 5, "!err ") == 0) {
            cerr << line.substr(5) << endl;
        } else if (line.compare(0, 7, "!sleep ") == 0) {
            this_thread::sleep_for(chrono::milliseconds(atoi(line.c_str() + 7)));
        } else if (line.compare(0, 6, "!exit ") == 0) {
            cout.flush();
            return atoi(line.c_str() + 6);
        } else if (line.compare(0, 7, "!write ") == 0) {
            string rest = line.substr(7);
            size_t sp = rest.find(' ');
            ofstream(replaceTarget(rest.substr(0, sp), target), ios::binary)
                << (sp == string::npos ? "" : rest.substr(sp + 1));
        } else if (line.compare(0, 6, "!move ") == 0) {
            string rest = line.substr(6);
            size_t sp = rest.find(' ');
            rename(replaceTarget(rest.substr(0, sp), target).c_str(),
                   replaceTarget(rest.substr(sp + 1), target).c_str());
        } else if (line.compare(0, 8, "!remove ") == 0) {
            remove(replaceTarget(line.substr(8), target).c_str());
        } else if (line.compare(0, 5, "!has ") == 0) {
            string suffix = line.substr(5);
            bool found = endsWith(target, suffix);
            for (const string &name : listDir(target))
                found = found || endsWith(name, suffix);
            if (!found)
                return 1;
        } else {
            cout << line << endl; // endl: flushed, so the launcher sees each line as it comes
        }
    }
    return 0;
}
