#include "ra_job_service.h"
#include "system.h"

#include <ableem/engine/filesystem.h>
#include <ableem/engine/log.h>
#include <ableem/engine/strings.h>
#include <ableem/engine/update_catalog.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace std;
using namespace ableem;

namespace {

int64_t nowMs() {
    return chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
}

string readText(const string &path) {
    ifstream in(path, ios::binary);
    if (!in)
        return "";
    stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// the last `bytes` of a file (the log can grow to megabytes with a progress counter)
string readTail(const string &path, streamoff bytes) {
    ifstream in(path, ios::binary);
    if (!in)
        return "";
    in.seekg(0, ios::end);
    const streamoff size = in.tellg();
    in.seekg(size > bytes ? size - bytes : 0, ios::beg);
    stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// everything under `path`, the bytes of its files; symlinks are not followed (a listing's kind is the entry's own)
uint64_t treeSize(const string &path, int depth) {
    uint64_t total = 0;
    for (const DirEntry &entry : DirEntry::diru(path)) {
        const string full = path + sep + entry.name;
        if (entry.isDir) {
            if (depth < 16)
                total += treeSize(full, depth + 1);
        } else {
            const long long size = DirEntry::fileSize(full);
            if (size > 0)
                total += static_cast<uint64_t>(size);
        }
    }
    return total;
}

string actionWord(RaJobService::Action action) {
    switch (action) {
    case RaJobService::Action::Install:
        return "install";
    case RaJobService::Action::Update:
        return "update";
    default:
        return "remove";
    }
}

bool underUsrBin(const string &path) {
    return path.compare(0, 9, "/usr/bin/") == 0;
}

} // namespace

//*******************************
// RaJobService::removableFolders
//*******************************
const vector<string> &RaJobService::removableFolders() {
    static const vector<string> folders{"cores",    "info",    "assets", "database", "cheats",
                                         "overlays", "shaders", "downloads", "records", "Retroarch themes"};
    return folders;
}

//*******************************
// RaJobService::commandFor
//*******************************
string RaJobService::commandFor(const string &commandTemplate, Action action, const string &progressFile,
                                const string &logFile, const string &usbRoot, const string &launcherDir) {
    string command = commandTemplate;
    Strings::replaceAll(command, "%a", actionWord(action));
    Strings::replaceAll(command, "%B", action == Action::RemoveWithBios ? " --bios" : "");
    Strings::replaceAll(command, "%p", progressFile);
    Strings::replaceAll(command, "%l", logFile);
    Strings::replaceAll(command, "%R", usbRoot);
    Strings::replaceAll(command, "%r", launcherDir);
    return command;
}

//*******************************
// RaJobService::parseProgress
//*******************************
bool RaJobService::parseProgress(const string &line, int &step, int &steps, string &title, uint64_t &done,
                                 uint64_t &total) {
    string text = Strings::trim(line);
    if (text.compare(0, 6, "phase ") == 0)
        text = text.substr(6);
    // i/n|title|done|total
    vector<string> parts;
    size_t from = 0;
    for (;;) {
        const size_t bar = text.find('|', from);
        if (bar == string::npos) {
            parts.push_back(text.substr(from));
            break;
        }
        parts.push_back(text.substr(from, bar - from));
        from = bar + 1;
    }
    if (parts.size() != 4)
        return false;
    const size_t slash = parts[0].find('/');
    if (slash == string::npos)
        return false;
    char *end = nullptr;
    const long i = strtol(parts[0].substr(0, slash).c_str(), &end, 10);
    if (end == parts[0].substr(0, slash).c_str() || *end != 0)
        return false;
    const string nText = parts[0].substr(slash + 1);
    const long n = strtol(nText.c_str(), &end, 10);
    if (end == nText.c_str() || *end != 0 || n < 0 || i < 0 || i > n)
        return false;
    const string doneText = Strings::trim(parts[2]), totalText = Strings::trim(parts[3]);
    if (doneText.empty() || totalText.empty() || doneText.find_first_not_of("0123456789") != string::npos ||
        totalText.find_first_not_of("0123456789") != string::npos)
        return false;
    step = static_cast<int>(i);
    steps = static_cast<int>(n);
    title = Strings::trim(parts[1]);
    done = strtoull(doneText.c_str(), nullptr, 10);
    total = strtoull(totalText.c_str(), nullptr, 10);
    return true;
}

//*******************************
// RaJobService::failureFor
//*******************************
RaJobService::Failure RaJobService::failureFor(int exitCode) {
    switch (exitCode) {
    case ExitOk:
        return Failure::None;
    case ExitNoNetwork:
        return Failure::NoNetwork;
    case ExitNoSpace:
        return Failure::NoSpace;
    default:
        return Failure::Other;
    }
}

//*******************************
// RaJobService::Status::fraction
//*******************************
double RaJobService::Status::fraction() const {
    if (steps <= 0)
        return -1;
    double inPhase = total > 0 ? static_cast<double>(min(done, total)) / static_cast<double>(total) : 0;
    if (step >= 1)
        return min(1.0, (static_cast<double>(step - 1) + inPhase) / static_cast<double>(steps));
    return 0;
}

//*******************************
// RaJobService::RaJobService / ~RaJobService
//*******************************
RaJobService::RaJobService(CommandRunner runner, function<int(const string &)> fetchRunner)
    : runner_(move(runner)), fetchRunner_(move(fetchRunner)) {
    if (!runner_)
        runner_ = [](const string &commandLine, const function<bool()> &cancelled) {
            return System::runShellCommand(commandLine, cancelled);
        };
    if (!fetchRunner_)
        fetchRunner_ = [](const string &commandLine) { return System::runShellCommand(commandLine); };
}

RaJobService::~RaJobService() {
    stopRequested_ = true; // a runner still going gets its SIGTERM; the thread ends within a second or so
    if (worker_.joinable())
        worker_.join();
    if (inspector_.joinable())
        inspector_.join();
}

void RaJobService::configure(const Config &config) {
    if (busy())
        return;
    config_ = config;
}

bool RaJobService::busy() const {
    return status_.phase == Phase::Running || inspecting_;
}

string RaJobService::progressFile() const {
    return config_.workDir + sep + "progress.txt";
}

string RaJobService::logFile() const {
    return config_.workDir + sep + "job.log";
}

//*******************************
// RaJobService::startInspect / inspectThread / pollInspect
//*******************************
void RaJobService::startInspect() {
    if (inspecting_)
        return;
    if (inspector_.joinable())
        inspector_.join();
    inspecting_ = true;
    inspectDone_ = false;
    inspection_ = Inspection();
    inspector_ = thread([this] { inspectThread(); });
}

void RaJobService::inspectThread() {
    Inspection found;
    // the program is ours to remove when any binary that exists is not the distribution's
    bool ours = false;
    for (const string &binary : config_.binaries) {
        if (!DirEntry::exists(binary))
            continue;
        found.installed = true;
        if (!underUsrBin(binary))
            ours = true;
    }
    found.systemPackage = found.installed && !ours;
    if (found.installed && config_.installedVersion)
        found.version = config_.installedVersion();
    if (found.installed && !found.systemPackage) {
        for (const string &folder : removableFolders())
            found.sizeBytes += treeSize(config_.retroarchDir + sep + folder, 0);
        for (const string &binary : config_.binaries) {
            const long long size = DirEntry::fileSize(binary);
            if (size > 0 && !underUsrBin(binary))
                found.sizeBytes += static_cast<uint64_t>(size);
        }
    }
    found.networkUp = !config_.networkUp || config_.networkUp();
    if (found.networkUp && !config_.catalog.empty() && !config_.repoUrl.empty() && !config_.arch.empty() &&
        !config_.fetchCommand.empty() && !config_.workDir.empty()) {
        DirEntry::createDirs(config_.workDir);
        const string scratch = config_.workDir + sep + "catalog.json";
        DirEntry::removeFile(scratch);
        string command = config_.fetchCommand;
        Strings::replaceAll(command, "%u", config_.repoUrl + "/" + config_.catalog);
        Strings::replaceAll(command, "%o", scratch);
        RetroArchCatalog catalog;
        if (fetchRunner_(command) == 0 && catalog.parse(readText(scratch)) && !catalog.version.empty())
            found.latestVersion = catalog.version;
        DirEntry::removeFile(scratch);
    }
    found.ready = true;
    lock_guard<mutex> lock(mutex_);
    inspectionWork_ = found;
    inspectDone_ = true;
}

RaJobService::Inspection RaJobService::pollInspect() {
    if (inspecting_ && inspectDone_) {
        if (inspector_.joinable())
            inspector_.join();
        lock_guard<mutex> lock(mutex_);
        inspection_ = inspectionWork_;
        inspecting_ = false;
    }
    return inspection_;
}

//*******************************
// RaJobService::start / stop
//*******************************
bool RaJobService::start(Action action) {
    if (!supported() || busy())
        return false;
    if (worker_.joinable())
        worker_.join();
    DirEntry::createDirs(config_.workDir);
    DirEntry::removeFile(progressFile());
    DirEntry::removeFile(logFile());
    status_ = Status();
    status_.phase = Phase::Running;
    status_.action = action;
    stopRequested_ = false;
    workerDone_ = false;
    workerExit_ = 0;
    lastRead_ = 0;
    const string command = commandFor(config_.jobCommand, action, progressFile(), logFile(), config_.usbRoot,
                                      config_.launcherDir) +
                           " > \"" + logFile() + "\" 2>&1";
    PLOG_INFO << "RetroArch job: " << command;
    worker_ = thread([this, command] { jobThread(command); });
    return true;
}

void RaJobService::stop() {
    if (status_.phase == Phase::Running)
        stopRequested_ = true;
}

void RaJobService::jobThread(string commandLine) {
    const int code = runner_(commandLine, [this] { return stopRequested_.load(); });
    lock_guard<mutex> lock(mutex_);
    workerExit_ = code;
    workerDone_ = true;
}

//*******************************
// RaJobService::readProgress
//*******************************
// the progress file's line and the log's last lines, at most every 150 ms
void RaJobService::readProgress() {
    const int64_t now = nowMs();
    if (lastRead_ != 0 && now - lastRead_ < 150)
        return;
    lastRead_ = now;
    istringstream lines(readText(progressFile()));
    string line, last;
    while (getline(lines, line)) {
        if (!Strings::trim(line).empty())
            last = line;
    }
    int step = 0, steps = 0;
    string title;
    uint64_t done = 0, total = 0;
    if (!last.empty() && parseProgress(last, step, steps, title, done, total)) {
        status_.step = step;
        status_.steps = steps;
        status_.title = title;
        status_.done = done;
        status_.total = total;
    }
    string tail = readTail(logFile(), 2048);
    replace(tail.begin(), tail.end(), '\r', '\n');
    istringstream logLines(tail);
    vector<string> keep;
    while (getline(logLines, line)) {
        line = Strings::trim(line);
        if (!line.empty())
            keep.push_back(line);
    }
    if (keep.size() > 3)
        keep.erase(keep.begin(), keep.end() - 3);
    status_.log = keep;
}

//*******************************
// RaJobService::poll
//*******************************
RaJobService::Status RaJobService::poll() {
    if (status_.phase != Phase::Running)
        return status_;
    readProgress();
    status_.stopping = stopRequested_;
    if (!workerDone_)
        return status_;
    if (worker_.joinable())
        worker_.join();
    lastRead_ = 0;
    readProgress(); // what the runner wrote last
    status_.exitCode = workerExit_;
    if (workerExit_ == ExitOk) {
        status_.phase = Phase::Succeeded;
    } else if (workerExit_ == -2 || workerExit_ == ExitStopped || stopRequested_) {
        status_.phase = Phase::Stopped;
    } else {
        status_.phase = Phase::Failed;
        status_.failure = failureFor(workerExit_);
    }
    status_.stopping = false;
    PLOG_INFO << "RetroArch job ended: exit " << workerExit_ << (status_.phase == Phase::Stopped ? " (stopped)" : "");
    return status_;
}
