#include "ableem/engine/metadata_lookup.h"
#include "ableem/engine/filesystem.h"
#include "ableem/engine/serial_scanner.h"
#include "ableem/engine/startup_timer.h"
#include "ableem/engine/strings.h"

#include <iostream>
#include "ableem/engine/log.h"

using namespace std;

namespace ableem {

//*******************************
// MetadataLookup::MetadataLookup
//*******************************
MetadataLookup::MetadataLookup(const string &coversDir, const string &rdbFile, Load mode)
    : coversDir_(coversDir), rdbFile_(rdbFile) {
    if (mode == Load::Now)
        load();
}

//*******************************
// MetadataLookup::~MetadataLookup
//*******************************
MetadataLookup::~MetadataLookup() {
    if (worker_.joinable())
        worker_.join();
}

//*******************************
// MetadataLookup::load / startLoading / loadSources
//*******************************
void MetadataLookup::load() {
    if (started_.exchange(true))
        return; // a load is under way (or done): a second one would write the same members
    loadSources();
}

void MetadataLookup::startLoading() {
    if (started_.exchange(true))
        return;
    worker_ = std::thread([this]() { loadSources(); });
}

void MetadataLookup::loadSources() {
    covers_.open(coversDir_);
    {
        StartupTimer timer("rdb-read");
        if (DirEntry::exists(rdbFile_)) {
            rdb_.open(rdbFile_); // logs what it found, or why not
        } else {
            PLOG_INFO << "rdb: no " << rdbFile_ << " - game metadata comes from the covers databases only";
        }
    }
    {
        std::lock_guard<std::mutex> lock(readyMutex_);
        ready_.store(true, std::memory_order_release); // publishes covers_ and rdb_
    }
    readyCv_.notify_all();
}

void MetadataLookup::waitReady() {
    if (ready() || !started_.load())
        return;
    std::unique_lock<std::mutex> lock(readyMutex_);
    readyCv_.wait(lock, [this]() { return ready(); });
}

//*******************************
// MetadataLookup::sourcesPresent
//*******************************
bool MetadataLookup::sourcesPresent(const string &coversDir, const string &rdbFile) {
    if (DirEntry::exists(rdbFile))
        return true;
    for (const char *region : {"U", "P", "J"}) {
        if (DirEntry::exists(coversDir + sep + "covers" + region + ".db"))
            return true;
    }
    return false;
}

//*******************************
// MetadataLookup::cleanTitle
//*******************************
string MetadataLookup::cleanTitle(const string &name) {
    string s = name;
    while (true) {
        auto pos = s.rfind(" (");
        if (pos == string::npos || s.empty() || s.back() != ')')
            break;
        s.erase(pos);
    }
    return s;
}

//*******************************
// MetadataLookup::regionLetter
//*******************************
string MetadataLookup::regionLetter(const string &rdbRegion, const string &serial) {
    if (rdbRegion == "Japan")
        return "J";
    if (rdbRegion == "USA")
        return "U";
    if (rdbRegion == "Europe")
        return "P";
    // the PAL countries the database names individually
    static const char *const pal[] = {"Australia", "France",      "Germany", "Italy",   "Spain",
                                      "Sweden",    "Netherlands", "Norway",  "Finland", "Denmark",
                                      "Portugal",  "Russia",      "Poland",  "Greece",  "UK"};
    for (const char *r : pal) {
        if (rdbRegion == r)
            return "P";
    }
    string fromSerial = SerialScanner::serialToRegion(serial);
    if (fromSerial == "Japan")
        return "J";
    if (fromSerial == "Europe-Aus")
        return "P";
    return "U";
}

//*******************************
// MetadataLookup::fromRecord
//*******************************
bool MetadataLookup::fromRecord(const RdbReader::Record &rec, const string &serial, GameMetadata &md) {
    md.title = cleanTitle(rec.name);
    md.recordName = rec.name;
    md.publisher = rec.publisher;
    Strings::cleanPublisherString(md.publisher);
    md.year = rec.releaseyear;
    md.players = rec.users > 0 ? rec.users : 1;
    md.serial = serial.empty() ? rec.serial : serial; // what the disc says, not the database's suffixed form
    md.region = SerialScanner::serialToRegion(md.serial);
    md.lastRegion = regionLetter(rec.region, md.serial);
    md.bytes.clear();
    md.valid = true;

    // the covers db's PNG is still the cover when no thumbnails tree has one
    if (!md.serial.empty()) {
        GameMetadata fromDb;
        if (covers_.findBySerial(md.serial, fromDb))
            md.bytes = std::move(fromDb.bytes);
    }
    return true;
}

//*******************************
// MetadataLookup::findBySerial
//*******************************
bool MetadataLookup::findBySerial(const string &serial, GameMetadata &md) {
    if (!ready())
        return false; // not yet
    if (rdb_.isValid()) {
        const RdbReader::Record *rec = rdb_.findBySerial(serial);
        if (rec != nullptr)
            return fromRecord(*rec, serial, md);
    }
    if (covers_.findBySerial(serial, md)) {
        md.recordName.clear();
        return true;
    }
    return false;
}

//*******************************
// MetadataLookup::findByTitle
//*******************************
bool MetadataLookup::findByTitle(const string &title, GameMetadata &md) {
    if (!ready())
        return false; // not yet
    if (rdb_.isValid()) {
        const RdbReader::Record *rec = rdb_.findByName(title);
        if (rec != nullptr)
            return fromRecord(*rec, rec->serial, md);
    }
    if (covers_.findByTitle(title, md)) {
        md.recordName.clear();
        return true;
    }
    return false;
}

} // namespace ableem
