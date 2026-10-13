#include "linux_cd_drive.h"

#include <dirent.h>
#include <fcntl.h>
#include <scsi/sg.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

using namespace std;

namespace {

const size_t RawSize = DiscReader::SectorSize;
const size_t SubSize = 96;
const int ProbeFirst = 16;
const int ProbeCount = 4;
const int ProbeTries = 3;
const int MaxShift = 2;
const int LeadIn = 150;
const unsigned TimeoutMs = 10000;

// two BCD digits -> value; -1 when a nibble is not a digit
int fromBcd(uint8_t b) {
    const int hi = b >> 4;
    const int lo = b & 15;
    return hi > 9 || lo > 9 ? -1 : hi * 10 + lo;
}

} // namespace

//*******************************
// LinuxCdDrive::findDrives
//*******************************
vector<int> LinuxCdDrive::findDrives(const string &sysfsRoot) {
    vector<int> found;
    DIR *dir = opendir(sysfsRoot.c_str());
    if (dir == nullptr)
        return found;
    while (const dirent *entry = readdir(dir)) {
        const string name = entry->d_name;
        if (name.size() < 3 || name.compare(0, 2, "sg") != 0 || name.find_first_not_of("0123456789", 2) != string::npos)
            continue;
        ifstream in(sysfsRoot + "/" + name + "/device/type");
        string type;
        if (in && getline(in, type)) {
            const size_t b = type.find_first_not_of(" \t\r\n");
            const size_t e = type.find_last_not_of(" \t\r\n");
            if (b != string::npos && b == e && type[b] == '5')
                found.push_back(atoi(name.c_str() + 2));
        }
    }
    closedir(dir);
    sort(found.begin(), found.end());
    return found;
}

//*******************************
// LinuxCdDrive::parseToc / readCdCommand / decideShift
//*******************************
bool LinuxCdDrive::parseToc(const uint8_t *r, size_t size, bool msf, CdToc &toc, string &error) {
    toc = CdToc();
    if (size < 4) {
        error = "the TOC is too short";
        return false;
    }
    const size_t length = static_cast<size_t>(r[0]) << 8 | r[1];
    const size_t end = min(size, length + 2);
    bool leadout = false;
    for (size_t at = 4; at + 8 <= end; at += 8) {
        const uint8_t control = r[at + 1] & 0x0f;
        const uint8_t number = r[at + 2];
        const uint8_t *a = r + at + 4;
        uint32_t lba;
        if (msf) {
            const int frames = (a[1] * 60 + a[2]) * 75 + a[3];
            lba = static_cast<uint32_t>(max(0, frames - LeadIn));
        } else {
            lba = static_cast<uint32_t>(a[0]) << 24 | static_cast<uint32_t>(a[1]) << 16 |
                  static_cast<uint32_t>(a[2]) << 8 | a[3];
        }
        if (number == 0xaa) {
            toc.leadout = lba;
            leadout = true;
        } else if (number >= 1 && number <= 99) {
            CdTrack t;
            t.number = number;
            t.audio = (control & 0x04) == 0;
            t.start = lba;
            toc.tracks.push_back(t);
        }
    }
    if (toc.tracks.empty() || !leadout) {
        error = "the TOC has no tracks or no lead-out";
        return false;
    }
    return true;
}

void LinuxCdDrive::readCdCommand(uint8_t *cdb, int32_t lba, bool audio, bool subchannel, uint32_t count) {
    memset(cdb, 0, 12);
    cdb[0] = 0xbe;
    cdb[1] = audio ? 0x04 : 0x00; // expected sector type: CD-DA, or any
    const uint32_t l = static_cast<uint32_t>(lba);
    cdb[2] = static_cast<uint8_t>(l >> 24);
    cdb[3] = static_cast<uint8_t>(l >> 16);
    cdb[4] = static_cast<uint8_t>(l >> 8);
    cdb[5] = static_cast<uint8_t>(l);
    cdb[6] = static_cast<uint8_t>(count >> 16); // transfer length, 3 bytes
    cdb[7] = static_cast<uint8_t>(count >> 8);
    cdb[8] = static_cast<uint8_t>(count);
    cdb[9] = 0xf8;                      // sync, headers, user data, EDC/ECC: all 2352 bytes
    cdb[10] = subchannel ? 0x01 : 0x00; // raw P-W
}

