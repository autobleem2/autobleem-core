#include "environment.h"
#include "../main.h"
#include "core/version.h"

#include <cstdlib>
#include <fstream>
#ifdef _WIN32
#include "windows_host.h"
#else
#include <unistd.h>
#endif

using namespace std;

bool Env::autobleemKernel = false;
bool Env::hiddenMenuEnabled = false;

namespace {
vector<string> retroArchBinaries_{"retroarch"};
string downloadCommand_;
string repoUrl_;
string updateDownloadCommand_;
string retroArchCatalog_;
bool directLaunch_ = false;
string pcsxDir_;
string pcsxNxtDir_;
vector<string> extraAppPlatformKeys_;
string storeDownloadCommand_;
} // namespace

//*******************************
// Env::buildTargetKey / buildOs / buildArch
//*******************************
// The one place the CPU is tested: an App's binary is built for an architecture, which is exactly what is
// being asked here - not a way to tell the targets apart (that stays AB_PLATFORM_*).
const char *Env::buildTargetKey() {
#if defined(AB_PLATFORM_PSC)
    return "psc";
#elif defined(AB_PLATFORM_RPI) && defined(__aarch64__)
    return "rpi64";
#elif defined(AB_PLATFORM_RPI)
    return "rpi";
#elif defined(AB_PLATFORM_PCUSB)
    return "pcusb";
#elif defined(AB_PLATFORM_WIN)
    return "win";
#else
    return "dev";
#endif
}

const char *Env::buildOs() {
#ifdef _WIN32
    return "windows";
#else
    return "linux";
#endif
}

const char *Env::buildArch() {
#if defined(__aarch64__)
    return "arm64";
#elif defined(__arm__)
    return "armhf";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    return "i386";
#else
    return "unknown";
#endif
}

//*******************************
// Env::appPlatformKeysFor
//*******************************
vector<string> Env::appPlatformKeysFor(const string &targetKey, const string &os, const string &arch) {
    // the console has no generic key: its glibc 2.24 and its Wayland-only SDL 2.0.14 load nothing built
    // against a current distribution, so only a binary made for it will do
    if (targetKey == "psc")
        return {"psc"};
    vector<string> keys{targetKey};
    // a dev host on Windows runs what was built for the Windows product
    if (targetKey == "dev" && os == "windows")
        keys.emplace_back("win");
    keys.push_back(os + "-" + arch);
    return keys;
}

//*******************************
// Env::appPlatformKeys
//*******************************
vector<string> Env::appPlatformKeys() {
    vector<string> keys = appPlatformKeysFor(buildTargetKey(), buildOs(), buildArch());
    for (const string &extra : extraAppPlatformKeys_) {
        bool known = false;
        for (const string &k : keys)
            known = known || k == extra;
        if (!known)
            keys.push_back(extra);
    }
    return keys;
}

void Env::setExtraAppPlatformKeys(const vector<string> &keys) {
    extraAppPlatformKeys_.clear();
    for (const string &k : keys)
        if (!Strings::trim(k).empty())
            extraAppPlatformKeys_.push_back(ableem::toLowerCopy(Strings::trim(k)));
}

//*******************************
// Env::platformName
//*******************************
const char *Env::platformName() {
#if defined(AB_PLATFORM_PSC)
    return "psc";
#elif defined(AB_PLATFORM_RPI)
    return "rpi";
#elif defined(AB_PLATFORM_PCUSB)
    return "pcusb";
#elif defined(AB_PLATFORM_WIN)
    return "win";
#else
    return "pc";
#endif
}

//*******************************
// Env::productVersion
//*******************************
std::string Env::productVersion() {
    const char *inherited = getenv("AB_VERSION");
    if (inherited && *inherited)
        return inherited;
    // the data root's (the stick's own), then next to the program (the assembly writes one into the
    // installer's and the flasher's zips and the Windows program folder), then one folder up
    // (<stick>/UpdateRoms/UpdateRoms.exe reads the stick's)
    vector<string> files;
    if (!getPathToUSBRoot().empty())
        files.push_back(getPathToUSBRoot() + sep + "VERSION");
    const string program = executableDir();
    if (!program.empty()) {
        files.push_back(program + sep + "VERSION");
        const size_t slash = program.find_last_of("/\\");
        if (slash != string::npos && slash > 0)
            files.push_back(program.substr(0, slash) + sep + "VERSION");
    }
    for (const string &path : files) {
        ifstream file(path);
        string line;
        if (file && getline(file, line)) {
            trim(line);
            if (!line.empty())
                return line;
        }
    }
    return Version::DESCRIBE;
}

//*******************************
// Env::executableDir
//*******************************
std::string Env::executableDir() {
#ifdef _WIN32
    return WindowsHost::programDir();
#else
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return "";
    const string exe(buf, static_cast<size_t>(n));
    const size_t slash = exe.rfind('/');
    return slash == string::npos ? "" : exe.substr(0, slash);
#endif
}

