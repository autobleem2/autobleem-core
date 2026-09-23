#include "ableem/engine/xz_file.h"
#include "ableem/engine/filesystem.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <vector>

#ifndef ABLEEM_NO_CHD
#include <7zCrc.h>
#include <Sha256.h>
#include <Xz.h>
#include <XzCrc64.h>
#endif

using namespace std;

namespace ableem {

#ifdef ABLEEM_NO_CHD

bool XzFile::decode(const string &, const Sink &, string &error, const Progress &) {
    error = "xz files are not supported in this build";
    return false;
}
bool XzFile::unpackedSize(const string &, uint64_t &) {
    return false;
}

#else

namespace {
void *xzAlloc(ISzAllocPtr, size_t size) {
    return size == 0 ? nullptr : malloc(size);
}
void xzFree(ISzAllocPtr, void *address) {
    free(address);
}
const ISzAlloc xzAllocator = {xzAlloc, xzFree};

const size_t InputBufferSize = 1 << 16;
const size_t OutputBufferSize = 1 << 20;

//*******************************
// the SDK's tables, once per process
//*******************************
void prepareTables() {
    static once_flag once;
    call_once(once, [] {
        CrcGenerateTable();
        Crc64GenerateTable();
        Sha256Prepare();
    });
}

string describe(SRes res) {
    switch (res) {
    case SZ_ERROR_MEM:
        return "out of memory";
    case SZ_ERROR_DATA:
        return "the compressed data is damaged";
    case SZ_ERROR_UNSUPPORTED:
        return "the file uses a filter this build cannot read";
    case SZ_ERROR_CRC:
        return "a checksum does not match - the file is damaged";
    case SZ_ERROR_NO_ARCHIVE:
        return "not an xz file";
    default:
        return "xz error " + to_string(res);
    }
}

//*******************************
// xz's multibyte integers: 7 bits a byte, low first, at most 9 bytes
//*******************************
bool readVarint(const vector<uint8_t> &buf, size_t &pos, uint64_t &value) {
    value = 0;
    for (int i = 0; i < 9 && pos < buf.size(); ++i) {
        uint8_t b = buf[pos++];
        value |= static_cast<uint64_t>(b & 0x7f) << (7 * i);
        if ((b & 0x80) == 0)
            return true;
    }
    return false;
}

uint32_t le32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}

bool readAt(ifstream &in, uint64_t pos, uint8_t *buf, size_t size) {
    in.clear();
    in.seekg(static_cast<streamoff>(pos));
    in.read(reinterpret_cast<char *>(buf), static_cast<streamsize>(size));
    return static_cast<size_t>(in.gcount()) == size;
}
} // namespace

//*******************************
// XzFile::decode
//*******************************
bool XzFile::decode(const string &path, const Sink &sink, string &error, const Progress &progress) {
    ifstream in(path, ios::binary);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    long long fileSize = DirEntry::fileSize(path);
    prepareTables();

    CXzUnpacker xz;
    XzUnpacker_Construct(&xz, &xzAllocator);
    XzUnpacker_Init(&xz);
    vector<Byte> input(InputBufferSize);
    vector<Byte> output(OutputBufferSize);
    size_t inPos = 0;
    size_t inLen = 0;
    bool inputDone = false;
    uint64_t readTotal = 0;
    bool ok = true;

    for (;;) {
        if (inPos == inLen && !inputDone) {
            in.read(reinterpret_cast<char *>(input.data()), static_cast<streamsize>(input.size()));
            inLen = static_cast<size_t>(in.gcount());
            inPos = 0;
            readTotal += inLen;
            if (inLen < input.size())
                inputDone = true;
            if (progress)
                progress(readTotal, fileSize < 0 ? 0 : static_cast<uint64_t>(fileSize));
        }
        SizeT outLen = output.size();
        SizeT srcLen = inLen - inPos;
        ECoderStatus status;
        SRes res = XzUnpacker_Code(&xz, output.data(), &outLen, input.data() + inPos, &srcLen, inputDone ? 1 : 0,
                                   CODER_FINISH_ANY, &status);
        inPos += srcLen;
        if (outLen > 0 && !sink(output.data(), outLen)) {
            error = "stopped";
            ok = false;
            break;
        }
        if (res != SZ_OK) {
            // a finished stream followed by something that is not another stream is
            // trailing garbage
            error = res == SZ_ERROR_NO_ARCHIVE && XzUnpacker_IsStreamWasFinished(&xz)
                        ? "unexpected data after the end of the xz stream"
                        : describe(res);
            ok = false;
            break;
        }
        if (inputDone && inPos == inLen && srcLen == 0 && outLen == 0)
            break;
    }
    if (ok && !XzUnpacker_IsStreamWasFinished(&xz)) {
        error = readTotal == 0 ? "the file is empty" : "the file ends before the xz stream does - incomplete download?";
        ok = false;
    }
    XzUnpacker_Free(&xz);
    return ok;
}

//*******************************
// XzFile::unpackedSize
//*******************************
// From the end: stream padding (zero groups of four), the 12-byte footer
// (CRC32, backward size, flags, "YZ"), the index it points back to (0x00, the
// record count, then unpadded + uncompressed size per block), and the 12-byte
// header before the blocks - which is where the stream before this one ends.
bool XzFile::unpackedSize(const string &path, uint64_t &size) {
    ifstream in(path, ios::binary);
    long long fileSize = DirEntry::fileSize(path);
    if (!in || fileSize < 32)
        return false;
    static const uint8_t HeaderMagic[6] = {0xfd, '7', 'z', 'X', 'Z', 0x00};
    const uint64_t MaxIndexSize = 64ull << 20; // a 4 GB image has a few thousand blocks at most
    uint64_t pos = static_cast<uint64_t>(fileSize);
    uint64_t total = 0;
    while (pos > 0) {
        uint8_t group[4];
        while (pos >= 4 && readAt(in, pos - 4, group, 4) && le32(group) == 0)
            pos -= 4;
        if (pos < 24 || pos % 4 != 0)
            return false;
        uint8_t footer[12];
        if (!readAt(in, pos - 12, footer, 12) || footer[10] != 'Y' || footer[11] != 'Z')
            return false;
        uint64_t indexSize = (static_cast<uint64_t>(le32(footer + 4)) + 1) * 4;
        if (indexSize > MaxIndexSize || indexSize + 12 + 12 > pos)
            return false;
        uint64_t indexPos = pos - 12 - indexSize;
        vector<uint8_t> index(indexSize);
        if (!readAt(in, indexPos, index.data(), index.size()) || index[0] != 0)
            return false;
        size_t at = 1;
        uint64_t records = 0;
        if (!readVarint(index, at, records))
            return false;
        uint64_t blocks = 0;
        for (uint64_t i = 0; i < records; ++i) {
            uint64_t unpadded = 0;
            uint64_t unpacked = 0;
            if (!readVarint(index, at, unpadded) || !readVarint(index, at, unpacked))
                return false;
            blocks += (unpadded + 3) & ~static_cast<uint64_t>(3);
            total += unpacked;
        }
        if (blocks + 12 > indexPos)
            return false;
        uint64_t streamStart = indexPos - blocks - 12;
        uint8_t header[6];
        if (!readAt(in, streamStart, header, 6) || memcmp(header, HeaderMagic, 6) != 0)
            return false;
        pos = streamStart;
    }
    size = total;
    return true;
}

#endif

} // namespace ableem
