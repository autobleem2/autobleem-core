// retroarch_version: the installed RetroArch build as the site catalog spells it, from either VERSION format
#include "doctest/doctest.h"

#include "../support/temp_dir.h"
#include "core/services/retroarch_version.h"

#include <string>

using namespace std;

TEST_CASE("retroarch_version::parse: a bare first line is the version") {
    CHECK(retroarch_version::parse("v1.22.2-6\n") == "v1.22.2-6");
    CHECK(retroarch_version::parse("v1.22.2-6") == "v1.22.2-6");
    CHECK(retroarch_version::parse("  v1.22.2-6  \n") == "v1.22.2-6");
    // the key=value lines below a bare first line are not read
    CHECK(retroarch_version::parse("v1.22.2-7\nretroarch_version=v1.22.2\npsc_build=7\n") == "v1.22.2-7");
}

TEST_CASE("retroarch_version::parse: the zip's own key=value file is retroarch_version-psc_build") {
    const string zip = "retroarch_version=v1.22.2\npsc_build=6\nbuild_date=2026-10-04T05:27:09Z\n"
                       "toolchain=autobleem-build-gcc6-glibc2.24\n";
    CHECK(retroarch_version::parse(zip) == "v1.22.2-6");
    CHECK(retroarch_version::parse("psc_build=6\nretroarch_version=v1.22.2\n") == "v1.22.2-6");
    CHECK(retroarch_version::parse("retroarch_version = v1.22.2 \n psc_build = 6 \n") == "v1.22.2-6");
    CHECK(retroarch_version::parse("retroarch_version=v1.22.2\n") == "v1.22.2");
    CHECK(retroarch_version::parse("retroarch_version=v1.22.2\npsc_build=\n") == "v1.22.2");
}

TEST_CASE("retroarch_version::parse: CRLF, empty and unreadable files") {
    CHECK(retroarch_version::parse("v1.22.2-6\r\n") == "v1.22.2-6");
    CHECK(retroarch_version::parse("retroarch_version=v1.22.2\r\npsc_build=6\r\nbuild_date=x\r\n") == "v1.22.2-6");
    CHECK(retroarch_version::parse("") == "");
    CHECK(retroarch_version::parse("\n\n") == "");
    CHECK(retroarch_version::parse("psc_build=6\nbuild_date=x\n") == ""); // no retroarch_version key
    CHECK(retroarch_version::parse("build_date=x\n") == "");
}

TEST_CASE("retroarch_version::installed reads RetroArch/bin/VERSION of the root") {
    TempDir tmp("ravers");
    CHECK(retroarch_version::installed(tmp.at("stick")) == ""); // no file
    tmp.writeFile("stick/RetroArch/bin/VERSION", "retroarch_version=v1.22.2\npsc_build=6\n");
    CHECK(retroarch_version::installed(tmp.at("stick")) == "v1.22.2-6");
    tmp.writeFile("stick/RetroArch/bin/VERSION", "v1.22.2-7\r\n");
    CHECK(retroarch_version::installed(tmp.at("stick")) == "v1.22.2-7");
}
