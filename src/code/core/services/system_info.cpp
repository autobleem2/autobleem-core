//
// SystemInfoService: the machine, the OS, the volumes and the network, for the Hardware Information screen.
//
#include "system.h"
#include "system_info.h"
#include "../main.h"
#include "environment.h"
#include "core/version.h" // generated into the build tree

#include <ableem/engine/log.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

using namespace std;

namespace {

//*******************************
// readTextFile
//*******************************
// the whole of a small text file, "" when it is not there - /proc and /sys files are read this way, so a
// missing one (another kernel, another platform) just leaves its row out
string readTextFile(const string &path) {
    ifstream in(path);
    if (!in)
        return "";
    stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// the first line of a file, trimmed; /sys values are one line with a newline, device-tree strings end in NUL
string readFirstLine(const string &path) {
    string text = readTextFile(path);
    size_t end = text.find_first_of("\n\0", 0, 2);
    if (end != string::npos)
        text.erase(end);
    return Strings::trim(text);
}

// "1.23" from a float - the formatting every size and temperature row uses
string fixed(double value, int decimals) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return buffer;
}

void addRow(InfoSection &section, const string &label, const string &value) {
    if (!value.empty())
        section.rows.push_back({label, value});
}

#ifndef _WIN32
//*******************************
// hciIsUp
//*******************************
// whether a Bluetooth adapter is up, asked of the kernel the way hciconfig does (HCIGETDEVINFO on a raw HCI
// socket; its flags' bit 0 is HCI_UP) - sysfs does not say, and running bluetoothctl every second would be
// too much. Spelled out rather than taken from <bluetooth/hci.h>, which the toolchains do not all carry: the
// kernel fills struct hci_dev_info, whose flags are a u32 after dev_id (u16), name (8) and bdaddr (6).
// -1 when it cannot be told (no Bluetooth socket support, no permission).
int hciIsUp(int index) {
    const int afBluetooth = 31, btprotoHci = 1;
    const unsigned long hciGetDevInfo = _IOR('H', 211, int);
    int fd = socket(afBluetooth, SOCK_RAW, btprotoHci);
    if (fd < 0)
        return -1;
    alignas(8) unsigned char info[256] = {};
    uint16_t id = static_cast<uint16_t>(index);
    memcpy(info, &id, sizeof(id));
    int result = -1;
    if (ioctl(fd, hciGetDevInfo, info) == 0) {
        uint32_t flags = 0;
        memcpy(&flags, info + 16, sizeof(flags));
        result = (flags & 1u) ? 1 : 0;
    }
    close(fd);
    return result;
}
#endif

string kindLabel(SystemInfoService::AdapterKind kind) {
    switch (kind) {
    case SystemInfoService::AdapterKind::Wifi:
        return _("Wi-Fi");
    case SystemInfoService::AdapterKind::Ethernet:
        return _("Ethernet");
    default:
        return _("Bluetooth");
    }
}

#ifdef _WIN32
// a REG_SZ under HKEY_LOCAL_MACHINE, "" when missing
string registryString(const char *key, const char *value) {
    char buffer[256];
    DWORD size = sizeof(buffer);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS)
        return "";
    return buffer;
}
#endif

} // namespace

//*******************************
// SystemInfoService::collect
//*******************************
vector<InfoSection> SystemInfoService::collect() const {
    return {system(), hardware(), storage(), network(), software()};
}

