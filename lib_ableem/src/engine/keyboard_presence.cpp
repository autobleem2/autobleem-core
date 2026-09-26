#include "ableem/engine/keyboard_presence.h"
#include "ableem/engine/filesystem.h"

#include <cstdlib>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;

namespace ableem {
namespace KeyboardPresence {

namespace {
// linux/input-event-codes.h
const int KeyEnter = 28;
// KEY_Q..KEY_P, KEY_A..KEY_L, KEY_Z..KEY_M: the three rows of letters
const int LetterRanges[3][2] = {{16, 25}, {30, 38}, {44, 50}};
// a keyboard without a few letters (a compact or odd layout) still counts
const int LettersNeeded = 20;
} // namespace

//*******************************
// KeyboardPresence::capabilitiesLookLikeKeyboard
//*******************************
bool capabilitiesLookLikeKeyboard(const string &keyBitmap) {
    vector<string> words;
    istringstream in(keyBitmap);
    string word;
    while (in >> word)
        words.push_back(word);
    if (words.empty())
        return false;
    int wordBits = 32;
    for (const string &w : words)
        if (w.size() > 8)
            wordBits = 64;
    auto bitSet = [&](int bit) {
        const size_t fromEnd = static_cast<size_t>(bit / wordBits);
        if (fromEnd >= words.size())
            return false;
        const unsigned long long value = strtoull(words[words.size() - 1 - fromEnd].c_str(), nullptr, 16);
        return ((value >> (bit % wordBits)) & 1ULL) != 0;
    };
    if (!bitSet(KeyEnter))
        return false;
    int letters = 0;
    for (const auto &range : LetterRanges)
        for (int bit = range[0]; bit <= range[1]; bit++)
            if (bitSet(bit))
                letters++;
    return letters >= LettersNeeded;
}

//*******************************
// KeyboardPresence::anyLinuxKeyboard
//*******************************
bool anyLinuxKeyboard(const string &sysClassInput) {
    for (const string &name : DirEntry::listNames(sysClassInput)) {
        if (name.compare(0, 5, "event") != 0)
            continue;
        string bitmap;
        if (DirEntry::readFile(sysClassInput + "/" + name + "/device/capabilities/key", bitmap) &&
            capabilitiesLookLikeKeyboard(bitmap))
            return true;
    }
    return false;
}

//*******************************
// KeyboardPresence::detect
//*******************************
bool detect() {
#ifdef _WIN32
    UINT count = 0;
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0 || count == 0)
        return false;
    vector<RAWINPUTDEVICELIST> devices(count);
    if (GetRawInputDeviceList(devices.data(), &count, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1))
        return false;
    for (UINT i = 0; i < count; i++)
        if (devices[i].dwType == RIM_TYPEKEYBOARD)
            return true;
    return false;
#elif defined(__linux__)
    return anyLinuxKeyboard();
#else
    return false;
#endif
}

} // namespace KeyboardPresence
} // namespace ableem
