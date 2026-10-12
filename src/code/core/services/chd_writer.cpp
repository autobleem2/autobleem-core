#include "chd_writer.h"

#include <miniz.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

using namespace std;

namespace {

const uint32_t TagCht2 = 0x43485432;   // 'CHT2'
const uint32_t CodecCdzl = 0x6364726c; // 'cdzl'
const uint8_t TypeCdzl = 0;            // the header's first compressor
const uint8_t TypeStored = 4;          // COMPRESSION_NONE

void be16(vector<uint8_t> &v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

void be32(vector<uint8_t> &v, uint32_t x) {
    be16(v, x >> 16);
    be16(v, x & 0xffff);
}

void be48(vector<uint8_t> &v, uint64_t x) {
    be16(v, static_cast<uint32_t>(x >> 32));
    be32(v, static_cast<uint32_t>(x));
}

void be64(vector<uint8_t> &v, uint64_t x) {
    be32(v, static_cast<uint32_t>(x >> 32));
    be32(v, static_cast<uint32_t>(x));
}

// most significant bit first, as libchdr's bitstream reads
class BitWriter {
public:
    void put(uint32_t value, int bits) {
        for (int i = bits - 1; i >= 0; i--) {
            current_ = static_cast<uint8_t>((current_ << 1) | ((value >> i) & 1));
            if (++used_ == 8) {
                bytes_.push_back(current_);
                current_ = 0;
                used_ = 0;
            }
        }
    }
    vector<uint8_t> finish() {
        while (used_ != 0)
            put(0, 1);
        return bytes_;
    }

private:
    vector<uint8_t> bytes_;
    uint8_t current_ = 0;
    int used_ = 0;
};

int bitsFor(uint32_t value) {
    int bits = 0;
    while (value != 0) {
        bits++;
        value >>= 1;
    }
    return bits;
}

// raw deflate (no zlib header), what the CHD zlib codec inflates; 0 when it does not fit in 'capacity'
size_t rawDeflate(const uint8_t *src, size_t size, uint8_t *dest, size_t capacity) {
    const int flags = static_cast<int>(tdefl_create_comp_flags_from_zip_params(9, -15, MZ_DEFAULT_STRATEGY));
    return tdefl_compress_mem_to_mem(dest, capacity, src, size, flags);
}

const array<uint16_t, 256> &crcTable() {
    static const array<uint16_t, 256> table = [] {
        array<uint16_t, 256> t;
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i << 8;
            for (int b = 0; b < 8; b++)
                c = (c & 0x8000) ? (c << 1) ^ 0x1021 : c << 1;
            t[i] = static_cast<uint16_t>(c);
        }
        return t;
    }();
    return table;
}

} // namespace

//*******************************
// Sha1
//*******************************
Sha1::Sha1() {
    h_[0] = 0x67452301;
    h_[1] = 0xefcdab89;
    h_[2] = 0x98badcfe;
    h_[3] = 0x10325476;
    h_[4] = 0xc3d2e1f0;
}

void Sha1::block(const uint8_t *p) {
    auto rol = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = static_cast<uint32_t>(p[i * 4]) << 24 | static_cast<uint32_t>(p[i * 4 + 1]) << 16 |
               static_cast<uint32_t>(p[i * 4 + 2]) << 8 | p[i * 4 + 3];
    for (int i = 16; i < 80; i++)
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5a827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ed9eba1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdc;
        } else {
            f = b ^ c ^ d;
            k = 0xca62c1d6;
        }
        const uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol(b, 30);
        b = a;
        a = t;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
}

void Sha1::update(const uint8_t *data, size_t size) {
    total_ += size;
    while (size > 0) {
        if (used_ == 0 && size >= 64) {
            block(data);
            data += 64;
            size -= 64;
            continue;
        }
        const size_t n = min(size, 64 - used_);
        memcpy(buffer_ + used_, data, n);
        used_ += n;
        data += n;
        size -= n;
        if (used_ == 64) {
            block(buffer_);
            used_ = 0;
        }
    }
}

