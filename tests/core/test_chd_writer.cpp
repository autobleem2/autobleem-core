//
// ChdWriter against the vendored libchdr (the reader): a synthetic two-track disc written and read back sector by
// sector, and the real chdman file tests/data/test.chd read, re-written and compared (metadata, header fields,
// both SHA1s).
//
#include "doctest/doctest.h"
#include "support/temp_dir.h"

#include "core/services/chd_writer.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifndef AB_NO_CHD
#include <libchdr/chd.h>
#endif

using namespace std;

namespace {

const uint32_t S = ChdWriter::SectorBytes;

// a sector's bytes from a seed; compresses badly when 'noisy', well otherwise
vector<uint8_t> pattern(uint32_t seed, bool noisy) {
    vector<uint8_t> s(S);
    uint32_t x = seed * 2654435761u + 12345;
    for (uint32_t i = 0; i < S; i++) {
        x = x * 1664525u + 1013904223u;
        s[i] = noisy ? static_cast<uint8_t>(x >> 24) : static_cast<uint8_t>(i & 0x0f);
    }
    return s;
}

uint8_t bcd(uint32_t v) {
    return static_cast<uint8_t>(((v / 10) << 4) | (v % 10));
}

// a Mode 2 data sector with its sync and header
vector<uint8_t> dataSector(uint32_t lba, bool noisy) {
    vector<uint8_t> s = pattern(lba, noisy);
    s[0] = 0;
    for (int i = 1; i <= 10; i++)
        s[i] = 0xff;
    s[11] = 0;
    const uint32_t f = lba + 150;
    s[12] = bcd(f / 4500);
    s[13] = bcd(f / 75 % 60);
    s[14] = bcd(f % 75);
    s[15] = 2;
    return s;
}

string readFile(const string &path) {
    ifstream in(path, ios::binary);
    return string((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
}

#ifndef AB_NO_CHD

struct Chd {
    chd_file *file = nullptr;
    ~Chd() {
        if (file != nullptr)
            chd_close(file);
    }
    chd_error error = CHDERR_NONE;
    bool open(const string &path) {
        error = chd_open(path.c_str(), CHD_OPEN_READ, nullptr, &file);
        if (error == CHDERR_NONE)
            return true;
        const string head = readFile(path).substr(0, 124);
        string hex;
        char two[4];
        for (unsigned char c : head) {
            snprintf(two, sizeof(two), "%02x", c);
            hex += two;
        }
        MESSAGE("chd_open " << path << ": " << chd_error_string(error) << " (" << error << "), header " << hex);
        return false;
    }
    const chd_header *header() const { return chd_get_header(file); }
    // the CHT2 texts, in order
    vector<string> tracks() const {
        vector<string> out;
        char buf[512];
        for (uint32_t i = 0;; i++) {
            uint32_t len = 0;
            if (chd_get_metadata(file, CDROM_TRACK_METADATA2_TAG, i, buf, sizeof(buf), &len, nullptr, nullptr) !=
                CHDERR_NONE)
                break;
            out.push_back(string(buf, strnlen(buf, len)));
        }
        return out;
    }
    // every frame of the image: 2448 bytes each
    bool frames(vector<uint8_t> &out) const {
        const chd_header *h = header();
        const uint32_t hunks = h->hunkcount;
        out.assign(static_cast<size_t>(hunks) * h->hunkbytes, 0);
        for (uint32_t i = 0; i < hunks; i++)
            if (chd_read(file, i, &out[static_cast<size_t>(i) * h->hunkbytes]) != CHDERR_NONE)
                return false;
        return true;
    }
};

#endif

} // namespace

TEST_CASE("ChdWriter's parts") {
    CHECK(ChdWriter::paddedFrames(1) == 4);
    CHECK(ChdWriter::paddedFrames(4) == 4);
    CHECK(ChdWriter::paddedFrames(33) == 36);
    vector<ChdWriter::Track> t(2);
    t[0].frames = 40;
    t[1].frames = 33;
    CHECK(ChdWriter::totalFrames(t) == 80);
    t[0].number = 1;
    t[0].type = "MODE2_RAW";
    CHECK(ChdWriter::trackMetadata(t[0]) ==
          "TRACK:1 TYPE:MODE2_RAW SUBTYPE:NONE FRAMES:40 PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0");
    const uint8_t abc[3] = {'a', 'b', 'c'};
    CHECK(ChdWriter::crc16(abc, 3) == 0x514a);
    uint8_t digest[20];
    Sha1 sha;
    sha.update(abc, 3);
    sha.final(digest);
    CHECK(Sha1::hex(digest) == "a9993e364706816aba3e25717850c26c9cd0d89d");
}

TEST_CASE("ChdWriter refuses an empty or incomplete image") {
    TempDir dir("chd_empty");
    ChdWriter w;
    string error;
    CHECK_FALSE(w.open(dir.path() + "/a.chd", {}, error));
    ChdWriter::Track t;
    t.frames = 4;
    CHECK(w.open(dir.path() + "/a.chd", {t}, error));
    CHECK_FALSE(w.finish(error));
}

#ifndef AB_NO_CHD

TEST_CASE("a two-track disc written by ChdWriter reads back sector by sector through libchdr") {
    TempDir dir("chd_two_track");
    const string path = dir.path() + "/disc.chd";
    const uint32_t dataFrames = 40, audioFrames = 33;
    vector<ChdWriter::Track> tracks(2);
    tracks[0].number = 1;
    tracks[0].type = "MODE2_RAW";
    tracks[0].frames = dataFrames;
    tracks[1].number = 2;
    tracks[1].type = "AUDIO";
    tracks[1].frames = audioFrames;

    vector<vector<uint8_t>> sectors;
    for (uint32_t i = 0; i < dataFrames; i++)
        sectors.push_back(dataSector(i, i % 3 == 0)); // some noisy (stored hunks possible), some regular
    for (uint32_t i = 0; i < audioFrames; i++)
        sectors.push_back(pattern(1000 + i, i % 2 == 0));

    ChdWriter w;
    string error;
    REQUIRE_MESSAGE(w.open(path, tracks, error), error);
    for (const auto &s : sectors)
        REQUIRE_MESSAGE(w.writeSector(s.data(), error), error);
    REQUIRE_MESSAGE(w.finish(error), error);

    Chd chd;
    REQUIRE(chd.open(path));
    const chd_header *h = chd.header();
    CHECK(h->version == 5);
    CHECK(h->hunkbytes == ChdWriter::HunkBytes);
    CHECK(h->unitbytes == ChdWriter::FrameBytes);
    CHECK(h->hunkcount == 10);
    CHECK(h->logicalbytes == 76u * ChdWriter::FrameBytes); // 40 + 33 padded to 36; the last hunk is partial
    CHECK(h->compression[0] == CHD_CODEC_CD_ZLIB);

    const vector<string> meta = chd.tracks();
    REQUIRE(meta.size() == 2);
    CHECK(meta[0] == "TRACK:1 TYPE:MODE2_RAW SUBTYPE:NONE FRAMES:40 PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0");
    CHECK(meta[1] == "TRACK:2 TYPE:AUDIO SUBTYPE:NONE FRAMES:33 PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0");

    vector<uint8_t> frames;
    REQUIRE(chd.frames(frames));
    for (uint32_t i = 0; i < dataFrames; i++) {
        INFO("data sector " << i);
        CHECK(memcmp(&frames[i * ChdWriter::FrameBytes], sectors[i].data(), S) == 0);
    }
    for (uint32_t i = 0; i < audioFrames; i++) {
        INFO("audio sector " << i);
        CHECK(memcmp(&frames[(dataFrames + i) * ChdWriter::FrameBytes], sectors[dataFrames + i].data(), S) == 0);
    }
    // the audio track's padding (33 -> 36) and the hunk's tail are zeros, and so is every subchannel
    for (uint32_t i = dataFrames + audioFrames; i < 80; i++)
        for (uint32_t b = 0; b < ChdWriter::FrameBytes; b++)
            REQUIRE(frames[i * ChdWriter::FrameBytes + b] == 0);
    for (uint32_t i = 0; i < 80; i++)
        for (uint32_t b = S; b < ChdWriter::FrameBytes; b++)
            REQUIRE(frames[i * ChdWriter::FrameBytes + b] == 0);
}

TEST_CASE("ChdWriter matches the file chdman wrote (tests/data/test.chd)") {
    const string reference = string(AB_TEST_DATA_DIR) + "/test.chd";
    Chd ref;
    REQUIRE(ref.open(reference));
    const chd_header *rh = ref.header();
    REQUIRE(rh->version == 5);
    const vector<string> refMeta = ref.tracks();
    REQUIRE_FALSE(refMeta.empty());
    vector<uint8_t> refFrames;
    REQUIRE(ref.frames(refFrames));
    INFO("compressors " << chd_get_codec_name(rh->compression[0]) << " " << chd_get_codec_name(rh->compression[1]));

    // the tracks from the metadata, the sectors from the frames (a track's padding is not re-read)
    vector<ChdWriter::Track> tracks;
    for (const string &m : refMeta) {
        int number = 0, frames = 0, pregap = 0, postgap = 0;
        char type[32], subtype[32], pgtype[32], pgsub[32];
        REQUIRE(sscanf(m.c_str(), CDROM_TRACK_METADATA2_FORMAT, &number, type, subtype, &frames, &pregap, pgtype, pgsub,
                       &postgap) == 8);
        ChdWriter::Track t;
        t.number = number;
        t.type = type;
        t.frames = static_cast<uint32_t>(frames);
        tracks.push_back(t);
    }

    TempDir dir("chd_chdman");
    const string path = dir.path() + "/copy.chd";
    ChdWriter w;
    string error;
    REQUIRE_MESSAGE(w.open(path, tracks, error), error);
    uint32_t at = 0;
    for (const auto &t : tracks) {
        for (uint32_t i = 0; i < t.frames; i++)
            REQUIRE_MESSAGE(w.writeSector(&refFrames[static_cast<size_t>(at + i) * ChdWriter::FrameBytes], error),
                            error);
        at += ChdWriter::paddedFrames(t.frames);
    }
    REQUIRE_MESSAGE(w.finish(error), error);

    Chd mine;
    REQUIRE(mine.open(path));
    const chd_header *mh = mine.header();
    CHECK(mine.tracks() == refMeta);
    CHECK(mh->version == rh->version);
    CHECK(mh->hunkbytes == rh->hunkbytes);
    CHECK(mh->unitbytes == rh->unitbytes);
    CHECK(mh->logicalbytes == rh->logicalbytes);
    CHECK(mh->hunkcount == rh->hunkcount);
    CHECK(Sha1::hex(mh->rawsha1) == Sha1::hex(rh->rawsha1));
    CHECK(Sha1::hex(mh->sha1) == Sha1::hex(rh->sha1));
    vector<uint8_t> mineFrames;
    REQUIRE(mine.frames(mineFrames));
    CHECK(mineFrames == refFrames);
}

#endif