//*******************************
// SystemInfoService::system
//*******************************
InfoSection SystemInfoService::system() const {
    InfoSection section{_("System"), {}};
#ifdef _WIN32
    const char *versionKey = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    string os = registryString(versionKey, "ProductName");
    string release = registryString(versionKey, "DisplayVersion");
    string build = registryString(versionKey, "CurrentBuildNumber");
    if (!release.empty())
        os += " " + release;
    if (!build.empty())
        os += " (" + _("build") + " " + build + ")";
    addRow(section, _("Operating system"), os);
    SYSTEM_INFO info;
    GetNativeSystemInfo(&info);
    addRow(section, _("Architecture"),
           info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64   ? "x86_64"
           : info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "arm64"
                                                                         : "x86");
    char host[256] = "";
    DWORD hostSize = sizeof(host);
    if (GetComputerNameA(host, &hostSize))
        addRow(section, _("Hostname"), host);
    addRow(section, _("Uptime"), formatDuration(GetTickCount64() / 1000));
    addRow(section, _("Time zone"), timeZoneText());
#else
    string os = prettyNameFromOsRelease(readTextFile("/etc/os-release"));
    utsname name{};
    if (uname(&name) == 0) {
        if (os.empty())
            os = name.sysname;
        addRow(section, _("Operating system"), os);
        addRow(section, _("Kernel"), string(name.sysname) + " " + name.release);
        addRow(section, _("Architecture"), name.machine);
        addRow(section, _("Hostname"), name.nodename);
    } else {
        addRow(section, _("Operating system"), os);
    }
    // /proc/uptime: "<seconds up> <seconds idle>"
    string uptime = readFirstLine("/proc/uptime");
    if (!uptime.empty())
        addRow(section, _("Uptime"), formatDuration(static_cast<uint64_t>(atof(uptime.c_str()))));
    // /proc/loadavg: "0.12 0.08 0.05 1/234 5678" - the three averages are what is worth showing
    string load = readFirstLine("/proc/loadavg");
    istringstream loadIn(load);
    string one, five, fifteen;
    if (loadIn >> one >> five >> fifteen)
        addRow(section, _("Load average"), one + "  " + five + "  " + fifteen);
    addRow(section, _("Time zone"), timeZoneText());
#endif
    return section;
}

//*******************************
// SystemInfoService::hardware
//*******************************
InfoSection SystemInfoService::hardware() const {
    InfoSection section{_("Hardware"), {}};
#ifdef _WIN32
    addRow(section, _("Processor"),
           Strings::trim(registryString("HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString")));
    SYSTEM_INFO info;
    GetNativeSystemInfo(&info);
    addRow(section, _("Cores"), to_string(info.dwNumberOfProcessors));
    DWORD mhz = 0, mhzSize = sizeof(mhz);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "~MHz", RRF_RT_REG_DWORD,
                     nullptr, &mhz, &mhzSize) == ERROR_SUCCESS)
        addRow(section, _("Clock"), to_string(mhz) + " MHz");
    MEMORYSTATUSEX memory;
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        addRow(section, _("Memory"), formatBytes(memory.ullTotalPhys));
        addRow(section, _("Memory free"), formatSpace(memory.ullAvailPhys, memory.ullTotalPhys));
    }
#else
    // the device tree names the board on a Pi ("Raspberry Pi 400 Rev 1.0") and on the console
    addRow(section, _("Model"), readFirstLine("/proc/device-tree/model"));
    Cpu cpu = parseCpuinfo(readTextFile("/proc/cpuinfo"));
    addRow(section, _("Processor"), cpu.model.empty() ? cpu.hardware : cpu.model);
    if (!cpu.model.empty() && !cpu.hardware.empty())
        addRow(section, _("SoC"), cpu.hardware);
    if (cpu.cores > 0)
        addRow(section, _("Cores"), to_string(cpu.cores));
    // cpufreq reports kHz; the max is the interesting one next to what it runs at now
    string cur = readFirstLine("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    string max = readFirstLine("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
    if (!cur.empty()) {
        string clock = to_string(atol(cur.c_str()) / 1000) + " MHz";
        if (!max.empty())
            clock += " / " + to_string(atol(max.c_str()) / 1000) + " MHz";
        addRow(section, _("Clock"), clock);
    }
    // millidegrees; the first thermal zone is the SoC on a Pi and on the console
    string temp = readFirstLine("/sys/class/thermal/thermal_zone0/temp");
    if (!temp.empty())
        addRow(section, _("Temperature"), fixed(atol(temp.c_str()) / 1000.0, 1) + " \xC2\xB0" + "C");
    Memory memory = parseMeminfo(readTextFile("/proc/meminfo"));
    if (memory.totalKb > 0) {
        addRow(section, _("Memory"), formatBytes(memory.totalKb * 1024));
        addRow(section, _("Memory free"), formatSpace(memory.availableKb * 1024, memory.totalKb * 1024));
    }
#endif
    return section;
}

