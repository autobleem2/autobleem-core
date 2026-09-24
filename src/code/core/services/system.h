//
// System: the process and console helpers - running a program, a shell command, powering off, free space,
// random numbers. What is not portable enough to live in lib_ableem.
//
#pragma once

#include "environment.h" // for AB_DEBUG_HOST

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

//******************
// System
//******************
// Was the non-string half of Util (the string half is ableem::Strings, reached as Strings::). Stateless
// calls into the OS, so static; the one that matters for testing, runAndWait, is behind ProcessRunner.
class System {
public:
    // fork + exec 'exe' with 'args' (argv[0] is added for you) and wait, started in 'cwd' when one is given.
    // returns exit code, -1 if it could not run. This is the only fork/exec in the code base - everything
    // that starts a process goes through here (CreateProcess on Windows, where the child gets no console
    // window of its own). `whileWaiting`, when given, is called every 100 ms of the wait - the Windows
    // product keeps its window's events pumped with it while an emulator runs in front of it. `env` is set
    // in the child's environment on top of the launcher's (an App's AB_APP_* and its ini's Env=).
    static int runAndWait(const std::string &exe, const std::vector<std::string> &args, const std::string &cwd = "",
                          const std::function<void()> &whileWaiting = {},
                          const std::vector<std::pair<std::string, std::string>> &env = {});
    // one line of a child's output, without its end of line ('\n', '\r\n' or a bare '\r' - a progress
    // counter that rewrites one line is a line per update), and whether it came from stderr
    using OutputLine = std::function<void(const std::string &line, bool fromStderr)>;
    // the other way to start a program, for a scanner processor (docs/scanner-processors-plan.md): like
    // runAndWait, but the child's stdout and stderr come back line by line through `onLine` as they are
    // written (stdin is empty), and `shouldStop` is asked every ~100 ms - once it answers true the child and
    // everything it started (its process group; a job object on Windows) gets SIGTERM, 3 s, then SIGKILL
    // (TerminateJobObject at once on Windows) and -2 is returned. The child runs at the calling thread's
    // priority on Linux (the scan worker's idle one) and at IDLE_PRIORITY_CLASS on Windows. Returns the
    // exit code, -1 when it could not be started.
    static int runStreaming(const std::string &exe, const std::vector<std::string> &args, const std::string &cwd,
                            const std::vector<std::pair<std::string, std::string>> &env, const OutputLine &onLine,
                            const std::function<bool()> &shouldStop);
    // starts a program and does not wait: the Windows product's update, which the launcher hands the
    // installer and leaves. True when it started.
    static bool startDetached(const std::string &exe, const std::vector<std::string> &args);

    // a directory link: a symlink, or on Windows a junction (mklink /J - no privilege needed, NTFS only)
    // - what a direct launch of pcsx-ab lays its run directory out with, the way launch.sh does with ln -s
    static bool makeDirectoryLink(const std::string &link, const std::string &target);
    // removes the link itself, never what it points at (RemoveDirectory on a junction, unlink on a symlink)
    static void removeDirectoryLink(const std::string &link);

    static std::string execUnixCommand(const char *cmd); // run a shell command, return its stdout ("" on failure)
    // runs a command line through the shell and waits: std::system, except that on Windows the child
    // (cmd.exe and what it starts - curl) gets no console window, so nothing flashes over the launcher.
    // returns the exit status (0 = success), -1 when the shell could not be started. What OnlineAssets and
    // UpdateService run their download commands with.
    static int runShellCommand(const std::string &commandLine);
    // the same, stoppable: `cancelled` is asked every 100 ms and, once it answers true, the command and
    // everything it started is killed and -2 returned (a download worker that must not hold up a power-off)
    static int runShellCommand(const std::string &commandLine, const std::function<bool()> &cancelled);
    // the same, one entry per non-empty line of stdout, trimmed - for a command that lists things
    static std::vector<std::string> execUnixCommandLines(const std::string &cmd);

    static void powerOff(); // halts the console; exits the app on a debug host

    // drops the calling thread (not the process) to the OS's lowest scheduling priority - idle-priority
    // where the platform has it (SCHED_IDLE / THREAD_PRIORITY_IDLE: runs at full speed when nothing else
    // wants the CPU, yields almost completely once something else does), a plain nice(19) otherwise. Used
    // by ScanService's worker thread so a background scan never competes with a running emulator for CPU.
    static void lowerCurrentThreadPriority();

    // The text console behind our window, on a machine where the launcher *is* the session - an
    // appliance on bare KMS, with no compositor and a getty's leftovers on the tty underneath.
    // Whenever we give the display up for a game, SDL puts that tty back into text mode and whatever
    // was last printed on it reappears: a login prompt, a systemd line, the tail of a script. Blanking
    // it means the hand-over reads as black rather than as somebody else's terminal.
    //
    // blankConsole() clears the tty and stops it drawing; restoreConsole() puts it back, and is
    // registered with atexit so a crash does not leave a machine that looks switched off. Both do
    // nothing where there is no Linux VT to speak of.
    static void blankConsole();
    static void restoreConsole();

    // the free and total bytes of the filesystem 'path' is on (statvfs / GetDiskFreeSpaceEx); false when
    // the path is not there
    static bool diskSpace(const std::string &path, uint64_t &freeBytes, uint64_t &totalBytes);
    static std::string getAvailableSpace(); // "N GB / M GB (P%)" for the status bar, of the USB root's filesystem

    // whether the machine has a way out at all - a default route that is up (Linux's /proc/net/route; true
    // where there is no such table to read). The console's update check asks this before it spends a
    // download command on a machine that has no network (a stock console never has one)
    static bool hasDefaultRoute();
    static bool defaultRouteIn(const std::string &routeTable); // the parse, for the tests

    static unsigned int getRandomNumber();
    static unsigned int getRandomIndex(unsigned int size); // 0 .. size-1
};
