//
// PbpImage - see the header.
//
#include <ableem/engine/filesystem.h>
#include <ableem/engine/param_sfo.h>
#include <ableem/engine/pbp_image.h>

#include <cstring>
#include <fstream>
#include <vector>

using namespace std;

namespace ableem {

namespace {

uint32_t le32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}

const uint8_t PgdMagic[4] = {0x00, 'P', 'G', 'D'};

// one PSISOIMG at `at`: false when it is not one; `protectedDisc` when its +0x400 is the NP DRM block
bool readDisc(const PbpImage::Reader &read, uint64_t size, uint64_t at, bool &protectedDisc) {
    uint8_t magic[12];
    if (at + 0x404 > size || read(at, magic, sizeof(magic)) != sizeof(magic) ||
        memcmp(magic, "PSISOIMG0000", 12) != 0)
        return false;
    uint8_t pgd[4];
    protectedDisc = read(at + 0x400, pgd, sizeof(pgd)) == sizeof(pgd) && memcmp(pgd, PgdMagic, 4) == 0;
    return true;
}

} // namespace

//*******************************
// PbpImage::inspect
//*******************************
PbpInfo PbpImage::inspect(const string &path) {
    const long long size = DirEntry::fileSize(path);
    if (size <= 0)
        return PbpInfo();
    ifstream in(path, ios::binary);
    if (!in)
        return PbpInfo();
    return inspect(
        [&in](uint64_t offset, uint8_t *buffer, size_t length) -> size_t {
            in.clear();
            in.seekg(static_cast<streamoff>(offset));
            in.read(reinterpret_cast<char *>(buffer), static_cast<streamsize>(length));
            return static_cast<size_t>(in.gcount());
        },
        static_cast<uint64_t>(size));
}

PbpInfo PbpImage::inspect(const Reader &read, uint64_t size) {
    PbpInfo info;
    uint8_t header[0x28];
    if (size < sizeof(header) || read(0, header, sizeof(header)) != sizeof(header) ||
        memcmp(header, "\0PBP", 4) != 0)
        return info;
    uint32_t offsets[8];
    for (int i = 0; i < 8; i++)
        offsets[i] = le32(header + 8 + 4 * i);
    // PARAM.SFO runs from the first offset to the second
    if (offsets[0] > offsets[1] || offsets[1] > size || offsets[1] - offsets[0] > 64 * 1024)
        return info;
    vector<uint8_t> sfo(offsets[1] - offsets[0]);
    if (read(offsets[0], sfo.data(), sfo.size()) != sfo.size() || !ParamSfo::parse(sfo.data(), sfo.size(), info.sfo))
        return info;
    info.valid = true;

    const uint64_t psar = offsets[7];
    uint8_t magic[16];
    if (psar + sizeof(magic) > size || read(psar, magic, sizeof(magic)) != sizeof(magic))
        return info;
    bool protectedDisc = false;
    if (memcmp(magic, "PSISOIMG0000", 12) == 0) {
        if (readDisc(read, size, psar, protectedDisc)) {
            info.ps1 = true;
            info.discs = 1;
            info.licenceProtected = protectedDisc;
        }
    } else if (memcmp(magic, "PSTITLEIMG000000", 16) == 0) {
        // the discs' offsets, relative to DATA.PSAR, at +0x200 - a zero ends the table
        uint8_t table[5 * 4];
        if (read(psar + 0x200, table, sizeof(table)) != sizeof(table))
            return info;
        for (int i = 0; i < 5; i++) {
            const uint32_t disc = le32(table + 4 * i);
            if (disc == 0)
                break;
            if (!readDisc(read, size, psar + disc, protectedDisc))
                return info;
            info.discs++;
            info.licenceProtected = info.licenceProtected || protectedDisc;
        }
        info.ps1 = info.discs > 0;
    }
    return info;
}

} // namespace ableem
