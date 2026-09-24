//
// ProcessorOutput: what a scanner processor's stdout says, one line at a time - the launcher's side of the
// processor protocol (docs/scanner-processors-plan.md in the launcher, "The output").
//
#pragma once

#include <string>
#include <vector>

namespace ableem {

//******************
// ProcessorOutput
//******************
// Feed it every line the processor prints; it keeps the state the notification bubble shows and the result.
//
//   #Starting - <text>   the run's title (the first line)
//   0..100               percent of the current stage
//   <n>/<m>              item counter in the current stage
//   #<text>              a new stage; the percent goes back to "none"
//   #WARN <text>         a warning; the job goes on
//   #DONE                finished, successfully
//   #ERROR - <text>      finished, failed
//   anything else        ignored (logged by the caller)
//
// Nothing after #DONE or #ERROR changes the state. A run succeeded only when it printed #DONE (and no #ERROR)
// *and* exited with 0 - succeeded() takes the exit code for that reason.
class ProcessorOutput {
public:
    // one line, without its end of line (a trailing '\r' is dropped here); returns true when the line
    // changed what the bubble shows (title, stage, percent or counter)
    bool feed(const std::string &line);

    const std::string &title() const { return title_; } // "" until #Starting
    const std::string &stage() const { return stage_; } // the last #<text>, "" before the first
    int percent() const { return percent_; }            // -1 = none in this stage
    int done() const { return done_; }                  // the counter, 0/0 = none
    int total() const { return total_; }
    // a percent, a counter or a stage arrived - a run that only says #Starting/#DONE is never announced
    bool active() const { return active_; }

    bool finished() const { return finished_; } // #DONE or #ERROR seen
    bool reportedDone() const { return done_seen_; }
    bool reportedError() const { return !error_.empty() || error_seen_; }
    const std::string &error() const { return error_; } // #ERROR's text
    const std::vector<std::string> &warnings() const { return warnings_; }

    // #DONE, no #ERROR, and the exit code the caller got was 0
    bool succeeded(int exitCode) const { return done_seen_ && !reportedError() && exitCode == 0; }

private:
    std::string title_, stage_, error_;
    std::vector<std::string> warnings_;
    int percent_ = -1;
    int done_ = 0, total_ = 0;
    bool active_ = false;
    bool finished_ = false;
    bool done_seen_ = false;
    bool error_seen_ = false;
};

} // namespace ableem
