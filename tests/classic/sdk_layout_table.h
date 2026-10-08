//
// The SDK layout table: sizeof/offsetof of the classes an extension reaches through code compiled into the plugin
// (inline accessors like app.audio(), members read by value). See "THE LAYOUT TABLE" in gui/extension.h.
// Numbers for x86_64 Linux with libstdc++ (the C++11 string ABI), the only layout the test checks. AB_SDK_ABI 11.
//
#pragma once

// X(label, actual, expected)
#define AB_SDK_LAYOUT_TABLE(X)                                                  \
    X("sizeof(AppBase)", sizeof(AppBase), 2544)                                 \
    X("offsetof(AppBase, cfg_)", LayoutProbe::off_cfg_(), 8)                \
    X("offsetof(AppBase, lang_)", LayoutProbe::off_lang_(), 152)            \
    X("offsetof(AppBase, theme_)", LayoutProbe::off_theme_(), 280)          \
    X("offsetof(AppBase, clock_)", LayoutProbe::off_clock_(), 2512)         \
    X("offsetof(AppBase, gui_)", LayoutProbe::off_gui_(), 2520)             \
    X("offsetof(AppBase, audio_)", LayoutProbe::off_audio_(), 2536)         \
    X("sizeof(Config)", sizeof(Config), 144)                                    \
    X("sizeof(Lang)", sizeof(Lang), 128)                                        \
    X("sizeof(Clock)", sizeof(Clock), 8)                                        \
    X("sizeof(Theme)", sizeof(Theme), 2232)                                     \
    X("sizeof(ThemeSpec)", sizeof(ThemeSpec), 2192)                             \
    X("sizeof(Gui)", sizeof(Gui), 2296)                                         \
    X("sizeof(AppAudio)", sizeof(AppAudio), 208)                                \
    X("sizeof(ThemeAssets)", sizeof(ThemeAssets), 768)                          \
    X("sizeof(TextRenderer)", sizeof(TextRenderer), 120)                        \
    X("sizeof(PanelStyle)", sizeof(PanelStyle), 128)                            \
    X("sizeof(DownloadRequest)", sizeof(DownloadRequest), 144)                  \
    X("sizeof(ableem::StoreItem)", sizeof(ableem::StoreItem), 480)              \
    X("sizeof(ableem::Event)", sizeof(ableem::Event), 64)                       \
    X("sizeof(ExtensionInfo)", sizeof(ExtensionInfo), 616)
