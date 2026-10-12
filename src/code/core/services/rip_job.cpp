#include "rip_job.h"

#include "core/services/chd_writer.h"
#include "core/services/system.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace std;

namespace {

const uint32_t MaxSectors = 450000;
const double ProgressSeconds = 0.25;

// the track an LBA belongs to: the last one that starts at or before it (the first one takes the lead-in)
size_t trackOf(const CdToc &toc, uint32_t lba) {
    size_t t = 0;
    for (size_t i = 1; i < toc.tracks.size(); i++)
        if (toc.tracks[i].start <= lba)
            t = i;
    return t;
}

// the sectors of track i in the image: from its start (the first track from 0) to the next one's, or the lead-out
uint32_t trackFrames(const CdToc &toc, size_t i) {
    const uint32_t from = i == 0 ? 0 : toc.tracks[i].start;
    const uint32_t to = i + 1 < toc.tracks.size() ? toc.tracks[i + 1].start : toc.leadout;
    return to > from ? to - from : 0;
}

// one sector with the retries; false = zeros were written in its place
bool readWithRetries(CdDrive &drive, uint32_t lba, bool audio, uint8_t *data) {
    for (int attempt = 0; attempt <= RipJob::Retries; attempt++)
        if (drive.readSector(lba, audio, data, nullptr))
            return true;
    memset(data, 0, DiscReader::SectorSize);
    return false;
}

// leaves nothing behind and the drive at 4x
struct Cleanup {
    CdDrive &drive;
    ChdWriter &writer;
    string part;
    bool keep = false;
    bool fast = false;
    ~Cleanup() {
        writer.abort();
        if (!keep)
            remove(part.c_str());
        if (fast)
            drive.setRipSpeed(false);
    }
};

} // namespace

//*******************************
// RipJob: the helpers
//*******************************
uint64_t RipJob::neededBytes(const CdToc &toc) { return static_cast<uint64_t>(toc.leadout) * DiscReader::SectorSize; }

bool RipJob::hasFreeSpace(const string &dir, uint64_t needed, uint64_t *freeBytes) {
    uint64_t freeB = 0, totalB = 0;
    if (!System::diskSpace(dir, freeB, totalB)) {
        if (freeBytes != nullptr)
            *freeBytes = 0;
        return true;
    }
    if (freeBytes != nullptr)
        *freeBytes = freeB;
    return freeB >= needed;
}

string RipJob::dataTrackType(const uint8_t *s) {
    static const uint8_t sync[12] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
    if (memcmp(s, sync, 12) == 0 && s[15] == 1)
        return "MODE1_RAW";
    return "MODE2_RAW";
}

//*******************************
// RipJob::start
//*******************************
RipJob::Result RipJob::start(CdDrive &drive, const string &outPath, const ProgressCallback &progress) {
    Result r;
    CdToc toc;
    string error;
    ChdWriter writer;
    const string part = outPath + ".part";
    Cleanup cleanup{drive, writer, part};

    if (!drive.readToc(toc, error)) {
        r.error = error.empty() ? "the disc's table of contents cannot be read" : error;
        cancelled_ = false;
        return r;
    }
    if (toc.tracks.empty() || toc.leadout == 0 || toc.leadout > MaxSectors) {
        r.error = "the disc's table of contents makes no sense - is there a disc in the drive?";
        cancelled_ = false;
        return r;
    }
    size_t slash = outPath.find_last_of("/\\");
    const string dir = slash == string::npos ? "." : outPath.substr(0, slash == 0 ? 1 : slash);
    if (!hasFreeSpace(dir, neededBytes(toc))) {
        r.error = "not enough free space for the disc image";
        cancelled_ = false;
        return r;
    }

    vector<uint8_t> sector(DiscReader::SectorSize);
    vector<ChdWriter::Track> tracks;
    for (size_t i = 0; i < toc.tracks.size(); i++) {
        ChdWriter::Track t;
        t.number = toc.tracks[i].number;
        t.frames = trackFrames(toc, i);
        if (toc.tracks[i].audio) {
            t.type = "AUDIO";
        } else {
            t.type = "MODE2_RAW"; // a PS1 disc; the first sector says otherwise when it is mode 1
            if (readWithRetries(drive, i == 0 ? 0 : toc.tracks[i].start, false, sector.data()))
                t.type = dataTrackType(sector.data());
        }
        if (t.frames == 0) {
            r.error = "a track of the disc has no sectors";
            cancelled_ = false;
            return r;
        }
        tracks.push_back(t);
    }

    if (!writer.open(part, tracks, error)) {
        r.error = error;
        cancelled_ = false;
        return r;
    }
    drive.setRipSpeed(true);
    cleanup.fast = true;

    using Clock = chrono::steady_clock;
    Clock::time_point lastTick = Clock::now();
    uint32_t lastTickSector = 0;
    Progress p;
    p.sectorsTotal = toc.leadout;
    auto report = [&](uint32_t done, bool force) {
        const Clock::time_point now = Clock::now();
        const double seconds = chrono::duration<double>(now - lastTick).count();
        if (!force && seconds < ProgressSeconds)
            return;
        p.sectorsDone = done;
        if (seconds > 0)
            p.bytesPerSecond = static_cast<double>(done - lastTickSector) * DiscReader::SectorSize / seconds;
        lastTick = now;
        lastTickSector = done;
        if (progress)
            progress(p);
    };

    for (uint32_t lba = 0; lba < toc.leadout; lba++) {
        if (lba % ChdWriter::FramesPerHunk == 0 && cancelled_) {
            r.cancelled = true;
            cancelled_ = false;
            return r;
        }
        const bool audio = toc.tracks[trackOf(toc, lba)].audio;
        if (!readWithRetries(drive, lba, audio, sector.data())) {
            if (r.firstBadLba < 0)
                r.firstBadLba = lba;
            r.badSectors++;
            p.badSectors = r.badSectors;
        }
        if (!writer.writeSector(sector.data(), error)) {
            r.error = error;
            cancelled_ = false;
            return r;
        }
        report(lba + 1, false);
    }
    if (!writer.finish(error)) {
        r.error = error;
        cancelled_ = false;
        return r;
    }
    remove(outPath.c_str()); // rename() does not replace a file on Windows
    if (rename(part.c_str(), outPath.c_str()) != 0) {
        r.error = "cannot rename the finished image to " + outPath;
        cancelled_ = false;
        return r;
    }
    cleanup.keep = true;
    report(toc.leadout, true);
    r.ok = true;
    r.file = outPath;
    r.sectors = toc.leadout;
    cancelled_ = false;
    return r;
}
