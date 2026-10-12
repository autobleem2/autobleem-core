#include "drive_watcher.h"

#include "image_cd_drive.h"
#ifndef _WIN32
#include "linux_cd_drive.h"

#include <climits>
#include <cstdlib>
#endif

#include <ableem/engine/log.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <fstream>

using namespace std;

namespace {

#ifndef _WIN32
// a drive over /dev/sgN
class LinuxHandle : public DriveWatcher::Handle {
public:
    bool open(int sg, string &error) { return drive_.open(sg, error); }
    bool ready() override { return drive_.testUnitReady(); }
    CdDrive &drive() override { return drive_; }

private:
    LinuxCdDrive drive_;
};
#endif

// AB_FAKE_DRIVE: an image as the disc
class ImageHandle : public DriveWatcher::Handle {
public:
    bool open(const string &cue, string &error) { return drive_.open(cue, error); }
    bool ready() override {
        const string eject = DriveWatcher::fakeEjectFile();
        if (eject.empty())
            return true;
        ifstream tray(eject);
        return !tray.good(); // the file present = the tray is open
    }
    CdDrive &drive() override { return drive_; }

private:
    ImageCdDrive drive_;
};

string firstLine(const string &path) {
    ifstream in(path);
    string line;
    getline(in, line);
    while (!line.empty() && isspace(static_cast<unsigned char>(line.back())))
        line.pop_back();
    return line;
}

string envValue(const char *name) {
    const char *v = getenv(name);
    return v != nullptr ? v : "";
}

} // namespace

//*******************************
// DriveWatcher: the dev switch, the cdfile, the backends
//*******************************
string DriveWatcher::fakeCue() { return envValue("AB_FAKE_DRIVE"); }

string DriveWatcher::fakeEjectFile() { return envValue("AB_FAKE_DRIVE_EJECT"); }

string DriveWatcher::cdfileFor(int sgIndex) {
    const string cue = fakeCue();
    return cue.empty() ? "cdrom://drive" + to_string(sgIndex) + ".cue" : cue;
}

DriveWatcher::Backend DriveWatcher::defaultBackend(const string &sysfsRoot) {
    Backend b;
    const string cue = fakeCue();
    if (!cue.empty()) {
        b.fake = true;
        b.fakeCue = cue;
        b.findDrives = [] { return vector<int>{0}; };
        b.open = [cue](int, string &error) -> unique_ptr<Handle> {
            unique_ptr<ImageHandle> handle(new ImageHandle());
            if (!handle->open(cue, error))
                return nullptr;
            return unique_ptr<Handle>(handle.release());
        };
        return b;
    }
#ifndef _WIN32
    b.findDrives = [sysfsRoot] { return LinuxCdDrive::findDrives(sysfsRoot); };
    b.open = [](int sg, string &error) -> unique_ptr<Handle> {
        unique_ptr<LinuxHandle> handle(new LinuxHandle());
        if (!handle->open(sg, error))
            return nullptr;
        return unique_ptr<Handle>(handle.release());
    };
    b.keepAwake = [sysfsRoot](int sg) { keepHubsOn(sysfsRoot, sg); };
#else
    (void)sysfsRoot;
    b.findDrives = [] { return vector<int>(); }; // pc-tools' WinCdDrive is the Windows back end
#endif
    return b;
}

//*******************************
// DriveWatcher::hubControlFiles / keepHubsOn
//*******************************
vector<string> DriveWatcher::hubControlFiles(const string &sysfsRoot, int sgIndex) {
    vector<string> files;
#ifndef _WIN32
    char resolved[PATH_MAX];
    if (realpath((sysfsRoot + "/sg" + to_string(sgIndex) + "/device").c_str(), resolved) == nullptr)
        return files;
    string dir = resolved;
    for (int level = 0; level < 24 && dir.size() > 1; level++) {
        if (firstLine(dir + "/bDeviceClass") == "09")
            files.push_back(dir + "/power/control");
        const size_t slash = dir.find_last_of('/');
        if (slash == string::npos || slash == 0)
            break;
        dir.resize(slash);
    }
#else
    (void)sysfsRoot;
    (void)sgIndex;
#endif
    return files;
}

bool DriveWatcher::keepHubsOn(const string &sysfsRoot, int sgIndex) {
    static atomic<bool> logged(false);
    bool all = true;
    for (const string &file : hubControlFiles(sysfsRoot, sgIndex)) {
        ofstream out(file);
        out << "on";
        out.flush();
        if (!out.good()) {
            all = false;
            if (!logged.exchange(true)) {
                PLOG_INFO << "cannot write \"on\" to " << file << " - the drive's hub may power down";
            }
        }
    }
    return all;
}

