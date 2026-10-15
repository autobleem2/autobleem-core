#include "installer/install_job_base.h"

#include <ableem/engine/filesystem.h>
#include <ableem/engine/ini_file.h>
#include <ableem/engine/log.h>
#include <ableem/engine/sha256.h>
#include <ableem/engine/strings.h>

#include <fstream>
#include <memory>
#include <sstream>

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
bool InstallJobBase::download(const string &url, const string &dest, string &error) {
    DirEntry::createDirs(dest.substr(0, dest.find_last_of('/')));
    bool ok = dl.fetch(
        url, dest,
        [this](uint64_t done, uint64_t total) {
            // countOnly: the phase counts packages, not bytes - the finished ones are reported by the caller
            if (!countOnly && progressTotal > 0) // one bar over several files: this file's bytes after the others
                out.onProgress(progressBase + done, progressTotal);
            else if (!countOnly)
                out.onProgress(done, total ? total : expectTotal);
            return !(stop && stop());
        },
        error);
    if (!ok && stop && stop())
        error = "Stopped";
    else if (!ok)
        classifyFailure(error);
    return ok;
}

void InstallJobBase::classifyFailure(const string &error) {
    failKind = error.rfind(Downloader::ErrNoNetwork, 0) == 0   ? FailKind::NoNetwork
               : error.rfind(Downloader::ErrChecksum, 0) == 0 ? FailKind::Checksum
                                                              : FailKind::Download;
}

