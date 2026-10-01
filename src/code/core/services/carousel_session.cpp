#include "carousel_session.h"
#include "environment.h"
#include "../main.h"
#include <cstdlib>
#include <sstream>

using namespace std;

namespace {

// a name goes on one line: the line breaks that would split it are dropped
string oneLine(const string &text) {
    string out;
    for (char c : text) {
        if (c != '\n' && c != '\r')
            out += c;
    }
    return out;
}

// a whole non-negative number or nothing: "12" yes, "" / "1x" / "-3" no
bool readCount(const string &text, int &value) {
    if (text.empty() || text.size() > 9 || text.find_first_not_of("0123456789") != string::npos)
        return false;
    value = atoi(text.c_str());
    return true;
}

} // namespace

//*******************************
// CarouselSession::file
//*******************************
string CarouselSession::file() {
    return Env::getPathToRuntimeDir() + sep + "carousel.session";
}

//*******************************
// CarouselSession::serialize
//*******************************
string CarouselSession::serialize(const GameSetSelection &s) {
    ostringstream out;
    out << "version=" << Version << "\n";
    out << "set=" << static_cast<int>(s.set) << "\n";
    out << "ps1=" << static_cast<int>(s.ps1SelectState) << "\n";
    out << "game=" << s.gameIndex << "\n";
    out << "usbdirindex=" << s.usbGameDirIndex << "\n";
    out << "usbdirname=" << oneLine(s.usbGameDirName) << "\n";
    out << "raindex=" << s.raPlaylistIndex << "\n";
    out << "raname=" << oneLine(s.raPlaylistName) << "\n";
    out << "appcategory=" << static_cast<int>(s.appCategory) << "\n";
    return out.str();
}

//*******************************
// CarouselSession::parse
//*******************************
bool CarouselSession::parse(const string &text, GameSetSelection &selection) {
    GameSetSelection s; // defaults for whatever the file leaves out
    bool versionOk = false;
    istringstream in(text);
    string line;
    while (getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t eq = line.find('=');
        if (eq == string::npos)
            continue;
        const string key = line.substr(0, eq);
        const string value = line.substr(eq + 1);
        int n = 0;
        if (key == "version") {
            if (!readCount(value, n) || n != Version)
                return false;
            versionOk = true;
        } else if (key == "usbdirname") {
            s.usbGameDirName = value;
        } else if (key == "raname") {
            s.raPlaylistName = value;
        } else if (key == "set" || key == "ps1" || key == "game" || key == "usbdirindex" || key == "raindex" ||
                   key == "appcategory") {
            if (!readCount(value, n))
                return false;
            if (key == "set") {
                if (n > static_cast<int>(GameSetLast))
                    return false;
                s.set = static_cast<GameSet>(n);
            } else if (key == "ps1") {
                if (n > static_cast<int>(Ps1SelectState::GamesSubdir))
                    return false;
                s.ps1SelectState = static_cast<Ps1SelectState>(n);
            } else if (key == "game") {
                s.gameIndex = n;
            } else if (key == "usbdirindex") {
                s.usbGameDirIndex = n;
            } else if (key == "raindex") {
                s.raPlaylistIndex = n;
            } else {
                if (n > static_cast<int>(AppCategoryLast))
                    return false;
                s.appCategory = static_cast<AppCategory>(n);
            }
        }
    }
    if (!versionOk)
        return false;
    selection = s;
    return true;
}

//*******************************
// CarouselSession::save / take
//*******************************
bool CarouselSession::save(const string &path, const GameSetSelection &selection) {
    DirEntry::createDirs(DirEntry::getDirNameFromPath(path));
    return DirEntry::writeFileIfChanged(path, serialize(selection)) != DirEntry::WriteResult::Failed;
}

bool CarouselSession::take(const string &path, GameSetSelection &selection) {
    string text;
    if (!DirEntry::readFile(path, text))
        return false;
    DirEntry::removeFile(path); // once: a later start (after a crash, say) must not restore an old place
    return parse(text, selection);
}
