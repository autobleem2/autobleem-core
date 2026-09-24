//
// Publisher - see the header.
//
#include <ableem/lanserver/publisher.h>

#include <ableem/engine/filesystem.h>
#include <ableem/engine/log.h>

#include <algorithm>
#include <fstream>
#include <vector>

using namespace std;

namespace ableem {

namespace {

string lower(string s) {
    transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return s;
}

// one file copied, progress over the whole game
bool copyFile(const string &from, const string &to, uint64_t &done, uint64_t total,
              const function<bool(uint64_t, uint64_t)> &progress, string &error) {
    ifstream in(from, ios::binary);
    ofstream out(to, ios::binary | ios::trunc);
    if (!in || !out) {
        error = "cannot copy " + from + " to " + to;
        return false;
    }
    vector<char> chunk(1 << 20);
    while (in) {
        in.read(chunk.data(), static_cast<streamsize>(chunk.size()));
        const streamsize got = in.gcount();
        if (got <= 0)
            break;
        out.write(chunk.data(), got);
        if (!out) {
            error = "cannot write " + to + " - is the share full?";
            return false;
        }
        done += static_cast<uint64_t>(got);
        if (progress && !progress(done, total)) {
            error = "stopped";
            return false;
        }
    }
    return true;
}

} // namespace

//*******************************
// Publisher::publish
//*******************************
Publisher::Result Publisher::publish(const vector<File> &files, const string &gameFolder, const Target &target,
                                     const function<bool(uint64_t, uint64_t)> &progress) {
    Result r;
    if (files.empty()) {
        r.error = "nothing to publish";
        return r;
    }
    uint64_t total = 0;
    for (const File &f : files) {
        const long long size = DirEntry::fileSize(f.localPath);
        if (size < 0) {
            r.error = "cannot read " + f.localPath;
            return r;
        }
        total += static_cast<uint64_t>(size);
    }
    uint64_t done = 0;

    if (!target.shareDir.empty() && DirEntry::isDirectory(target.shareDir)) {
        r.viaShare = true;
        const string staged = target.shareDir + sep + ".uploading" + sep + gameFolder;
        DirEntry::removeDirAndContents(staged); // an earlier copy that did not finish
        if (!DirEntry::createDirs(staged)) {
            r.error = "cannot write to the share " + target.shareDir;
            return r;
        }
        for (const File &f : files)
            if (!copyFile(f.localPath, staged + sep + f.name, done, total, progress, r.error)) {
                DirEntry::removeDirAndContents(staged);
                return r;
            }
        r.folder = gameFolder;
        string dest = target.shareDir + sep + gameFolder;
        for (int n = 2; DirEntry::exists(dest); n++) {
            r.folder = gameFolder + " (" + to_string(n) + ")";
            dest = target.shareDir + sep + r.folder;
        }
        if (!DirEntry::renameFile(staged, dest)) {
            DirEntry::removeDirAndContents(staged);
            r.error = "cannot move the game into place on the share";
            return r;
        }
        string ignored;
        if (target.client != nullptr)
            target.client->rescan(ignored); // it would notice by itself within 10 s anyway
        r.ok = true;
        PLOG_INFO << "published " << r.folder << " to the share " << target.shareDir;
        return r;
    }

    if (target.client == nullptr || !target.client->valid()) {
        r.error = "no server to publish to";
        return r;
    }
    for (const File &f : files) {
        const uint64_t before = done;
        const long long size = DirEntry::fileSize(f.localPath);
        if (!target.client->upload(
                gameFolder, f.localPath, f.name, target.library,
                [&](uint64_t sent, uint64_t) { return !progress || progress(before + sent, total); }, r.error))
            return r;
        done = before + static_cast<uint64_t>(size);
    }
    if (!target.client->commit(gameFolder, target.library, r.folder, r.error))
        return r;
    r.ok = true;
    PLOG_INFO << "published " << r.folder << " to " << target.client->baseUrl();
    return r;
}

//*******************************
// Publisher::remove
//*******************************
bool Publisher::remove(const string &gameId, const Target &target, string &error) {
    if (!target.shareDir.empty() && DirEntry::isDirectory(target.shareDir)) {
        const string source = target.shareDir + sep + gameId;
        if (!DirEntry::isDirectory(source)) {
            error = gameId + " is not on the share";
            return false;
        }
        const size_t slash = gameId.find_last_of('/');
        const string name = slash == string::npos ? gameId : gameId.substr(slash + 1);
        const string removed = target.shareDir + sep + ".removed";
        DirEntry::createDirs(removed);
        string dest = removed + sep + name;
        for (int n = 2; DirEntry::exists(dest); n++)
            dest = removed + sep + name + " (" + to_string(n) + ")";
        if (!DirEntry::renameFile(source, dest)) {
            error = "cannot move " + gameId + " on the share";
            return false;
        }
        string ignored;
        if (target.client != nullptr)
            target.client->rescan(ignored);
        return true;
    }
    if (target.client == nullptr || !target.client->valid()) {
        error = "no server";
        return false;
    }
    return target.client->remove(gameId, error);
}

//*******************************
// Publisher::filesOf / serverHas / folderNameFor
//*******************************
vector<Publisher::File> Publisher::filesOf(const LanGame &game, const LanLibrary &library) {
    vector<File> out;
    for (const LanFile &f : game.files)
        out.push_back({library.absolutePath(f.relPath), f.name});
    if (!game.coverFile.empty())
        out.push_back({game.coverFile, DirEntry::getFileNameFromPath(game.coverFile)});
    return out;
}

bool Publisher::serverHas(const LanClient::Status &status, const string &serial, const string &title) {
    for (const LanClient::Game &g : status.games) {
        if (!serial.empty() && !g.serial.empty()) {
            if (lower(g.serial) == lower(serial))
                return true;
        } else if (!title.empty() && lower(g.title) == lower(title)) {
            return true;
        }
    }
    return false;
}

string Publisher::folderNameFor(const string &title) {
    string out;
    for (char c : title) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 32 || string("<>:\"/\\|?*").find(c) != string::npos)
            out += (c == ':' ? " -" : "");
        else
            out += c;
    }
    while (!out.empty() && (out.front() == ' ' || out.front() == '.'))
        out.erase(0, 1);
    while (!out.empty() && (out.back() == '.' || out.back() == ' '))
        out.pop_back();
    return out.empty() ? "Game" : out;
}

} // namespace ableem