void Sha1::final(uint8_t *digest) {
    const uint64_t bits = total_ * 8;
    const uint8_t one = 0x80;
    const uint8_t zero = 0;
    update(&one, 1);
    while (used_ != 56)
        update(&zero, 1);
    uint8_t length[8];
    for (int i = 0; i < 8; i++)
        length[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
    update(length, 8);
    for (int i = 0; i < 5; i++) {
        digest[i * 4] = static_cast<uint8_t>(h_[i] >> 24);
        digest[i * 4 + 1] = static_cast<uint8_t>(h_[i] >> 16);
        digest[i * 4 + 2] = static_cast<uint8_t>(h_[i] >> 8);
        digest[i * 4 + 3] = static_cast<uint8_t>(h_[i]);
    }
}

string Sha1::hex(const uint8_t *digest) {
    string out;
    char two[3];
    for (int i = 0; i < 20; i++) {
        snprintf(two, sizeof(two), "%02x", digest[i]);
        out += two;
    }
    return out;
}

//*******************************
// ChdWriter: the parts
//*******************************
// C++14: the static constants are odr-used by the tests (a CHECK takes them by reference)
const uint32_t ChdWriter::SectorBytes;
const uint32_t ChdWriter::FrameBytes;
const uint32_t ChdWriter::FramesPerHunk;
const uint32_t ChdWriter::HunkBytes;
const uint32_t ChdWriter::TrackPadding;
const uint32_t ChdWriter::HeaderBytes;

uint32_t ChdWriter::paddedFrames(uint32_t frames) {
    return (frames + TrackPadding - 1) / TrackPadding * TrackPadding;
}

uint32_t ChdWriter::totalFrames(const vector<Track> &tracks) {
    uint32_t total = 0;
    for (const Track &t : tracks)
        total += paddedFrames(t.frames);
    return (total + FramesPerHunk - 1) / FramesPerHunk * FramesPerHunk;
}

string ChdWriter::trackMetadata(const Track &track) {
    char text[160];
    snprintf(text, sizeof(text), "TRACK:%d TYPE:%s SUBTYPE:NONE FRAMES:%u PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0",
             track.number, track.type.c_str(), track.frames);
    return text;
}

uint16_t ChdWriter::crc16(const uint8_t *data, size_t size) {
    const array<uint16_t, 256> &table = crcTable();
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < size; i++)
        crc = static_cast<uint16_t>((crc << 8) ^ table[(crc >> 8) ^ data[i]]);
    return crc;
}

bool ChdWriter::cdzlHunk(const uint8_t *hunk, vector<uint8_t> &out) {
    const size_t sectors = FramesPerHunk * SectorBytes;
    const size_t subs = FramesPerHunk * (FrameBytes - SectorBytes);
    vector<uint8_t> base(sectors), sub(subs), packedBase(HunkBytes), packedSub(HunkBytes);
    for (uint32_t f = 0; f < FramesPerHunk; f++) {
        memcpy(&base[f * SectorBytes], hunk + f * FrameBytes, SectorBytes);
        memcpy(&sub[f * (FrameBytes - SectorBytes)], hunk + f * FrameBytes + SectorBytes, FrameBytes - SectorBytes);
    }
    const size_t b = rawDeflate(base.data(), base.size(), packedBase.data(), packedBase.size());
    const size_t s = rawDeflate(sub.data(), sub.size(), packedSub.data(), packedSub.size());
    const size_t header = (FramesPerHunk + 7) / 8 + 2; // the ECC bitmap (none stripped), the base length
    if (b == 0 || s == 0 || header + b + s >= HunkBytes)
        return false;
    out.assign(header, 0);
    out[header - 2] = static_cast<uint8_t>(b >> 8);
    out[header - 1] = static_cast<uint8_t>(b);
    out.insert(out.end(), packedBase.begin(), packedBase.begin() + b);
    out.insert(out.end(), packedSub.begin(), packedSub.begin() + s);
    return true;
}

