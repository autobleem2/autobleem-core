#include "chd_writer.h"

#include "flac_encoder.h"

#include <miniz.h>

#ifndef AB_NO_CHD
#include <LzmaEnc.h>
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

using namespace std;

namespace {

const uint32_t TagCht2 = 0x43485432;   // 'CHT2'
const uint32_t CodecCdlz = 0x63646c7a; // 'cdlz'
const uint32_t CodecCdzl = 0x63647a6c; // 'cdzl'
const uint32_t CodecCdfl = 0x6364666c; // 'cdfl'
// a hunk's type is the slot of its codec in the header (cdlz, cdzl, cdfl, none), as chdman lists them for a CD
const uint8_t TypeCdlz = 0;
const uint8_t TypeCdzl = 1;
const uint8_t TypeCdfl = 2;
const uint8_t TypeStored = 4; // COMPRESSION_NONE
const uint8_t FirstNonCodecType = 4;

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

#ifndef AB_NO_CHD
void *lzmaAlloc(ISzAllocPtr, size_t size) {
    return malloc(size);
}

void lzmaFree(ISzAllocPtr, void *address) {
    free(address);
}

// The raw LZMA stream of a hunk's sectors, with the properties libchdr's decoder derives for itself (level 9,
// the dictionary sized for the sector data of one hunk: it builds the decoder from the same call, because the
// format carries no properties). The encoder is made once and reused for every hunk.
class LzmaHunkEncoder {
public:
    LzmaHunkEncoder() {
        alloc_.Alloc = lzmaAlloc;
        alloc_.Free = lzmaFree;
        CLzmaEncProps props;
        LzmaEncProps_Init(&props);
        props.level = 9;
        props.reduceSize = ChdWriter::FramesPerHunk * ChdWriter::SectorBytes;
        LzmaEncProps_Normalize(&props);
        enc_ = LzmaEnc_Create(&alloc_);
        if (enc_ != nullptr && LzmaEnc_SetProps(enc_, &props) != SZ_OK) {
            LzmaEnc_Destroy(enc_, &alloc_, &alloc_);
            enc_ = nullptr;
        }
    }
    ~LzmaHunkEncoder() {
        if (enc_ != nullptr)
            LzmaEnc_Destroy(enc_, &alloc_, &alloc_);
    }
    LzmaHunkEncoder(const LzmaHunkEncoder &) = delete;
    LzmaHunkEncoder &operator=(const LzmaHunkEncoder &) = delete;

