// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which file a transition picture (splash/autobleem.jpg, retroarch.jpg, updating.jpg, poweroff.jpg) is shown from: on
// a 4:3 output (720x480, 640x480, 800x600, 1024x768, 1280x1024) its twin "<name>-4x3.<ext>" when there is one, else the
// 16:9 picture itself. Pure - the existence check is passed in (tests/classic/test_splash_picture.cpp).
//
#pragma once

#include <functional>
#include <string>

namespace abgui {

// "<dir>/name.jpg" -> "<dir>/name-4x3.jpg" (no extension: "<dir>/name-4x3")
inline std::string splashFourByThreeTwin(const std::string &path) {
    const size_t dot = path.rfind('.');
    const size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return path + "-4x3";
    return path.substr(0, dot) + "-4x3" + path.substr(dot);
}

inline std::string splashPicturePath(const std::string &path, bool fourByThree,
                                     const std::function<bool(const std::string &)> &exists) {
    if (!fourByThree)
        return path;
    const std::string twin = splashFourByThreeTwin(path);
    return exists(twin) ? twin : path;
}

} // namespace abgui
