//
// System: the process and console helpers.
//
#include "system.h"

#if defined(__linux__) && !defined(AB_DEBUG_HOST)
#include <fcntl.h>
#include <linux/kd.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif
#include "../main.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <ableem/engine/log.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <sched.h>
#endif
#include <unistd.h>

using namespace std;

namespace {
string floatToString(float value, int precision) {
    ostringstream oss;
    oss << fixed << setprecision(precision) << value;
    return oss.str();
}
#ifdef _WIN32
wstring wide(const string &s) {
    if (s.empty()) {
        return L"";
    }
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) {
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    }
    return w;
}

// one command line, quoted by the rules CommandLineToArgvW / the CRT undo: an argument with a space, a
// tab or a quote goes in quotes, backslashes before a quote (or the closing one) are doubled
wstring quotedArgument(const string &arg) {
    wstring w = wide(arg);
    if (!w.empty() && w.find_first_of(L" \t\"") == wstring::npos) {
        return w;
    }
    wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : w) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
        } else {
            out.append(backslashes, L'\\');
        }
        backslashes = 0;
        out += c;
    }
    out.append(backslashes * 2, L'\\');
    out += L'"';
    return out;
}

wstring commandLineFor(const string &exe, const vector<string> &args) {
    wstring cmd = quotedArgument(exe);
    for (const string &arg : args) {
        cmd += L" " + quotedArgument(arg);
    }
    return cmd;
}

// the one CreateProcess: a ready command line (the program first), the directory to start in ("" = ours),
// waited for unless `wait` is false; the exit code (0 when not waited for), -1 when it could not start.
// CREATE_NO_WINDOW: a console program (cmd, curl) gets no console window of its own; a GUI program is
// unaffected.
int createProcessAndWait(const wstring &commandLine, const wstring &dir, const string &what, bool wait = true,
                         const function<void()> &whileWaiting = {}) {
    vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back(L'\0');

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                        nullptr, dir.empty() ? nullptr : dir.c_str(), &si, &pi)) {
        PLOG_WARNING << "could not start " << what << ": error " << GetLastError();
        return -1;
    }
    CloseHandle(pi.hThread);
    if (!wait) {
        CloseHandle(pi.hProcess);
        PLOG_INFO << what << " started";
        return 0;
    }
    if (whileWaiting) {
        while (WaitForSingleObject(pi.hProcess, 100) == WAIT_TIMEOUT) {
            whileWaiting();
        }
    } else {
        WaitForSingleObject(pi.hProcess, INFINITE);
    }
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    PLOG_INFO << what << " exited with " << code;
    return static_cast<int>(code);
}
#endif
} // namespace

//*******************************
// System::powerOff
//*******************************
// The one way the app powers the console off: the launcher's L2+R2, the classic menu's L2+R2 and the
// console's power button all come here. sync() first, so the last log lines and any ini just written
// reach the USB stick before the halt.
void System::powerOff() {
#if defined(AB_PLATFORM_PSC) || defined(AB_APPLIANCE)
    System::execUnixCommand("shutdown -h now");
    sync();
    exit(0);
#else
    exit(0); // a dev host or a Windows PC: leaving the launcher is all "power off" means
#endif
}

//*******************************
// System::lowerCurrentThreadPriority
//*******************************
void System::lowerCurrentThreadPriority() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
#elif defined(SCHED_IDLE)
    struct sched_param param;
    param.sched_priority = 0; // SCHED_IDLE requires 0
    if (sched_setscheduler(0, SCHED_IDLE, &param) != 0)
        nice(19); // SCHED_IDLE refused (needs a capability some setups don't grant) - a plain nice bump instead
#else
    nice(19);
#endif
}

//*******************************
// System::getAvailableSpace
//*******************************
/*
 * Return the available space of a usb device
 */