    // 0 when it does not fit in 'capacity' (or the encoder could not be made)
    size_t encode(const uint8_t *src, size_t size, uint8_t *dest, size_t capacity) {
        if (enc_ == nullptr)
            return 0;
        SizeT length = capacity;
        if (LzmaEnc_MemEncode(enc_, dest, &length, src, size, 0, nullptr, &alloc_, &alloc_) != SZ_OK)
            return 0;
        return length;
    }

private:
    ISzAlloc alloc_;
    CLzmaEncHandle enc_ = nullptr;
};
#endif

// the compression of one hunk's sector data and subcode, the parts every CD codec stores separately
struct HunkParts {
    explicit HunkParts(const uint8_t *hunk)
        : base(ChdWriter::FramesPerHunk * ChdWriter::SectorBytes),
          sub(ChdWriter::FramesPerHunk * (ChdWriter::FrameBytes - ChdWriter::SectorBytes)),
          packedSub(ChdWriter::HunkBytes) {
        const size_t subBytes = ChdWriter::FrameBytes - ChdWriter::SectorBytes;
        for (uint32_t f = 0; f < ChdWriter::FramesPerHunk; f++) {
            memcpy(&base[f * ChdWriter::SectorBytes], hunk + f * ChdWriter::FrameBytes, ChdWriter::SectorBytes);
            memcpy(&sub[f * subBytes], hunk + f * ChdWriter::FrameBytes + ChdWriter::SectorBytes, subBytes);
        }
        packedSubBytes = rawDeflate(sub.data(), sub.size(), packedSub.data(), packedSub.size());
    }
    vector<uint8_t> base, sub, packedSub;
    size_t packedSubBytes = 0; // 0: did not compress
};

// cdlz and cdzl: the ECC bitmap (nothing stripped), the length of the compressed sectors (2 bytes: a hunk is
// under 64 KB), the compressed sectors, the deflated subcode - libchdr's cdlz_codec_decompress / cdzl_codec_decompress
bool packCdBase(const HunkParts &parts, const uint8_t *packedBase, size_t baseBytes, vector<uint8_t> &out) {
    const size_t header = (ChdWriter::FramesPerHunk + 7) / 8 + 2;
    if (baseBytes == 0 || parts.packedSubBytes == 0 || baseBytes > 0xffff ||
        header + baseBytes + parts.packedSubBytes >= ChdWriter::HunkBytes)
        return false;
    out.assign(header, 0);
    out[header - 2] = static_cast<uint8_t>(baseBytes >> 8);
    out[header - 1] = static_cast<uint8_t>(baseBytes);
    out.insert(out.end(), packedBase, packedBase + baseBytes);
    out.insert(out.end(), parts.packedSub.begin(), parts.packedSub.begin() + parts.packedSubBytes);
    return true;
}

bool encodeCdzl(const HunkParts &parts, vector<uint8_t> &out) {
    vector<uint8_t> packed(ChdWriter::HunkBytes);
    const size_t b = rawDeflate(parts.base.data(), parts.base.size(), packed.data(), packed.size());
    return packCdBase(parts, packed.data(), b, out);
}

bool encodeCdlz(const HunkParts &parts, vector<uint8_t> &out) {
#ifdef AB_NO_CHD
    (void)parts;
    (void)out;
    return false;
#else
    static thread_local LzmaHunkEncoder encoder;
    vector<uint8_t> packed(ChdWriter::HunkBytes);
    const size_t b = encoder.encode(parts.base.data(), parts.base.size(), packed.data(), packed.size());
    return packCdBase(parts, packed.data(), b, out);
#endif
}

// cdfl: the sectors as a FLAC stream of 16-bit stereo samples, then the deflated subcode up to the end of the
// hunk's data - libchdr's cdfl_codec_decompress. The samples are the hunk's bytes read big-endian (what chdman
// stores CD audio as); the block size is cdfl_codec_blocksize: the sector data in samples, halved to at most 2352.
bool encodeCdfl(const HunkParts &parts, vector<uint8_t> &out) {
    if (parts.packedSubBytes == 0)
        return false;
    const size_t samples = parts.base.size() / 4;
    vector<int16_t> pcm(samples * 2);
    for (size_t i = 0; i < pcm.size(); i++)
        pcm[i] = static_cast<int16_t>((parts.base[2 * i] << 8) | parts.base[2 * i + 1]);
    uint32_t blockSize = static_cast<uint32_t>(samples);
    while (blockSize > ChdWriter::SectorBytes)
        blockSize /= 2;
    out = FlacEncoder::encode(pcm.data(), samples, blockSize);
    if (out.size() + parts.packedSubBytes >= ChdWriter::HunkBytes)
        return false;
    out.insert(out.end(), parts.packedSub.begin(), parts.packedSub.begin() + parts.packedSubBytes);
    return true;
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

bool ChdWriter::cdlzHunk(const uint8_t *hunk, vector<uint8_t> &out) {
    return encodeCdlz(HunkParts(hunk), out);
}

bool ChdWriter::cdzlHunk(const uint8_t *hunk, vector<uint8_t> &out) {
    return encodeCdzl(HunkParts(hunk), out);
}

bool ChdWriter::cdflHunk(const uint8_t *hunk, vector<uint8_t> &out) {
    return encodeCdfl(HunkParts(hunk), out);
}

uint8_t ChdWriter::smallestHunk(const uint8_t *hunk, vector<uint8_t> &out) {
    const HunkParts parts(hunk);
    uint8_t type = TypeStored;
    vector<uint8_t> candidate;
    // chdman's compressor keeps the first of the smallest, in the order of the header
    const struct {
        uint8_t type;
        bool (*encode)(const HunkParts &, vector<uint8_t> &);
    } codecs[] = {{TypeCdlz, encodeCdlz}, {TypeCdzl, encodeCdzl}, {TypeCdfl, encodeCdfl}};
    for (const auto &c : codecs) {
        if (c.encode(parts, candidate) && (type == TypeStored || candidate.size() < out.size())) {
            type = c.type;
            out.swap(candidate);
        }
    }
    return type;
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
        if (h.type < FirstNonCodecType)
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
        if (h.type < FirstNonCodecType)
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
    stopWorker(false); // before the file closes: the worker writes nothing after this
    open_ = false;
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
    logicalBytes_ = 0;
    for (const Track &t : tracks_)
        logicalBytes_ += static_cast<uint64_t>(paddedFrames(t.frames)) * FrameBytes;
    entries_.clear();
    hunksOfType_.fill(0);
    entries_.reserve(hunkCount_);
    raw_ = Sha1();
    offset_ = 0;
    stop_ = closing_ = failed_ = false;
    queue_.clear();
    workerError_.clear();
    const vector<uint8_t> placeholder(HeaderBytes, 0); // the header is written last
    if (!put(placeholder.data(), placeholder.size(), error))
        return false;
    open_ = true;
    if (!synchronous_)
        worker_ = thread(&ChdWriter::workerLoop, this);
    return true;
}

bool ChdWriter::processHunk(const uint8_t *hunk, string &error) {
    const uint64_t before = static_cast<uint64_t>(hunksDone_) * HunkBytes;
    raw_.update(hunk, static_cast<size_t>(min<uint64_t>(HunkBytes, logicalBytes_ - before)));
    HunkEntry entry;
    entry.crc = crc16(hunk, HunkBytes);
    vector<uint8_t> packed;
    bool ok;
    entry.type = smallestHunk(hunk, packed);
    if (entry.type != TypeStored) {
        entry.length = static_cast<uint32_t>(packed.size());
        ok = put(packed.data(), packed.size(), error);
    } else {
        entry.length = HunkBytes;
        ok = put(hunk, HunkBytes, error);
    }
    hunksOfType_[entry.type]++;
    entries_.push_back(entry);
    hunksDone_++;
    return ok;
}

void ChdWriter::workerLoop() {
    for (;;) {
        vector<uint8_t> hunk;
        {
            unique_lock<mutex> lock(mutex_);
            notEmpty_.wait(lock, [this] { return stop_ || closing_ || !queue_.empty(); });
            if (stop_ || queue_.empty()) // empty and not stopped: finish() has queued the last hunk
                return;
            hunk = move(queue_.front());
            queue_.pop_front();
        }
        notFull_.notify_one();
        string error;
        bool ok = false;
        try {
            ok = processHunk(hunk.data(), error);
        } catch (const std::exception &e) { // an exception must not leave the thread (it would end the program)
            error = string("the CHD worker failed: ") + e.what();
        }
        if (!ok) {
            {
                lock_guard<mutex> lock(mutex_);
                failed_ = true;
                workerError_ = error;
            }
            notFull_.notify_all();
            return;
        }
    }
}

bool ChdWriter::stopWorker(bool drain) {
    {
        lock_guard<mutex> lock(mutex_);
        if (drain) {
            closing_ = true;
        } else {
            stop_ = true;
            queue_.clear();
        }
    }
    notEmpty_.notify_all();
    notFull_.notify_all();
    if (worker_.joinable())
        worker_.join();
    lock_guard<mutex> lock(mutex_);
    return !failed_;
}

bool ChdWriter::flushHunk(string &error) {
    if (synchronous_) {
        const bool ok = processHunk(hunk_.data(), error);
        fill(hunk_.begin(), hunk_.end(), 0);
        frameInHunk_ = 0;
        return ok;
    }
    {
        unique_lock<mutex> lock(mutex_);
        notFull_.wait(lock, [this] { return failed_ || queue_.size() < QueueHunks; });
        if (failed_) {
            error = workerError_;
            return false;
        }
        queue_.push_back(move(hunk_));
    }
    notEmpty_.notify_one();
    hunk_.assign(HunkBytes, 0);
    frameInHunk_ = 0;
    return true;
}

bool ChdWriter::pushFrame(const uint8_t *sector, bool swapBytes, string &error) {
    if (sector != nullptr) {
        uint8_t *frame = &hunk_[frameInHunk_ * FrameBytes];
        if (swapBytes) {
            for (uint32_t i = 0; i < SectorBytes; i += 2) {
                frame[i] = sector[i + 1];
                frame[i + 1] = sector[i];
            }
        } else {
            memcpy(frame, sector, SectorBytes);
        }
    }
    return ++frameInHunk_ < FramesPerHunk || flushHunk(error);
}

bool ChdWriter::writeSector(const uint8_t *sector, string &error) {
    if (!open_ || track_ >= tracks_.size()) {
        error = "more sectors than the tracks hold";
        return false;
    }
    {
        lock_guard<mutex> lock(mutex_);
        if (failed_) {
            error = workerError_;
            return false;
        }
    }
    // CD audio is stored big-endian, as chdman does (the drive delivers it little-endian)
    if (!pushFrame(sector, tracks_[track_].type == "AUDIO", error))
        return false;
    if (++inTrack_ == tracks_[track_].frames) {
        for (uint32_t pad = paddedFrames(inTrack_) - inTrack_; pad > 0; pad--)
            if (!pushFrame(nullptr, false, error))
                return false;
        track_++;
        inTrack_ = 0;
    }
    return true;
}

bool ChdWriter::finish(string &error) {
    if (!open_ || track_ != tracks_.size()) {
        error = "the image is not complete";
        return false;
    }
    while (frameInHunk_ != 0)
        if (!pushFrame(nullptr, false, error))
            return false;
    if (!stopWorker(true)) { // every hunk is written; offset_, entries_ and raw_ are ours again
        error = workerError_;
        return false;
    }

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
    be32(header, CodecCdlz);
    be32(header, CodecCdzl);
    be32(header, CodecCdfl);
    be32(header, 0);
    be64(header, logicalBytes_);
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
    open_ = false;
    return !out_.fail();
}