//*******************************
// Env::exportProductVersion
//*******************************
void Env::exportProductVersion() {
    const string version = productVersion();
#ifdef _WIN32
    _putenv_s("AB_VERSION", version.c_str());
#else
    setenv("AB_VERSION", version.c_str(), 1);
#endif
}

//*******************************
// Env::keepLogsMarkerFile / keepLogsRequested / exportLogDirs
//*******************************
string Env::keepLogsMarkerFile() {
    return getPathToPersistentLogsDir() + sep + "keep";
}

bool Env::keepLogsRequested() {
    const char *fromEnv = getenv("AB_KEEP_LOGS");
    if (fromEnv != nullptr && string(fromEnv) == "1")
        return true;
    if (DirEntry::exists(keepLogsMarkerFile()))
        return true;
    // config.ini is not loaded yet this early (the log file is opened before App exists): read the one key
    IniFile config;
    config.load(getPathToStateDir() + sep + "config.ini");
    if (config.values["keeplogs"] != "true")
        return false;
    DirEntry::createDirs(getPathToPersistentLogsDir());
    ofstream(keepLogsMarkerFile(), ios::binary) << "config.ini keeplogs=true (Options -> Keep logs on the stick)\n";
    return true;
}

namespace {
void exportVariable(const char *name, const string &value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}
} // namespace

void Env::exportLogDirs() {
    DirEntry::createDirs(getPathToLogsDir());
    exportVariable("AB_RUNTIME_DIR", getPathToRuntimeDir());
    exportVariable("AB_LOG_DIR", getPathToLogsDir());
    DirEntry::createDirs(getPathToRuntimeDir());
    DirEntry::writeFileIfChanged(getPathToRuntimeDir() + sep + "log_dir", getPathToLogsDir() + "\n");
}

//*******************************
// Env::setStoreDownloadCommand / storeDownloadCommand
//*******************************
void Env::setStoreDownloadCommand(const string &command) {
    storeDownloadCommand_ = command;
}

string Env::storeDownloadCommand() {
    string command = storeDownloadCommand_.empty() ? updateDownloadCommand() : storeDownloadCommand_;
    Strings::replaceAll(command, "%r", getWorkingPath());
    return command;
}

//*******************************
// Env::platformKeysFile / writePlatformKeysFile
//*******************************
string Env::platformKeysFile() {
    return getPathToSystemDir() + sep + "platform_keys";
}

void Env::writePlatformKeysFile() {
    string line;
    for (const string &k : appPlatformKeys())
        line += (line.empty() ? "" : " ") + k;
    // only when it changed: the console's stick is written as little as can be
    {
        ifstream in(platformKeysFile());
        string existing;
        if (in && getline(in, existing) && Strings::trim(existing) == line)
            return;
    }
    DirEntry::createDirs(getPathToSystemDir());
    ofstream out(platformKeysFile(), ios::binary | ios::trunc);
    out << line << "\n";
}

//*******************************
// Env::padMappingFiles
//*******************************
std::vector<std::string> Env::padMappingFiles() {
    std::vector<std::string> files;
    if (!getPathToKernelConfigDir().empty())
        files.push_back(getPathToKernelConfigDir() + sep + "gamecontrollerdb.txt");
    files.push_back(getPathToGameControllerDb());
    return files;
}

//*******************************
// Env:: RetroArch binaries
//*******************************
void Env::setRetroArchBinaries(const vector<string> &paths) {
    retroArchBinaries_ = paths;
}

//*******************************
// Env::setDownloadCommand / downloadCommand
//*******************************
void Env::setDownloadCommand(const string &command) {
    downloadCommand_ = command;
}
const string &Env::downloadCommand() {
    return downloadCommand_;
}
void Env::setUpdateSource(const string &repoUrl, const string &downloadCommand, const string &retroarchCatalog) {
    repoUrl_ = repoUrl;
    while (!repoUrl_.empty() && repoUrl_.back() == '/')
        repoUrl_.pop_back();
    updateDownloadCommand_ = downloadCommand;
    retroArchCatalog_ = retroarchCatalog;
    while (!retroArchCatalog_.empty() && retroArchCatalog_.front() == '/')
        retroArchCatalog_.erase(0, 1);
}
const string &Env::retroArchCatalog() {
    return retroArchCatalog_;
}
void Env::setDirectLaunch(bool direct) {
    directLaunch_ = direct;
}
bool Env::directLaunch() {
    return directLaunch_;
}
void Env::setPcsxDir(const string &path) {
    pcsxDir_ = path;
}
const string &Env::pcsxDir() {
    return pcsxDir_;
}
void Env::setPcsxNxtDir(const string &path) {
    pcsxNxtDir_ = path;
}
const string &Env::pcsxNxtDir() {
    return pcsxNxtDir_;
}
const string &Env::repoUrl() {
    return repoUrl_;
}
const string &Env::updateDownloadCommand() {
    return updateDownloadCommand_;
}
const vector<string> &Env::retroArchBinaries() {
    return retroArchBinaries_;
}

bool Env::retroArchInstalled() {
    for (const string &path : retroArchBinaries_) {
        if (DirEntry::exists(path))
            return true;
    }
    return false;
}