bool LinuxCdDrive::decideShift(const uint8_t *qs, int count, uint32_t firstLba, int &shiftOut) {
    int shift = 0;
    for (int i = 0; i < count; i++) {
        const uint8_t *q = qs + i * 12;
        if ((q[0] & 0x0f) != 1)
            return false;
        const int m = fromBcd(q[7]);
        const int s = fromBcd(q[8]);
        const int f = fromBcd(q[9]);
        if (m < 0 || s < 0 || f < 0)
            return false;
        const int address = (m * 60 + s) * 75 + f - LeadIn;
        const int d = address - static_cast<int>(firstLba + static_cast<uint32_t>(i));
        if (d < -MaxShift || d > MaxShift || (i > 0 && d != shift))
            return false;
        shift = d;
    }
    shiftOut = shift;
    return count > 0;
}

LinuxCdDrive::ReadPlan LinuxCdDrive::planRead(int32_t lba, int shift, bool wantQ) {
    ReadPlan plan;
    plan.mainLba = lba;
    plan.subLba = lba;
    if (!wantQ)
        return plan;
    if (shift == 0) {
        plan.mainWithSub = true;
        return plan;
    }
    plan.extraSubRead = true;
    plan.subLba = lba - shift;
    return plan;
}

void LinuxCdDrive::setSpeedCommand(uint8_t *cdb, unsigned kbPerSecond) {
    memset(cdb, 0, 12);
    cdb[0] = 0xbb;
    cdb[2] = static_cast<uint8_t>(kbPerSecond >> 8);
    cdb[3] = static_cast<uint8_t>(kbPerSecond);
    cdb[4] = 0xff; // write speed: unchanged / maximum
    cdb[5] = 0xff;
}

bool LinuxCdDrive::decodeQ(const uint8_t *subcode96, uint8_t *q12) {
    DiscReader::deinterleaveQ(subcode96, q12);
    return DiscReader::qCrc(q12) == static_cast<uint16_t>(q12[10] << 8 | q12[11]);
}

//*******************************
// LinuxCdDrive: the drive
//*******************************
LinuxCdDrive::~LinuxCdDrive() {
    close();
}

bool LinuxCdDrive::open(int sgIndex, string &error) {
    close();
    char path[32];
    snprintf(path, sizeof(path), "/dev/sg%d", sgIndex);
    fd_ = ::open(path, O_RDWR | O_NONBLOCK);
    if (fd_ < 0) {
        error = string("cannot open ") + path + ": " + strerror(errno);
        return false;
    }
    return true;
}

void LinuxCdDrive::close() {
    if (fd_ >= 0)
        ::close(fd_);
    fd_ = -1;
    subchannel_ = Subchannel::Unprobed;
    shift_ = 0;
}

bool LinuxCdDrive::commandSucceeded(unsigned info, int resid, bool exact) {
    return (info & SG_INFO_OK_MASK) == SG_INFO_OK && (!exact || resid == 0);
}

bool LinuxCdDrive::command(const uint8_t *cdb, int cdbSize, uint8_t *buffer, size_t size, bool exact) {
    if (fd_ < 0)
        return false;
    uint8_t sense[32];
    sg_io_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.interface_id = 'S';
    hdr.dxfer_direction = size > 0 ? SG_DXFER_FROM_DEV : SG_DXFER_NONE;
    hdr.cmd_len = static_cast<unsigned char>(cdbSize);
    hdr.mx_sb_len = sizeof(sense);
    hdr.dxfer_len = static_cast<unsigned int>(size);
    hdr.dxferp = buffer;
    hdr.cmdp = const_cast<uint8_t *>(cdb);
    hdr.sbp = sense;
    hdr.timeout = TimeoutMs;
    if (ioctl(fd_, SG_IO, &hdr) != 0)
        return false;
    return commandSucceeded(hdr.info, hdr.resid, exact);
}