string System::getAvailableSpace() {
    // the filesystem the USB root (the data partition on a Pi, the data tree's drive on Windows) is on
    uint64_t freeBytes = 0, totalBytes = 0;
    if (!diskSpace(Env::getPathToUSBRoot(), freeBytes, totalBytes)) {
        return "";
    }
    const double gb = 1024.0 * 1024.0 * 1024.0;
    int freeSpacePerc = totalBytes > 0 ? static_cast<int>(freeBytes * 100 / totalBytes) : 0;
    return floatToString(static_cast<float>(freeBytes / gb), 2) + " GB / " +
           floatToString(static_cast<float>(totalBytes / gb), 2) + " GB (" + to_string(freeSpacePerc) + "%)";
}

//*******************************
// System::hasDefaultRoute
//*******************************
// /proc/net/route: a header, then Iface, Destination, Gateway, Flags, ... in hex; the default route is the
// one to 00000000 that is up (RTF_UP, flag 1) on anything but the loopback
bool System::defaultRouteIn(const string &routeTable) {
    istringstream in(routeTable);
    string line;
    getline(in, line); // the header
    while (getline(in, line)) {
        istringstream fields(line);
        string iface, destination, gateway, flags;
        if (!(fields >> iface >> destination >> gateway >> flags))
            continue;
        if (iface != "lo" && destination == "00000000" && (strtoul(flags.c_str(), nullptr, 16) & 1u) != 0)
            return true;
    }
    return false;
}

bool System::hasDefaultRoute() {
#ifdef _WIN32
    return true; // Windows is asked by the download itself
#else
    ifstream in("/proc/net/route");
    if (!in)
        return true; // no procfs to ask: let the download decide
    stringstream text;
    text << in.rdbuf();
    return defaultRouteIn(text.str());
#endif
}

//*******************************
// System::diskSpace
//*******************************
bool System::diskSpace(const string &path, uint64_t &freeBytes, uint64_t &totalBytes) {
#ifdef _WIN32
    ULARGE_INTEGER freeToCaller, total;
    if (!GetDiskFreeSpaceExW(wide(path).c_str(), &freeToCaller, &total, nullptr)) {
        return false;
    }
    freeBytes = freeToCaller.QuadPart;
    totalBytes = total.QuadPart;
    return true;
#else
    struct statvfs fs{};
    if (statvfs(path.c_str(), &fs) != 0) {
        return false;
    }
    freeBytes = static_cast<uint64_t>(fs.f_bavail) * fs.f_frsize;
    totalBytes = static_cast<uint64_t>(fs.f_blocks) * fs.f_frsize;
    return true;
#endif
}

namespace {
//******************
// LineSplitter
//******************
// bytes from a pipe -> whole lines; '\n', '\r\n' and a bare '\r' all end one, an empty line is dropped
class LineSplitter {
public:
    LineSplitter(const System::OutputLine &onLine, bool fromStderr) : onLine_(onLine), stderr_(fromStderr) {}
    void add(const char *data, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            char c = data[i];
            if (c == '\n' || c == '\r') {
                flush();
            } else {
                pending_ += c;
            }
        }
    }
    void flush() {
        if (!pending_.empty() && onLine_)
            onLine_(pending_, stderr_);
        pending_.clear();
    }

private:
    const System::OutputLine &onLine_;
    bool stderr_;
    string pending_;
};
} // namespace

