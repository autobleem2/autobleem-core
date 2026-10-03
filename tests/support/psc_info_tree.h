//
// PscInfoTree: a RetroArch tree made from the console's real core info files (tests/data/psc-info - the info/
// folder of the cores tarball the PSC installs, one .info per core), with an empty fake .so beside every one of
// them, so a CoreInfoTable loaded from it sees the console's whole core set as installed.
//
#pragma once

#include "temp_dir.h"

#include <ableem/engine/filesystem.h>

#include <fstream>
#include <sstream>
#include <string>

struct PscInfoTree {
    PscInfoTree() : tmp("pscinfo") {
        tmp.makeSubDir("retroarch/info");
        tmp.makeSubDir("retroarch/cores");
        tmp.makeSubDir("roms");
        const std::string dir = std::string(AB_TEST_DATA_DIR) + "/psc-info";
        for (const ableem::DirEntry &entry : ableem::DirEntry::diru_FilesOnly(dir)) {
            if (ableem::DirEntry::getFileExtension(entry.name) != "info")
                continue;
            std::ifstream in(dir + "/" + entry.name, std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            tmp.writeFile("retroarch/info/" + entry.name, text.str());
            tmp.writeFile("retroarch/cores/" + ableem::DirEntry::getFileNameWithoutExtension(entry.name) + ".so",
                          "core");
            infoCount++;
        }
    }

    std::string retroarch() const { return tmp.at("retroarch"); }
    std::string roms() const { return tmp.at("roms"); }

    TempDir tmp;
    int infoCount = 0;
};