//*******************************
// InstallJobBase::downloadVerified
//*******************************
bool InstallJobBase::downloadVerified(const UpdateFile &file, const string &dest, string &error) {
    out.onItem(file.name, 1, 1, "bytes");
    if (DirEntry::exists(dest) && !file.sha256.empty() && Sha256::ofFile(dest) == file.sha256) {
        say("  " + file.name + " is already there");
        out.onProgress(file.size ? file.size : 1, file.size ? file.size : 1);
        return true;
    }
    say("  " + file.name + (file.size ? " (" + humanSize(file.size) + ")" : ""));
    if (file.size)
        out.onProgress(0, file.size);
    // a downloader that checks the file keeps its own <dest>.part (and continues it); any other gets ours
    const string part = dest + ".part";
    const bool checkedByDownloader = dl.expect(file.size, file.sha256);
    expectTotal = file.size;
    const bool fetched = download(file.url, checkedByDownloader ? dest : part, error);
    expectTotal = 0;
    dl.expect(0, "");
    if (!fetched)
        return false;
    if (file.size)
        out.onProgress(file.size, file.size);
    if (checkedByDownloader)
        return true;
    if (!file.sha256.empty() && Sha256::ofFile(part) != file.sha256) {
        DirEntry::removeFile(part);
        error = file.name + ": the download does not match its sha256";
        failKind = FailKind::Checksum;
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
// InstallJobBase::fetchCatalog / untar
//*******************************
bool InstallJobBase::fetchCatalog(const string &rel, string &text, string &error) {
    return dl.fetchText(repoUrl + "/" + rel, scratch + "/catalog.json", text, error);
}

bool InstallJobBase::untar(const string &tarball, const string &dest, string &error, const TarArchive::Filter &filter,
                           const string &prefix) {
    // the extractor ends with a (1, 1); the bar of a package is its bytes read, so the end is "all of them"
    auto lastTotal = make_shared<uint64_t>(0);
    return TarArchive::extract(
        tarball, dest, error, filter,
        [this, lastTotal](uint64_t done, uint64_t total) {
            if (done == 1 && total == 1 && *lastTotal > 1) {
                done = total = *lastTotal;
            } else if (total > 0) {
                *lastTotal = total;
            }
            out.onProgress(done, total);
        },
        prefix);
}

//*******************************
// InstallJobBase::fetchBiosPack
//*******************************
bool InstallJobBase::fetchBiosPack(const string &catalogRel, const string &dir, string &error) {
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
    struct Item {
        string sha, url, path;
        uint64_t size;
    };
    vector<Item> items;
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
        Item it;
        it.sha = line.substr(0, a);
        it.size = strtoull(line.substr(a + 1, b - a - 1).c_str(), nullptr, 10);
        it.url = line.substr(b + 1, c - b - 1);
        it.path = line.substr(c + 1);
        // the PlayStation emulator's names under System/Bios and the OpenBIOS are never the pack's to give
        if (it.path == "romw.bin" || it.path == "romJP.bin" || it.path == "openbios.bin")
            continue;
        if (TarArchive::isSafeName(it.path))
            items.push_back(it);
    }
    say("  " + to_string(items.size()) + " files, " + humanSize(cat.totalBytes) + " - what is there already is kept");
    int fetched = 0, kept = 0, failed = 0;
    for (size_t i = 0; i < items.size(); i++) {
        if (stopped(error))
            return false;
        const Item &it = items[i];
        out.onItem(it.path, static_cast<int>(i + 1), static_cast<int>(items.size()), "files");
        out.onProgress(i, items.size());
        const string dest = dir + "/" + it.path;
        if (DirEntry::exists(dest) && static_cast<uint64_t>(DirEntry::fileSize(dest)) == it.size &&
            Sha256::ofFile(dest) == it.sha) {
            kept++;
            continue;
        }
        DirEntry::createDirs(dest.substr(0, dest.find_last_of('/')));
        const string part = dest + ".part";
        string why;
        const bool checkedByDownloader = dl.expect(it.size, it.sha);
        bool ok = dl.fetch(it.url, checkedByDownloader ? dest : part,
                           [this](uint64_t, uint64_t) { return !(stop && stop()); }, why);
        dl.expect(0, "");
        bool mismatch = !ok && why.rfind(Downloader::ErrChecksum, 0) == 0;
        if (ok && !checkedByDownloader && Sha256::ofFile(part) != it.sha) {
            ok = false;
            mismatch = true;
            why = "checksum mismatch";
        }
        if (ok && !checkedByDownloader) {
            DirEntry::removeFile(dest);
            ok = DirEntry::renameFile(part, dest);
        }
        if (ok) {
            fetched++;
        } else {
            if (mismatch) // a part that failed its check is no use to a resume; any other part is kept for one
                DirEntry::removeFile(part);
            failed++;
            if (failed <= 20)
                say("  could not fetch " + it.path + ": " + why);
            if (stop && stop()) {
                error = "Stopped";
                return false;
            }
        }
        if ((fetched + failed) % 50 == 0 && fetched + failed > 0)
            say("  " + to_string(fetched) + " fetched, " + to_string(kept) + " kept, " + to_string(failed) +
                " failed so far");
    }
    if (items.empty())
        out.onItem("", 0, 0, "files");
    out.onProgress(items.empty() ? 1 : items.size(), items.empty() ? 1 : items.size());
    say("  " + to_string(fetched) + " fetched, " + to_string(kept) + " already there, " + to_string(failed) +
        " failed");
    return true;
}

//*******************************
// InstallJobBase::installPs1Bios
//*******************************
void InstallJobBase::installPs1Bios(const string &biosDir, const string &openBios) {
    DirEntry::createDirs(biosDir);
    for (const char *name : {"romw.bin", "romJP.bin"}) {
        const string dest = biosDir + "/" + name;
        if (DirEntry::exists(dest)) {
            say(string("  keeping the existing ") + name);
        } else if (!openBios.empty() && DirEntry::exists(openBios)) {
            if (DirEntry::copyFile(openBios, dest))
                say(string("  PlayStation BIOS: OpenBIOS -> System/Bios/") + name);
            else
                say(string("  could not copy OpenBIOS to ") + dest);
        } else {
            say(string("  no ") + name + " - the PlayStation emulator will use its built-in BIOS");
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
