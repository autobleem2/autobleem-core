#include "installer/install_job_base.h"

#include <ableem/engine/filesystem.h>
#include <ableem/engine/ini_file.h>
#include <ableem/engine/log.h>
#include <ableem/engine/sha256.h>
#include <ableem/engine/strings.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>

using namespace std;
using ableem::DirEntry;
using ableem::PackCatalog;
using ableem::Sha256;
using ableem::TarArchive;
using ableem::UpdateFile;

string readText(const string &path) {
    ifstream in(path, ios::binary);
    if (!in)
        return "";
    stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool setIniValue(const string &path, const string &key, const string &value) {
    ableem::IniFile ini;
    if (DirEntry::exists(path))
        ini.load(path);
    if (ini.section.empty())
        ini.section = "General";
    ini.values[key] = value;
    ini.save(path);
    return DirEntry::exists(path);
}

bool writeText(const string &path, const string &text) {
    ofstream out(path, ios::binary | ios::trunc);
    out << text;
    return static_cast<bool>(out);
}

string trimmed(const string &s) {
    return ableem::Strings::trim(s);
}

string firstLine(const string &text) {
    size_t nl = text.find_first_of("\r\n");
    return trimmed(nl == string::npos ? text : text.substr(0, nl));
}

string sidecarHash(const string &text) {
    string line = firstLine(text);
    size_t sp = line.find(' ');
    return sp == string::npos ? line : line.substr(0, sp);
}

string humanSize(uint64_t bytes) {
    char buf[32];
    if (bytes >= 1024ull * 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1f GB", bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024ull * 1024)
        snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024));
    else
        snprintf(buf, sizeof(buf), "%.0f KB", bytes / 1024.0);
    return buf;
}

//*******************************
// InstallJobBase::stopped / phase / say
//*******************************
bool InstallJobBase::stopped(string &error) {
    if (stop && stop()) {
        error = "Stopped";
        return true;
    }
    return false;
}

void InstallJobBase::phase(const string &title) {
    phaseIndex++;
    out.onPhase(phaseIndex, static_cast<int>(phases.size()), title);
    out.onProgress(0, 0);
    say("== " + title);
}

void InstallJobBase::say(const string &line) {
    lock_guard<mutex> lock(sayMutex);
    out.onLine(line);
    PLOG_INFO << line;
    if (!logPath.empty()) {
        ofstream log(logPath, ios::binary | ios::app);
        log << line << "\n";
    }
}

//*******************************
// InstallJobBase::download
//*******************************
Downloader::Progress InstallJobBase::barProgress() {
    return [this](uint64_t done, uint64_t total) {
        out.onProgress(done, total);
        return !(stop && stop());
    };
}

bool InstallJobBase::download(const string &url, const string &dest, string &error) {
    DirEntry::createDirs(dest.substr(0, dest.find_last_of('/')));
    bool ok = dl.fetch(url, dest, barProgress(), error);
    if (!ok && stop && stop())
        error = "Stopped";
    return ok;
}

//*******************************
// InstallJobBase::downloadVerified
//*******************************
bool InstallJobBase::downloadVerified(const UpdateFile &file, const string &dest, string &error) {
    if (DirEntry::exists(dest) && !file.sha256.empty() && Sha256::ofFile(dest) == file.sha256) {
        say("  " + file.name + " is already there");
        return true;
    }
    say("  " + file.name + (file.size ? " (" + humanSize(file.size) + ")" : ""));
    const string part = dest + ".part";
    // a file the downloader holds and has checked (a bundle): copied as it is, not hashed a second time
    string held, why;
    if (dl.localFile(file.url, held, barProgress(), why)) {
        DirEntry::createDirs(dest.substr(0, dest.find_last_of('/')));
        DirEntry::removeFile(part);
        if (!DirEntry::copyFile(held, part)) {
            error = "cannot write " + dest;
            return false;
        }
        DirEntry::removeFile(dest);
        if (!DirEntry::renameFile(part, dest)) {
            error = "cannot write " + dest;
            return false;
        }
        return true;
    }
    if (!why.empty()) {
        error = why;
        return false;
    }
    if (!download(file.url, part, error))
        return false;
    if (!file.sha256.empty() && Sha256::ofFile(part) != file.sha256) {
        DirEntry::removeFile(part);
        error = file.name + ": the download does not match its sha256";
        return false;
    }
    DirEntry::removeFile(dest);
    if (!DirEntry::renameFile(part, dest)) {
        error = "cannot write " + dest;
        return false;
    }
    return true;
}

