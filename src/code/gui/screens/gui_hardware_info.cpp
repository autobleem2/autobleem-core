//
// GuiHardwareInfo: the built-in Hardware Information screen.
//
#include "gui_hardware_info.h"
#include "../gui.h"
#include "core/model/pad_assignment.h"
#include "core/model/pad_battery_match.h"

#include <algorithm>
#include <cstdio>

using namespace std;

//*******************************
// psPlayerSlotLabel (local)
//*******************************
// the enum's UI text, literal _() calls at each branch so tools/lang_tools.py's extract (which only
// recognises a literal string inside _(...), not a runtime value) picks up the three keys.
static string psPlayerSlotLabel(PsPlayerSlot slot) {
    switch (slot) {
    case PsPlayerSlot::Player1:
        return _("Player 1");
    case PsPlayerSlot::Player2:
        return _("Player 2");
    case PsPlayerSlot::Unused:
    default:
        return _("not used by the PS1 emulator");
    }
}

//*******************************
// GuiHardwareInfo::collect
//*******************************
vector<abgui::FactsSection> GuiHardwareInfo::collect() {
    vector<InfoSection> sections = systemInfo.collect();
    sections.push_back(displayAndInput());
    sections.push_back(logs());
    return sectionsOf(sections);
}

//*******************************
// GuiHardwareInfo::logs / extraHints / onButton
//*******************************
// where this run's logs are - RAM unless "Keep logs on the stick" is on - and, after Square, where they were
// copied to (docs/quiet-stick-plan.md)
InfoSection GuiHardwareInfo::logs() {
    InfoSection section{_("Logs"), {}};
    section.rows.push_back({_("Folder"), Env::getPathToLogsDir()});
    if (!savedLogs.empty())
        section.rows.push_back({_("Saved to"), "System/Logs/" + savedLogs});
    return section;
}

string GuiHardwareInfo::extraHints() {
    return Env::keepLogs() ? "" : "|@S| " + _("Save logs");
}

bool GuiHardwareInfo::onButton(ableem::Button button) {
    if (button != ableem::Button::Square || Env::keepLogs())
        return false;
    savedLogs = Env::copyLogsToStick();
    app.audio().cursor.play();
    refresh();
    return true;
}

//*******************************
// GuiHardwareInfo::displayAndInput
//*******************************
// what SystemInfoService cannot know: the window it is drawn in and the pads it is driven with
InfoSection GuiHardwareInfo::displayAndInput() {
    InfoSection section{_("Display and input"), {}};
    auto add = [&](const string &label, const string &value) {
        if (!value.empty())
            section.rows.push_back({label, value});
    };
    ableem::Platform &platform = gui->platform();
    string rendererName = renderer.driverName();
    if (platform.multisampleSamples() > 0)
        rendererName += ", " + to_string(platform.multisampleSamples()) + "x MSAA";
    add(_("Renderer"), rendererName);
    add(_("Video driver"), platform.videoDriverName());
    add(_("Display mode"), platform.displayModeString());
    // the canvas every screen draws on, and how many output pixels one of its pixels is (see Gui::outputScale)
    string canvas = to_string(renderer.width()) + "x" + to_string(renderer.height());
    if (renderer.outputScale() != 1.0f) {
        char scale[16];
        snprintf(scale, sizeof(scale), " x%.2g", renderer.outputScale());
        canvas += scale;
    }
    add(_("Canvas"), canvas);
    add(_("Audio driver"), gui->audio().driverName());
    add("SDL", platform.linkedVersion());
    vector<ableem::PadInfo> pads = gui->input().pads();
    if (pads.empty()) {
        add(_("Controllers"), platform.isDevHost() ? _("Keyboard") : _("None"));
    } else {
        // matched to each pad by its SDL serial (a Bluetooth pad's own MAC) against the sysfs battery
        // nodes' addresses - see core/model/pad_battery_match.h. A pad with no match (unplugged since, no
        // serial reported, or simply no battery node - a wired pad) gets no battery row at all.
        vector<PadBatterySource> sources;
        for (size_t i = 0; i < pads.size(); i++)
            sources.push_back({static_cast<int>(i), pads[i].serial});
        vector<MatchedPadBattery> matches = matchPadBatteries(padBattery.list(), sources);
        auto batteryTextFor = [&](size_t padIndex) -> string {
            for (const MatchedPadBattery &m : matches) {
                if (m.padIndex == static_cast<int>(padIndex) && m.battery.known()) {
                    string text = to_string(m.battery.percent) + "%";
                    string status = batteryStatusText(m.battery.status);
                    if (!status.empty())
                        text += " (" + status + ")";
                    return text;
                }
            }
            return "";
        };

        // pads() is in ascending SDL device-index order - the same order pcsx-ab/pcsx-abnxt assign
        // PS1 ports 1/2 by, so this position is that assignment (psPlayerSlot, core/model/pad_assignment.h) -
        // taking Options -> "Swap Player 1 / Player 2" (C11, config.ini "padswap") into account, so this
        // screen tells the same story LaunchService's AB_PAD_ORDER is about to hand the emulator.
        // A matched battery gets its own row, "Battery", right under the pad's own row - never appended to
        // the pad's label: concatenating a translated label and a translated word ("Player 1" + " " +
        // "battery") reads backwards in more than one language (Polish among them), and a capitalised,
        // stand-alone key reads as belonging to the row above it in every language instead.
        bool padSwap = app.config().inifile.values["padswap"] == "true";
        for (size_t i = 0; i < pads.size(); i++) {
            PsPlayerSlot slot = psPlayerSlot(static_cast<int>(i), static_cast<int>(pads.size()), padSwap);
            string playerLabel = psPlayerSlotLabel(slot);
            if (i < 2)
                add(playerLabel, pads[i].name);
            else
                add(_("Controller") + " " + to_string(i + 1), pads[i].name + " (" + playerLabel + ")");
            add(_("Battery"), batteryTextFor(i));
        }
    }
    // the gamecontrollerdb.txt the pads were mapped from (Env::padMappingFiles() - the first that loaded)
    string mappings = gui->input().currentMappingPath();
    add(_("Controller mappings"), mappings.empty() ? _("SDL's built-in") : mappings);
    return section;
}
