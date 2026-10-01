//
// Strings::upperUtf8: the upper-casing the launcher's Play label uses (G5j) - UTF-8 safe for every shipped language.
//
#include "doctest/doctest.h"

#include <ableem/engine/strings.h>

#include <string>

using ableem::Strings;
using std::string;

TEST_CASE("ASCII goes up, everything else of it stays") {
    CHECK(Strings::upperUtf8("Play") == "PLAY");
    CHECK(Strings::upperUtf8("Start 1-2!") == "START 1-2!");
    CHECK(Strings::upperUtf8("").empty());
}

TEST_CASE("Latin letters with marks (Polish, French, German, Spanish, Czech, Turkish)") {
    CHECK(Strings::upperUtf8("Zagraj \xC5\x82\xC4\x85\xC5\xBC\xC5\x9B") == "ZAGRAJ \xC5\x81\xC4\x84\xC5\xBB\xC5\x9A");
    CHECK(Strings::upperUtf8("\xC3\xA9t\xC3\xA9 \xC3\xB1 \xC3\xBC \xC3\xB8") ==
          "\xC3\x89T\xC3\x89 \xC3\x91 \xC3\x9C \xC3\x98");
    CHECK(Strings::upperUtf8("\xC3\x9F") == "\xC3\x9F");             // the sharp s stays
    CHECK(Strings::upperUtf8("\xC3\xB7") == "\xC3\xB7");             // the division sign is no letter
    CHECK(Strings::upperUtf8("\xC3\xBF") == "\xC5\xB8");             // y with diaeresis -> capital Y with diaeresis
    CHECK(Strings::upperUtf8("\xC4\x8D\xC5\x99\xC5\xBE") == "\xC4\x8C\xC5\x98\xC5\xBD"); // Czech
    CHECK(Strings::upperUtf8("\xC4\xB1") == "I");                     // Turkish dotless i
    CHECK(Strings::upperUtf8("\xC5\x84") == "\xC5\x83");              // n with acute: the even-pair run
    CHECK(Strings::upperUtf8("\xC4\xBA") == "\xC4\xB9");              // l with acute
}

TEST_CASE("Greek and Cyrillic") {
    CHECK(Strings::upperUtf8("\xCF\x80\xCE\xB1\xCE\xB9\xCF\x87\xCE\xBD\xCE\xAF\xCE\xB4\xCE\xB9") ==
          "\xCE\xA0\xCE\x91\xCE\x99\xCE\xA7\xCE\x9D\xCE\x8A\xCE\x94\xCE\x99");
    CHECK(Strings::upperUtf8("\xCF\x82") == "\xCE\xA3"); // final sigma
    CHECK(Strings::upperUtf8("\xD0\x98\xD0\xB3\xD1\x80\xD0\xB0\xD1\x82\xD1\x8C") ==
          "\xD0\x98\xD0\x93\xD0\xA0\xD0\x90\xD0\xA2\xD0\xAC"); // Russian
    CHECK(Strings::upperUtf8("\xD1\x91") == "\xD0\x81");        // io
}

TEST_CASE("text it does not know, and broken bytes, pass through untouched") {
    const string cjk = "\xE9\x96\x8B\xE5\xA7\x8B play";
    CHECK(Strings::upperUtf8(cjk) == "\xE9\x96\x8B\xE5\xA7\x8B PLAY");
    const string broken = "a\xC3";
    CHECK(Strings::upperUtf8(broken) == "A\xC3");
    const string stray = "\x80z";
    CHECK(Strings::upperUtf8(stray) == "\x80Z");
}