//*******************************
// SystemInfoService::storage
//*******************************
InfoSection SystemInfoService::storage() const {
    InfoSection section{_("Storage"), {}};
    // the AutoBleem data root first - the games' volume, what "free space" means to a user of this program
    uint64_t freeBytes = 0, totalBytes = 0;
    if (spaceOf(Env::getPathToUSBRoot(), freeBytes, totalBytes))
        addRow(section, _("AutoBleem data"), formatSpace(freeBytes, totalBytes));
#ifdef _WIN32
    char drives[256] = "";
    GetLogicalDriveStringsA(sizeof(drives), drives);
    for (const char *drive = drives; *drive; drive += strlen(drive) + 1) {
        UINT type = GetDriveTypeA(drive);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE)
            continue;
        char label[64] = "", fsName[32] = "";
        GetVolumeInformationA(drive, label, sizeof(label), nullptr, nullptr, nullptr, fsName, sizeof(fsName));
        if (!spaceOf(drive, freeBytes, totalBytes))
            continue; // a card reader with no card
        string name = string(drive, 2);
        if (*label)
            name += " " + string(label);
        if (*fsName)
            name += " (" + string(fsName) + ")";
        addRow(section, name, formatSpace(freeBytes, totalBytes));
    }
#else
    for (const Mount &mount : parseMounts(readTextFile("/proc/mounts"))) {
        if (!spaceOf(mount.mountPoint, freeBytes, totalBytes) || totalBytes == 0)
            continue;
        addRow(section, mount.mountPoint + " (" + mount.fsType + ")", formatSpace(freeBytes, totalBytes));
    }
#endif
    return section;
}

//*******************************
// SystemInfoService::network
//*******************************
InfoSection SystemInfoService::network() const {
    InfoSection section{_("Network"), {}};
#ifdef _WIN32
    vector<Adapter> adapters;
#else
    vector<Adapter> adapters = readAdapters("/sys/class/net", "/sys/class/bluetooth");
    for (Adapter &adapter : adapters) {
        if (adapter.kind != AdapterKind::Bluetooth || adapter.name.compare(0, 3, "hci") != 0)
            continue;
        int up = hciIsUp(atoi(adapter.name.c_str() + 3));
        adapter.upKnown = up >= 0;
        adapter.up = up == 1;
    }
#endif
    // filled in below on Windows, from the same call as the addresses
    const size_t adapterRows = section.rows.size();
#ifdef _WIN32
    ULONG size = 16 * 1024;
    vector<char> buffer(size);
    auto *addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    if (GetAdaptersAddresses(AF_INET, flags, nullptr, addresses, &size) == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(size);
        addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());
    }
    if (GetAdaptersAddresses(AF_INET, flags, nullptr, addresses, &size) == NO_ERROR) {
        for (auto *adapter = addresses; adapter; adapter = adapter->Next) {
            if (adapter->IfType == IF_TYPE_IEEE80211 || adapter->IfType == IF_TYPE_ETHERNET_CSMACD) {
                // what Windows lists as Ethernet includes the virtual switches and VPNs; their description
                // says so, and a physical adapter has a hardware address
                wstring wideName(adapter->FriendlyName);
                Adapter a;
                a.name = string(wideName.begin(), wideName.end());
                a.kind = adapter->IfType == IF_TYPE_IEEE80211 ? AdapterKind::Wifi : AdapterKind::Ethernet;
                a.up = adapter->OperStatus == IfOperStatusUp;
                if (adapter->PhysicalAddressLength > 0)
                    adapters.push_back(a);
            }
            if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
                continue;
            for (auto *unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
                char address[INET_ADDRSTRLEN] = "";
                auto *in = reinterpret_cast<sockaddr_in *>(unicast->Address.lpSockaddr);
                inet_ntop(AF_INET, &in->sin_addr, address, sizeof(address));
                // the friendly name is UTF-16; the characters a user names an adapter with fit in a char
                wstring wide(adapter->FriendlyName);
                addRow(section, string(wide.begin(), wide.end()), address);
            }
        }
    }
#else
    ifaddrs *list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (ifaddrs *entry = list; entry; entry = entry->ifa_next) {
            if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET)
                continue;
            if (entry->ifa_flags & IFF_LOOPBACK)
                continue;
            char address[INET_ADDRSTRLEN] = "";
            auto *in = reinterpret_cast<sockaddr_in *>(entry->ifa_addr);
            inet_ntop(AF_INET, &in->sin_addr, address, sizeof(address));
            addRow(section, entry->ifa_name, address);
        }
        freeifaddrs(list);
    }
#endif
    if (section.rows.size() == adapterRows)
        addRow(section, _("Address"), _("Not connected"));
    // the adapters go first: what is there, then what it got
    vector<InfoRow> rows;
    for (AdapterKind kind : {AdapterKind::Wifi, AdapterKind::Ethernet, AdapterKind::Bluetooth}) {
#ifdef _WIN32
        if (kind == AdapterKind::Bluetooth)
            continue; // not asked on Windows: it has its own settings, and nothing here pairs a pad
#endif
        rows.push_back({kindLabel(kind), adapterSummary(adapters, kind)});
    }
    section.rows.insert(section.rows.begin(), rows.begin(), rows.end());
    return section;
}

