//
// DiscReader - see the header.
//
#include "disc_reader.h"
#include "content_installer.h"
#include "../main.h"

#include <ableem/engine/crc32.h>
#include <ableem/engine/log.h>
#include <ableem/engine/metadata_lookup.h>
#include <ableem/engine/serial_scanner.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>

using namespace std;

namespace {

const uint32_t LeadIn = 150;      // the 2 s before LBA 0: an MSF address is LBA + 150
const uint32_t AudioPregap = 150; // the 2 s pregap an audio track after a data track has, when not told
const int UnreliableQ = 64;       // more mismatched Q sectors than LibCrypt ever marks: it is the drive
const uint32_t ProgressEvery = 256;

uint8_t bcd(int value) {
    return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

int fromBcd(uint8_t value) {
    return (value >> 4) * 10 + (value & 0x0f);
}

void msfBytes(uint32_t frames, uint8_t *out) {
    out[0] = bcd(static_cast<int>(frames / (60 * 75)));
    out[1] = bcd(static_cast<int>(frames / 75 % 60));
    out[2] = bcd(static_cast<int>(frames % 75));
}

// a disc serial ("SLUS-00123"): four letters, a dash, digits - anything else is not one
string plausibleSerial(const string &serial) {
    if (serial.size() < 8 || serial.size() > 12 || serial[4] != '-')
        return "";
    for (size_t i = 0; i < 4; i++)
        if (!isalpha(static_cast<unsigned char>(serial[i])))
            return "";
    return serial;
}

// the track a sector belongs to: its pregap counts as its own
size_t trackOf(const CdToc &toc, const vector<uint32_t> &pregaps, uint32_t lba) {
    size_t t = 0;
    for (size_t i = 0; i < toc.tracks.size(); i++)
        if (lba + pregaps[i] >= toc.tracks[i].start)
            t = i;
    return t;
}

bool writeText(const string &path, const string &text) {
    ofstream out(path, ios::binary | ios::trunc);
    out << text;
    return static_cast<bool>(out);
}

} // namespace

//*******************************
// DiscReader::qCrc / expectedQ / deinterleaveQ
//*******************************
uint16_t DiscReader::qCrc(const uint8_t *q10) {
    uint16_t crc = 0;
    for (int i = 0; i < 10; i++) {
        crc = static_cast<uint16_t>(crc ^ (q10[i] << 8));
        for (int b = 0; b < 8; b++)
            crc = static_cast<uint16_t>((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    return static_cast<uint16_t>(~crc);
}

void DiscReader::expectedQ(const CdToc &toc, uint32_t lba, uint8_t *q) {
    vector<uint32_t> pregaps;
    for (const CdTrack &t : toc.tracks)
        pregaps.push_back(t.pregap);
    const CdTrack &t = toc.tracks[trackOf(toc, pregaps, lba)];
    const bool inPregap = lba < t.start;
    q[0] = t.audio ? 0x01 : 0x41;
    q[1] = bcd(t.number);
    q[2] = bcd(inPregap ? 0 : 1);
    msfBytes(inPregap ? t.start - lba : lba - t.start, q + 3);
    q[6] = 0;
    msfBytes(lba + LeadIn, q + 7);
    const uint16_t crc = qCrc(q);
    q[10] = static_cast<uint8_t>(crc >> 8);
    q[11] = static_cast<uint8_t>(crc);
}

void DiscReader::deinterleaveQ(const uint8_t *subcode96, uint8_t *q) {
    for (int i = 0; i < 12; i++) {
        uint8_t byte = 0;
        for (int b = 0; b < 8; b++)
            byte = static_cast<uint8_t>((byte << 1) | ((subcode96[i * 8 + b] >> 6) & 1));
        q[i] = byte;
    }
}

//*******************************
// DiscReader::msf / cue / sbi
//*******************************
string DiscReader::msf(uint32_t frames) {
    char text[16];
    snprintf(text, sizeof(text), "%02u:%02u:%02u", frames / (60 * 75), frames / 75 % 60, frames % 75);
    return text;
}

string DiscReader::cue(const CdToc &toc, const string &binName, bool firstTrackMode1) {
    string out = "FILE \"" + binName + "\" BINARY\n";
    for (size_t i = 0; i < toc.tracks.size(); i++) {
        const CdTrack &t = toc.tracks[i];
        char number[8];
        snprintf(number, sizeof(number), "%02d", t.number);
        out += string("  TRACK ") + number + " " +
               (t.audio ? "AUDIO" : (i == 0 && firstTrackMode1 ? "MODE1/2352" : "MODE2/2352")) + "\n";
        if (t.pregap > 0 && t.pregap <= t.start)
            out += "    INDEX 00 " + msf(t.start - t.pregap) + "\n";
        out += "    INDEX 01 " + msf(t.start) + "\n";
    }
    return out;
}

string DiscReader::sbi(const vector<pair<uint32_t, vector<uint8_t>>> &entries) {
    string out("SBI\0", 4);
    for (const auto &e : entries) {
        uint8_t where[3];
        msfBytes(e.first + LeadIn, where);
        out.append(reinterpret_cast<const char *>(where), 3);
        out += '\x01'; // a whole Q, its 10 bytes without the CRC
        out.append(reinterpret_cast<const char *>(e.second.data()), 10);
    }
    return out;
}

//*******************************
// DiscReader::read
//*******************************
DiscReader::Result DiscReader::read(CdDrive &drive, const Options &options,
                                    const function<bool(uint32_t, uint32_t)> &progress) {
    Result r;
    CdToc toc;
    string error;
    if (!drive.readToc(toc, error)) {
        r.error = error.empty() ? "the disc's table of contents cannot be read" : error;
        return r;
    }
    if (toc.tracks.empty() || toc.leadout == 0 || toc.leadout > 450000) {
        r.error = "the disc's table of contents makes no sense - is there a disc in the drive?";
        return r;
    }
    if (toc.tracks.front().audio) {
        r.error = "this is an audio CD, not a PlayStation disc";
        return r;
    }
    if (options.discNumber >= 2 && !DirEntry::isDirectory(options.gameFolder)) {
        r.error = "the folder of the game's first disc is not there: " + options.gameFolder;
        return r;
    }

    // an audio track straight after a data track has a 2 s pregap; the Q channel corrects this as it is read
    vector<uint32_t> pregaps;
    for (size_t i = 0; i < toc.tracks.size(); i++) {
        uint32_t p = toc.tracks[i].pregap;
        if (p == 0 && i > 0 && toc.tracks[i].audio && !toc.tracks[i - 1].audio)
            p = AudioPregap;
        pregaps.push_back(min(p, toc.tracks[i].start));
    }
    map<int, uint32_t> firstIndex0; // track number -> the first sector the Q channel put in its pregap

    const string stage = options.libraryDir + sep + ".reading-disc";
    DirEntry::removeDirAndContents(stage);
    if (!DirEntry::createDirs(stage)) {
        r.error = "cannot write to " + options.libraryDir;
        return r;
    }
    const string stagedBin = stage + sep + "disc.bin";
    auto fail = [&](const string &why) {
        DirEntry::removeDirAndContents(stage);
        r.ok = false;
        r.error = why;
        return r;
    };

    r.subchannel = drive.hasSubchannel();
    // LibCrypt marks only the data track's sectors: the Q channel is looked at up to the next track's pregap
    const uint32_t firstTrackEnd = toc.tracks.size() > 1 ? toc.tracks[1].start - pregaps[1] : toc.leadout;
    vector<pair<uint32_t, vector<uint8_t>>> marked; // the data track's sectors whose Q is not what it should be
    bool mode1 = false;
    {
        ofstream bin(stagedBin, ios::binary | ios::trunc);
        if (!bin)
            return fail("cannot write to " + options.libraryDir);
        vector<uint8_t> data(SectorSize);
        uint8_t q[12];
        for (uint32_t lba = 0; lba < toc.leadout; lba++) {
            if (lba % ProgressEvery == 0 && progress && !progress(lba, toc.leadout))
                return fail("stopped");
            const size_t t = trackOf(toc, pregaps, lba);
            const bool audio = toc.tracks[t].audio;
            bool read = false;
            for (int attempt = 0; attempt <= options.retries && !read; attempt++)
                read = drive.readSector(lba, audio, data.data(), r.subchannel ? q : nullptr);
            if (!read) // a guess at the sector's kind may be wrong at a track's edge: the other kind once
                read = drive.readSector(lba, !audio, data.data(), r.subchannel ? q : nullptr);
            if (!read) {
                PLOG_WARNING << "sector " << lba << " cannot be read - written as zeros";
                fill(data.begin(), data.end(), 0);
                r.badSectors.push_back(lba);
            }
            if (lba == 0 && read)
                mode1 = data[15] == 1;
            bin.write(reinterpret_cast<const char *>(data.data()), SectorSize);
            if (!bin)
                return fail("cannot write the image - is the disk full?");

            if (r.subchannel && read) {
                const bool crcOk = qCrc(q) == static_cast<uint16_t>(q[10] << 8 | q[11]);
                const bool position = (q[0] & 0x0f) == 1;
                if (crcOk && position && fromBcd(q[2]) == 0) { // INDEX 00: a pregap, as the disc says
                    const int number = fromBcd(q[1]);
                    if (!firstIndex0.count(number))
                        firstIndex0[number] = lba;
                }
                uint8_t expected[12];
                expectedQ(toc, lba, expected);
                const bool addressOk = memcmp(q + 7, expected + 7, 3) == 0;
                // a clean sector of the mode 2/3 kind (the catalogue number, the ISRC) says no address
                if (lba < firstTrackEnd && !(crcOk && (!position || addressOk)))
                    marked.emplace_back(lba, vector<uint8_t>(q, q + 12));
            }
        }
    }
    if (progress)
        progress(toc.leadout, toc.leadout);
    r.sectors = toc.leadout;
    for (size_t i = 1; i < toc.tracks.size(); i++) {
        auto it = firstIndex0.find(toc.tracks[i].number);
        if (it != firstIndex0.end() && it->second < toc.tracks[i].start)
            pregaps[i] = toc.tracks[i].start - it->second;
    }
    for (size_t i = 0; i < toc.tracks.size(); i++)
        toc.tracks[i].pregap = pregaps[i];
    if (static_cast<int>(marked.size()) > UnreliableQ) {
        PLOG_WARNING << marked.size() << " sectors with a mismatched Q channel - the drive's subchannel is not "
                     << "reliable, no .sbi";
        r.subchannelUnreliable = true;
        marked.clear();
    }

    // the data track's checksum, as a per-track dump names it: from the start to the next track's pregap
    {
        const uint32_t end = toc.tracks.size() > 1 ? toc.tracks[1].start - toc.tracks[1].pregap : toc.leadout;
        ifstream in(stagedBin, ios::binary);
        vector<char> chunk(SectorSize * 64);
        uint64_t left = static_cast<uint64_t>(end) * SectorSize;
        uint32_t crc = 0;
        while (left > 0 && in) {
            in.read(chunk.data(), static_cast<streamsize>(min<uint64_t>(left, chunk.size())));
            const streamsize got = in.gcount();
            if (got <= 0)
                break;
            crc = ableem::Crc32::update(crc, chunk.data(), static_cast<size_t>(got));
            left -= static_cast<uint64_t>(got);
        }
        r.dataCrc = crc;
    }

    // the serial, from the image just written
    if (!writeText(stage + sep + "disc.cue", cue(toc, "disc.bin", mode1)))
        return fail("cannot write to " + options.libraryDir);
    r.serial = plausibleSerial(ableem::SerialScanner::readSerial(ableem::IMAGE_BIN, stage, stagedBin));

    // the title, and how the rdb sees the dump
    ableem::MetadataLookup metadata(options.coversDir, options.rdbFile);
    if (options.discNumber >= 2 && !options.title.empty()) {
        r.title = options.title;
    } else {
        ableem::GameMetadata md;
        if (!r.serial.empty() && metadata.findBySerial(r.serial, md) && !md.title.empty())
            r.title = md.title;
        else
            r.title = r.serial.empty() ? "Unknown disc" : r.serial;
    }
    if (metadata.hasRdb()) {
        const ableem::RdbReader::Record *rec = r.serial.empty() ? nullptr : metadata.rdb().findBySerial(r.serial);
        if (rec != nullptr && rec->crc != 0)
            r.verified = rec->crc == r.dataCrc ? Verified::Matches : Verified::Differs;
        else if (metadata.rdb().findByCrc(r.dataCrc) != nullptr)
            r.verified = Verified::Matches;
    }

    // the game's folder, and the files in it under the title
    const string base = GameInstaller::folderNameFor(r.title);
    const string fileBase = options.discNumber > 0 ? base + " (Disc " + to_string(options.discNumber) + ")" : base;
    if (options.discNumber >= 2) {
        r.folder = options.gameFolder;
    } else {
        r.folder = options.libraryDir + sep + base;
        for (int n = 2; DirEntry::exists(r.folder); n++)
            r.folder = options.libraryDir + sep + base + " (" + to_string(n) + ")";
        if (!DirEntry::createDirs(r.folder))
            return fail("cannot write to " + options.libraryDir);
    }
    const string bin = r.folder + sep + fileBase + ".bin";
    if (DirEntry::exists(bin))
        return fail(fileBase + " is already in " + r.folder);
    if (!DirEntry::renameFile(stagedBin, bin))
        return fail("cannot move the image into " + r.folder);
    r.cueFile = r.folder + sep + fileBase + ".cue";
    if (!writeText(r.cueFile, cue(toc, fileBase + ".bin", mode1)))
        return fail("cannot write " + r.cueFile);
    if (!marked.empty()) {
        if (!writeText(r.folder + sep + fileBase + ".sbi", sbi(marked)))
            return fail("cannot write the .sbi into " + r.folder);
        r.sbiSectors = static_cast<int>(marked.size());
    }
    DirEntry::removeDirAndContents(stage);
    r.ok = true;
    PLOG_INFO << "disc read into " << r.folder << ": " << r.title << " (" << r.serial << "), " << r.sectors
              << " sectors, " << r.badSectors.size() << " unreadable, " << r.sbiSectors << " LibCrypt sectors";
    return r;
}
