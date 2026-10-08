//
// BUG-55: a plugin compiles AppBase's inline accessors against the SDK headers, so any change to the layout of what
// it reaches must come with an AB_SDK_ABI bump. This compares the build's sizes and offsets with the table in
// sdk_layout_table.h (documented in gui/extension.h). Host build only: x86_64 Linux, libstdc++ with the C++11 ABI.
//
#include "doctest/doctest.h"

#include <cstddef>
#include <string>

#include "app_base.h"
#include "core/services/downloader.h"
#include "core/services/extension_catalog.h"
#include "gui/extension.h"
#include <ableem/engine/store_catalog.h>
#include <ableem/ui/input.h>

#if defined(__linux__) && defined(__x86_64__) && defined(_GLIBCXX_USE_CXX11_ABI) && _GLIBCXX_USE_CXX11_ABI == 1

#pragma GCC diagnostic ignored "-Winvalid-offsetof"

namespace {
// lets offsetof reach AppBase's protected members
struct LayoutProbe : AppBase {
    LayoutProbe() : AppBase("") {}
    static size_t off_cfg_() { return offsetof(LayoutProbe, cfg_); }
    static size_t off_lang_() { return offsetof(LayoutProbe, lang_); }
    static size_t off_theme_() { return offsetof(LayoutProbe, theme_); }
    static size_t off_clock_() { return offsetof(LayoutProbe, clock_); }
    static size_t off_gui_() { return offsetof(LayoutProbe, gui_); }
    static size_t off_audio_() { return offsetof(LayoutProbe, audio_); }
};
} // namespace

#include "sdk_layout_table.h"

TEST_CASE("the SDK-visible class layout matches the table (change it only with an AB_SDK_ABI bump)") {
    std::string changed;
#define X(label, actual, expected)                                                                      \
    if (static_cast<size_t>(actual) != static_cast<size_t>(expected))                                   \
        changed += std::string("\n  ") + label + ": table " + std::to_string(static_cast<size_t>(expected)) + \
                   ", now " + std::to_string(static_cast<size_t>(actual));
    AB_SDK_LAYOUT_TABLE(X)
#undef X
    INFO("layout changed - bump AB_SDK_ABI (now " << AB_SDK_ABI << ") and update the table:" << changed);
    CHECK(changed.empty());
}

#else

TEST_CASE("the SDK-visible class layout matches the table (x86_64 Linux libstdc++ only)") {
    MESSAGE("skipped: the layout table is for the x86_64 Linux host build");
}

#endif