bool LinuxCdDrive::testUnitReady() {
    const uint8_t cdb[6] = {0, 0, 0, 0, 0, 0};
    return command(cdb, 6, nullptr, 0);
}

bool LinuxCdDrive::readToc(CdToc &toc, string &error) {
    subchannel_ = Subchannel::Unprobed;
    shift_ = 0;
    uint8_t response[4 + 100 * 8];
    memset(response, 0, sizeof(response));
    uint8_t cdb[10] = {0x43, 0, 0, 0, 0, 0, 0, 0, 0, 0}; // READ TOC/PMA/ATIP, LBA addresses, format 0
    cdb[7] = static_cast<uint8_t>(sizeof(response) >> 8);
    cdb[8] = static_cast<uint8_t>(sizeof(response));
    if (!command(cdb, 10, response, sizeof(response))) {
        error = "the drive did not return the TOC (no disc, or not ready)";
        return false;
    }
    if (!parseToc(response, sizeof(response), false, toc, error))
        return false;
    probeSubchannel(toc.tracks[0].audio);
    return true;
}

bool LinuxCdDrive::rawRead(int32_t lba, bool audio, uint8_t *buffer, bool subchannel) {
    uint8_t cdb[12];
    readCdCommand(cdb, lba, audio, subchannel);
    return command(cdb, 12, buffer, subchannel ? RawSize + SubSize : RawSize);
}

bool LinuxCdDrive::setSpeed(unsigned kbPerSecond) {
    uint8_t cdb[12];
    setSpeedCommand(cdb, kbPerSecond);
    return command(cdb, 12, nullptr, 0);
}

bool LinuxCdDrive::readSubcode(uint32_t lba, bool audio, uint8_t *subcode96) {
    uint8_t buffer[RawSize + SubSize];
    if (!rawRead(static_cast<int32_t>(lba), audio, buffer, true))
        return false;
    memcpy(subcode96, buffer + RawSize, SubSize);
    return true;
}

bool LinuxCdDrive::probeSubchannel(bool audio) {
    subchannel_ = Subchannel::None;
    shift_ = 0;
    for (int attempt = 0; attempt < ProbeTries; attempt++) {
        if (attempt > 0)
            usleep(500 * 1000); // a drive spinning up fails the first reads
        uint8_t qs[ProbeCount * 12];
        bool ok = true;
        for (int i = 0; i < ProbeCount && ok; i++) {
            uint8_t buffer[RawSize + SubSize];
            ok = rawRead(ProbeFirst + i, audio, buffer, true);
            if (ok)
                DiscReader::deinterleaveQ(buffer + RawSize, qs + i * 12);
        }
        int shift = 0;
        if (ok && decideShift(qs, ProbeCount, ProbeFirst, shift)) {
            subchannel_ = Subchannel::RawPW;
            shift_ = shift;
            return true;
        }
    }
    return false;
}

bool LinuxCdDrive::readSectors(uint32_t lba, uint32_t count, bool audio, uint8_t *data) {
    if (count == 0)
        return true;
    uint8_t cdb[12];
    readCdCommand(cdb, static_cast<int32_t>(lba), audio, false, count);
    // a batch the drive delivers short is a failure: the caller then reads the sectors one by one
    return command(cdb, 12, data, static_cast<size_t>(count) * RawSize, true);
}

bool LinuxCdDrive::readSector(uint32_t lba, bool audio, uint8_t *data, uint8_t *q) {
    if (q == nullptr || subchannel_ != Subchannel::RawPW) {
        if (q != nullptr)
            memset(q, 0, 12);
        return rawRead(static_cast<int32_t>(lba), audio, data, false);
    }
    const ReadPlan plan = planRead(static_cast<int32_t>(lba), shift_, true);
    uint8_t buffer[RawSize + SubSize];
    if (!rawRead(plan.mainLba, audio, plan.mainWithSub ? buffer : data, plan.mainWithSub))
        return false;
    if (plan.mainWithSub)
        memcpy(data, buffer, RawSize);
    else if (plan.extraSubRead && !rawRead(plan.subLba, audio, buffer, true))
        return false;
    DiscReader::deinterleaveQ(buffer + RawSize, q);
    return true;
}
