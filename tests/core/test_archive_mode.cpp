//
// The Unix mode of an archive entry reaches the file on disk (the exec bit of scripts and binaries), and
// DirEntry::copyFile keeps the mode of its source. POSIX only: FAT/exFAT and Windows have no such bit.
//
#include "doctest/doctest.h"

#include "../support/temp_dir.h"
#include "../support/zip_builder.h"

#include <ableem/engine/filesystem.h>
#include <ableem/engine/zip_archive.h>

#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>

using ableem::DirEntry;
using ableem::ZipArchive;
using ableem::ZipEntry;
using std::string;
using std::vector;
using test_support::ZipBuilder;

namespace {

unsigned modeOf(const string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 ? static_cast<unsigned>(st.st_mode & 07777) : 0xffffu;
}

} // namespace

TEST_CASE("ZipArchive reads the Unix mode of an entry and extract applies it") {
    TempDir tmp("zip_mode");
    ZipBuilder z;
    z.file("bin/run.sh", "#!/bin/sh\n", 0755).file("bin/data.txt", "x", 0644);
    REQUIRE(z.write(tmp.at("m.zip")));

    vector<ZipEntry> entries;
    REQUIRE(ZipArchive::listEntries(tmp.at("m.zip"), entries));
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].mode == 0755);
    CHECK(entries[1].mode == 0644);

    REQUIRE(ZipArchive::extract(tmp.at("m.zip"), tmp.at("out")));
    CHECK(modeOf(tmp.at("out/bin/run.sh")) == 0755);
    CHECK((modeOf(tmp.at("out/bin/data.txt")) & 0111) == 0);
    // the same through the streaming path
    REQUIRE(ZipArchive::extract(tmp.at("m.zip"), tmp.at("out2"), [](uint64_t, uint64_t) {}));
    CHECK(modeOf(tmp.at("out2/bin/run.sh")) == 0755);
}

TEST_CASE("ZipArchive: an entry from a non-Unix archiver has no mode and keeps the default") {
    TempDir tmp("zip_mode");
    ZipBuilder z;
    z.file("a.txt", "x", 0);
    REQUIRE(z.write(tmp.at("d.zip")));
    vector<ZipEntry> entries;
    REQUIRE(ZipArchive::listEntries(tmp.at("d.zip"), entries));
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].mode == 0);
    REQUIRE(ZipArchive::extract(tmp.at("d.zip"), tmp.at("out")));
    CHECK((modeOf(tmp.at("out/a.txt")) & 0111) == 0);
}

TEST_CASE("DirEntry::copyFile keeps the source's mode") {
    TempDir tmp("copy_mode");
    tmp.writeFile("exe", "bin");
    tmp.writeFile("plain", "txt");
    REQUIRE(chmod(tmp.at("exe").c_str(), 0755) == 0);
    REQUIRE(chmod(tmp.at("plain").c_str(), 0644) == 0);
    REQUIRE(DirEntry::copyFile(tmp.at("exe"), tmp.at("exe.copy")));
    REQUIRE(DirEntry::copyFile(tmp.at("plain"), tmp.at("plain.copy")));
    CHECK(modeOf(tmp.at("exe.copy")) == 0755);
    CHECK((modeOf(tmp.at("plain.copy")) & 0111) == 0);
}

TEST_CASE("DirEntry::applyMode: 0 leaves the file alone") {
    TempDir tmp("apply_mode");
    tmp.writeFile("f", "x");
    REQUIRE(chmod(tmp.at("f").c_str(), 0640) == 0);
    CHECK(DirEntry::applyMode(tmp.at("f"), 0));
    CHECK(modeOf(tmp.at("f")) == 0640);
    CHECK(DirEntry::applyMode(tmp.at("f"), 0100755)); // the file-type bits are masked off
    CHECK(modeOf(tmp.at("f")) == 0755);
}
#endif
