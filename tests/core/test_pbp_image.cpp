//
// ParamSfo, and PbpImage's licence check over made-up PBPs: a disc-made PBP plays, a PSN release (the NP DRM
// block at a disc's +0x400) is licence-protected.
//
#include "doctest/doctest.h"
#include "support/temp_dir.h"

#include <ableem/engine/param_sfo.h>
#include <ableem/engine/pbp_image.h>

#include <cstring>
#include <string>
#include <vector>

using ableem::ParamSfo;
using ableem::PbpImage;
using std::string;
using std::vector;

namespace {

void put32le(string &s, size_t at, uint32_t v) {
    for (int i = 0; i < 4; i++)
        s[at + i] = static_cast<char>(v >> (8 * i));
}

// a PARAM.SFO of string values
string sfo(const vector<std::pair<string, string>> &values) {
    string keys, data;
    string entries;
    for (const auto &v : values) {
        string e(16, '\0');
        e[0] = static_cast<char>(keys.size());
        e[1] = static_cast<char>(keys.size() >> 8);
        e[2] = 0x04;
        e[3] = 0x02;
        put32le(e, 4, static_cast<uint32_t>(v.second.size() + 1));
        put32le(e, 8, static_cast<uint32_t>(v.second.size() + 1));
        put32le(e, 12, static_cast<uint32_t>(data.size()));
        entries += e;
        keys += v.first + '\0';
        data += v.second + '\0';
    }
    while (keys.size() % 4)
        keys += '\0';
    string out(20, '\0');
    out[1] = 'P';
    out[2] = 'S';
    out[3] = 'F';
    out[4] = 1;
    out[5] = 1;
    put32le(out, 8, static_cast<uint32_t>(20 + entries.size()));
    put32le(out, 12, static_cast<uint32_t>(20 + entries.size() + keys.size()));
    put32le(out, 16, static_cast<uint32_t>(values.size()));
    return out + entries + keys + data;
}

// one PSISOIMG of 0x1000 bytes; `pgd` puts the NP DRM block at +0x400 as a PSN release has it
string psisoimg(bool pgd) {
    string d(0x1000, '\0');
    memcpy(&d[0], "PSISOIMG0000", 12);
    if (pgd)
        memcpy(&d[0x400], "\0PGD", 4);
    else
        memcpy(&d[0x400], "_SLUS_00001", 11); // what popstation writes there
    return d;
}

// a PBP: the header, the SFO, and DATA.PSAR = `psar`
string pbp(const string &psar) {
    const string s = sfo({{"DISC_ID", "SLUS00001"}, {"TITLE", "Test Game"}});
    string out(0x28, '\0');
    memcpy(&out[0], "\0PBP", 4);
    put32le(out, 4, 0x10000);
    const uint32_t sfoAt = 0x28;
    const uint32_t psarAt = static_cast<uint32_t>(0x28 + s.size());
    put32le(out, 8, sfoAt);
    for (int i = 1; i < 8; i++)
        put32le(out, 8 + 4 * i, psarAt);
    return out + s + psar;
}

// PSTITLEIMG with the given discs
string multiDisc(const vector<string> &discs) {
    string psar(0x400, '\0');
    memcpy(&psar[0], "PSTITLEIMG000000", 16);
    for (size_t i = 0; i < discs.size(); i++) {
        put32le(psar, 0x200 + 4 * i, static_cast<uint32_t>(psar.size()));
        psar += discs[i];
    }
    return psar;
}

} // namespace

TEST_CASE("ParamSfo: strings, and not an SFO") {
    const string s = sfo({{"TITLE", "Resident Evil"}, {"DISC_ID", "SLUS00923"}});
    std::map<string, string> values;
    REQUIRE(ParamSfo::parse(reinterpret_cast<const uint8_t *>(s.data()), s.size(), values));
    CHECK(values["TITLE"] == "Resident Evil");
    CHECK(values["DISC_ID"] == "SLUS00923");
    CHECK_FALSE(ParamSfo::parse(reinterpret_cast<const uint8_t *>("nothing here"), 12, values));
}

TEST_CASE("PbpImage: a PSN release is licence-protected, a disc-made PBP is not") {
    TempDir tmp("pbp");
    tmp.writeFile("plain.pbp", pbp(psisoimg(false)));
    tmp.writeFile("psn.pbp", pbp(psisoimg(true)));
    tmp.writeFile("multi.pbp", pbp(multiDisc({psisoimg(false), psisoimg(true)})));
    tmp.writeFile("multiplain.pbp", pbp(multiDisc({psisoimg(false), psisoimg(false)})));
    tmp.writeFile("notpbp.pbp", "just some bytes, not a PBP at all - long enough to be read");

    auto plain = PbpImage::inspect(tmp.at("plain.pbp"));
    CHECK(plain.valid);
    CHECK(plain.ps1);
    CHECK(plain.discs == 1);
    CHECK_FALSE(plain.licenceProtected);
    CHECK(plain.sfo["DISC_ID"] == "SLUS00001");

    auto psn = PbpImage::inspect(tmp.at("psn.pbp"));
    CHECK(psn.ps1);
    CHECK(psn.licenceProtected);

    auto multi = PbpImage::inspect(tmp.at("multi.pbp"));
    CHECK(multi.discs == 2);
    CHECK(multi.licenceProtected);
    CHECK_FALSE(PbpImage::inspect(tmp.at("multiplain.pbp")).licenceProtected);

    auto junk = PbpImage::inspect(tmp.at("notpbp.pbp"));
    CHECK_FALSE(junk.valid);
    CHECK_FALSE(junk.licenceProtected);
}
