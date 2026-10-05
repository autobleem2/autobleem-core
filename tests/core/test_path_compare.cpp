//
// PathCompare: paths compared as text, whatever separators, redundant parts and (on Windows) case they carry.
//
#include "doctest/doctest.h"

#include <ableem/engine/path_compare.h>

#include <string>

using ableem::PathCompare;
using std::string;

TEST_CASE("normalize: one form for separators, redundant parts and (on Windows) case") {
    using S = PathCompare;
    CHECK(S::normalize("C:\\usb\\RetroArch\\x.sfc") == "C:/usb/RetroArch/x.sfc");
    CHECK(S::normalize("/media//autobleem/./RetroArch/") == "/media/autobleem/RetroArch");
    CHECK(S::normalize("/media/a/../b") == "/media/b");
    CHECK(S::normalize("/../media") == "/media");
    CHECK(S::normalize("C:/..") == "C:");
    CHECK(S::normalize("a/../..") == "..");
    CHECK(S::normalize("./") == ".");
    CHECK(S::normalize("") == "");
    CHECK(S::normalize("//server/share\\x") == "//server/share/x");
    CHECK(S::normalize("DETECT") == "DETECT");
    CHECK(S::normalize("C:\\Usb\\X.SFC", true) == "c:/usb/x.sfc");
    CHECK(S::normalize("C:\\Usb\\X.SFC", false) == "C:/Usb/X.SFC");
}

TEST_CASE("same and isUnder: case counts only when asked to ignore it, a folder name is not a prefix") {
    using S = PathCompare;
    CHECK(S::same("C:\\usb\\core.dll", "C:/usb/./core.dll", false));
    CHECK_FALSE(S::same("C:/usb/core.dll", "c:/USB/Core.dll", false));
    CHECK(S::same("C:/usb/core.dll", "c:/USB/Core.dll", true));
    CHECK_FALSE(S::same("C:/usb/core.dll", "C:/usb/core2.dll", true));
    CHECK(S::isUnder("C:\\usb\\RetroArch\\roms\\x.sfc", "C:/usb/RetroArch/roms", false));
    CHECK(S::isUnder("c:/USB/retroarch/roms/x.sfc", "C:/usb/RetroArch/roms", true));
    CHECK_FALSE(S::isUnder("c:/USB/retroarch/roms/x.sfc", "C:/usb/RetroArch/roms", false));
    CHECK(S::isUnder("/media/x", "/media", false));
    CHECK(S::isUnder("/media", "/media", false));
    CHECK_FALSE(S::isUnder("/media2/x", "/media", false));
    CHECK_FALSE(S::isUnder("/media/x", "", false));
    CHECK(S::isUnder("/x", "/", false));
}

TEST_CASE("relativeTo: what follows the folder, in the path's own case") {
    string rest;
    CHECK(PathCompare::relativeTo("/r//nes/./sub\\Toads.nes", "/r/nes/", rest));
    CHECK(rest == "sub/Toads.nes");
    CHECK(PathCompare::relativeTo("/r/nes", "/r/nes", rest));
    CHECK(rest == "");
    CHECK_FALSE(PathCompare::relativeTo("/r/nes2/x.nes", "/r/nes", rest));
    CHECK(PathCompare::relativeTo("/x", "/", rest));
    CHECK(rest == "x");
}
