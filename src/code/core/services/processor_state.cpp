#include "processor_state.h"
#include "../main.h"

#include <ableem/engine/md5.h>

#include <fstream>
#include <sstream>

using namespace std;

namespace {

void walk(const string &path, const string &rel, map<string, long long> &out) {
    for (const DirEntry &entry : DirEntry::diru(path)) {
        if (ProcessorState::ignoredName(entry.name))
            continue;
        string childRel = rel.empty() ? entry.name : rel + "/" + entry.name;
        string childPath = path + sep + entry.name;
        if (entry.isDir) {
            if (entry.name == ableem::SAVESTATES_DIR_NAME || entry.name == ableem::MEMCARDS_DIR_NAME)
                continue;
            walk(childPath, childRel, out);
        } else {
            out[childRel] = DirEntry::fileSize(childPath);
        }
    }
}

} // namespace

//*******************************
// ProcessorState::ignoredName
//*******************************
bool ProcessorState::ignoredName(const string &name) {
    if (name.empty() || name[0] == '.')
        return true;
    string l = name;
    lcase(l);
    if (l == "game.ini" || l == "pcsx.cfg")
        return true;
    auto endsWith = [&l](const string &suffix) {
        return l.size() >= suffix.size() && l.compare(l.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    return endsWith(".part") || endsWith(".m3u");
}

//*******************************
// ProcessorState::files
//*******************************
map<string, long long> ProcessorState::files(const string &dir) {
    map<string, long long> out;
    walk(DirEntry::removeSeparatorFromEndOfPath(dir), "", out);
    return out;
}

//*******************************
// ProcessorState::digest
//*******************************
string ProcessorState::digest(const string &path) {
    string text;
    if (DirEntry::isDirectory(path)) {
        for (const auto &f : files(path))
            text += f.first + "\t" + to_string(f.second) + "\n";
    } else if (DirEntry::exists(path)) {
        text = DirEntry::getFileNameFromPath(path) + "\t" + to_string(DirEntry::fileSize(path)) + "\n";
    }
    return ableem::Md5::ofString(text);
}

//*******************************
// ProcessorState::digestOfOwnFiles
//*******************************
string ProcessorState::digestOfOwnFiles(const string &dir) {
    string text;
    map<string, long long> own;
    for (const DirEntry &entry : DirEntry::diru_FilesOnly(dir)) {
        if (!ignoredName(entry.name))
            own[entry.name] = DirEntry::fileSize(dir + sep + entry.name);
    }
    for (const auto &f : own)
        text += f.first + "\t" + to_string(f.second) + "\n";
    return ableem::Md5::ofString(text);
}

//*******************************
// ProcessorState::resultName
//*******************************
const char *ProcessorState::resultName(ProcessorResult result) {
    switch (result) {
    case ProcessorResult::Ok:
        return "ok";
    case ProcessorResult::Failed:
        return "failed";
    case ProcessorResult::Interrupted:
        return "interrupted";
    }
    return "";
}

//*******************************
// ProcessorState::key
//*******************************
string ProcessorState::key(const string &processor, const string &kind, const string &target) {
    return processor + "\t" + kind + "\t" + target;
}

//*******************************
// ProcessorState::load
//*******************************
bool ProcessorState::load() {
    entries_.clear();
    ifstream in(file_, ios::binary);
    if (!in.is_open())
        return true;
    string line;
    while (getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        vector<string> f;
        stringstream ss(line);
        string field;
        while (getline(ss, field, '\t'))
            f.push_back(field);
        if (line.size() && line.back() == '\t')
            f.emplace_back();
        if (f.size() != 6)
            continue;
        Line l;
        l.version = f[1];
        l.digest = f[4];
        l.result = f[5] == "ok"       ? ProcessorResult::Ok
                   : f[5] == "failed" ? ProcessorResult::Failed
                                      : ProcessorResult::Interrupted;
        entries_[key(f[0], f[2], f[3])] = l;
    }
    return true;
}

//*******************************
// ProcessorState::save
//*******************************
bool ProcessorState::save() const {
    ofstream out(file_, ios::binary);
    if (!DirEntry::checkWritable(out, file_))
        return false;
    for (const auto &e : entries_) {
        // the key is processor \t kind \t target already
        size_t t1 = e.first.find('\t');
        out << e.first.substr(0, t1) << "\t" << e.second.version << "\t" << e.first.substr(t1 + 1) << "\t"
            << e.second.digest << "\t" << resultName(e.second.result) << "\n";
    }
    out.close();
    return !out.fail();
}

//*******************************
// ProcessorState::isSettled
//*******************************
bool ProcessorState::isSettled(const string &processor, const string &version, const string &kind, const string &target,
                               const string &digest) const {
    auto it = entries_.find(key(processor, kind, target));
    if (it == entries_.end())
        return false;
    const Line &l = it->second;
    return l.version == version && l.digest == digest && l.result != ProcessorResult::Interrupted;
}

//*******************************
// ProcessorState::record
//*******************************
void ProcessorState::record(const string &processor, const string &version, const string &kind, const string &target,
                            const string &digest, ProcessorResult result) {
    Line l;
    l.version = version;
    l.digest = digest;
    l.result = result;
    entries_[key(processor, kind, target)] = l;
}

//*******************************
// ProcessorState::forget
//*******************************
void ProcessorState::forget(const string &processor) {
    string prefix = processor + "\t";
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->first.compare(0, prefix.size(), prefix) == 0)
            it = entries_.erase(it);
        else
            ++it;
    }
}
