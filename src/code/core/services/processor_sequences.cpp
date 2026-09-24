#include "processor_sequences.h"
#include "../main.h"

#include <algorithm>
#include <fstream>

using namespace std;

//*******************************
// ProcessorSequences::sectionName
//*******************************
const char *ProcessorSequences::sectionName(ProcessorSequence sequence) {
    return sequence == ProcessorSequence::Ps1 ? "ps1" : "roms";
}

//*******************************
// ProcessorSequences::entries
//*******************************
const vector<ProcessorSequences::Entry> &ProcessorSequences::entries(ProcessorSequence sequence) const {
    return sequence == ProcessorSequence::Ps1 ? ps1_ : roms_;
}

//*******************************
// ProcessorSequences::load
//*******************************
bool ProcessorSequences::load(const vector<ProcessorInfo> &installed) {
    vector<Entry> fromFile[2];
    ifstream in(file_, ios::binary);
    int section = -1;
    string line;
    while (in && getline(in, line)) {
        line = ProcessorCatalog::stripComment(Strings::trim(line));
        if (line.empty())
            continue;
        if (line.front() == '[' && line.back() == ']') {
            string name = Strings::trim(line.substr(1, line.size() - 2));
            lcase(name);
            section = name == "ps1" ? 0 : name == "roms" ? 1 : -1;
            continue;
        }
        if (section < 0)
            continue;
        Entry entry;
        if (line[0] == '-') {
            entry.enabled = false;
            line = Strings::trim(line.substr(1));
        }
        entry.name = line;
        if (!entry.name.empty())
            fromFile[section].push_back(entry);
    }

    bool changed = false;
    for (int s = 0; s < 2; ++s) {
        ProcessorSequence sequence = s == 0 ? ProcessorSequence::Ps1 : ProcessorSequence::Roms;
        auto belongs = [&](const string &name) {
            for (const ProcessorInfo &p : installed) {
                if (p.name == name)
                    return p.belongsTo(sequence);
            }
            return false;
        };
        vector<Entry> merged;
        for (const Entry &e : fromFile[s]) {
            bool dup = any_of(merged.begin(), merged.end(), [&e](const Entry &m) { return m.name == e.name; });
            if (belongs(e.name) && !dup)
                merged.push_back(e);
        }
        vector<const ProcessorInfo *> fresh;
        for (const ProcessorInfo &p : installed) {
            if (!p.belongsTo(sequence))
                continue;
            if (none_of(merged.begin(), merged.end(), [&p](const Entry &m) { return m.name == p.name; }))
                fresh.push_back(&p);
        }
        stable_sort(fresh.begin(), fresh.end(), [](const ProcessorInfo *a, const ProcessorInfo *b) {
            return a->order != b->order ? a->order < b->order : a->name < b->name;
        });
        for (const ProcessorInfo *p : fresh)
            merged.push_back({p->name, true});

        changed = changed || merged != fromFile[s];
        list(sequence) = merged;
    }
    return changed;
}

//*******************************
// ProcessorSequences::save
//*******************************
bool ProcessorSequences::save() const {
    ofstream out(file_, ios::binary);
    if (!DirEntry::checkWritable(out, file_))
        return false;
    out << "; the order the scan runs the processors in, per sequence; a leading '-' switches one off\n"
           "; (the System menu's Scanner processors edits this file)\n";
    for (ProcessorSequence sequence : {ProcessorSequence::Ps1, ProcessorSequence::Roms}) {
        out << "\n[" << sectionName(sequence) << "]\n";
        for (const Entry &e : entries(sequence))
            out << (e.enabled ? "" : "-") << e.name << "\n";
    }
    out.close();
    return !out.fail();
}

//*******************************
// ProcessorSequences::move
//*******************************
bool ProcessorSequences::move(ProcessorSequence sequence, int from, int to) {
    vector<Entry> &l = list(sequence);
    if (l.empty())
        return false;
    int last = static_cast<int>(l.size()) - 1;
    from = max(0, min(from, last));
    to = max(0, min(to, last));
    if (from == to)
        return false;
    Entry moving = l[from];
    l.erase(l.begin() + from);
    l.insert(l.begin() + to, moving);
    return true;
}

//*******************************
// ProcessorSequences::setEnabled
//*******************************
void ProcessorSequences::setEnabled(ProcessorSequence sequence, int index, bool enabled) {
    vector<Entry> &l = list(sequence);
    if (index >= 0 && index < static_cast<int>(l.size()))
        l[index].enabled = enabled;
}

//*******************************
// ProcessorSequences::chain
//*******************************
vector<const ProcessorInfo *> ProcessorSequences::chain(ProcessorSequence sequence,
                                                        const vector<ProcessorInfo> &installed) const {
    vector<const ProcessorInfo *> out;
    for (const Entry &e : entries(sequence)) {
        if (!e.enabled)
            continue;
        for (const ProcessorInfo &p : installed) {
            if (p.name == e.name && p.builtForThisSystem() && p.belongsTo(sequence))
                out.push_back(&p);
        }
    }
    return out;
}
