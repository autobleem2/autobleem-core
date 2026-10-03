// lib_ableem - engine: RetroArch's installed cores as their .info files describe them, and which core plays
// which database (system). Was the core-mapping half of the app's RetroArchService; the scanner needs the
// same table on its own thread, so it lives here and each side builds its own.
#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ableem {

//******************
// CoreInfo
//******************
// one <retroarch>/info/<core>.info file: what the core is called, what it plays, and the .so it lives in
struct CoreInfo {
    std::string name; // display_name
    // the core's file name without the extension and the "_libretro" suffix: "km_snes9x2010" for
    // cores/km_snes9x2010_libretro.so - the exact name a cores.cfg line can use, and the order's tie-break
    std::string stem;
    std::vector<std::string> extensions;
    std::vector<std::string> databases;
    std::string core_path;
    // the core reads archives itself (arcade sets): a .zip is handed over whole, never looked into
    bool block_extract = false;

    // the part of the display name that tells the builds of one system apart: "km_Snes9x 2010" of
    // "Nintendo - SNES (km_Snes9x 2010)" - what is inside the last pair of parentheses, else the whole name
    std::string shortName() const;
};

using CoreInfoPtr = std::shared_ptr<CoreInfo>;
using CoreInfos = std::vector<CoreInfoPtr>;

//******************
// CoreInfoTable
//******************
// load() reads every .info and keeps the cores whose .so (.dll on Windows - Environment::getRetroarchCoreExtension())
// is actually in <retroarch>/cores - the info bundle
// describes every core libretro builds, a few hundred, and a database mapped to a core that is not
// installed would make every game of that system unplayable. Each database then gets the first installed
// core (the one with the most extensions first, equal counts by file stem - the same stick always gives the
// same core) whose .info lists it, unless the cores.cfg says otherwise: "<database name>=<core>", one per line,
// '#' comments - which core plays a system is platform knowledge (resources/platform/<platform>.cores.cfg in
// the app). <core> is matched in this order: the core's file stem exactly ("km_snes9x2010" = cores/
// km_snes9x2010_libretro.so), then its display name exactly, then a part of its display name (the first such
// core in the order above).
class CoreInfoTable {
public:
    // userCfgPath: the user's own choices in the same format, read after coresCfgPath so they win ("" = none)
    void load(const std::string &retroarchDir, const std::string &coresCfgPath, const std::string &userCfgPath = "");

    // the user's file as database -> core file stem, written as the window saves it (an empty map removes the file)
    static std::map<std::string, std::string> loadUserPicks(const std::string &path);
    static bool saveUserPicks(const std::string &path, const std::map<std::string, std::string> &picks);

    // one .info file; corePath is the core it names (<retroarch>/cores/<stem>.so)
    static CoreInfoPtr parseInfoFile(const std::string &file, const std::string &corePath);

    const CoreInfos &cores() const { return cores_; }
    const std::set<std::string> &databases() const { return databases_; } // every one an installed core lists
    bool empty() const { return cores_.empty(); }

    // the override for the database if the cfg named one, else the .info mapping; nullptr when no
    // installed core plays it. A ".lpl" suffix on the name is ignored.
    CoreInfoPtr coreForDatabase(const std::string &dbName) const;
    // the two halves of coreForDatabase, for a caller that wants to know which answered
    CoreInfoPtr overrideCoreFor(const std::string &dbName) const;
    CoreInfoPtr defaultCoreFor(const std::string &dbName) const;
    // every installed core that can play the database, the one coreForDatabase answers first (a cores.cfg core
    // is in the list even when its .info does not list the database), the rest by file stem. Empty when none.
    CoreInfos coresForDatabase(const std::string &dbName) const;
    // the core the PLATFORM's cfg (else the .info mapping) picks, whatever the user's file says
    CoreInfoPtr platformCoreFor(const std::string &dbName) const;
    // the same cores as coresForDatabase, in an order that does not move with the user's choice: the platform's
    // pick first, then by file stem
    CoreInfos platformOrder(const std::string &dbName) const;

private:
    // the core a cores.cfg value names - see the class comment; nullptr when none matches
    CoreInfoPtr coreForCfgValue(const std::string &value) const;
    std::vector<std::pair<std::string, CoreInfoPtr>> readCfg(const std::string &path) const;

    CoreInfos cores_;
    std::set<std::string> databases_;
    std::vector<std::pair<std::string, CoreInfoPtr>> defaultCores_;  // database name -> core
    std::vector<std::pair<std::string, CoreInfoPtr>> overrideCores_; // lower-cased database name -> core; user's first
    std::vector<std::pair<std::string, CoreInfoPtr>> platformCores_; // the platform cfg's lines alone
};

} // namespace ableem
