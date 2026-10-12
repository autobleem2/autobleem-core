//
// ImageCdDrive - see the header.
//
#include "image_cd_drive.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <set>

using namespace std;

namespace {

const uint32_t LeadIn = 150; // an MSF address is LBA + 150
const uint32_t SectorSize = DiscReader::SectorSize;

// the line's words; a "quoted name" is one word
vector<string> words(const string &line) {
    vector<string> out;
    size_t i = 0;
    while (i < line.size()) {
        if (isspace(static_cast<unsigned char>(line[i]))) {
            i++;
        } else if (line[i] == '"') {
            const size_t end = line.find('"', i + 1);
            if (end == string::npos) {
                out.push_back("\"" + line.substr(i + 1)); // an unterminated quote: left for the caller to refuse
                break;
            }
            out.push_back(line.substr(i + 1, end - i - 1));
            i = end + 1;
        } else {
            size_t end = i;
            while (end < line.size() && !isspace(static_cast<unsigned char>(line[end])))
                end++;
            out.push_back(line.substr(i, end - i));
            i = end;
        }
    }
    return out;
}

bool parseMsf(const string &text, uint32_t &frames) {
    unsigned m = 0, s = 0, f = 0;
    char tail = 0;
    if (sscanf(text.c_str(), "%u:%u:%u%c", &m, &s, &f, &tail) != 3 || s >= 60 || f >= 75 || m > 99)
        return false;
    frames = (m * 60 + s) * 75 + f;
    return true;
}

bool parseNumber(const string &text, int &value) {
    if (text.empty() || text.size() > 3)
        return false;
    for (char c : text)
        if (!isdigit(static_cast<unsigned char>(c)))
            return false;
    value = stoi(text);
    return value >= 1 && value <= 99;
}

string upper(string s) {
    for (char &c : s)
        c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return s;
}

string dirOf(const string &path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == string::npos ? "" : path.substr(0, slash + 1);
}

bool fileExists(const string &path) {
    ifstream in(path, ios::binary);
    return static_cast<bool>(in);
}

uint8_t fromBcd(uint8_t value) {
    return static_cast<uint8_t>((value >> 4) * 10 + (value & 0x0f));
}

struct ParsedTrack {
    int number = 0;
    bool audio = false;
    size_t file = 0;
    bool has00 = false, has01 = false;
    uint32_t i00 = 0, i01 = 0; // in the file
    uint32_t silence = 0;      // PREGAP: sectors before the track that no file has
};

} // namespace

//*******************************
// ImageCdDrive::open
//*******************************
bool ImageCdDrive::open(const string &cuePath, string &error, const string &sbiPath) {
    toc_ = CdToc();
    files_.clear();
    segments_.clear();
    sbi_.clear();

    ifstream cueIn(cuePath, ios::binary);
    if (!cueIn) {
        error = "the cue sheet cannot be opened: " + cuePath;
        return false;
    }

    // ---- the cue's lines ----
    vector<ParsedTrack> tracks;
    const string dir = dirOf(cuePath);
    static const set<string> ignored = {"REM",    "CATALOG",  "CDTEXTFILE", "FLAGS",
                                        "ISRC",   "PERFORMER", "SONGWRITER", "TITLE", "POSTGAP"};
    string line;
    int lineNo = 0;
    bool haveFile = false;
    while (getline(cueIn, line)) {
        lineNo++;
        const vector<string> w = words(line);
        if (w.empty())
            continue;
        const string bad = "bad line " + to_string(lineNo) + " in the cue sheet: " + line;
        const string key = upper(w[0]);
        if (ignored.count(key) != 0) {
            continue;
        } else if (key == "FILE") {
            if (w.size() != 3 || w[1].empty() || w[1][0] == '"') {
                error = bad;
                return false;
            }
            if (upper(w[2]) != "BINARY") {
                error = "only BINARY files are supported (line " + to_string(lineNo) + "): " + line;
                return false;
            }
            File f;
            f.path = dir + w[1];
            f.stream.reset(new ifstream(f.path, ios::binary));
            if (!*f.stream) {
                error = "the image file is missing: " + f.path;
                return false;
            }
            f.stream->seekg(0, ios::end);
            const streamoff size = f.stream->tellg();
            f.stream->seekg(0, ios::beg);
            if (size <= 0 || size % SectorSize != 0) {
                error = "the image file is not a whole number of 2352-byte sectors: " + f.path;
                return false;
            }
            f.sectors = static_cast<uint32_t>(size / SectorSize);
            files_.push_back(std::move(f));
            haveFile = true;
        } else if (key == "TRACK") {
            ParsedTrack t;
            const string mode = w.size() == 3 ? upper(w[2]) : "";
            if (!haveFile || w.size() != 3 || !parseNumber(w[1], t.number) ||
                (mode != "AUDIO" && mode != "MODE1/2352" && mode != "MODE2/2352")) {
                error = bad;
                return false;
            }
            t.audio = mode == "AUDIO";
            t.file = files_.size() - 1;
            tracks.push_back(t);
        } else if (key == "INDEX") {
            uint32_t frames = 0;
            if (tracks.empty() || w.size() != 3 || w[1].size() != 2 || !isdigit(static_cast<unsigned char>(w[1][0])) ||
                !isdigit(static_cast<unsigned char>(w[1][1])) || !parseMsf(w[2], frames)) {
                error = bad;
                return false;
            }
            const int number = stoi(w[1]);
            ParsedTrack &t = tracks.back();
            if (number == 0 && !t.has00) {
                t.has00 = true;
                t.i00 = frames;
            } else if (number == 1 && !t.has01) {
                t.has01 = true;
                t.i01 = frames;
            } else if (number > 1) {
                // sub-indexes inside a track do not matter here
            } else {
                error = bad;
                return false;
            }
        } else if (key == "PREGAP") {
            uint32_t frames = 0;
            if (tracks.empty() || w.size() != 2 || !parseMsf(w[1], frames) || tracks.back().has01) {
                error = bad;
                return false;
            }
            tracks.back().silence = frames;
        } else {
            error = bad;
            return false;
        }
    }
    if (tracks.empty()) {
        error = "the cue sheet has no tracks";
        return false;
    }

    // ---- where each file lies on the disc ----
    vector<int64_t> base(files_.size(), -1);
    uint32_t cursor = 0;
    for (const ParsedTrack &t : tracks) {
        if (!t.has01) {
            error = "track " + to_string(t.number) + " has no INDEX 01 in the cue sheet";
            return false;
        }
        if (base[t.file] < 0) {
            if (t.silence > 0) {
                segments_.push_back({cursor, t.silence, -1});
                cursor += t.silence;
            }
            base[t.file] = cursor;
            segments_.push_back({cursor, files_[t.file].sectors, static_cast<int>(t.file)});
            cursor += files_[t.file].sectors;
        } else if (t.silence > 0) {
            error = "track " + to_string(t.number) + ": a PREGAP in the middle of a file is not supported";
            return false;
        }
        if (t.i01 >= files_[t.file].sectors || (t.has00 && t.i00 > t.i01)) {
            error = "track " + to_string(t.number) + ": its INDEX lies outside the image file";
            return false;
        }
        CdTrack out;
        out.number = t.number;
        out.audio = t.audio;
        out.start = static_cast<uint32_t>(base[t.file]) + t.i01;
        out.pregap = (t.has00 ? t.i01 - t.i00 : 0) + t.silence;
        toc_.tracks.push_back(out);
    }
    toc_.leadout = cursor;

    // ---- the .sbi ----
    string sbi = sbiPath;
    if (sbi.empty()) {
        const size_t dot = cuePath.find_last_of('.');
        const size_t slash = cuePath.find_last_of("/\\");
        const string stem = dot == string::npos || (slash != string::npos && dot < slash) ? cuePath : cuePath.substr(0, dot);
        if (fileExists(stem + ".sbi"))
            sbi = stem + ".sbi";
    } else if (!fileExists(sbi)) {
        error = "the .sbi file cannot be opened: " + sbi;
        return false;
    }
    if (!sbi.empty() && !loadSbi(sbi, error))
        return false;
    return true;
}

