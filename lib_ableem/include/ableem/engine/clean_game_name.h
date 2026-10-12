// lib_ableem - engine: the game name without the bracketed tags of a ROM file name.
#pragma once

#include <string>

namespace ableem {

// "Space Invaders (USA)" -> "Space Invaders", "Sonic (Rev 1) [!]" -> "Sonic". Only the trailing (...) and [...]
// groups go (nested ones too, any number of them); a group in the middle ("Tom (& Jerry) Run") stays. Runs of
// spaces become one and the ends are trimmed. A name that would end up empty (all of it is tags) is returned
// as it came in.
std::string cleanGameName(const std::string &name);

} // namespace ableem
