// lib_ableem - engine: PARAM.SFO, the key/value table Sony's PSP-era formats carry (a PBP's first section, a
// PSN package's own copy) - TITLE, DISC_ID, CATEGORY, ... Strings come back without their padding, integers
// as decimal text.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace ableem {

class ParamSfo {
public:
    // false when the bytes are not an SFO ("\0PSF") or a table runs past the end - nothing is filled then
    static bool parse(const uint8_t *data, size_t size, std::map<std::string, std::string> &values);
};

} // namespace ableem