//*******************************
// System::runStreaming
//*******************************
int System::runStreaming(const string &exe, const vector<string> &args, const string &cwd,
                         const vector<pair<string, string>> &env, const OutputLine &onLine,
                         const function<bool()> &shouldStop) {
    string what = "'" + exe + "'";
    for (const string &arg : args)
        what += " '" + arg + "'";
    PLOG_INFO << "Streaming: " << what;

    LineSplitter out(onLine, false), err(onLine, true);

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE outRead = nullptr, outWrite = nullptr, errRead = nullptr, errWrite = nullptr;
    if (!CreatePipe(&outRead, &outWrite, &sa, 0) || !CreatePipe(&errRead, &errWrite, &sa, 0)) {
        PLOG_WARNING << "runStreaming: CreatePipe failed: " << GetLastError();
        return -1;
    }
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    // the environment block: ours, with `env` laid over it (names compared without case, as Windows does)
    vector<wstring> entries;
    if (LPWCH block = GetEnvironmentStringsW()) {
        for (LPWCH p = block; *p; p += wcslen(p) + 1)
            entries.emplace_back(p);
        FreeEnvironmentStringsW(block);
    }
    for (const auto &kv : env) {
        wstring name = wide(kv.first);
        auto sameName = [&name](const wstring &entry) {
            size_t eq = entry.find(L'=', 1);
            return eq == name.size() && _wcsnicmp(entry.c_str(), name.c_str(), name.size()) == 0;
        };
        entries.erase(remove_if(entries.begin(), entries.end(), sameName), entries.end());
        entries.push_back(name + L"=" + wide(kv.second));
    }
    vector<wchar_t> envBlock;
    for (const wstring &entry : entries) {
        envBlock.insert(envBlock.end(), entry.begin(), entry.end());
        envBlock.push_back(L'\0');
    }
    envBlock.push_back(L'\0');

    wstring commandLine = commandLineFor(exe, args);
    vector<wchar_t> cmd(commandLine.begin(), commandLine.end());
    cmd.push_back(L'\0');
    wstring dir = wide(cwd);

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = outWrite;
    si.hStdError = errWrite;
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    BOOL started =
        CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | IDLE_PRIORITY_CLASS,
                       envBlock.data(), dir.empty() ? nullptr : dir.c_str(), &si, &pi);
    CloseHandle(outWrite);
    CloseHandle(errWrite);
    if (nul != INVALID_HANDLE_VALUE)
        CloseHandle(nul);
    if (!started) {
        PLOG_WARNING << "could not start " << what << ": error " << GetLastError();
        CloseHandle(outRead);
        CloseHandle(errRead);
        return -1;
    }
    // everything the processor starts goes into the job, and goes with it
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
        memset(&limits, 0, sizeof(limits));
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        AssignProcessToJobObject(job, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    auto drain = [](HANDLE pipe, LineSplitter &splitter, bool &open) {
        bool any = false;
        while (open) {
            DWORD available = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
                open = false; // the writer is gone
                break;
            }
            if (available == 0)
                break;
            char buffer[4096];
            DWORD got = 0;
            if (!ReadFile(pipe, buffer, available < sizeof(buffer) ? available : sizeof(buffer), &got, nullptr) ||
                got == 0) {
                open = false;
                break;
            }
            splitter.add(buffer, got);
            any = true;
        }
        return any;
    };

    bool outOpen = true, errOpen = true, stopped = false;
    for (;;) {
        bool any = drain(outRead, out, outOpen);
        any = drain(errRead, err, errOpen) || any;
        if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
            drain(outRead, out, outOpen);
            drain(errRead, err, errOpen);
            break;
        }
        if (shouldStop && shouldStop()) {
            stopped = true;
            if (job)
                TerminateJobObject(job, 1);
            else
                TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 3000);
            break;
        }
        if (!any)
            Sleep(50);
    }
    out.flush();
    err.flush();
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(outRead);
    CloseHandle(errRead);
    if (job)
        CloseHandle(job);
    if (stopped) {
        PLOG_INFO << what << " stopped";
        return -2;
    }
    PLOG_INFO << what << " exited with " << code;
    return static_cast<int>(code);