//*******************************
// InstallJobBase::obtain / discard
//*******************************
bool InstallJobBase::obtain(const UpdateFile &file, string &path, string &error) {
    string why;
    if (dl.localFile(file.url, path, barProgress(), why)) {
        borrowed.insert(path);
        say("  " + file.name + ": in this download, checked against its manifest");
        return true;
    }
    if (!why.empty()) {
        error = why;
        return false;
    }
    path = scratch + "/" + file.name;
    return downloadVerified(file, path, error);
}

void InstallJobBase::discard(const string &path) {
    if (!borrowed.count(path))
        DirEntry::removeFile(path);
}

//*******************************
// InstallJobBase::fetchCatalog / untar
//*******************************
bool InstallJobBase::fetchCatalog(const string &rel, string &text, string &error) {
    return dl.fetchText(repoUrl + "/" + rel, scratch + "/catalog.json", text, error);
}

bool InstallJobBase::untar(const string &tarball, const string &dest, string &error, const TarArchive::Filter &filter,
                           const string &prefix) {
    return TarArchive::extract(
        tarball, dest, error, filter, [this](uint64_t done, uint64_t total) { out.onProgress(done, total); }, prefix);
}

//*******************************
// InstallJobBase::fetchBiosPack
//*******************************
const char *const InstallJobBase::BiosRecord = ".biospack-verified";

namespace {

struct BiosItem {
    string sha, url, path;
    uint64_t size;
};

// what an earlier run verified: "<sha256> <size> <path>" per line, the path may hold spaces
map<string, BiosItem> loadBiosRecord(const string &file) {
    map<string, BiosItem> record;
    istringstream in(readText(file));
    string line;
    while (getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        size_t a = line.find(' '), b = a == string::npos ? string::npos : line.find(' ', a + 1);
        if (b == string::npos)
            continue;
        BiosItem it;
        it.sha = line.substr(0, a);
        it.size = strtoull(line.substr(a + 1, b - a - 1).c_str(), nullptr, 10);
        it.path = line.substr(b + 1);
        record[it.path] = it;
    }
    return record;
}

enum class BiosResult { Kept, Fetched, Failed, Stopped };

} // namespace

