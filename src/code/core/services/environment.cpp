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
} // namespace

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
