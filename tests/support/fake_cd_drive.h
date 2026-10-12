//
// FakeCdDrive: a CdDrive over sectors held in memory, with sectors that fail and Q buffers that are set by hand.
//
#pragma once

#include "core/services/disc_reader.h"

#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

struct FakeCdDrive : CdDrive {
    CdToc toc;
    std::vector<std::vector<uint8_t>> sectors;   // 2352 bytes each, one per LBA
    std::set<uint32_t> failing;                  // readSector() always fails there
    std::map<uint32_t, int> failFirst;           // lba -> reads that fail before it works
    std::map<uint32_t, std::vector<uint8_t>> qs; // lba -> the 12 Q bytes given; absent: a good Q is made up
    bool subchannel = true;
    int speedCalls = 0;
    bool lastSpeedMaximum = false;
    int reads = 0; // sectors asked for, by readSector() and readSectors() alike
    int singleReads = 0;
    struct Batch {
        uint32_t lba, count;
        bool audio;
    };
    std::vector<Batch> batches;           // every readSectors() call
    bool batchRead = true;                // false: readSectors() is the CdDrive default (one readSector() each)
    std::function<void(uint32_t)> onRead; // called with the LBA of every read

    // a batch fails as a whole when one of its sectors would fail (nothing says which), without using up failFirst
    bool readSectors(uint32_t lba, uint32_t count, bool audio, uint8_t *data) override {
        if (!batchRead)
            return CdDrive::readSectors(lba, count, audio, data);
        batches.push_back({lba, count, audio});
        bool ok = true;
        for (uint32_t i = 0; i < count; i++) {
            reads++;
            if (onRead)
                onRead(lba + i);
            auto f = failFirst.find(lba + i);
            if (failing.count(lba + i) || lba + i >= sectors.size() || (f != failFirst.end() && f->second > 0))
                ok = false;
        }
        if (!ok)
            return false;
        for (uint32_t i = 0; i < count; i++)
            memcpy(data + static_cast<size_t>(i) * DiscReader::SectorSize, sectors[lba + i].data(),
                   DiscReader::SectorSize);
        return true;
    }

    bool readToc(CdToc &out, std::string &) override {
        out = toc;
        return true;
    }
    bool readSector(uint32_t lba, bool, uint8_t *data, uint8_t *q) override {
        reads++;
        singleReads++;
        if (onRead)
            onRead(lba);
        if (failing.count(lba) || lba >= sectors.size())
            return false;
        auto f = failFirst.find(lba);
        if (f != failFirst.end() && f->second > 0) {
            f->second--;
            return false;
        }
        memcpy(data, sectors[lba].data(), DiscReader::SectorSize);
        if (q != nullptr) {
            auto it = qs.find(lba);
            if (it != qs.end()) {
                memcpy(q, it->second.data(), 12);
            } else {
                DiscReader::expectedQ(toc, lba, q);
            }
        }
        return true;
    }
    bool hasSubchannel() const override { return subchannel; }
    bool setRipSpeed(bool maximum) override {
        speedCalls++;
        lastSpeedMaximum = maximum;
        return true;
    }
};