#else
    int outPipe[2], errPipe[2];
    if (pipe(outPipe) != 0) {
        PLOG_WARNING << "runStreaming: pipe() failed: " << strerror(errno);
        return -1;
    }
    if (pipe(errPipe) != 0) {
        PLOG_WARNING << "runStreaming: pipe() failed: " << strerror(errno);
        close(outPipe[0]);
        close(outPipe[1]);
        return -1;
    }

    // the child changes to `cwd` before its exec: a relative path to the program has to be made absolute
    // first, or it is looked for in the wrong directory
    string program = exe;
    if (!cwd.empty() && !program.empty() && program[0] != '/' && program.find('/') != string::npos) {
        char here[PATH_MAX];
        if (getcwd(here, sizeof(here)))
            program = string(here) + "/" + program;
    }
    vector<const char *> argv;
    argv.push_back(program.c_str());
    for (const string &arg : args)
        argv.push_back(arg.c_str());
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid == -1) {
        PLOG_WARNING << "fork() failed: " << strerror(errno);
        close(outPipe[0]);
        close(outPipe[1]);
        close(errPipe[0]);
        close(errPipe[1]);
        return -1;
    }
    if (pid == 0) {
        // a group of its own, so a stop reaches whatever it started too
        setpgid(0, 0);
        int nullFd = open("/dev/null", O_RDONLY);
        if (nullFd >= 0) {
            dup2(nullFd, 0);
            close(nullFd);
        }
        dup2(outPipe[1], 1);
        dup2(errPipe[1], 2);
        close(outPipe[0]);
        close(outPipe[1]);
        close(errPipe[0]);
        close(errPipe[1]);
        if (!cwd.empty() && chdir(cwd.c_str()) != 0)
            _exit(126);
        for (const auto &kv : env)
            setenv(kv.first.c_str(), kv.second.c_str(), 1);
        execvp(program.c_str(), const_cast<char **>(argv.data()));
        // why, on stderr - which is the caller's onLine: a processor that cannot run says so in the log
        const char *why = strerror(errno);
        ssize_t ignored = write(2, "exec failed: ", 13);
        ignored = write(2, why, strlen(why));
        ignored = write(2, "\n", 1);
        (void)ignored;
        _exit(127);
    }
    setpgid(pid, pid); // both sides, so there is no window where the group does not exist yet
    close(outPipe[1]);
    close(errPipe[1]);

    struct pollfd fds[2];
    fds[0].fd = outPipe[0];
    fds[0].events = POLLIN;
    fds[1].fd = errPipe[0];
    fds[1].events = POLLIN;
    bool outOpen = true, errOpen = true, stopped = false;
    while (outOpen || errOpen) {
        fds[0].fd = outOpen ? outPipe[0] : -1;
        fds[1].fd = errOpen ? errPipe[0] : -1;
        int ready = poll(fds, 2, 100);
        if (ready < 0 && errno != EINTR)
            break;
        for (int i = 0; i < 2 && ready > 0; ++i) {
            if (fds[i].fd < 0 || !(fds[i].revents & (POLLIN | POLLHUP | POLLERR)))
                continue;
            char buffer[4096];
            ssize_t got = read(fds[i].fd, buffer, sizeof(buffer));
            if (got > 0) {
                (i == 0 ? out : err).add(buffer, static_cast<size_t>(got));
            } else if (got == 0 || (errno != EINTR && errno != EAGAIN)) {
                (i == 0 ? outOpen : errOpen) = false;
            }
        }
        if (shouldStop && shouldStop()) {
            stopped = true;
            break;
        }
    }
    out.flush();
    err.flush();
    close(outPipe[0]);
    close(errPipe[0]);

    int status = 0;
    if (stopped) {
        kill(-pid, SIGTERM);
        bool gone = false;
        for (int i = 0; i < 30 && !gone; ++i) {
            if (waitpid(pid, &status, WNOHANG) == pid)
                gone = true;
            else
                usleep(100 * 1000);
        }
        kill(-pid, SIGKILL); // what is left of the group, the processor itself included when it ignored TERM
        if (!gone)
            waitpid(pid, &status, 0);
        PLOG_INFO << what << " stopped";
        return -2;
    }
    if (waitpid(pid, &status, 0) == -1) {
        PLOG_WARNING << "waitpid() failed: " << strerror(errno);
        return -1;
    }
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code == 127) {
            PLOG_WARNING << "could not start: " << exe;
            return -1;
        }
        PLOG_INFO << what << " exited with " << code;
        return code;
    }
    PLOG_WARNING << what << " was killed by a signal";
    return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
#endif
}

