//
// XzFile: the vendored xz decoder over the fixtures
// tests/data/make_xz_fixtures.py writes - one CRC64 stream, two concatenated
// streams (SHA-256 and CRC32) with stream padding between them, a damaged file
// and a truncated one. The payload is "line 0\n" .. "line 19999\n".
//
#include "doctest/doctest.h"

#include <ableem/engine/xz_file.h>

#include <string>

using ableem::XzFile;
using std::string;

namespace {
const string dataDir = AB_TEST_DATA_DIR;

string payload() {
    string s;
    for (int i = 0; i < 20000; ++i)
        s += "line " + std::to_string(i) + "\n";
    return s;
}

bool decodeAll(const string &file, string &out, string &error) {
    out.clear();
    return XzFile::decode(
        dataDir + "/" + file,
        [&out](const uint8_t *data, size_t size) {
            out.append(reinterpret_cast<const char *>(data), size);
            return true;
        },
        error);
}
} // namespace

TEST_CASE("decode streams the whole payload of a CRC64 stream") {
    string out;
    string error;
    REQUIRE(decodeAll("test_crc64.xz", out, error));
    CHECK(error.empty());
    CHECK(out == payload());
}

TEST_CASE("concatenated streams with padding between them decode as one file") {
    string out;
    string error;
    REQUIRE(decodeAll("test_two.xz", out, error));
    CHECK(out == payload());
}

TEST_CASE("a stream of several blocks decodes whole") {
    string out;
    string error;
    REQUIRE(decodeAll("test_blocks.xz", out, error));
    CHECK(out == payload());
}

TEST_CASE("a damaged or truncated file fails with a reason") {
    string out;
    string error;
    CHECK_FALSE(decodeAll("test_damaged.xz", out, error));
    CHECK_FALSE(error.empty());
    error.clear();
    CHECK_FALSE(decodeAll("test_truncated.xz", out, error));
    CHECK(error.find("ends before") != string::npos);
}

TEST_CASE("a file that is not xz is refused") {
    string out;
    string error;
    CHECK_FALSE(decodeAll("test.cue", out, error));
    CHECK(error == "not an xz file");
    CHECK_FALSE(decodeAll("no-such-file.xz", out, error));
}

TEST_CASE("the sink can stop the decode") {
    size_t seen = 0;
    string error;
    CHECK_FALSE(XzFile::decode(
        dataDir + "/test_crc64.xz",
        [&seen](const uint8_t *, size_t size) {
            seen += size;
            return false;
        },
        error));
    CHECK(error == "stopped");
    CHECK(seen > 0);
}

TEST_CASE("progress reaches the file's size") {
    uint64_t lastRead = 0;
    uint64_t lastTotal = 0;
    string error;
    REQUIRE(XzFile::decode(
        dataDir + "/test_crc64.xz", [](const uint8_t *, size_t) { return true; }, error,
        [&](uint64_t read, uint64_t total) {
            lastRead = read;
            lastTotal = total;
        }));
    CHECK(lastTotal == 6844);
    CHECK(lastRead == lastTotal);
}

TEST_CASE("unpackedSize reads the indexes without decoding") {
    uint64_t size = 0;
    REQUIRE(XzFile::unpackedSize(dataDir + "/test_crc64.xz", size));
    CHECK(size == payload().size());
    size = 0;
    REQUIRE(XzFile::unpackedSize(dataDir + "/test_two.xz", size));
    CHECK(size == payload().size());
    size = 0;
    REQUIRE(XzFile::unpackedSize(dataDir + "/test_blocks.xz", size));
    CHECK(size == payload().size());
    CHECK_FALSE(XzFile::unpackedSize(dataDir + "/test_truncated.xz", size));
    CHECK_FALSE(XzFile::unpackedSize(dataDir + "/test.cue", size));
}
