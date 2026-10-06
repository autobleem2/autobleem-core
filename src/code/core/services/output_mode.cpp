#include "output_mode.h"
#include <ableem/ui/canvas.h>
#include "environment.h"
#include "../main.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>

using namespace std;

const char *OutputMode::ConfigKey = "outputmode";
const char *OutputMode::MarginKey = "crtmargin";

int OutputMode::crtMargin(const string &value) {
    if (value.empty() || value.find_first_not_of("0123456789") != string::npos)
        return ableem::DefaultSafeMargin;
    return std::min(ableem::MaxSafeMargin, atoi(value.c_str())); // digits only: never negative
}

//*******************************
// OutputMode::parse / token / label
//*******************************
OutputMode OutputMode::parse(const string &token) {
    OutputMode m;
    if (token == "720") {
        m.w = 1280;
        m.h = 720;
    } else if (token == "1080") {
        m.w = 1920;
        m.h = 1080;
    } else {
        const size_t x = token.find('x');
        if (x != string::npos && x > 0 && x + 1 < token.size() &&
            token.find_first_not_of("0123456789x") == string::npos) {
            m.w = atoi(token.substr(0, x).c_str());
            m.h = atoi(token.substr(x + 1).c_str());
            if (m.w <= 0 || m.h <= 0)
                m = OutputMode();
        }
    }
    return m;
}

string OutputMode::token() const {
    if (isAuto())
        return "auto";
    if (w == 1280 && h == 720)
        return "720";
    if (w == 1920 && h == 1080)
        return "1080";
    return to_string(w) + "x" + to_string(h);
}

string OutputMode::label() const {
    if (isAuto())
        return "";
    if (isCrt())
        return _("CRT 4:3");
    if (w * 9 == h * 16)
        return to_string(h) + "p";
    return to_string(w) + "x" + to_string(h);
}

//*******************************
// OutputMode::placeCrt / themesFor
//*******************************
vector<string> OutputMode::placeCrt(vector<string> tokens) {
    auto crt = find(tokens.begin(), tokens.end(), CrtToken());
    if (crt == tokens.end())
        return tokens;
    long after = -1; // the index of the last "720" / "1080"
    for (size_t i = 0; i < tokens.size(); i++)
        if (tokens[i] == "720" || tokens[i] == "1080")
            after = static_cast<long>(i);
    if (after < 0)
        return tokens;
    const string token = *crt;
    const long from = crt - tokens.begin();
    if (from == after + 1)
        return tokens;
    tokens.erase(crt);
    if (from < after)
        after--; // the erase moved the anchor up by one
    tokens.insert(tokens.begin() + after + 1, token);
    return tokens;
}

vector<string> OutputMode::themesFor(const OutputMode &running, const vector<string> &themes,
                                     const function<bool(const string &)> &supports) {
    if (!running.is43())
        return themes;
    vector<string> only;
    for (const string &name : themes)
        if (supports(name))
            only.push_back(name);
    return only.empty() ? themes : only;
}

//*******************************
// OutputMode::defaultToken / pendingFile / emulatorFile / readToken
//*******************************
string OutputMode::defaultToken() {
#ifdef AB_PLATFORM_PSC
    return "720"; // 1080p is opt-in on the console (the owner, 2026-09-28)
#else
    return "auto";
#endif
}

string OutputMode::pendingFile() {
    return Env::getPathToRuntimeDir() + sep + "outputmode.pending";
}

string OutputMode::emulatorFile() {
    return Env::getPathToRuntimeDir() + sep + "outputmode";
}

bool OutputMode::readToken(const string &file, string &token) {
    ifstream in(file);
    string line;
    if (!in || !getline(in, line))
        return false;
    token = Strings::trim(line);
    return !token.empty();
}
