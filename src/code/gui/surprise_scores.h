//
// SurpriseScores: the high score table of "BleemStrike: Reloaded" (UIREV-39 follow-up) - ten rows of three initials
// and a score, as in the old arcade games. Pure (no drawing, no files): the game keeps one table, the About screen
// reads and writes it as one config.ini value ("surprisescores", "AAA:12345;BBB:9000;...").
//
#pragma once

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace surprise {

const int ScoreRows = 10;
const int InitialsLength = 3;
// what an initial can be: the letters, a space and a full stop (Up steps forward through it, Down back)
const std::string InitialsAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ .";

struct ScoreRow {
    std::string initials;
    int score = 0;
};

// the table a fresh stick starts with: ten rows to beat, 10000 down to 1000
inline std::vector<ScoreRow> defaultScoreTable() {
    static const char *const names[ScoreRows] = {"AB2", "PSX", "CPU", "GPU", "SPU", "CDR", "PAD", "RAM", "ROM", "MEM"};
    std::vector<ScoreRow> rows;
    for (int i = 0; i < ScoreRows; i++)
        rows.push_back({names[i], (ScoreRows - i) * 1000});
    return rows;
}

// three characters of the alphabet: anything else becomes a space, a short name is padded
inline std::string cleanInitials(const std::string &text) {
    std::string out;
    for (size_t i = 0; i < static_cast<size_t>(InitialsLength); i++) {
        char c = i < text.size() ? text[i] : ' ';
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
        out += InitialsAlphabet.find(c) == std::string::npos ? ' ' : c;
    }
    return out;
}

inline void sortScores(std::vector<ScoreRow> &rows) {
    std::stable_sort(rows.begin(), rows.end(), [](const ScoreRow &a, const ScoreRow &b) { return a.score > b.score; });
    if (rows.size() > static_cast<size_t>(ScoreRows))
        rows.resize(ScoreRows);
}

// "AAA:12345;BBB:9000" -> rows (bad pieces skipped, best first, at most ScoreRows). An empty value is the default
// table; the single "surprisehighscore" of before the table, when it is in neither, joins it as "???".
inline std::vector<ScoreRow> parseScoreTable(const std::string &text, int legacyHighScore = 0) {
    std::vector<ScoreRow> rows;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(';', start);
        if (end == std::string::npos)
            end = text.size();
        const std::string piece = text.substr(start, end - start);
        const size_t colon = piece.find(':');
        if (colon != std::string::npos && colon + 1 < piece.size()) {
            const int score = std::atoi(piece.c_str() + colon + 1);
            if (score >= 0)
                rows.push_back({cleanInitials(piece.substr(0, colon)), score});
        }
        start = end + 1;
    }
    if (rows.empty())
        rows = defaultScoreTable();
    if (legacyHighScore > 0 &&
        std::none_of(rows.begin(), rows.end(), [&](const ScoreRow &r) { return r.score == legacyHighScore; }))
        rows.push_back({"???", legacyHighScore});
    sortScores(rows);
    return rows;
}

inline std::string formatScoreTable(const std::vector<ScoreRow> &rows) {
    std::string out;
    for (const ScoreRow &r : rows) {
        if (!out.empty())
            out += ';';
        out += cleanInitials(r.initials) + ":" + std::to_string(r.score);
    }
    return out;
}

// the row a score would take (0 = the top), or -1 when it does not beat the last of a full table; a tie goes under
// the rows already there
inline int scoreRank(const std::vector<ScoreRow> &rows, int score) {
    if (score <= 0)
        return -1;
    for (size_t i = 0; i < rows.size(); i++)
        if (score > rows[i].score)
            return static_cast<int>(i);
    return rows.size() < static_cast<size_t>(ScoreRows) ? static_cast<int>(rows.size()) : -1;
}

inline void insertScore(std::vector<ScoreRow> &rows, int rank, const ScoreRow &row) {
    if (rank < 0)
        return;
    rows.insert(rows.begin() + std::min(static_cast<size_t>(rank), rows.size()), row);
    if (rows.size() > static_cast<size_t>(ScoreRows))
        rows.resize(ScoreRows);
}

// the initial `step` places on in the alphabet (Up +1, Down -1), wrapping round
inline char stepInitial(char c, int step) {
    const int n = static_cast<int>(InitialsAlphabet.size());
    size_t at = InitialsAlphabet.find(c);
    int i = at == std::string::npos ? 0 : static_cast<int>(at);
    i = ((i + step) % n + n) % n;
    return InitialsAlphabet[static_cast<size_t>(i)];
}

} // namespace surprise
