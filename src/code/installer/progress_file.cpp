//
// ProgressFile - see the header.
//
#include "installer/progress_file.h"

#include <ableem/engine/filesystem.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <fstream>

using namespace std;
using ableem::DirEntry;

namespace {
volatile sig_atomic_t stopFlag = 0;

extern "C" void onTerminate(int) {
    stopFlag = 1;
}
} // namespace

//*******************************
// ProgressFile: the format
//*******************************
string ProgressFile::phaseLine(int index, int total, const string &key, uint64_t done, uint64_t size) {
    return "phase " + to_string(index) + "/" + to_string(total) + "|" + key + "|" + to_string(done) + "|" +
           to_string(size);
}

string ProgressFile::endLine(bool ok, bool stopped, const string &errorCode) {
    if (ok)
        return "end|ok";
    if (stopped)
        return "end|stopped";
    return "end|error|" + (errorCode.empty() ? string("other") : errorCode);
}

int ProgressFile::exitCode(bool ok, bool stopped) {
    return ok ? 0 : (stopped ? 3 : 1);
}

//*******************************
// ProgressFile: the stop flag
//*******************************
void ProgressFile::installStopHandler() {
    signal(SIGTERM, onTerminate);
    signal(SIGINT, onTerminate);
}

bool ProgressFile::stopRequested() {
    return stopFlag != 0;
}

void ProgressFile::requestStop() {
    stopFlag = 1;
}

void ProgressFile::clearStop() {
    stopFlag = 0;
}

InstallerJob::ShouldStop ProgressFile::shouldStop() {
    return []() { return stopRequested(); };
}

//*******************************
// ProgressFile: the file
//*******************************
ProgressFile::ProgressFile(string path, unsigned heartbeatMs) : path_(move(path)), heartbeatMs_(heartbeatMs) {}

ProgressFile::~ProgressFile() {
    {
        lock_guard<mutex> lock(mutex_);
        running_ = false;
    }
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

void ProgressFile::write(const string &line) {
    if (path_.empty())
        return;
    const string tmp = path_ + ".tmp";
    {
        ofstream out(tmp, ios::binary | ios::trunc);
        out << line;
        out.flush();
        if (!out)
            return;
    }
    // rename over the old file is atomic where it is a real rename; replaceFile covers a platform where it is not
    if (rename(tmp.c_str(), path_.c_str()) != 0)
        DirEntry::replaceFile(tmp, path_);
}

void ProgressFile::onPhase(int index, int total, const string &title) {
    lock_guard<mutex> lock(mutex_);
    if (ended_)
        return;
    index_ = index;
    count_ = total;
    key_ = title;
    done_ = total_ = 0;
    write(phaseLine(index_, count_, key_, done_, total_));
    if (!running_ && heartbeatMs_ > 0 && !path_.empty()) {
        running_ = true;
        thread_ = thread([this]() { heartbeat(); });
    }
}

void ProgressFile::onProgress(uint64_t done, uint64_t total) {
    lock_guard<mutex> lock(mutex_);
    done_ = done;
    total_ = total;
}

void ProgressFile::onLine(const string &line) {
    if (path_.empty())
        return;
    lock_guard<mutex> lock(mutex_);
    ofstream log(path_ + ".log", ios::binary | ios::app);
    log << line << "\n";
}

void ProgressFile::heartbeat() {
    unique_lock<mutex> lock(mutex_);
    while (running_) {
        wake_.wait_for(lock, chrono::milliseconds(heartbeatMs_));
        if (running_ && !ended_ && index_ > 0)
            write(phaseLine(index_, count_, key_, done_, total_));
    }
}

int ProgressFile::finish(bool ok, bool stopped, const string &errorCode) {
    {
        lock_guard<mutex> lock(mutex_);
        running_ = false;
    }
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();
    lock_guard<mutex> lock(mutex_);
    ended_ = true;
    write(endLine(ok, stopped, errorCode));
    return exitCode(ok, stopped);
}
