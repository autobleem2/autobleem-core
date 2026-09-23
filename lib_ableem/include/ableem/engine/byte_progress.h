// lib_ableem - engine: how far a long read or write is, in bytes - for a progress bar over a copy, a zip, an
// unzip or a checksum of something the size of a partition. Called on the caller's thread, after every chunk
// (64 KB or so), so a caller that draws should throttle; `total` is 0 when the size is not known.
#pragma once

#include <cstdint>
#include <functional>

namespace ableem {

using ByteProgress = std::function<void(uint64_t done, uint64_t total)>;

} // namespace ableem
