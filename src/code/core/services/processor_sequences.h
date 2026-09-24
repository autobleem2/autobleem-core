//
// ProcessorSequences: the order the user put the processors in, and which are on - System/Processors/sequence.ini
// (docs/scanner-processors-plan.md in the launcher, "Sequences").
//
#pragma once

#include "processor_catalog.h"

#include <string>
#include <vector>

//******************
// ProcessorSequences
//******************
// Two ordered lists, [ps1] and [roms], one processor name per line, a leading '-' for one that is switched
// off; ';' or '#' starts a comment. load() merges the file with what is installed:
//   - the file's order is kept for every processor still installed and belonging to that sequence;
//   - a processor not in the file yet goes to the end, switched on (several new ones by Order=, then name) -
//     Order is the author's suggestion for a new one, never a reason to reorder what the user sorted;
//   - a name that is not installed (or does not belong there) is dropped.
// A processor with no binary for this machine stays in its place (the stick may go to another machine).
class ProcessorSequences {
public:
    struct Entry {
        std::string name;
        bool enabled = true;
        bool operator==(const Entry &o) const { return name == o.name && enabled == o.enabled; }
    };

    explicit ProcessorSequences(std::string file) : file_(std::move(file)) {}

    // reads the file (a missing one is empty) and merges it with `installed`; true when the result is not
    // what the file says, so the caller saves it
    bool load(const std::vector<ProcessorInfo> &installed);
    bool save() const;

    const std::vector<Entry> &entries(ProcessorSequence sequence) const;
    // moves the entry at `from` to `to` (both clamped); false when nothing changed
    bool move(ProcessorSequence sequence, int from, int to);
    void setEnabled(ProcessorSequence sequence, int index, bool enabled);

    // the switched-on processors of a sequence, in order, that can run here
    std::vector<const ProcessorInfo *> chain(ProcessorSequence sequence,
                                             const std::vector<ProcessorInfo> &installed) const;

    const std::string &file() const { return file_; }
    static const char *sectionName(ProcessorSequence sequence); // "ps1", "roms"

private:
    std::vector<Entry> &list(ProcessorSequence sequence) { return sequence == ProcessorSequence::Ps1 ? ps1_ : roms_; }
    std::string file_;
    std::vector<Entry> ps1_, roms_;
};