//*******************************
// System::startDetached
//*******************************
bool System::startDetached(const string &exe, const vector<string> &args) {
    string line = "Starting (not waited for): '" + exe + "'";
    for (const string &arg : args) {
        line += " '" + arg + "'";
    }
    PLOG_INFO << line;
#ifdef _WIN32
    return createProcessAndWait(commandLineFor(exe, args), L"", exe, false) == 0;
#else
    vector<const char *> argv;
    argv.push_back(exe.c_str());
    for (const string &arg : args) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);
    pid_t pid = fork();
    if (pid == -1) {
        PLOG_WARNING << "fork() failed: " << strerror(errno);
        return false;
    }
    if (pid == 0) {
        setsid();
        execvp(exe.c_str(), const_cast<char **>(argv.data()));
        _exit(127);
    }
    return true;
#endif
}

//*******************************
// System::makeDirectoryLink / removeDirectoryLink
//*******************************
bool System::makeDirectoryLink(const string &link, const string &target) {
    removeDirectoryLink(link);
#ifdef _WIN32
    string l = link, t = target;
    for (char &c : l)
        if (c == '/')
            c = '\\';
    for (char &c : t)
        if (c == '/')
            c = '\\';
    // a junction wants a directory that exists
    if (!DirEntry::isDirectory(target)) {
        return false;
    }
    return runShellCommand("mklink /J \"" + l + "\" \"" + t + "\" >nul") == 0 && DirEntry::isDirectory(link);
#else
    // a relative target would be taken relative to the link's own directory: the target as an absolute path
    char resolved[PATH_MAX];
    const char *abs = realpath(target.c_str(), resolved);
    return abs != nullptr && symlink(abs, link.c_str()) == 0;
#endif
}

void System::removeDirectoryLink(const string &link) {
#ifdef _WIN32
    RemoveDirectoryW(wide(link).c_str()); // a junction goes, its target stays; a real directory only when empty
#else
    unlink(link.c_str());
#endif
}

