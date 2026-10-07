//
// RaJobService: installing, updating and removing the RetroArch program from the launcher (System menu ->
// RetroArch...). SDL-free, UpdateService-style: the work is done by the platform's RUNNER, a child process
// (Env::raJobCommand(), the platform ini's retroarch_job_command), and this service only starts it, watches its
// progress file and log, stops it on request and turns its exit code into a result. The panel (GuiRaManager) polls it
// once a frame, on the main thread.
//
// The runner contract (what step 2's abupdate / appliance script implement):
//  - Command line: the ini's template with these placeholders replaced - %a the action ("install", "update",
//    "remove"), %B " --bios" when the BIOS files go too (remove only), else "", %p the progress file, %l the log
//    file (the service appends `> %l 2>&1` itself), %R the USB root, %r the launcher's own folder. It is run
//    through /bin/sh in a process group of its own.
//  - Progress file: ONE line, rewritten as the job goes (write a temp file and rename, or a single write):
//        phase <i>/<n>|<title>|<done>|<total>
//    i of n phases, the phase's title, and the bytes (or items) done of total in that phase; total 0 = unknown.
//    The titles the launcher translates: "Preparing", "Downloading RetroArch", "Installing RetroArch", "Downloading
//    cores", "Installing cores", "Downloading libraries and apps", "Downloading BIOS files", "Finishing",
//    "Removing RetroArch", "Removing BIOS files"; any other text is shown as it is. The "phase " prefix is optional.
//    A line that does not parse is ignored.
//  - Log: whatever the runner prints; the last lines are shown under the bar.
//  - Exit code: 0 done; 3 no network; 4 not enough free space on the stick; 5 stopped (SIGTERM) with a resumable
//    state; anything else failed. The log's last line says why.
//  - SIGTERM: sent to the runner's process group when the user stops the job. It must leave a RESUMABLE state (the
//    next start of the same action carries on: partial downloads kept, binary laid last, idempotent phases) and exit
//    within ~1 s - then the group is killed.
//  - Idempotent: the same action again after any end (stop, kill, power loss) completes it.
//
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

//******************
// RaJobService
//******************
class RaJobService {
public:
    enum class Action { Install, Update, Remove, RemoveWithBios };

    // the exit codes of the contract
    static const int ExitOk = 0;
    static const int ExitNoNetwork = 3;
    static const int ExitNoSpace = 4;
    static const int ExitStopped = 5;

    struct Config {
        std::string jobCommand;  // the template, "" = this platform has no runner
        std::string usbRoot;     // %R
        std::string launcherDir; // %r (Env::raJobCommand has it replaced already; kept for a template given as is)
        std::string workDir;     // where the progress file, the log and the catalog scratch live (made on use)
        std::string retroarchDir; // RetroArch's own folder: the folders Remove deletes are under it
        std::vector<std::string> binaries; // Env::retroArchBinaries(): the program may be at any of these
        // the platform's RetroArch catalog, for "is there a newer version": repoUrl + "/" + catalog; "" = none
        std::string repoUrl;
        std::string catalog;
        std::string arch;         // the catalog's key for this machine ("armhf", "i386", "zip")
        std::string fetchCommand; // %u %o
        // what is installed ("" = RetroArch is there but says no version); asked again after every job
        std::function<std::string()> installedVersion;
        // whether there is a network (System::hasDefaultRoute); unset = assume there is
        std::function<bool()> networkUp;
    };
    // runs a command line through the shell until it ends or `cancelled` answers true (-2 then)
    using CommandRunner = std::function<int(const std::string &commandLine, const std::function<bool()> &cancelled)>;

    //*** what the panel shows before anything runs
    struct Inspection {
        bool ready = false;        // the worker has finished
        bool installed = false;    // the program is on the machine
        std::string version;       // the stamp's, "" = none
        uint64_t sizeBytes = 0;    // what Remove would delete: the program and RetroArch's own folders
        bool systemPackage = false; // only a distribution's /usr/bin/retroarch: not ours to remove
        std::string latestVersion; // the catalog's, "" = not known (no network, no catalog)
        bool networkUp = true;
        bool updateAvailable() const {
            return installed && !version.empty() && !latestVersion.empty() && latestVersion != version;
        }
    };

    //*** a job
    enum class Phase { Idle, Running, Succeeded, Failed, Stopped };
    enum class Failure { None, NoNetwork, NoSpace, Other };
    struct Status {
        Phase phase = Phase::Idle;
        Action action = Action::Install;
        bool stopping = false; // a stop was asked, the runner has not ended yet
        int step = 0;          // the runner's phase i of n (0/0 until it writes one)
        int steps = 0;
        std::string title;
        uint64_t done = 0;
        uint64_t total = 0;
        std::vector<std::string> log; // the last lines
        int exitCode = 0;
        Failure failure = Failure::None;
        // the whole job as 0..1 from the phases and the bytes of the running one; < 0 = no phase reported yet
        double fraction() const;
        bool over() const { return phase != Phase::Idle && phase != Phase::Running; }
    };

    explicit RaJobService(CommandRunner runner = CommandRunner(),
                          std::function<int(const std::string &)> fetchRunner = nullptr);
    ~RaJobService();

    void configure(const Config &config);
    const Config &config() const { return config_; }
    bool supported() const { return !config_.jobCommand.empty(); }
    bool busy() const; // a job or an inspection is in flight

    // inspection: start it when the panel opens, poll() it every frame until ready
    void startInspect();
    Inspection pollInspect();

    // true when started (supported, nothing running)
    bool start(Action action);
    // asks the runner to stop (SIGTERM); the job ends Stopped (or whatever the runner says first)
    void stop();
    // main thread, once a frame; Idle until a job was started, the final state stays until the next start
    Status poll();
    const Status &status() const { return status_; }

    // the pure parts, for the tests
    static std::string commandFor(const std::string &commandTemplate, Action action, const std::string &progressFile,
                                  const std::string &logFile, const std::string &usbRoot,
                                  const std::string &launcherDir);
    // "phase 2/5|Downloading cores|1048576|734003200"; false when the line does not parse
    static bool parseProgress(const std::string &line, int &step, int &steps, std::string &title, uint64_t &done,
                              uint64_t &total);
    static Failure failureFor(int exitCode);
    // the folders (under RetroArch's own) that Remove deletes - and nothing of what it keeps (roms, saves, ...)
    static const std::vector<std::string> &removableFolders();

private:
    void jobThread(std::string commandLine);
    void inspectThread();
    void readProgress();
    std::string progressFile() const;
    std::string logFile() const;

    Config config_;
    CommandRunner runner_;
    std::function<int(const std::string &)> fetchRunner_;
    Status status_;
    std::thread worker_;
    std::atomic<bool> workerDone_{false};
    std::atomic<bool> stopRequested_{false};
    int workerExit_ = 0;
    bool stopped_ = false; // what the worker saw: the runner ended because of the stop
    mutable std::mutex mutex_; // the worker's writes
    int64_t lastRead_ = 0;     // ms, the progress file's last read

    std::thread inspector_;
    std::atomic<bool> inspectDone_{false};
    bool inspecting_ = false;
    Inspection inspectionWork_; // the worker's, under mutex_
    Inspection inspection_;
};
