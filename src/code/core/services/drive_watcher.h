//
// DriveWatcher: the CD drive and the disc in it, watched by a worker thread (docs/disc-drive-plan.md, piece 3).
// SDL-free; nothing here calls into a UI thread - the UI asks status() and compares changeCount() with the one
// it drew last.
//
// Once a second (pollOnce): no drive open -> look for one (LinuxCdDrive::findDrives, the first); TEST UNIT READY
// says whether a disc is in. The edge "not ready -> ready" reads the disc once with DiscIdentity::identify (state
// Reading meanwhile); a disc that comes back Unreadable is tried ReadTries times in all, a spinning-up drive fails
// the first reads. Eject goes back to Empty, an unplugged drive to NoDrive.
//
// pause()/resume() go around a game: the emulator owns the drive, so the fd is closed on pause and the drive is
// opened and the disc read again after resume (the change counter moves only when the answer is another one).
//
// Dev switches (VM UI tests, no drive): env AB_FAKE_DRIVE=<a .cue> makes the watcher read that image through
// ImageCdDrive instead of a sysfs drive, and while the file named by AB_FAKE_DRIVE_EJECT exists the tray is open.
//
#pragma once

#include "disc_identity.h"
#include "disc_reader.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

//******************
// DriveWatcher
//******************
class DriveWatcher {
public:
    enum class State {
        NoDrive,    // no CD drive found
        Empty,      // a drive, no disc (or not ready)
        Reading,    // a disc went in, DiscIdentity::identify is running
        Ready,      // identity().kind is Ps1Game or AudioCd
        NotPs1,     // a data disc without a PS1 SYSTEM.CNF (identity().kind OtherDisc)
        Unreadable  // the disc could not be read
    };

    struct Status {
        State state = State::NoDrive;
        DiscIdentity disc;   // meaningful in Ready and NotPs1 (the volume label)
        int sgIndex = -1;    // the drive's /dev/sgN; -1 with AB_FAKE_DRIVE and with NoDrive
        unsigned changes = 0; // changeCount() at the moment of this answer
    };

    // an open drive: its TEST UNIT READY and the CdDrive the disc is read with
    class Handle {
    public:
        virtual ~Handle() = default;
        virtual bool ready() = 0;
        virtual CdDrive &drive() = 0;
    };

    // where the drives come from; the tests give their own
    struct Backend {
        std::function<std::vector<int>()> findDrives;                            // sg indexes, best first
        std::function<std::unique_ptr<Handle>(int sg, std::string &error)> open; // null and an error: not opened
        std::function<void(int sg)> keepAwake;                                   // after open; may be empty
        bool fake = false;                                                       // AB_FAKE_DRIVE
        std::string fakeCue;
    };

    static const int ReadTries = 3;

    explicit DriveWatcher(Backend backend, int pollMs = 1000);
    ~DriveWatcher();
    DriveWatcher(const DriveWatcher &) = delete;
    DriveWatcher &operator=(const DriveWatcher &) = delete;

    // the real drive over sysfsRoot, or the image of AB_FAKE_DRIVE when that is set
    static Backend defaultBackend(const std::string &sysfsRoot = "/sys/class/scsi_generic");

    void start(); // the worker thread
    void stop();  // joins it; the destructor does too
    void pause(); // returns when the worker is not touching the drive; the fd is closed
    void resume();
    bool paused() const;

    void pollOnce(); // one poll in the calling thread (what the worker does every pollMs; for tests)

    Status status() const;
    State state() const { return status().state; }
    unsigned changeCount() const; // up by one whenever the state or the disc's identity changes

    // --- dev switch and the launch ---
    static std::string fakeCue();       // env AB_FAKE_DRIVE, "" when unset
    static std::string fakeEjectFile(); // env AB_FAKE_DRIVE_EJECT, "" when unset
    // the emulator's -cdfile for the drive: "cdrom://drive<N>.cue" (N the sg index), or, with AB_FAKE_DRIVE, the cue
    static std::string cdfileFor(int sgIndex);

    // --- the hubs: runtime power management keeps a USB drive's hub asleep, and a sleeping one drops the disc ---
    // the power/control file of every USB hub above /sys/class/scsi_generic/sg<N>/device (a directory with
    // bDeviceClass 09), nearest first
    static std::vector<std::string> hubControlFiles(const std::string &sysfsRoot, int sgIndex);
    // writes "on" to each of them; errors are ignored (not root on some platforms), logged once. true: all written
    static bool keepHubsOn(const std::string &sysfsRoot, int sgIndex);

private:
    void run();
    void set(State state, const DiscIdentity &disc);
    void dropDrive();

    Backend backend_;
    int pollMs_;

    mutable std::mutex stateMutex_; // status_
    Status status_;

    std::mutex stepMutex_; // held for a whole poll; pause() takes it to wait the worker out
    std::unique_ptr<Handle> handle_;
    bool inserted_ = false;   // the disc went in (ready edge seen)
    bool done_ = false;       // ... and its answer is published
    bool rereading_ = false;  // after a pause: the answer stands while the disc is read again
    int tries_ = 0;
    std::atomic<bool> paused_{false};
    int sg_ = -1;

    std::thread thread_;
    std::mutex wakeMutex_;
    std::condition_variable wake_;
    bool stopping_ = false;
};
