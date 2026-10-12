//
// ChdWriter against the vendored libchdr (the reader): a synthetic two-track disc written and read back sector by
// sector, and the real chdman file tests/data/test.chd read, re-written and compared (metadata, header fields,
// both SHA1s).
//
#include "doctest/doctest.h"
#include "support/temp_dir.h"

#include "core/services/chd_writer.h"

#include "core/services/flac_encoder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifndef AB_NO_CHD
#include <libchdr/chd.h>
extern "C" {
#include <libchdr/flac.h>
}
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

// the sector of an AUDIO track as the image stores it: the drive's little-endian samples, byte pairs swapped
vector<uint8_t> bigEndian(vector<uint8_t> sector) {
    for (uint32_t i = 0; i + 1 < S; i += 2)
        swap(sector[i], sector[i + 1]);
    return sector;
}

// 588 stereo samples of CD audio (little-endian, as a drive reads them): 'music' is two sines and a little noise,
// otherwise silence; 'sector' is the position in the track, so the waves run on from one sector to the next
vector<uint8_t> audioSector(uint32_t sector, bool music) {
    vector<uint8_t> s(S, 0);
    uint32_t x = sector * 2654435761u + 7;
    for (uint32_t i = 0; music && i < S / 4; i++) {
        const double t = (sector * (S / 4.0) + i) / 44100.0;
        x = x * 1664525u + 1013904223u;
        const int noiseL = static_cast<int>(x >> 24) - 128;
        x = x * 1664525u + 1013904223u;
        const int noiseR = static_cast<int>(x >> 24) - 128;
        const double common = 2000 * sin(2 * 3.14159265358979 * 1250 * t);
        const int16_t left = static_cast<int16_t>(9000 * sin(2 * 3.14159265358979 * 440 * t) + common + noiseL * 8);
        const int16_t right = static_cast<int16_t>(7000 * sin(2 * 3.14159265358979 * 660 * t) + common + noiseR * 8);
        s[i * 4] = static_cast<uint8_t>(left);
        s[i * 4 + 1] = static_cast<uint8_t>(left >> 8);
        s[i * 4 + 2] = static_cast<uint8_t>(right);
        s[i * 4 + 3] = static_cast<uint8_t>(right >> 8);
    }
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
    CHECK(h->compression[0] == CHD_CODEC_CD_LZMA);
    CHECK(h->compression[1] == CHD_CODEC_CD_ZLIB);
    CHECK(h->compression[2] == CHD_CODEC_CD_FLAC);
    CHECK(h->compression[3] == 0);

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
        CHECK(memcmp(&frames[(dataFrames + i) * ChdWriter::FrameBytes], bigEndian(sectors[dataFrames + i]).data(), S) ==
              0);
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

namespace {

// a Mode 2 data track and four audio tracks (three of music, one silent) - the kind of disc a game with a CD
// soundtrack is
struct CodecDisc {
    vector<ChdWriter::Track> tracks;
    vector<vector<uint8_t>> sectors; // as a drive delivers them, track after track
    vector<uint32_t> firstSector;    // of each track in the padded image
    CodecDisc() {
        const char *types[5] = {"MODE2_RAW", "AUDIO", "AUDIO", "AUDIO", "AUDIO"};
        const uint32_t frames[5] = {400, 300, 301, 200, 160};
        uint32_t at = 0;
        for (int t = 0; t < 5; t++) {
            ChdWriter::Track track;
            track.number = t + 1;
            track.type = types[t];
            track.frames = frames[t];
            tracks.push_back(track);
            firstSector.push_back(at);
            at += ChdWriter::paddedFrames(frames[t]);
            for (uint32_t i = 0; i < frames[t]; i++) {
                if (t == 0)
                    sectors.push_back(dataSector(i, i % 3 == 0));
                else
                    sectors.push_back(audioSector(i + t * 1000, t != 4));
            }
        }
    }
    bool write(const string &path, ChdWriter &w, string &error) const {
        if (!w.open(path, tracks, error))
            return false;
        for (const auto &s : sectors)
            if (!w.writeSector(s.data(), error))
                return false;
        return w.finish(error);
    }
};

} // namespace

TEST_CASE("every CD codec is chosen for some hunk and the disc reads back sector by sector through libchdr") {
    TempDir dir("chd_codecs");
    const string path = dir.path() + "/disc.chd";
    const CodecDisc disc;
    ChdWriter w;
    string error;
    REQUIRE_MESSAGE(disc.write(path, w, error), error);
    INFO("hunks: cdlz " << w.hunksOfType(0) << ", cdzl " << w.hunksOfType(1) << ", cdfl " << w.hunksOfType(2)
                        << ", stored " << w.hunksOfType(4));
    CHECK(w.hunksOfType(0) > 0);
    CHECK(w.hunksOfType(1) > 0);
    CHECK(w.hunksOfType(2) > 0);

    Chd chd;
    REQUIRE(chd.open(path));
    const chd_header *h = chd.header();
    CHECK(h->compression[0] == CHD_CODEC_CD_LZMA);
    CHECK(h->compression[1] == CHD_CODEC_CD_ZLIB);
    CHECK(h->compression[2] == CHD_CODEC_CD_FLAC);
    CHECK(w.hunksOfType(0) + w.hunksOfType(1) + w.hunksOfType(2) + w.hunksOfType(4) == h->hunkcount);

    vector<uint8_t> frames;
    REQUIRE(chd.frames(frames));
    size_t track = 0, inTrack = 0;
    for (size_t i = 0; i < disc.sectors.size(); i++) {
        while (inTrack == disc.tracks[track].frames) {
            track++;
            inTrack = 0;
        }
        const bool audio = disc.tracks[track].type == "AUDIO";
        const vector<uint8_t> want = audio ? bigEndian(disc.sectors[i]) : disc.sectors[i];
        const size_t frame = disc.firstSector[track] + inTrack;
        INFO("track " << track + 1 << " sector " << inTrack);
        REQUIRE(memcmp(&frames[frame * ChdWriter::FrameBytes], want.data(), S) == 0);
        for (uint32_t b = S; b < ChdWriter::FrameBytes; b++)
            REQUIRE(frames[frame * ChdWriter::FrameBytes + b] == 0);
        inTrack++;
    }
}

TEST_CASE("an audio-heavy disc comes out clearly smaller than with cdzl alone") {
    TempDir dir("chd_size");
    const string path = dir.path() + "/disc.chd";
    const CodecDisc disc;
    ChdWriter w;
    string error;
    REQUIRE_MESSAGE(disc.write(path, w, error), error);

    // what the same hunks cost as cdzl only (stored when that does not shrink them)
    Chd chd;
    REQUIRE(chd.open(path));
    vector<uint8_t> frames;
    REQUIRE(chd.frames(frames));
    uint64_t cdzlOnly = 0;
    for (size_t hunk = 0; hunk < chd.header()->hunkcount; hunk++) {
        vector<uint8_t> packed;
        cdzlOnly += ChdWriter::cdzlHunk(&frames[hunk * ChdWriter::HunkBytes], packed) ? packed.size()
                                                                                       : ChdWriter::HunkBytes;
    }
    const uint64_t all = readFile(path).size();
    MESSAGE("file " << all << " bytes; the hunks as cdzl alone " << cdzlOnly << " (" << cdzlOnly * 100 / all << "%)");
    CHECK(all * 10 < cdzlOnly * 9); // at least 10% smaller
}

TEST_CASE("each codec packs a hunk of music, of silence and of data, and smallestHunk takes the best") {
    for (int kind = 0; kind < 3; kind++) {
        vector<uint8_t> hunk(ChdWriter::HunkBytes, 0);
        for (uint32_t f = 0; f < ChdWriter::FramesPerHunk; f++) {
            vector<uint8_t> s = kind == 2 ? dataSector(f, false) : bigEndian(audioSector(f, kind == 0));
            memcpy(&hunk[f * ChdWriter::FrameBytes], s.data(), S);
        }
        vector<uint8_t> packed;
        const bool lz = ChdWriter::cdlzHunk(hunk.data(), packed);
        const size_t lzBytes = packed.size();
        const bool zl = ChdWriter::cdzlHunk(hunk.data(), packed);
        const size_t zlBytes = packed.size();
        const bool fl = ChdWriter::cdflHunk(hunk.data(), packed);
        INFO("kind " << kind << ": cdlz " << (lz ? lzBytes : 0) << ", cdzl " << (zl ? zlBytes : 0) << ", cdfl "
                     << (fl ? packed.size() : 0));
        CHECK(lz);
        CHECK(zl);
        if (kind < 2)
            CHECK(fl);
        const size_t flBytes = fl ? packed.size() : ChdWriter::HunkBytes;
        const uint8_t type = ChdWriter::smallestHunk(hunk.data(), packed);
        CHECK(type < 3);
        const size_t lzCost = lz ? lzBytes : ChdWriter::HunkBytes, zlCost = zl ? zlBytes : ChdWriter::HunkBytes;
        CHECK(packed.size() == min({lzCost, zlCost, flBytes}));
        if (kind == 0)
            CHECK(type == 2); // music: FLAC
    }
}

TEST_CASE("FlacEncoder: frames libchdr's (dr_flac) decoder reads back, on odd sizes too") {
    const size_t sizes[] = {1, 2, 3, 4, 5, 17, 100, 255, 256, 1000, 2352, 2353, 4704, 5000};
    for (size_t n : sizes) {
        for (int kind = 0; kind < 4; kind++) {
            vector<int16_t> pcm(n * 2);
            uint32_t x = static_cast<uint32_t>(n * 31 + kind);
            for (size_t i = 0; i < n; i++) {
                x = x * 1664525u + 1013904223u;
                switch (kind) {
                case 0: // a tone with a little noise
                    pcm[i * 2] = static_cast<int16_t>(8000 * sin(static_cast<double>(i) * 0.05) + (x >> 28));
                    pcm[i * 2 + 1] = static_cast<int16_t>(6000 * sin(static_cast<double>(i) * 0.07));
                    break;
                case 1: // white noise, both channels
                    pcm[i * 2] = static_cast<int16_t>(x >> 16);
                    pcm[i * 2 + 1] = static_cast<int16_t>(x);
                    break;
                case 2: // full scale, opposite signs (the side channel needs 17 bits)
                    pcm[i * 2] = (x >> 20) & 1 ? -32768 : 32767;
                    pcm[i * 2 + 1] = (x >> 21) & 1 ? 32767 : -32768;
                    break;
                default: // silence, then one constant level
                    pcm[i * 2] = 0;
                    pcm[i * 2 + 1] = i < n / 2 ? 0 : -1234;
                    break;
                }
            }
            const uint32_t blockSize = static_cast<uint32_t>(n < 2352 ? n : 2352);
            const vector<uint8_t> coded = FlacEncoder::encode(pcm.data(), n, blockSize);
            INFO("samples " << n << ", kind " << kind << ", " << coded.size() << " bytes");
            REQUIRE_FALSE(coded.empty());

            flac_decoder decoder;
            flac_decoder_init(&decoder);
            vector<int16_t> back(n * 2, 0x5555);
            const uint32_t length = static_cast<uint32_t>(coded.size());
            REQUIRE(flac_decoder_reset(&decoder, 44100, 2, blockSize, coded.data(), length));
            REQUIRE(flac_decoder_decode_interleaved(&decoder, back.data(), static_cast<uint32_t>(n), 0));
            CHECK(flac_decoder_finish(&decoder) == coded.size()); // the decoder consumed exactly the frames
            flac_decoder_free(&decoder);
            CHECK(back == pcm);
        }
    }
}

#endif

namespace {

// the tracks and sectors of a synthetic disc: a data track then an audio track, enough hunks to fill the queue
struct SyntheticDisc {
    vector<ChdWriter::Track> tracks;
    vector<vector<uint8_t>> sectors;
    SyntheticDisc(uint32_t dataFrames, uint32_t audioFrames) : tracks(2) {
        tracks[0].number = 1;
        tracks[0].type = "MODE2_RAW";
        tracks[0].frames = dataFrames;
        tracks[1].number = 2;
        tracks[1].type = "AUDIO";
        tracks[1].frames = audioFrames;
        for (uint32_t i = 0; i < dataFrames; i++)
            sectors.push_back(dataSector(i, i % 3 == 0));
        for (uint32_t i = 0; i < audioFrames; i++)
            sectors.push_back(audioSector(i, i % 4 != 3));
    }
};

bool writeDisc(const SyntheticDisc &disc, const string &path, bool synchronous, string &error) {
    ChdWriter w;
    w.setSynchronous(synchronous);
    if (!w.open(path, disc.tracks, error))
        return false;
    for (const auto &s : disc.sectors)
        if (!w.writeSector(s.data(), error))
            return false;
    return w.finish(error);
}

bool exists(const string &path) {
    return ifstream(path).good();
}

} // namespace

TEST_CASE("ChdWriter: the threaded file is byte for byte the single-threaded one") {
    TempDir dir("chd_threads");
    const SyntheticDisc disc(203, 117); // 40+ hunks: the queue of 4 is full again and again
    string error;
    REQUIRE_MESSAGE(writeDisc(disc, dir.path() + "/sync.chd", true, error), error);
    REQUIRE_MESSAGE(writeDisc(disc, dir.path() + "/threaded.chd", false, error), error);
    const string a = readFile(dir.path() + "/sync.chd");
    CHECK(a.size() > 1000);
    CHECK(a == readFile(dir.path() + "/threaded.chd"));

    // and again with a tiny disc (fewer hunks than the queue holds)
    const SyntheticDisc small(5, 4);
    REQUIRE_MESSAGE(writeDisc(small, dir.path() + "/sync2.chd", true, error), error);
    REQUIRE_MESSAGE(writeDisc(small, dir.path() + "/threaded2.chd", false, error), error);
    CHECK(readFile(dir.path() + "/sync2.chd") == readFile(dir.path() + "/threaded2.chd"));
}

#ifndef AB_NO_CHD

TEST_CASE("ChdWriter: a threaded write reads back sector by sector through libchdr") {
    TempDir dir("chd_threads_read");
    const SyntheticDisc disc(203, 117);
    const string path = dir.path() + "/disc.chd";
    string error;
    REQUIRE_MESSAGE(writeDisc(disc, path, false, error), error);
    Chd chd;
    REQUIRE(chd.open(path));
    vector<uint8_t> frames;
    REQUIRE(chd.frames(frames));
    for (uint32_t i = 0; i < 203; i++) {
        INFO("data sector " << i);
        CHECK(memcmp(&frames[i * ChdWriter::FrameBytes], disc.sectors[i].data(), S) == 0);
    }
    const uint32_t audioStart = ChdWriter::paddedFrames(203);
    for (uint32_t i = 0; i < 117; i++) {
        INFO("audio sector " << i);
        CHECK(memcmp(&frames[(audioStart + i) * ChdWriter::FrameBytes], bigEndian(disc.sectors[203 + i]).data(), S) ==
              0);
    }
}

#endif

TEST_CASE("ChdWriter: abort in the middle of a write joins the worker and writes nothing after it") {
    TempDir dir("chd_abort");
    const SyntheticDisc disc(203, 117);
    const string path = dir.path() + "/a.chd";
    string error;
    ChdWriter w;
    REQUIRE_MESSAGE(w.open(path, disc.tracks, error), error);
    for (size_t i = 0; i < 150; i++)
        REQUIRE_MESSAGE(w.writeSector(disc.sectors[i].data(), error), error);
    w.abort();
    const string atAbort = readFile(path);
    CHECK_FALSE(w.writeSector(disc.sectors[150].data(), error)); // closed
    CHECK_FALSE(w.finish(error));
    CHECK(readFile(path) == atAbort); // the joined worker wrote no more
    remove(path.c_str());
    CHECK_FALSE(exists(path));

    // abort twice, and a writer destroyed with hunks in flight, both join cleanly
    w.abort();
    {
        ChdWriter busy;
        REQUIRE_MESSAGE(busy.open(dir.path() + "/b.chd", disc.tracks, error), error);
        for (size_t i = 0; i < 100; i++)
            REQUIRE_MESSAGE(busy.writeSector(disc.sectors[i].data(), error), error);
    }
    // the writer can be used again after an abort
    REQUIRE_MESSAGE(w.open(path, disc.tracks, error), error);
    for (const auto &s : disc.sectors)
        REQUIRE_MESSAGE(w.writeSector(s.data(), error), error);
    CHECK_MESSAGE(w.finish(error), error);
}

TEST_CASE("ChdWriter: a write error of the worker comes back from writeSector or finish") {
    ifstream probe("/dev/full");
    if (!probe.good())
        return; // no such device here
    probe.close();
    const SyntheticDisc disc(203, 117);
    ChdWriter w;
    string error;
    if (!w.open("/dev/full", disc.tracks, error))
        return; // the header block is rejected at once: also an error, nothing to test about the worker
    bool failed = false;
    for (const auto &s : disc.sectors)
        if (!w.writeSector(s.data(), error)) {
            failed = true;
            break;
        }
    if (!failed)
        failed = !w.finish(error);
    CHECK(failed);
    CHECK_FALSE(error.empty());
    CHECK_FALSE(w.writeSector(disc.sectors[0].data(), error)); // and it stays failed
}
