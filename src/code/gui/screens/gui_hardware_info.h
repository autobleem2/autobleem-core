//
// GuiHardwareInfo: the Hardware Information screen, on every platform (since 2026-09-26 the console's too -
// PSC-Bios's setup screens are behind the System menu's Network & Controllers item instead).
//
#pragma once

#include "gui_facts_page.h"
#include "../../core/services/system_info.h"

//********************
// GuiHardwareInfo
//********************
// A GuiFactsPage of SystemInfoService's sections - the OS, the hardware, the volumes and their free space,
// the network - followed by what only the running program can tell (the renderer and display, SDL, the
// audio driver, the connected pads and the mapping file they were read with). The values that move (uptime, temperature, free memory, free space)
// are re-read every second while the screen is up.
class GuiHardwareInfo : public GuiFactsPage {
public:
    using GuiFactsPage::GuiFactsPage;

protected:
    std::string title() override { return _("Hardware Information"); }
    std::vector<InfoSection> collect() override;
    // Square: this run's logs from RAM to System/Logs/saved-<n> (docs/quiet-stick-plan.md)
    std::string extraHints() override;
    bool onButton(ableem::Button button) override;

private:
    SystemInfoService systemInfo;
    std::string savedLogs;         // the folder the last "Save logs" made, shown in the Logs section
    InfoSection displayAndInput(); // the section only the screen can fill in
    InfoSection logs();
};
