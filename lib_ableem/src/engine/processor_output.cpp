#include "ableem/engine/processor_output.h"

#include <cctype>

using namespace std;

namespace ableem {

namespace {

string trimmed(const string &s) {
    size_t b = 0, e = s.size();
    while (b < e && isspace(static_cast<unsigned char>(s[b])))
        ++b;
    while (e > b && isspace(static_cast<unsigned char>(s[e - 1])))
        --e;
    return s.substr(b, e - b);
}

// "<word>" or "<word> - text" / "<word> text" / "<word>: text" -> text, when `line` starts with the word
// (case-insensitive) followed by the end or a separator; false otherwise
bool keyword(const string &line, const string &word, string &rest) {
    if (line.size() < word.size())
        return false;
    for (size_t i = 0; i < word.size(); ++i) {
        if (toupper(static_cast<unsigned char>(line[i])) != word[i])
            return false;
    }
    if (line.size() > word.size()) {
        char next = line[word.size()];
        if (next != ' ' && next != '\t' && next != '-' && next != ':')
            return false;
    }
    string text = trimmed(line.substr(word.size()));
    while (!text.empty() && (text[0] == '-' || text[0] == ':'))
        text = trimmed(text.substr(1));
    rest = text;
    return true;
}

bool allDigits(const string &s) {
    if (s.empty() || s.size() > 9)
        return false;
    for (char c : s) {
        if (!isdigit(static_cast<unsigned char>(c)))
            return false;
    }
    return true;
}

} // namespace

//*******************************
// ProcessorOutput::feed
//*******************************
bool ProcessorOutput::feed(const string &raw) {
    if (finished_)
        return false;
    string line = trimmed(raw);
    if (line.empty())
        return false;

    if (line[0] == '#') {
        string body = trimmed(line.substr(1));
        string rest;
        if (keyword(body, "DONE", rest)) {
            done_seen_ = true;
            finished_ = true;
            return false;
        }
        if (keyword(body, "ERROR", rest)) {
            error_seen_ = true;
            error_ = rest.empty() ? "error" : rest;
            finished_ = true;
            return false;
        }
        if (keyword(body, "WARN", rest)) {
            if (!rest.empty())
                warnings_.push_back(rest);
            return false;
        }
        if (keyword(body, "STARTING", rest)) {
            title_ = rest;
            return true;
        }
        if (body.empty())
            return false;
        stage_ = body;
        percent_ = -1;
        done_ = total_ = 0;
        active_ = true;
        return true;
    }

    if (allDigits(line)) {
        int p = stoi(line);
        percent_ = p > 100 ? 100 : p;
        active_ = true;
        return true;
    }

    size_t slash = line.find('/');
    if (slash != string::npos) {
        string n = trimmed(line.substr(0, slash)), m = trimmed(line.substr(slash + 1));
        if (allDigits(n) && allDigits(m) && stoi(m) > 0) {
            done_ = stoi(n);
            total_ = stoi(m);
            active_ = true;
            return true;
        }
    }
    return false;
}

} // namespace ableem
