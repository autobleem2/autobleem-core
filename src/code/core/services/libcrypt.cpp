#include "libcrypt.h"

#include <fstream>

using namespace std;

namespace {

// MSF frames of the 16 places, as in pcsx-abnxt's libpcsxcore/database.c
const uint32_t Msf[LibCrypt::Places] = {14105, 14231, 14485, 14579, 14649, 14899, 15056, 15130,
                                        15242, 15312, 15378, 15628, 15919, 16031, 16101, 16167};

// the Q of one sector, read until 'LibCrypt::Reads' agree on a bad CRC; false when the sector cannot be read
bool readBadQ(CdDrive &drive, uint32_t lba, uint8_t *q, bool &bad) {
    vector<uint8_t> data(DiscReader::SectorSize);
    bad = true;
    for (int i = 0; i < LibCrypt::Reads; i++) {
        bool read = false;
        for (int attempt = 0; attempt <= 3 && !read; attempt++)
            read = drive.readSector(lba, false, data.data(), q);
        if (!read)
            return false;
        if (LibCrypt::crcOk(q)) {
            bad = false;
            return true;
        }
    }
    return true;
}

} // namespace

const int LibCrypt::Places;
const uint32_t LibCrypt::PairDistance;
const uint32_t LibCrypt::LeadIn;
const int LibCrypt::Reads;

uint32_t LibCrypt::placeMsf(int place) {
    return place >= 0 && place < Places ? Msf[place] : 0;
}

bool LibCrypt::crcOk(const uint8_t *q) {
    return DiscReader::qCrc(q) == static_cast<uint16_t>(q[10] << 8 | q[11]);
}

vector<LibCrypt::Mark> LibCrypt::scan(CdDrive &drive, uint32_t leadout, int *places) {
    vector<Mark> marks;
    if (places != nullptr)
        *places = 0;
    if (!drive.hasSubchannel())
        return marks;
    for (int p = 0; p < Places; p++) {
        const uint32_t lba = placeLba(p);
        if (lba + PairDistance >= leadout)
            continue;
        Mark a, b;
        a.lba = lba;
        b.lba = lba + PairDistance;
        bool badA = false, badB = false;
        if (!readBadQ(drive, a.lba, a.q, badA) || !badA)
            continue;
        if (!readBadQ(drive, b.lba, b.q, badB) || !badB)
            continue;
        marks.push_back(a);
        marks.push_back(b);
        if (places != nullptr)
            (*places)++;
    }
    return marks;
}

string LibCrypt::sbi(const vector<Mark> &marks) {
    vector<pair<uint32_t, vector<uint8_t>>> entries;
    for (const Mark &m : marks)
        entries.emplace_back(m.lba, vector<uint8_t>(m.q, m.q + 12));
    return DiscReader::sbi(entries);
}

bool LibCrypt::writeSbi(const string &path, const vector<Mark> &marks) {
    ofstream out(path, ios::binary | ios::trunc);
    const string bytes = sbi(marks);
    out.write(bytes.data(), static_cast<streamsize>(bytes.size()));
    out.close();
    return !out.fail();
}