vector<uint8_t> ChdWriter::packMap(const vector<HunkEntry> &hunks, uint64_t firstOffset) {
    // the map as the reader rebuilds it, for its CRC: type, length (24 bits), offset (48), CRC-16
    vector<uint8_t> raw;
    uint64_t at = firstOffset;
    uint32_t longest = 0;
    for (const HunkEntry &h : hunks) {
        raw.push_back(h.type);
        raw.push_back(static_cast<uint8_t>(h.length >> 16));
        be16(raw, h.length & 0xffff);
        be48(raw, at);
        be16(raw, h.crc);
        at += h.length;
        if (h.type == TypeCdzl)
            longest = max(longest, h.length);
    }
    const int lengthBits = max(1, bitsFor(longest));

    BitWriter bits;
    // the types' huffman code, as an RLE-coded tree: 16 symbols, each 4 bits long (a complete code, symbol = code)
    for (int i = 0; i < 16; i++)
        bits.put(4, 4);
    for (const HunkEntry &h : hunks)
        bits.put(h.type, 4);
    for (const HunkEntry &h : hunks) {
        if (h.type == TypeCdzl)
            bits.put(h.length, lengthBits);
        bits.put(h.crc, 16);
    }
    const vector<uint8_t> packed = bits.finish();

    vector<uint8_t> map;
    be32(map, static_cast<uint32_t>(packed.size()));
    be48(map, firstOffset);
    be16(map, crc16(raw.data(), raw.size()));
    map.push_back(static_cast<uint8_t>(lengthBits));
    map.push_back(0); // selfbits
    map.push_back(0); // parentbits
    map.push_back(0);
    map.insert(map.end(), packed.begin(), packed.end());
    return map;
}

//*******************************
// ChdWriter: the file
//*******************************
ChdWriter::~ChdWriter() {
    abort();
}

void ChdWriter::abort() {
    if (out_.is_open())
        out_.close();
}

bool ChdWriter::put(const uint8_t *data, size_t size, string &error) {
    out_.write(reinterpret_cast<const char *>(data), static_cast<streamsize>(size));
    if (!out_) {
        error = "the CHD file cannot be written (disk full?)";
        return false;
    }
    offset_ += size;
    return true;
}

bool ChdWriter::open(const string &path, const vector<Track> &tracks, string &error) {
    abort();
    if (tracks.empty()) {
        error = "a CHD needs at least one track";
        return false;
    }
    for (const Track &t : tracks) {
        if (t.frames == 0) {
            error = "a track has no sectors";
            return false;
        }
    }
    out_.open(path, ios::binary | ios::trunc);
    if (!out_) {
        error = "cannot create " + path;
        return false;
    }
    tracks_ = tracks;
    track_ = 0;
    inTrack_ = 0;
    hunk_.assign(HunkBytes, 0);
    frameInHunk_ = 0;
    hunksDone_ = 0;
    hunkCount_ = totalFrames(tracks_) / FramesPerHunk;
    entries_.clear();
    entries_.reserve(hunkCount_);
    raw_ = Sha1();
    offset_ = 0;
    const vector<uint8_t> placeholder(HeaderBytes, 0); // the header is written last
    return put(placeholder.data(), placeholder.size(), error);
}

bool ChdWriter::flushHunk(string &error) {
    raw_.update(hunk_.data(), HunkBytes);
    HunkEntry entry;
    entry.crc = crc16(hunk_.data(), HunkBytes);
    vector<uint8_t> packed;
    bool ok;
    if (cdzlHunk(hunk_.data(), packed)) {
        entry.type = TypeCdzl;
        entry.length = static_cast<uint32_t>(packed.size());
        ok = put(packed.data(), packed.size(), error);
    } else {
        entry.type = TypeStored;
        entry.length = HunkBytes;
        ok = put(hunk_.data(), HunkBytes, error);
    }
    entries_.push_back(entry);
    hunksDone_++;
    fill(hunk_.begin(), hunk_.end(), 0);
    frameInHunk_ = 0;
    return ok;
}