bool InstallJobBase::fetchBiosPack(const string &catalogRel, const string &dir, string &error, const BiosFilter &only) {
    string text;
    PackCatalog cat;
    if (!fetchCatalog(catalogRel, text, error) || !cat.parse(text)) {
        if (error.empty())
            error = catalogRel + " is not what was expected";
        return false;
    }
    string list;
    if (!dl.fetchText(cat.file.url, scratch + "/biospack.txt", list, error))
        return false;
    vector<BiosItem> items;
    istringstream in(list);
    string line;
    while (getline(in, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        // <sha256> <size> <url> <path> - the path may contain spaces
        size_t a = line.find(' '), b = line.find(' ', a + 1), c = line.find(' ', b + 1);
        if (a == string::npos || b == string::npos || c == string::npos)
            continue;
        BiosItem it;
        it.sha = line.substr(0, a);
        it.size = strtoull(line.substr(a + 1, b - a - 1).c_str(), nullptr, 10);
        it.url = line.substr(b + 1, c - b - 1);
        it.path = line.substr(c + 1);
        if (TarArchive::isSafeName(it.path) && (!only || only(it.path)))
            items.push_back(it);
    }
    if (only)
        say("  " + to_string(items.size()) + " files - what is there already is kept");
    else
        say("  " + to_string(items.size()) + " files, " + humanSize(cat.totalBytes) +
            " - what is there already is kept");

    DirEntry::createDirs(dir);
    const string recordFile = dir + "/" + BiosRecord;
    map<string, BiosItem> record = loadBiosRecord(recordFile);
    mutex recordMutex;
    atomic<size_t> next(0);
    atomic<int> fetched(0), kept(0), failed(0), finished(0);
    atomic<bool> halted(false);

    // one file: kept when it is there (the record, or its sha256), else fetched to .part - continued when an
    // earlier run left one - checked and renamed over the target
    auto one = [&](const BiosItem &it, string &why) {
        const string dest = dir + "/" + it.path;
        const long long have = DirEntry::fileSize(dest);
        if (have >= 0 && static_cast<uint64_t>(have) == it.size) {
            bool trusted = false;
            {
                lock_guard<mutex> lock(recordMutex);
                auto r = record.find(it.path);
                trusted = r != record.end() && r->second.sha == it.sha && r->second.size == it.size;
            }
            if (trusted || Sha256::ofFile(dest) == it.sha)
                return BiosResult::Kept;
        }
        DirEntry::createDirs(dest.substr(0, dest.find_last_of('/')));
        const string part = dest + ".part";
        const Downloader::Progress alive = [this](uint64_t, uint64_t) { return !(stop && stop()); };
        for (int attempt = 0; attempt < 2; attempt++) {
            const long long partial = DirEntry::fileSize(part);
            const bool resumed = attempt == 0 && partial > 0 && static_cast<uint64_t>(partial) < it.size;
            if (!resumed)
                DirEntry::removeFile(part);
            if (!dl.fetchResumable(it.url, part, alive, why)) {
                if (stop && stop())
                    return BiosResult::Stopped;
                return BiosResult::Failed; // what was written stays: the next run continues there
            }
            if (Sha256::ofFile(part) == it.sha) {
                DirEntry::removeFile(dest);
                if (DirEntry::renameFile(part, dest))
                    return BiosResult::Fetched;
                why = "cannot write " + dest;
                return BiosResult::Failed;
            }
            DirEntry::removeFile(part);
            why = "checksum mismatch";
            if (!resumed)
                break;
        }
        return BiosResult::Failed;
    };
    auto worker = [&]() {
        while (!halted) {
            const size_t i = next++;
            if (i >= items.size())
                return;
            if (stop && stop()) {
                halted = true;
                return;
            }
            const BiosItem &it = items[i];
            string why;
            const BiosResult result = one(it, why);
            if (result == BiosResult::Stopped) {
                halted = true;
                return;
            }
            if (result == BiosResult::Failed) {
                const int n = ++failed;
                {
                    lock_guard<mutex> lock(recordMutex);
                    record.erase(it.path);
                }
                if (n <= 20)
                    say("  could not fetch " + it.path + ": " + why);
            } else {
                if (result == BiosResult::Fetched)
                    ++fetched;
                else
                    ++kept;
                lock_guard<mutex> lock(recordMutex);
                record[it.path] = it;
            }
            const int done = ++finished;
            {
                lock_guard<mutex> lock(sayMutex);
                out.onProgress(static_cast<uint64_t>(done), items.size());
            }
            if (result != BiosResult::Kept && (fetched + failed) % 50 == 0)
                say("  " + to_string(fetched) + " fetched, " + to_string(kept) + " kept, " + to_string(failed) +
                    " failed so far");
        }
    };
    const int connections = BiosConnections;
    const int count = max(1, min(min(connections, dl.connections()), static_cast<int>(items.size())));
    if (count == 1) {
        worker();
    } else {
        vector<thread> pool;
        for (int i = 0; i < count; i++)
            pool.emplace_back(worker);
        for (thread &t : pool)
            t.join();
    }
    if (halted) {
        error = "Stopped";
        return false;
    }
    {
        // the files of this pass that checked out, with those of earlier passes the pass did not look at
        string recordText;
        for (const auto &r : record)
            recordText += r.second.sha + " " + to_string(r.second.size) + " " + r.second.path + "\n";
        writeText(recordFile, recordText);
    }
    out.onProgress(items.size(), items.size());
    say("  " + to_string(fetched) + " fetched, " + to_string(kept) + " already there, " + to_string(failed) +
        " failed");
    return true;
}

//*******************************
// InstallJobBase::isPs1BiosFile
//*******************************
bool InstallJobBase::isPs1BiosFile(const string &path) {
    return path == "scph5501.bin" || path == "scph5500.bin";
}

//*******************************
// InstallJobBase::installPs1Bios
//*******************************
void InstallJobBase::installPs1Bios(const string &systemDir, const string &biosDir) {
    DirEntry::createDirs(biosDir);
    for (const auto &pair : {make_pair("romw.bin", "scph5501.bin"), make_pair("romJP.bin", "scph5500.bin")}) {
        const string dest = biosDir + "/" + pair.first, src = systemDir + "/" + pair.second;
        if (DirEntry::exists(dest)) {
            say(string("  keeping the existing ") + pair.first);
        } else if (!DirEntry::exists(src)) {
            say(string("  no ") + pair.second + " - the PlayStation emulator has no " + pair.first +
                " and will use its built-in BIOS");
        } else if (DirEntry::copyFile(src, dest)) {
            say(string("  PlayStation BIOS: ") + pair.second + " -> System/Bios/" + pair.first);
        } else {
            say(string("  could not copy ") + pair.second + " to " + dest);
        }
    }
}

//*******************************
// InstallJobBase::createRomFolders
//*******************************
void InstallJobBase::createRomFolders(const string &listFile, const string &romsDir) {
    const string text = readText(listFile);
    if (text.empty()) {
        say("  (no roms_systems.cfg - the per-system folders were not created)");
        return;
    }
    int made = 0, there = 0;
    istringstream in(text);
    string line;
    while (getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        const size_t start = line.find_first_not_of(" \t");
        if (start == string::npos || line[start] == '#')
            continue;
        const string dir = romsDir + "/" + line.substr(start);
        if (DirEntry::isDirectory(dir))
            there++;
        else if (DirEntry::createDirs(dir))
            made++;
    }
    say("  roms/: " + to_string(made) + " system folder(s) made, " + to_string(there) + " already there");
}
