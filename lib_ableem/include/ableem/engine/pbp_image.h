// lib_ableem - engine: what a PBP (EBOOT.PBP) holds, without reading the disc in it.
//
// A PBP is a header of eight section offsets (PARAM.SFO, icons, pictures, a sound, DATA.PSP, DATA.PSAR). A PS1
// game's DATA.PSAR is "PSISOIMG0000" (one disc) or "PSTITLEIMG000000" (a table of up to five PSISOIMGs at
// +0x200). In each PSISOIMG the disc's table of contents is at +0x800 and the compressed blocks' index at
// +0x4000 - which is what pcsx reads.
//
// A PS1 Classic as Sony sells it (PSN) keeps those under NP DRM: a "\0PGD" block at the PSISOIMG's +0x400,
// the TOC and the index encrypted with a key that comes with the buyer's licence. Such a PBP runs on a PSP or
// a Vita that holds the licence and nowhere else - pcsx-ab reads noise. `licenceProtected` says so, and the
// launcher refuses to start the game rather than start an emulator on it. Nothing here decrypts anything.
// A PBP made from a disc (popstation and its successors) has no PGD block and plays.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace ableem {

struct PbpInfo {
    bool valid = false;            // a PBP header, and a PARAM.SFO where it says
    bool ps1 = false;              // DATA.PSAR is a PS1 disc image (PSISOIMG / PSTITLEIMG)
    bool licenceProtected = false; // a disc of it carries the NP DRM block (see above)
    int discs = 0;
    std::map<std::string, std::string> sfo; // TITLE, DISC_ID, ...
};

class PbpImage {
public:
    // reads `length` bytes at `offset` into `buffer`, returns how many it could
    using Reader = std::function<size_t(uint64_t offset, uint8_t *buffer, size_t length)>;

    static PbpInfo inspect(const std::string &path);
    // the same over any byte source of `size` bytes - a PBP still inside a PSN package, say
    static PbpInfo inspect(const Reader &read, uint64_t size);
};

} // namespace ableem
