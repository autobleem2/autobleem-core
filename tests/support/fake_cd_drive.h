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
    int reads = 0;
    std::function<void(uint32_t)> onRead; // called with the LBA of every read

    bool readToc(CdToc &out, std::string &) override {
        out = toc;
        return true;
    }
    bool readSector(uint32_t lba, bool, uint8_t *data, uint8_t *q) override {
        reads++;
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