//*******************************
// SystemInfoService::software
//*******************************
InfoSection SystemInfoService::software() const {
    InfoSection section{_("AutoBleem"), {}};
    addRow(section, _("Version"), Env::productVersion());
    addRow(section, _("Built"), Version::BUILD_TIMESTAMP);
    addRow(section, _("Platform"), Env::platformName());
    addRow(section, _("Data root"), Env::getPathToUSBRoot());
    addRow(section, _("Games"), Env::getPathToGamesDir());
    addRow(section, _("RetroArch"), Env::retroArchInstalled() ? Env::getPathToRetroarchDir() : _("Not installed"));
    return section;
}

//*******************************
// SystemInfoService::timeZoneText
//*******************************
string SystemInfoService::timeZoneText() {
    string zone;
    long offset = 0;
    bool haveOffset = false;
#ifdef _WIN32
    DYNAMIC_TIME_ZONE_INFORMATION info{};
    DWORD which = GetDynamicTimeZoneInformation(&info);
    if (which != TIME_ZONE_ID_INVALID) {
        wstring key(info.TimeZoneKeyName);
        zone = string(key.begin(), key.end()); // "Central European Standard Time" - ASCII names
        long bias = info.Bias + (which == TIME_ZONE_ID_DAYLIGHT ? info.DaylightBias : info.StandardBias);
        offset = -bias * 60;
        haveOffset = true;
    }
#else
    // TZ wins when it is set; else /etc/timezone (Debian), else where /etc/localtime points
    const char *tz = getenv("TZ");
    if (tz != nullptr && *tz)
        zone = *tz == ':' ? tz + 1 : tz;
    if (zone.empty())
        zone = readFirstLine("/etc/timezone");
    if (zone.empty()) {
        char target[512];
        ssize_t n = readlink("/etc/localtime", target, sizeof(target) - 1);
        if (n > 0) {
            target[n] = 0;
            zone = zoneFromLocaltimeLink(target);
        }
    }
    time_t now = time(nullptr);
    tm local{};
    if (localtime_r(&now, &local) != nullptr) {
        offset = local.tm_gmtoff;
        haveOffset = true;
    }
#endif
    if (!haveOffset)
        return zone;
    string utc = formatUtcOffset(offset);
    return zone.empty() ? utc : zone + " (" + utc + ")";
}

//*******************************
// SystemInfoService::zoneFromLocaltimeLink / formatUtcOffset
//*******************************
string SystemInfoService::zoneFromLocaltimeLink(const string &target) {
    const string marker = "zoneinfo/";
    size_t at = target.rfind(marker);
    if (at == string::npos)
        return "";
    string zone = target.substr(at + marker.size());
    // the "posix/" and "right/" copies of the database name the same zones
    for (const char *prefix : {"posix/", "right/"})
        if (zone.compare(0, strlen(prefix), prefix) == 0)
            zone = zone.substr(strlen(prefix));
    return zone;
}

string SystemInfoService::formatUtcOffset(long seconds) {
    if (seconds == 0)
        return "UTC";
    char sign = seconds < 0 ? '-' : '+';
    long minutes = labs(seconds) / 60;
    char text[16];
    snprintf(text, sizeof(text), "UTC%c%02ld:%02ld", sign, minutes / 60, minutes % 60);
    return text;
}

