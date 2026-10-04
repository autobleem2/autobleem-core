//
// The RetroArch build installed on a stick: what RetroArch/bin/VERSION says, as the site catalog's version
// ("v1.22.2-6", psc/retroarch/latest.json) spells it. SDL-free.
//
#pragma once

#include <string>

namespace retroarch_version {

// The version a VERSION file's text names:
//  - a first line without `=` is the version itself ("v1.22.2-6", what the stamp InstallerJob writes holds);
//  - else the key=value file the RetroArch zip carries: `retroarch_version=X` with `psc_build=N` is "X-N",
//    `retroarch_version=X` alone is "X";
//  - anything else (empty text, no such key) is "".
// CRLF line ends and blanks around values are fine.
std::string parse(const std::string &text);

// parse() of <usbRoot>/RetroArch/bin/VERSION; "" when the file is missing
std::string installed(const std::string &usbRoot);

} // namespace retroarch_version
