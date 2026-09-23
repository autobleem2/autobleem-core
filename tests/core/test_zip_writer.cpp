//
// ZipWriter (the write side of the vendored miniz) and Md5::ofFile - what abflashkit's backup is made of.
//
#include "doctest/doctest.h"

#include "../support/temp_dir.h"

#include <ableem/engine/filesystem.h>
#include <ableem/engine/md5.h>
#include <ableem/engine/zip_archive.h>
#include <ableem/engine/zip_writer.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

using std::string;
using std::vector;

TEST_CASE("a zip written entry by entry lists and extracts back to the same bytes") {
    TempDir tmp("zip_writer");
    // one file bigger than miniz's 64 KB read buffer, so the streamed path takes several chunks
    string big(200 * 1024, 'x');
    for (size_t i = 0; i < big.size(); i += 7)
        big[i] = static_cast<char>('a' + i % 26);
    tmp.writeFile("big.bin", big);
    tmp.writeFile("small.txt", "hello");

    string zipPath = tmp.at("out.zip");
    {
        ableem::ZipWriter writer;
        REQUIRE(writer.open(zipPath));
        CHECK(writer.isOpen());
        CHECK(writer.addFile(tmp.at("big.bin"), "images/big.bin"));
        CHECK(writer.addFile(tmp.at("small.txt"), "small.txt"));
        CHECK(writer.addBytes("note.txt", "from memory"));
        CHECK(writer.close());
        CHECK_FALSE(writer.isOpen());
    }

    vector<string> names;
    REQUIRE(ableem::ZipArchive::list(zipPath, names));
    CHECK(std::find(names.begin(), names.end(), "images/big.bin") != names.end());
    CHECK(std::find(names.begin(), names.end(), "small.txt") != names.end());
    CHECK(std::find(names.begin(), names.end(), "note.txt") != names.end());

    string dest = tmp.makeSubDir("unpacked");
    REQUIRE(ableem::ZipArchive::extract(zipPath, dest));
    CHECK(tmp.readFile("unpacked/images/big.bin") == big);
    CHECK(tmp.readFile("unpacked/small.txt") == "hello");
    CHECK(tmp.readFile("unpacked/note.txt") == "from memory");
}

TEST_CASE("ZipWriter refuses what it cannot do and says so") {
    TempDir tmp("zip_writer_bad");
    ableem::ZipWriter writer;
    CHECK_FALSE(writer.addBytes("x", "y")); // not open
    CHECK_FALSE(writer.close());
    CHECK_FALSE(writer.open(tmp.at("no/such/dir/out.zip")));
    REQUIRE(writer.open(tmp.at("out.zip")));
    CHECK_FALSE(writer.addFile(tmp.at("missing.bin"), "missing.bin"));
    CHECK(writer.close());
}

TEST_CASE("Md5 of a file streams and matches the string digest") {
    TempDir tmp("md5");
    CHECK(ableem::Md5::ofString("") == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(ableem::Md5::ofString("The quick brown fox jumps over the lazy dog") == "9e107d9d372bb6826bd81d3542a419d6");
    string big(150 * 1024, 'q');
    tmp.writeFile("big.bin", big);
    CHECK(ableem::Md5::ofFile(tmp.at("big.bin")) == ableem::Md5::ofString(big));
    CHECK(ableem::Md5::ofFile(tmp.at("absent.bin")).empty());
}

// the console's LBOOT.EPB is a zip with a 4 KB trailer after it; miniz's backward scan for the end record
// had a signed/unsigned slip that lost it when the zip itself was small (patched in third_party/miniz)
TEST_CASE("a zip with 4 KB of trailing data (a recovery trailer) still lists") {
    TempDir tmp("zip_trailer");
    tmp.writeFile("a.bin", "AAAA");
    string zipPath = tmp.at("t.zip");
    {
        ableem::ZipWriter writer;
        REQUIRE(writer.open(zipPath));
        CHECK(writer.addFile(tmp.at("a.bin"), "a.bin"));
        CHECK(writer.close());
    }
    {
        std::ofstream out(zipPath, std::ios::binary | std::ios::app);
        out << string(4096, 'x');
    }
    vector<string> names;
    CHECK(ableem::ZipArchive::list(zipPath, names));
    CHECK(names.size() == 1);
}

// abflashkit's progress bars: every streamed read or write reports bytes against the whole, and ends there
TEST_CASE("zip, unzip, md5 and copy report their bytes as they go") {
    TempDir tmp("byte_progress");
    string big(300 * 1024, 'p');
    tmp.writeFile("big.bin", big);
    tmp.writeFile("small.bin", "12345");
    CHECK(ableem::DirEntry::sizeBySeeking(tmp.at("big.bin")) == static_cast<long long>(big.size()));
    CHECK(ableem::DirEntry::sizeBySeeking(tmp.at("absent.bin")) == -1);

    // calls counted, and each one's (done, total) kept: done grows, total stays, the last is total
    struct Recorder {
        vector<std::pair<uint64_t, uint64_t>> calls;
        ableem::ByteProgress fn() {
            return [this](uint64_t done, uint64_t total) { calls.emplace_back(done, total); };
        }
        bool wellFormed(uint64_t expectedTotal) const {
            if (calls.size() < 2)
                return false;
            for (size_t i = 0; i < calls.size(); i++) {
                if (calls[i].second != expectedTotal || (i > 0 && calls[i].first <= calls[i - 1].first))
                    return false;
            }
            return calls.back().first == expectedTotal;
        }
    };

    Recorder zipping;
    string zipPath = tmp.at("out.zip");
    {
        ableem::ZipWriter writer;
        REQUIRE(writer.open(zipPath));
        CHECK(writer.addFile(tmp.at("big.bin"), "big.bin", zipping.fn()));
        CHECK(writer.addFile(tmp.at("small.bin"), "small.bin")); // no progress asked: the old call
        CHECK(writer.close());
    }
    CHECK(zipping.wellFormed(big.size()));

    Recorder unzipping;
    REQUIRE(ableem::ZipArchive::extract(zipPath, tmp.at("unpacked"), unzipping.fn()));
    CHECK(unzipping.wellFormed(big.size() + 5)); // both entries, one total
    CHECK(tmp.readFile("unpacked/big.bin") == big);
    CHECK(tmp.readFile("unpacked/small.bin") == "12345");

    Recorder hashing;
    CHECK(ableem::Md5::ofFile(tmp.at("big.bin"), hashing.fn()) == ableem::Md5::ofString(big));
    CHECK(hashing.wellFormed(big.size()));

    string large(1200 * 1024, 'c'); // more than DirEntry::copy's 512 KB buffer
    tmp.writeFile("large.bin", large);
    Recorder copying;
    CHECK(ableem::DirEntry::copy(tmp.at("large.bin"), tmp.at("copy.bin"), copying.fn()));
    CHECK(copying.wellFormed(large.size()));
    CHECK(tmp.readFile("copy.bin") == large);
}