//*******************************
// SystemInfoService::readAdapters
//*******************************
vector<SystemInfoService::Adapter> SystemInfoService::readAdapters(const string &sysClassNet,
                                                                   const string &sysClassBluetooth) {
    vector<Adapter> wifi, ethernet, bluetooth;
    for (const DirEntry &entry : DirEntry::diru(sysClassNet)) {
        const string dir = sysClassNet + sep + entry.name;
        Adapter a;
        a.name = entry.name;
        if (DirEntry::exists(dir + sep + "wireless") || DirEntry::exists(dir + sep + "phy80211"))
            a.kind = AdapterKind::Wifi;
        else if (readFirstLine(dir + sep + "type") == "1" && DirEntry::exists(dir + sep + "device"))
            a.kind = AdapterKind::Ethernet;
        else
            continue; // lo, a bridge, a tunnel, a veth: nothing a user plugged in
        const string state = readFirstLine(dir + sep + "operstate");
        a.up = state == "up" || (state == "unknown" && readFirstLine(dir + sep + "carrier") == "1");
        (a.kind == AdapterKind::Wifi ? wifi : ethernet).push_back(a);
    }
    for (const DirEntry &entry : DirEntry::diru(sysClassBluetooth)) {
        if (entry.name.compare(0, 3, "hci") != 0 || entry.name.find(':') != string::npos)
            continue; // hci0:12 and the like are connections, not adapters
        Adapter a;
        a.name = entry.name;
        a.kind = AdapterKind::Bluetooth;
        a.upKnown = false;
        bluetooth.push_back(a);
    }
    vector<Adapter> all;
    for (vector<Adapter> *list : {&wifi, &ethernet, &bluetooth}) {
        sort(list->begin(), list->end(), [](const Adapter &x, const Adapter &y) { return x.name < y.name; });
        all.insert(all.end(), list->begin(), list->end());
    }
    return all;
}

//*******************************
// SystemInfoService::adapterSummary
//*******************************
string SystemInfoService::adapterSummary(const vector<Adapter> &adapters, AdapterKind kind) {
    string text;
    for (const Adapter &a : adapters) {
        if (a.kind != kind)
            continue;
        if (!text.empty())
            text += ", ";
        text += a.name;
        if (a.upKnown)
            text += " (" + (a.up ? _("up") : _("down")) + ")";
    }
    return text.empty() ? _("None") : text;
}

//*******************************
// SystemInfoService::prettyNameFromOsRelease
//*******************************
string SystemInfoService::prettyNameFromOsRelease(const string &text) {
    istringstream in(text);
    string line;
    while (getline(in, line)) {
        if (line.compare(0, 12, "PRETTY_NAME=") != 0)
            continue;
        string value = Strings::trim(line.substr(12));
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
            value = value.substr(1, value.size() - 2);
        return value;
    }
    return "";
}

//*******************************
// SystemInfoService::parseMeminfo
//*******************************
// "MemTotal:        3884356 kB" lines; MemAvailable (3.14+) counts the reclaimable caches as free, which is
// what a user means by free, MemFree is the fallback for an older kernel
SystemInfoService::Memory SystemInfoService::parseMeminfo(const string &text) {
    Memory memory;
    uint64_t memFree = 0;
    bool haveAvailable = false;
    istringstream in(text);
    string line;
    while (getline(in, line)) {
        istringstream fields(line);
        string key;
        uint64_t value = 0;
        if (!(fields >> key >> value))
            continue;
        if (key == "MemTotal:")
            memory.totalKb = value;
        else if (key == "MemAvailable:") {
            memory.availableKb = value;
            haveAvailable = true;
        } else if (key == "MemFree:")
            memFree = value;
    }
    if (!haveAvailable)
        memory.availableKb = memFree;
    return memory;
}

//*******************************
// SystemInfoService::parseCpuinfo
//*******************************
// "key\t: value" lines, one block per processor; "model name" is per processor (all the same, the first is
// kept), "Hardware" is once at the end on the ARM kernels that have it
SystemInfoService::Cpu SystemInfoService::parseCpuinfo(const string &text) {
    Cpu cpu;
    string implementer, part;
    istringstream in(text);
    string line;
    while (getline(in, line)) {
        size_t colon = line.find(':');
        if (colon == string::npos)
            continue;
        string key = Strings::trim(line.substr(0, colon));
        string value = Strings::trim(line.substr(colon + 1));
        if (key == "processor")
            cpu.cores++;
        else if (key == "model name" && cpu.model.empty())
            cpu.model = value;
        else if (key == "Hardware" && cpu.hardware.empty())
            cpu.hardware = value;
        else if (key == "CPU implementer" && implementer.empty())
            implementer = value;
        else if (key == "CPU part" && part.empty())
            part = value;
    }
    // a 64-bit ARM kernel (a Pi 4/400 with the v8 kernel, the console's own) prints neither a model name
    // nor a Hardware line - only the core's implementer and part ids, which name it well enough
    if (cpu.model.empty() && !part.empty())
        cpu.model = armCoreName(implementer, part);
    return cpu;
}

