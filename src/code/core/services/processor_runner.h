//
// ProcessorRunner: one scanner processor run on one target - the command line, the environment, AB_TMP, the
// timeout, System/Logs/processors.log - and what came of it (docs/scanner-processors-plan.md in the launcher).
//
#pragma once

#include "processor_catalog.h"
#include "processor_state.h"
#include "system.h"

#include <ableem/engine/processor_output.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

//******************
// ProcessorProcess
//******************
// how a processor is started - System::runStreaming in the program, a scripted fake in a test
class ProcessorProcess {
public:
    virtual ~ProcessorProcess() = default;
    virtual int run(const std::string &exe, const std::vector<std::string> &args, const std::string &cwd,
                    const std::vector<std::pair<std::string, std::string>> &env, const System::OutputLine &onLine,
                    const std::function<bool()> &shouldStop) = 0;
};

class StreamingProcess : public ProcessorProcess {
public:
    int run(const std::string &exe, const std::vector<std::string> &args, const std::string &cwd,
            const std::vector<std::pair<std::string, std::string>> &env, const System::OutputLine &onLine,
            const std::function<bool()> &shouldStop) override {
        return System::runStreaming(exe, args, cwd, env, onLine, shouldStop);
    }
};

//******************
// ProcessorRunner
//******************
class ProcessorRunner {
public:
    struct Options {
        std::string logFile; // System/Logs/processors.log ("" = no log)
        std::string tmpBase; // AB_TMP is <tmpBase>/<processor name>, made empty before a run, removed after
        std::vector<std::pair<std::string, std::string>> env; // AB_ROOT, AB_GAMES_DIR, ... (the protocol's)
    };

    struct Outcome {
        ProcessorResult result = ProcessorResult::Failed;
        std::string message; // why it failed ("" when it did not)
        std::vector<std::string> warnings;
        bool announced = false; // it said something worth showing (a stage, a percent, a counter)
    };

    using Progress = std::function<void(const ableem::ProcessorOutput &)>;

    ProcessorRunner(ProcessorProcess &process, Options options) : process_(process), options_(std::move(options)) {}

    // processor --ismine <target args>: exit 0 = mine, 1 = not mine, anything else is logged and means not mine
    bool isMine(const ProcessorInfo &processor, const std::vector<std::string> &targetArgs,
                const std::function<bool()> &stop);

    // processor --start <target args>; `label` names the target in the log; `onProgress` is called whenever
    // the output changed what a bubble would show; `stop` interrupts it (the run is then Interrupted)
    Outcome start(const ProcessorInfo &processor, const std::vector<std::string> &targetArgs, const std::string &label,
                  const Progress &onProgress, const std::function<bool()> &stop);

    // /tmp/abproc on Linux, %TEMP%\abproc on Windows - off the stick (the console's /tmp is RAM: small files only)
    static std::string defaultTmpBase();

private:
    void log(const std::string &text);
    std::vector<std::pair<std::string, std::string>> environment(const ProcessorInfo &processor,
                                                                 const std::string &tmp) const;
    ProcessorProcess &process_;
    Options options_;
};
