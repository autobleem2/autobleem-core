#include "disc_identity.h"

#include <ableem/engine/serial_scanner.h>

#include <algorithm>
#include <cctype>

using namespace std;

namespace {

const size_t UserSize = 2048;
const uint32_t MaxDirSectors = 16;

string upper(string s) {
    for (char &c : s)
        c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return s;
}

string trim(const string &s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && isspace(static_cast<unsigned char>(s[b])))
        b++;
    while (e > b && (isspace(static_cast<unsigned char>(s[e - 1])) || s[e - 1] == '\0'))
        e--;
    return s.substr(b, e - b);
}

uint32_t le32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}

// AAAA[_-]NNN[.]NN, case folded: a PS1 serial as a file name or a label writes it
bool looksLikeSerial(const string &token) {
    if (token.size() < 9)
        return false;
    for (int i = 0; i < 4; i++)
        if (!isupper(static_cast<unsigned char>(token[i])))
            return false;
    size_t i = 4;
    if (token[i] == '_' || token[i] == '-')
        i++;
    int digits = 0;
    for (; i < token.size(); i++) {
        if (isdigit(static_cast<unsigned char>(token[i])))
            digits++;
        else if (token[i] == '.' && digits == 3)
            continue;
        else
            return false;
    }
    return digits == 5;
}

string serialFromToken(const string &token) {
    const string t = upper(trim(token));
    return looksLikeSerial(t) ? ableem::SerialScanner::normalizeSerial(t) : "";
}

enum class UserRead { Ok, Failed, NotData };

// the 2048 bytes of a data sector: Mode 1 has them at byte 16 of the raw 2352, Mode 2 Form 1 at 24
UserRead readUser(CdDrive &drive, uint32_t lba, vector<uint8_t> &user) {
    vector<uint8_t> raw(DiscReader::SectorSize);
    if (!drive.readSector(lba, false, raw.data(), nullptr))
        return UserRead::Failed;
    size_t at;
    if (raw[15] == 1)
        at = 16;
    else if (raw[15] == 2)
        at = 24;
    else
        return UserRead::NotData;
    user.assign(raw.begin() + static_cast<long>(at), raw.begin() + static_cast<long>(at + UserSize));
    return UserRead::Ok;
}

string fileName(const uint8_t *rec) {
    string name(reinterpret_cast<const char *>(rec + 33), rec[32]);
    const size_t semi = name.find(';');
    if (semi != string::npos)
        name.resize(semi);
    return upper(name);
}

// the first sectors of a file in the root directory, as text; false when there is no such file
bool readRootFile(CdDrive &drive, uint32_t base, const uint8_t *pvd, const string &wanted, string &text) {
    const uint8_t *root = pvd + 156;
    const uint32_t dirLba = le32(root + 2);
    const uint32_t dirSize = le32(root + 10);
    const uint32_t sectors = min<uint32_t>(MaxDirSectors, (dirSize + UserSize - 1) / UserSize);
    for (uint32_t s = 0; s < sectors; s++) {
        vector<uint8_t> dir;
        if (readUser(drive, base + dirLba + s, dir) != UserRead::Ok)
            return false;
        size_t at = 0;
        while (at + 34 <= UserSize && dir[at] != 0) {
            const uint8_t *rec = &dir[at];
            const size_t len = rec[0];
            if (at + len > UserSize || 33u + rec[32] > len)
                break;
            if ((rec[25] & 2) == 0 && fileName(rec) == wanted) {
                const uint32_t fileLba = le32(rec + 2);
                const uint32_t fileSize = le32(rec + 10);
                const uint32_t n = min<uint32_t>(2, (fileSize + UserSize - 1) / UserSize);
                text.clear();
                for (uint32_t i = 0; i < n; i++) {
                    vector<uint8_t> data;
                    if (readUser(drive, base + fileLba + i, data) != UserRead::Ok)
                        return false;
                    text.append(data.begin(), data.end());
                }
                text.resize(min<size_t>(text.size(), fileSize));
                return true;
            }
            at += len;
        }
    }
    return false;
}

} // namespace

//*******************************
// DiscIdentity::serialFromSystemCnf / serialFromLabel
//*******************************
string DiscIdentity::serialFromSystemCnf(const string &text) {
    size_t from = 0;
    while (from <= text.size()) {
        size_t end = text.find('\n', from);
        if (end == string::npos)
            end = text.size();
        const string line = text.substr(from, end - from);
        from = end + 1;
        const size_t eq = line.find('=');
        if (eq == string::npos || upper(trim(line.substr(0, eq))) != "BOOT")
            continue;
        string value = trim(line.substr(eq + 1));
        const size_t semi = value.find(';');
        if (semi != string::npos)
            value.resize(semi);
        const size_t slash = value.find_last_of("\\/:");
        if (slash != string::npos)
            value = value.substr(slash + 1);
        return serialFromToken(value);
    }
    return "";
}

string DiscIdentity::serialFromLabel(const string &label) { return serialFromToken(label); }

//*******************************
// DiscIdentity::identify
//*******************************
DiscIdentity DiscIdentity::identify(CdDrive &drive) {
    DiscIdentity id;
    CdToc toc;
    string error;
    if (!drive.readToc(toc, error) || toc.tracks.empty() || toc.leadout < toc.tracks.back().start)
        return id;

    if (toc.tracks[0].audio) {
        id.kind = Kind::AudioCd;
        id.trackCount = static_cast<int>(toc.tracks.size());
        id.totalFrames = toc.leadout - toc.tracks[0].start;
        for (size_t i = 0; i < toc.tracks.size(); i++) {
            const uint32_t next = i + 1 < toc.tracks.size() ? toc.tracks[i + 1].start : toc.leadout;
            id.trackLengths.push_back(next > toc.tracks[i].start ? next - toc.tracks[i].start : 0);
        }
        return id;
    }

    const uint32_t base = toc.tracks[0].start;
    vector<uint8_t> pvd;
    switch (readUser(drive, base + 16, pvd)) {
    case UserRead::Failed:
        return id; // Unreadable
    case UserRead::NotData:
        id.kind = Kind::OtherDisc;
        return id;
    case UserRead::Ok:
        break;
    }
    id.kind = Kind::OtherDisc;
    if (pvd[0] != 1 || string(pvd.begin() + 1, pvd.begin() + 6) != "CD001")
        return id;
    id.volumeLabel = trim(string(pvd.begin() + 40, pvd.begin() + 72));

    string cnf;
    string serial;
    if (readRootFile(drive, base, pvd.data(), "SYSTEM.CNF", cnf))
        serial = serialFromSystemCnf(cnf);
    else
        serial = serialFromLabel(id.volumeLabel);
    if (!serial.empty()) {
        id.kind = Kind::Ps1Game;
        id.serial = serial;
        id.region = ableem::SerialScanner::serialToRegion(serial);
    }
    return id;
}