//*******************************
// System::runShellCommand
//*******************************
int System::runShellCommand(const string &commandLine) {
    PLOG_INFO << "Shell: " << commandLine;
#ifdef _WIN32
    // cmd.exe parses its own command line, not by the CRT's rules: `/c "<line>"` - the outer quotes go
    // and everything between them is the command, its own quotes intact. ComSpec is where cmd.exe is.
    const char *comspec = getenv("ComSpec");
    wstring cmd = L"\"" + wide(comspec ? comspec : "cmd.exe") + L"\" /c \"" + wide(commandLine) + L"\"";
    return createProcessAndWait(cmd, L"", "cmd /c " + commandLine);
#else
    int status = system(commandLine.c_str());
    if (status == -1) {
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

//*******************************
// System::runShellCommand (cancellable)
//*******************************
// The same command line, run so it can be stopped: `cancelled` is asked every 100 ms, and when it says yes
// the command and everything it started goes - a process group on Linux (SIGTERM, then SIGKILL a second
// later), a job object on Windows (cmd.exe and the curl it runs alike). An extension's download worker needs
// this: a power-off or a game launch must not wait for a download of hundreds of MB to finish.
int System::runShellCommand(const string &commandLine, const function<bool()> &cancelled) {
    PLOG_INFO << "Shell: " << commandLine;
#ifdef _WIN32
    const char *comspec = getenv("ComSpec");
    wstring cmd = L"\"" + wide(comspec ? comspec : "cmd.exe") + L"\" /c \"" + wide(commandLine) + L"\"";
    vector<wchar_t> buffer(cmd.begin(), cmd.end());
    buffer.push_back(L'\0');
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
        memset(&limits, 0, sizeof(limits));
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }
    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
        PLOG_WARNING << "could not start cmd /c " << commandLine << ": error " << GetLastError();
        if (job != nullptr)
            CloseHandle(job);
        return -1;
    }
    if (job != nullptr)
        AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    int result = 0;
    while (WaitForSingleObject(pi.hProcess, 100) == WAIT_TIMEOUT) {
        if (cancelled && cancelled()) {
            PLOG_INFO << "Shell command stopped: " << commandLine;
            if (job != nullptr)
                TerminateJobObject(job, 1);
            else
                TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            result = -2;
            break;
        }
    }
    if (result == 0) {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        result = static_cast<int>(code);
    }
    CloseHandle(pi.hProcess);
    if (job != nullptr)
        CloseHandle(job);
    return result;
#else
    pid_t pid = fork();
    if (pid == -1) {
        PLOG_WARNING << "fork() failed: " << strerror(errno);
        return -1;
    }
    if (pid == 0) {
        setpgid(0, 0); // a group of its own: stopping it stops what the shell started too
        execl("/bin/sh", "sh", "-c", commandLine.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }
    setpgid(pid, pid); // the parent too, so the group exists before either side can race past it
    int status = 0;
    for (;;) {
        pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid)
            break;
        if (done == -1) {
            PLOG_WARNING << "waitpid() failed: " << strerror(errno);
            return -1;
        }
        if (cancelled && cancelled()) {
            PLOG_INFO << "Shell command stopped: " << commandLine;
            kill(-pid, SIGTERM);
            for (int i = 0; i < 10 && waitpid(pid, &status, WNOHANG) == 0; i++)
                usleep(100 * 1000);
            if (waitpid(pid, &status, WNOHANG) == 0) {
                kill(-pid, SIGKILL);
                waitpid(pid, &status, 0);
            }
            return -2;
        }
        usleep(100 * 1000);
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

//*******************************
// System::execUnixCommandLines
//*******************************
vector<string> System::execUnixCommandLines(const string &cmd) {
    vector<string> lines;
    PLOG_INFO << "Exec:" << cmd;
    struct PipeCloser {
        void operator()(FILE *f) const { pclose(f); }
    };
    unique_ptr<FILE, PipeCloser> pipe(popen(cmd.c_str(), "r"));
    if (!pipe) {
        PLOG_WARNING << "popen() failed for: " << cmd;
        return lines;
    }
    array<char, 512> buffer;
    string output;
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
        output += buffer.data();
    istringstream in(output);
    string line;
    while (getline(in, line)) {
        line = Strings::trim(line);
        if (!line.empty())
            lines.push_back(line);
    }
    return lines;
}

//*******************************
// System::execUnixCommand
//*******************************
/*
 * Execute a shell command and return output
 */
string System::execUnixCommand(const char *cmd) {
    array<char, 128> buffer;
    string result;
    PLOG_INFO << "Exec:" << cmd;
    struct PipeCloser {
        void operator()(FILE *f) const { pclose(f); }
    }; // not decltype(&pclose): its nonnull attribute is lost on the template argument (GCC 14 warns)
    unique_ptr<FILE, PipeCloser> pipe(popen(cmd, "r"));
    if (!pipe) {
        PLOG_WARNING << "popen() failed for: " << cmd;
        return result; // never throw: there is no handler anywhere and an abort() takes the whole UI down
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    result.erase(remove(result.begin(), result.end(), '\n'), result.end());
    return result;
}

//*******************************
// System::runAndWait
//*******************************
// fork + exec the program and wait for it to finish.
// returns the exit status of the program, or -1 if it could not be started.
int System::runAndWait(const string &exe, const vector<string> &args, const string &cwd,
                       const function<void()> &whileWaiting, const vector<pair<string, string>> &env) {
    string line = "CMD line to execute: '" + exe + "'";
    for (const string &arg : args) {
        line += " '" + arg + "'";
    }
    if (!cwd.empty()) {
        line += " (in " + cwd + ")";
    }
    PLOG_INFO << line;

#ifdef _WIN32
    // the child inherits the launcher's environment block: set the extra variables around the start and
    // put back what was there, so nothing of one App's lingers into the next
    vector<pair<wstring, pair<bool, wstring>>> saved;
    for (const auto &kv : env) {
        wstring name = wide(kv.first);
        DWORD size = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
        wstring previous;
        if (size > 0) {
            previous.resize(size);
            DWORD got = GetEnvironmentVariableW(name.c_str(), &previous[0], size);
            previous.resize(got);
        }
        saved.push_back({name, {size > 0, previous}});
        SetEnvironmentVariableW(name.c_str(), wide(kv.second).c_str());
    }
    int result = createProcessAndWait(commandLineFor(exe, args), wide(cwd), exe, true, whileWaiting);
    for (const auto &s : saved)
        SetEnvironmentVariableW(s.first.c_str(), s.second.first ? s.second.second.c_str() : nullptr);
    return result;
#else
    // argv[0] is the program itself, then the args, then a null terminator
    vector<const char *> argv;
    argv.push_back(exe.c_str());
    for (const string &arg : args) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid == -1) {
        PLOG_WARNING << "fork() failed: " << strerror(errno);
        return -1;
    }
    if (pid == 0) {
        // child. if exec fails we must not return into the parent's code path (that would run a second GUI).
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) {
            _exit(126);
        }
        for (const auto &kv : env)
            setenv(kv.first.c_str(), kv.second.c_str(), 1);
        execvp(exe.c_str(), const_cast<char **>(argv.data()));
        _exit(127);
    }

    int status = 0;
    if (whileWaiting) {
        // polled, so the caller's hook runs meanwhile
        pid_t done = 0;
        while ((done = waitpid(pid, &status, WNOHANG)) == 0) {
            whileWaiting();
            usleep(100 * 1000);
        }
        if (done == -1) {
            PLOG_WARNING << "waitpid() failed: " << strerror(errno);
            return -1;
        }
    } else if (waitpid(pid, &status, 0) == -1) {
        PLOG_WARNING << "waitpid() failed: " << strerror(errno);
        return -1;
    }
    if (WIFEXITED(status)) {
        int exitCode = WEXITSTATUS(status);
        if (exitCode == 127) {
            PLOG_WARNING << "could not start: " << exe;
        } else if (exitCode == 126) {
            PLOG_WARNING << "could not start " << exe << " in " << cwd;
        }
        return exitCode;
    }
    if (WIFSIGNALED(status)) {
        PLOG_INFO << exe << " was killed by signal " << WTERMSIG(status);
    }
    return -1;
#endif
}

//*******************************
// System::getRandomNumber
//*******************************
unsigned int System::getRandomNumber() {
    static bool firstTime{true};
    if (firstTime) {
        srand(time(nullptr));
        firstTime = false;
    }

    return rand();
}

//*******************************
// System::getRandomIndex
// pass 100, get a random index between 0 and 99
//*******************************
unsigned int System::getRandomIndex(unsigned int size) {
    return getRandomNumber() % size;
}

//*******************************
// blankConsole / restoreConsole
//*******************************
#if defined(__linux__) && !defined(AB_DEBUG_HOST)

namespace {

// The VT the launcher runs on. /dev/tty0 is whichever is current, which is the one we are on, and
// works whether the session was started on tty1 or moved with chvt.
int openConsole() {
    return open("/dev/tty0", O_RDWR | O_NOCTTY);
}

bool g_consoleBlanked = false;

} // namespace

void System::blankConsole() {
    int fd = openConsole();
    if (fd < 0) {
        return; // no VT here - a desktop, a container, an ssh session
    }
    // clear it and hide the cursor first: KD_GRAPHICS stops the console *drawing*, but what it has
    // already put in the framebuffer would otherwise still be on the screen
    static const char clear[] = "\033[H\033[2J\033[3J\033[?25l";
    ssize_t written = write(fd, clear, sizeof(clear) - 1);
    (void)written;
    if (ioctl(fd, KDSETMODE, KD_GRAPHICS) == 0 && !g_consoleBlanked) {
        g_consoleBlanked = true;
        atexit(System::restoreConsole); // never leave a machine looking dead
    }
    close(fd);
}

void System::restoreConsole() {
    if (!g_consoleBlanked) {
        return;
    }
    int fd = openConsole();
    if (fd < 0) {
        return;
    }
    ioctl(fd, KDSETMODE, KD_TEXT);
    static const char show[] = "\033[?25h";
    ssize_t written = write(fd, show, sizeof(show) - 1);
    (void)written;
    close(fd);
    g_consoleBlanked = false;
}

#else

void System::blankConsole() {}

void System::restoreConsole() {}

#endif
