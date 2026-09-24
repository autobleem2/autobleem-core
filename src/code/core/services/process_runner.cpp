//
// ForkProcessRunner: the production ProcessRunner.
//
#include "process_runner.h"
#include "system.h"

//*******************************
// LaunchPlan::toString
//*******************************
std::string LaunchPlan::toString() const {
    std::string line = "'" + exe + "'";
    for (const std::string &arg : args) {
        line += " '" + arg + "'";
    }
    if (!cwd.empty()) {
        line += " (in " + cwd + ")";
    }
    for (const auto &kv : env) {
        line += " " + kv.first + "=" + kv.second;
    }
    return line;
}

//*******************************
// ForkProcessRunner::run
//*******************************
void ForkProcessRunner::run(const LaunchPlan &plan) {
    System::runAndWait(plan.exe, plan.args, plan.cwd, {}, plan.env);
}

//*******************************
// WinProcessRunner::run
//*******************************
void WinProcessRunner::run(const LaunchPlan &plan) {
    System::runAndWait(plan.exe, plan.args, plan.cwd, whileWaiting_, plan.env);
}