//*******************************
// SystemInfoService::armCoreName
//*******************************
string SystemInfoService::armCoreName(const string &implementer, const string &part) {
    struct Core {
        const char *part;
        const char *name;
    };
    static const Core armCores[] = {{"0xc07", "Cortex-A7"},  {"0xc0f", "Cortex-A15"}, {"0xd03", "Cortex-A53"},
                                    {"0xd04", "Cortex-A35"}, {"0xd05", "Cortex-A55"}, {"0xd07", "Cortex-A57"},
                                    {"0xd08", "Cortex-A72"}, {"0xd09", "Cortex-A73"}, {"0xd0a", "Cortex-A75"},
                                    {"0xd0b", "Cortex-A76"}, {"0xd0d", "Cortex-A77"}, {"0xd41", "Cortex-A78"}};
    string id = part;
    lcase(id);
    if (implementer == "0x41") { // ARM Ltd
        for (const Core &core : armCores)
            if (id == core.part)
                return string("ARM ") + core.name;
        return "ARM (part " + part + ")";
    }
    if (implementer == "0x42" || implementer == "0x43")
        return "Broadcom/Cavium (part " + part + ")";
    return "ARM implementer " + implementer + ", part " + part;
}

//*******************************
// SystemInfoService::parseMounts
//*******************************
// "<device> <mount point> <fstype> <options> 0 0", the fields separated by spaces and a space inside a path
// written as "\040"
vector<SystemInfoService::Mount> SystemInfoService::parseMounts(const string &text) {
    static const char *const volumeTypes[] = {"ext2",    "ext3",  "ext4", "vfat", "exfat",   "ntfs", "ntfs3",
                                              "fuseblk", "btrfs", "xfs",  "f2fs", "hfsplus", "apfs", "msdos"};
    vector<Mount> mounts;
    istringstream in(text);
    string line;
    while (getline(in, line)) {
        istringstream fields(line);
        Mount mount;
        if (!(fields >> mount.device >> mount.mountPoint >> mount.fsType))
            continue;
        if (find(begin(volumeTypes), end(volumeTypes), mount.fsType) == end(volumeTypes))
            continue;
        mount.device = unescapeMountPath(mount.device);
        mount.mountPoint = unescapeMountPath(mount.mountPoint);
        mounts.push_back(mount);
    }
    return mounts;
}

//*******************************
// SystemInfoService::unescapeMountPath
//*******************************
string SystemInfoService::unescapeMountPath(const string &path) {
    string out;
    for (size_t i = 0; i < path.size(); i++) {
        if (path[i] == '\\' && i + 3 < path.size() && isdigit(static_cast<unsigned char>(path[i + 1])) &&
            isdigit(static_cast<unsigned char>(path[i + 2])) && isdigit(static_cast<unsigned char>(path[i + 3]))) {
            out += static_cast<char>(strtol(path.substr(i + 1, 3).c_str(), nullptr, 8));
            i += 3;
        } else {
            out += path[i];
        }
    }
    return out;
}

//*******************************
// SystemInfoService::formatBytes
//*******************************
string SystemInfoService::formatBytes(uint64_t bytes) {
    static const char *const units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        unit++;
    }
    // whole bytes and kilobytes, one decimal from megabytes up - "1.5 GB" reads better than "1.53 GB"
    return fixed(value, unit >= 2 ? 1 : 0) + " " + units[unit];
}

//*******************************
// SystemInfoService::formatDuration
//*******************************
string SystemInfoService::formatDuration(uint64_t seconds) {
    uint64_t days = seconds / 86400;
    seconds %= 86400;
    char clock[16];
    snprintf(clock, sizeof(clock), "%02d:%02d:%02d", static_cast<int>(seconds / 3600),
             static_cast<int>(seconds % 3600 / 60), static_cast<int>(seconds % 60));
    if (days == 0)
        return clock;
    return to_string(days) + " " + (days == 1 ? _("day") : _("days")) + " " + clock;
}

//*******************************
// SystemInfoService::formatSpace
//*******************************
string SystemInfoService::formatSpace(uint64_t freeBytes, uint64_t totalBytes) {
    int percent = totalBytes > 0 ? static_cast<int>(freeBytes * 100 / totalBytes) : 0;
    return formatBytes(freeBytes) + " " + _("free of") + " " + formatBytes(totalBytes) + " (" + to_string(percent) +
           "%)";
}

//*******************************
// SystemInfoService::spaceOf
//*******************************
bool SystemInfoService::spaceOf(const string &path, uint64_t &freeBytes, uint64_t &totalBytes) {
    return System::diskSpace(path, freeBytes, totalBytes);
}
