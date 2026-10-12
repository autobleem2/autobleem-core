//
// SbiService - see the header.
//
#include "sbi_service.h"
#include "../main.h"

#include <ableem/engine/log.h>

#include <fstream>

using namespace std;

namespace {

// a JSON string at text[pos] (pos on the opening quote); pos ends after the closing one
bool readString(const string &text, size_t &pos, string &out) {
    if (pos >= text.size() || text[pos] != '"') {
        return false;
    }
    out.clear();
    for (pos++; pos < text.size(); pos++) {
        char c = text[pos];
        if (c == '"') {
            pos++;
            return true;
        }
        if (c == '\\') {
            if (++pos >= text.size()) {
                return false;
            }
            c = text[pos];
            if (c == 'u') { // a file name or serial never needs it
                return false;
            }
            out += c == 'n' ? '\n' : c == 't' ? '\t' : c;
        } else {
            out += c;
        }
    }
    return false;
}

void skipSpace(const string &text, size_t &pos) {
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r')) {
        pos++;
    }
}

bool expect(const string &text, size_t &pos, char c) {
    skipSpace(text, pos);
    if (pos < text.size() && text[pos] == c) {
        pos++;
        return true;
    }
    return false;
}

// a file name from the index goes into a URL and nowhere else, but keep it plain anyway
bool plainName(const string &name) {
    return !name.empty() && name.find('/') == string::npos && name.find('\\') == string::npos && name[0] != '.';
}

} // namespace

//*******************************
// SbiService::cdromId / targetNames / describe
//*******************************
string SbiService::cdromId(const string &serial) {
    string id;
    for (char c : serial) {
        if (c != '-') {
            id += c;
        }
    }
    return id;
}

vector<string> SbiService::targetNames(const string &serial, size_t versions) {
    const string id = cdromId(serial);
    vector<string> names;
    if (versions == 1) {
        names.push_back(id + ".sbi");
    } else {
        for (size_t i = 1; i <= versions; i++) {
            names.push_back(id + "." + to_string(i) + ".sbi");
        }
    }
    return names;
}

const char *SbiService::describe(Result result) {
    switch (result) {
    case Result::Fetched:
        return "fetched";
    case Result::Present:
        return "present";
    case Result::NotLibCrypt:
        return "not a LibCrypt serial";
    case Result::Offline:
        return "offline";
    case Result::Error:
        break;
    }
    return "error";
}

//*******************************
// SbiService::parseIndex
//*******************************
bool SbiService::parseIndex(const string &text, map<string, vector<string>> &index) {
    index.clear();
    size_t pos = 0;
    if (!expect(text, pos, '{')) {
        return false;
    }
    if (expect(text, pos, '}')) {
        return true;
    }
    for (;;) {
        string serial;
        skipSpace(text, pos);
        if (!readString(text, pos, serial) || !expect(text, pos, ':') || !expect(text, pos, '[')) {
            return false;
        }
        vector<string> files;
        if (!expect(text, pos, ']')) {
            for (;;) {
                string file;
                skipSpace(text, pos);
                if (!readString(text, pos, file)) {
                    return false;
                }
                files.push_back(file);
                if (expect(text, pos, ',')) {
                    continue;
                }
                if (expect(text, pos, ']')) {
                    break;
                }
                return false;
            }
        }
        index[serial] = files;
        if (expect(text, pos, ',')) {
            continue;
        }
        return expect(text, pos, '}');
    }
}

//*******************************
// SbiService::looksLikeSbi
//*******************************
bool SbiService::looksLikeSbi(const string &path) {
    ifstream in(path, ios::binary);
    char head[4] = {0, 0, 0, 0};
    in.read(head, 4);
    return in.gcount() == 4 && head[0] == 'S' && head[1] == 'B' && head[2] == 'I' && head[3] == '\0';
}

//*******************************
// SbiService::loadIndex
//*******************************
SbiService::Index SbiService::loadIndex() {
    if (index_ != Index::Untried) {
        return index_; // once per run, whatever the answer was
    }
    index_ = Index::Offline;
    string base = siteUrl_;
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    if (base.empty() || !DirEntry::createDirs(scratchDir_)) {
        return index_;
    }
    DownloadRequest request;
    request.url = base + "/sbi/index.json";
    request.target = scratchDir_ + sep + "sbi-index.json";
    DirEntry::removeFile(request.target);
    string error;
    if (downloader_.fetch(request, error) != Downloader::Result::Downloaded) {
        PLOG_INFO << "SbiService: no index (" << error << ")";
        return index_;
    }
    string text;
    const bool read = DirEntry::readFile(request.target, text);
    DirEntry::removeFile(request.target);
    index_ = read && parseIndex(text, entries_) ? Index::Ok : Index::Bad;
    if (index_ == Index::Bad) {
        PLOG_WARNING << "SbiService: the index is not readable";
    }
    return index_;
}

//*******************************
// SbiService::fetchFor
//*******************************
SbiService::Result SbiService::fetchFor(const string &serial, const string &saveFolder) {
    try {
        if (serial.empty() || saveFolder.empty()) {
            return Result::NotLibCrypt;
        }
        switch (loadIndex()) {
        case Index::Offline:
            return Result::Offline;
        case Index::Bad:
            return Result::Error;
        default:
            break;
        }
        auto found = entries_.find(serial);
        if (found == entries_.end() || found->second.empty()) {
            return Result::NotLibCrypt;
        }
        const vector<string> &sources = found->second;
        const vector<string> names = targetNames(serial, sources.size());

        string base = siteUrl_;
        while (!base.empty() && base.back() == '/') {
            base.pop_back();
        }
        bool fetched = false;
        for (size_t i = 0; i < sources.size(); i++) {
            const string target = saveFolder + sep + names[i];
            if (DirEntry::exists(target) && looksLikeSbi(target)) {
                continue;
            }
            if (!plainName(sources[i])) {
                PLOG_WARNING << "SbiService: odd file name in the index: " << sources[i];
                return Result::Error;
            }
            if (!DirEntry::createDirs(saveFolder)) {
                return Result::Error;
            }
            DownloadRequest request;
            request.url = base + "/sbi/" + sources[i];
            request.target = target + ".dl";
            DirEntry::removeFile(request.target);
            string error;
            if (downloader_.fetch(request, error) != Downloader::Result::Downloaded) {
                PLOG_WARNING << "SbiService: " << sources[i] << ": " << error;
                return Result::Error;
            }
            if (!looksLikeSbi(request.target)) {
                PLOG_WARNING << "SbiService: " << sources[i] << " is not an .sbi - dropped";
                DirEntry::removeFile(request.target);
                return Result::Error;
            }
            if (!DirEntry::renameFile(request.target, target)) {
                DirEntry::removeFile(request.target);
                return Result::Error;
            }
            fetched = true;
        }
        return fetched ? Result::Fetched : Result::Present;
    } catch (const exception &e) {
        PLOG_WARNING << "SbiService: " << e.what();
        return Result::Error;
    }
}