bool ImageCdDrive::loadSbi(const string &path, string &error) {
    ifstream in(path, ios::binary);
    const string data((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    if (data.size() < 4 || memcmp(data.data(), "SBI\0", 4) != 0) {
        error = "not an .sbi file: " + path;
        return false;
    }
    size_t pos = 4;
    while (pos < data.size()) {
        if (data.size() - pos < 14 || data[pos + 3] != 1) { // only the whole-Q entry (format 1) is known
            error = "the .sbi file is damaged or has an entry of a kind that is not supported: " + path;
            return false;
        }
        const uint8_t *e = reinterpret_cast<const uint8_t *>(data.data()) + pos;
        const uint32_t frames = (fromBcd(e[0]) * 60u + fromBcd(e[1])) * 75u + fromBcd(e[2]);
        if (frames < LeadIn) {
            error = "the .sbi file has an address before the disc starts: " + path;
            return false;
        }
        sbi_[frames - LeadIn] = vector<uint8_t>(e + 4, e + 14);
        pos += 14;
    }
    return true;
}

//*******************************
// CdDrive
//*******************************
bool ImageCdDrive::readToc(CdToc &toc, string &error) {
    if (toc_.tracks.empty()) {
        error = "no disc image is open";
        return false;
    }
    toc = toc_;
    for (CdTrack &t : toc.tracks)
        t.pregap = 0; // as a drive's TOC reports it: INDEX 01 only
    return true;
}

bool ImageCdDrive::readSector(uint32_t lba, bool audio, uint8_t *data, uint8_t *q) {
    if (lba >= toc_.leadout)
        return false;
    // the track the sector belongs to, its pregap counting as its own
    size_t track = 0;
    for (size_t i = 0; i < toc_.tracks.size(); i++)
        if (lba + toc_.tracks[i].pregap >= toc_.tracks[i].start)
            track = i;
    if (toc_.tracks[track].audio != audio)
        return false; // read as the wrong kind of sector

    for (const Segment &s : segments_) {
        if (lba < s.start || lba - s.start >= s.count)
            continue;
        if (s.file < 0) {
            memset(data, 0, SectorSize);
        } else {
            ifstream &in = *files_[s.file].stream;
            in.clear();
            in.seekg(static_cast<streamoff>(lba - s.start) * SectorSize, ios::beg);
            in.read(reinterpret_cast<char *>(data), SectorSize);
            if (in.gcount() != static_cast<streamsize>(SectorSize))
                return false;
        }
        break;
    }
    if (q != nullptr) {
        DiscReader::expectedQ(toc_, lba, q);
        const auto mark = sbi_.find(lba);
        if (mark != sbi_.end())
            memcpy(q, mark->second.data(), 10); // the CRC stays the clean Q's: it does not fit, as on the disc
    }
    return true;
}