bool ChdWriter::pushFrame(const uint8_t *sector, string &error) {
    if (sector != nullptr)
        memcpy(&hunk_[frameInHunk_ * FrameBytes], sector, SectorBytes);
    return ++frameInHunk_ < FramesPerHunk || flushHunk(error);
}

bool ChdWriter::writeSector(const uint8_t *sector, string &error) {
    if (!out_.is_open() || track_ >= tracks_.size()) {
        error = "more sectors than the tracks hold";
        return false;
    }
    if (!pushFrame(sector, error))
        return false;
    if (++inTrack_ == tracks_[track_].frames) {
        for (uint32_t pad = paddedFrames(inTrack_) - inTrack_; pad > 0; pad--)
            if (!pushFrame(nullptr, error))
                return false;
        track_++;
        inTrack_ = 0;
    }
    return true;
}

bool ChdWriter::finish(string &error) {
    if (!out_.is_open() || track_ != tracks_.size()) {
        error = "the image is not complete";
        return false;
    }
    while (frameInHunk_ != 0)
        if (!pushFrame(nullptr, error))
            return false;

    const uint64_t mapOffset = offset_;
    const vector<uint8_t> map = packMap(entries_, HeaderBytes);
    if (!put(map.data(), map.size(), error))
        return false;

    // the metadata, one CHT2 entry a track, linked through 'next'
    const uint64_t metaOffset = offset_;
    vector<array<uint8_t, 24>> hashes;
    for (size_t i = 0; i < tracks_.size(); i++) {
        string text = trackMetadata(tracks_[i]);
        text.push_back('\0');
        vector<uint8_t> entry;
        be32(entry, TagCht2);
        be32(entry, 0x01000000 | static_cast<uint32_t>(text.size())); // flags: checksummed; 24-bit length
        be64(entry, i + 1 < tracks_.size() ? offset_ + 16 + text.size() : 0);
        entry.insert(entry.end(), text.begin(), text.end());
        if (!put(entry.data(), entry.size(), error))
            return false;
        array<uint8_t, 24> hash;
        hash[0] = static_cast<uint8_t>(TagCht2 >> 24);
        hash[1] = static_cast<uint8_t>(TagCht2 >> 16);
        hash[2] = static_cast<uint8_t>(TagCht2 >> 8);
        hash[3] = static_cast<uint8_t>(TagCht2);
        Sha1 s;
        s.update(reinterpret_cast<const uint8_t *>(text.data()), text.size());
        s.final(&hash[4]);
        hashes.push_back(hash);
    }
    sort(hashes.begin(), hashes.end());

    uint8_t rawSha[20], overall[20];
    Sha1 r = raw_;
    r.final(rawSha);
    Sha1 o;
    o.update(rawSha, 20);
    for (const auto &h : hashes)
        o.update(h.data(), 24);
    o.final(overall);

    vector<uint8_t> header = {'M', 'C', 'o', 'm', 'p', 'r', 'H', 'D'};
    be32(header, HeaderBytes);
    be32(header, 5);
    be32(header, CodecCdzl);
    be32(header, 0);
    be32(header, 0);
    be32(header, 0);
    be64(header, static_cast<uint64_t>(hunkCount_) * HunkBytes);
    be64(header, mapOffset);
    be64(header, metaOffset);
    be32(header, HunkBytes);
    be32(header, FrameBytes);
    header.insert(header.end(), rawSha, rawSha + 20);
    header.insert(header.end(), overall, overall + 20);
    header.insert(header.end(), 20, 0); // no parent
    out_.seekp(0);
    out_.write(reinterpret_cast<const char *>(header.data()), static_cast<streamsize>(header.size()));
    out_.flush();
    if (!out_) {
        error = "the CHD header cannot be written";
        return false;
    }
    out_.close();
    return !out_.fail();
}