//*******************************
// DriveWatcher: the object
//*******************************
const int DriveWatcher::ReadTries;

DriveWatcher::DriveWatcher(Backend backend, int pollMs) : backend_(std::move(backend)), pollMs_(pollMs) {}

DriveWatcher::~DriveWatcher() { stop(); }

DriveWatcher::Status DriveWatcher::status() const {
    lock_guard<mutex> lock(stateMutex_);
    return status_;
}

unsigned DriveWatcher::changeCount() const {
    lock_guard<mutex> lock(stateMutex_);
    return status_.changes;
}

bool DriveWatcher::paused() const { return paused_; }

void DriveWatcher::set(State state, const DiscIdentity &disc) {
    lock_guard<mutex> lock(stateMutex_);
    const int sg = backend_.fake || sg_ < 0 ? -1 : sg_;
    const Status &was = status_;
    const bool same = was.state == state && was.sgIndex == sg && was.disc.kind == disc.kind &&
                      was.disc.serial == disc.serial && was.disc.volumeLabel == disc.volumeLabel &&
                      was.disc.trackCount == disc.trackCount && was.disc.totalFrames == disc.totalFrames;
    if (same)
        return;
    status_.state = state;
    status_.disc = disc;
    status_.sgIndex = sg;
    status_.changes++;
}

void DriveWatcher::dropDrive() {
    handle_.reset();
    inserted_ = false;
    done_ = false;
    rereading_ = false;
    tries_ = 0;
}

//*******************************
// DriveWatcher::pollOnce
//*******************************
void DriveWatcher::pollOnce() {
    lock_guard<mutex> step(stepMutex_);
    if (paused_)
        return;
    const DiscIdentity none;

    if (!handle_) {
        const vector<int> drives = backend_.findDrives ? backend_.findDrives() : vector<int>();
        string error;
        if (drives.empty() || !backend_.open || !(handle_ = backend_.open(drives[0], error))) {
            sg_ = -1;
            set(State::NoDrive, none);
            return;
        }
        sg_ = drives[0];
        if (backend_.keepAwake)
            backend_.keepAwake(sg_);
        inserted_ = false;
        done_ = false;
        tries_ = 0;
    }

    if (!handle_->ready()) {
        const vector<int> drives = backend_.findDrives ? backend_.findDrives() : vector<int>();
        if (find(drives.begin(), drives.end(), sg_) == drives.end()) {
            dropDrive();
            sg_ = -1;
            set(State::NoDrive, none);
            return;
        }
        inserted_ = false;
        done_ = false;
        rereading_ = false;
        tries_ = 0;
        set(State::Empty, none);
        return;
    }
    if (done_)
        return; // this disc has been read

    if (!inserted_) { // the edge: not ready -> ready
        inserted_ = true;
        tries_ = 0;
        if (!rereading_)
            set(State::Reading, none);
    }
    const DiscIdentity id = DiscIdentity::identify(handle_->drive());
    if (id.kind == DiscIdentity::Kind::Unreadable && ++tries_ < ReadTries) {
        set(State::Reading, none); // a spinning-up drive fails the first reads: again on the next poll
        return;
    }
    done_ = true;
    rereading_ = false;
    switch (id.kind) {
    case DiscIdentity::Kind::Ps1Game:
    case DiscIdentity::Kind::AudioCd:
        set(State::Ready, id);
        break;
    case DiscIdentity::Kind::OtherDisc:
        set(State::NotPs1, id);
        break;
    case DiscIdentity::Kind::Unreadable:
        set(State::Unreadable, none);
        break;
    }
}

//*******************************
// DriveWatcher::pause / resume / start / stop
//*******************************
void DriveWatcher::pause() {
    lock_guard<mutex> step(stepMutex_); // waits a poll in progress out
    paused_ = true;
    dropDrive(); // the fd is closed; the emulator owns the drive
}

void DriveWatcher::resume() {
    lock_guard<mutex> step(stepMutex_);
    if (!paused_)
        return;
    paused_ = false;
    const State now = status().state;
    rereading_ = now == State::Ready || now == State::NotPs1; // the same disc is read again without a flicker
}

void DriveWatcher::start() {
    if (thread_.joinable())
        return;
    {
        lock_guard<mutex> lock(wakeMutex_);
        stopping_ = false;
    }
    thread_ = thread([this] { run(); });
}

void DriveWatcher::stop() {
    {
        lock_guard<mutex> lock(wakeMutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();
    lock_guard<mutex> step(stepMutex_);
    handle_.reset();
}

void DriveWatcher::run() {
    for (;;) {
        pollOnce();
        unique_lock<mutex> lock(wakeMutex_);
        if (wake_.wait_for(lock, chrono::milliseconds(pollMs_), [this] { return stopping_; }))
            return;
    }
}
