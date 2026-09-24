#include "processor_runner.h"
#include "../main.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <ableem/engine/log.h>

using namespace std;

namespace {

const long long LogLimitBytes = 1024 * 1024; // processors.log goes to processors.log.1 past this

string now() {
    time_t t = time(nullptr);
    char text[32];
    strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", localtime(&t));
    return text;
}

string commandText(const ProcessorInfo &processor, const vector<string> &args) {
    string text = processor.name + " " + (processor.version.empty() ? "?" : processor.version);
    for (const string &a : args)
        text += a.find(' ') == string::npos ? " " + a : " \"" + a + "\"";
    return text;
}

} // namespace

//*******************************
// ProcessorRunner::defaultTmpBase
//*******************************
string ProcessorRunner::defaultTmpBase() {
#ifdef _WIN32
    const char *temp = getenv("TEMP");
    if (!temp)
        temp = getenv("TMP");
    return string(temp ? temp : ".") + sep + "abproc";
#else
    return "/tmp/abproc";
#endif
}

//*******************************
// ProcessorRunner::log
//*******************************
void ProcessorRunner::log(const string &text) {
    if (options_.logFile.empty())
        return;
    if (DirEntry::exists(options_.logFile) && DirEntry::fileSize(options_.logFile) > LogLimitBytes) {
        DirEntry::removeFile(options_.logFile + ".1");
        DirEntry::renameFile(options_.logFile, options_.logFile + ".1");
    }
    ofstream out(options_.logFile, ios::binary | ios::app);
    out << text;
}

//*******************************
// ProcessorRunner::environment
//*******************************
vector<pair<string, string>> ProcessorRunner::environment(const ProcessorInfo &processor, const string &tmp) const {
    vector<pair<string, string>> env = options_.env;
    env.emplace_back("AB_PROCESSOR_PROTOCOL", "1");
    env.emplace_back("AB_PROCESSOR_NAME", processor.name);
    if (!tmp.empty())
        env.emplace_back("AB_TMP", tmp);
    for (const auto &kv : processor.manifest.env)
        env.push_back(kv);
    return env;
}

//*******************************
// ProcessorRunner::isMine
//*******************************
bool ProcessorRunner::isMine(const ProcessorInfo &processor, const vector<string> &targetArgs,
                             const function<bool()> &stop) {
    vector<string> args = {"--ismine"};
    args.insert(args.end(), targetArgs.begin(), targetArgs.end());
    int code =
        process_.run(processor.manifest.program, args, processor.folder, environment(processor, ""), nullptr, stop);
    if (code == 0)
        return true;
    if (code != 1) {
        log("=== " + now() + "  " + commandText(processor, args) + "\n=== exit " + to_string(code) +
            " - taken as not mine\n");
    }
    return false;
}

//*******************************
// ProcessorRunner::start
//*******************************
ProcessorRunner::Outcome ProcessorRunner::start(const ProcessorInfo &processor, const vector<string> &targetArgs,
                                                const string &label, const Progress &onProgress,
                                                const function<bool()> &stop) {
    vector<string> args = {"--start"};
    args.insert(args.end(), targetArgs.begin(), targetArgs.end());

    string tmp;
    if (!options_.tmpBase.empty()) {
        tmp = options_.tmpBase + sep + processor.name;
        DirEntry::removeDirAndContents(tmp);
        DirEntry::createDirs(tmp);
    }

    string header = "=== " + now() + "  " + commandText(processor, args);
    if (!label.empty())
        header += "  (" + label + ")";
    log(header + "\n");
    PLOG_INFO << "processor: " << commandText(processor, args);

    ableem::ProcessorOutput output;
    auto started = chrono::steady_clock::now();
    auto lastLine = started;
    bool timedOut = false;
    string logged;

    System::OutputLine onLine = [&](const string &line, bool fromStderr) {
        lastLine = chrono::steady_clock::now();
        logged += (fromStderr ? "! " : "") + line + "\n";
        if (logged.size() > 4096) { // written in batches, not a file open per percent
            log(logged);
            logged.clear();
        }
        if (!fromStderr && output.feed(line) && onProgress)
            onProgress(output);
    };
    auto shouldStop = [&]() {
        if (stop && stop())
            return true;
        if (processor.timeoutSeconds > 0 &&
            chrono::steady_clock::now() - lastLine > chrono::seconds(processor.timeoutSeconds)) {
            timedOut = true;
            return true;
        }
        return false;
    };

    int code = process_.run(processor.manifest.program, args, processor.folder, environment(processor, tmp), onLine,
                            shouldStop);
    log(logged);

    if (!tmp.empty())
        DirEntry::removeDirAndContents(tmp);

    Outcome outcome;
    outcome.warnings = output.warnings();
    outcome.announced = output.active();
    if (code == -2 && timedOut) {
        outcome.result = ProcessorResult::Failed;
        outcome.message = "no output for " + to_string(processor.timeoutSeconds) + " s";
    } else if (code == -2) {
        outcome.result = ProcessorResult::Interrupted;
    } else if (code == -1) {
        outcome.result = ProcessorResult::Failed;
        outcome.message = "could not be started";
    } else if (output.succeeded(code)) {
        outcome.result = ProcessorResult::Ok;
    } else {
        outcome.result = ProcessorResult::Failed;
        if (output.reportedError())
            outcome.message = output.error();
        else if (!output.reportedDone())
            outcome.message = "ended (exit " + to_string(code) + ") without #DONE";
        else
            outcome.message = "said #DONE but exited with " + to_string(code);
    }

    double seconds = chrono::duration<double>(chrono::steady_clock::now() - started).count();
    ostringstream footer;
    footer << "=== exit " << code << ", " << ProcessorState::resultName(outcome.result);
    if (!outcome.message.empty())
        footer << " - " << outcome.message;
    footer << ", " << fixed << setprecision(1) << seconds << " s\n";
    log(footer.str());
    if (outcome.result != ProcessorResult::Ok) {
        PLOG_WARNING << "processor " << processor.name << " " << ProcessorState::resultName(outcome.result)
                     << (outcome.message.empty() ? "" : ": " + outcome.message);
    }
    return outcome;
}
