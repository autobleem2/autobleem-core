// lib_ableem - engine: reading a .xz file as a stream, for the PC stick's
// flasher (the image is published as autobleem-<v>-pcusb-i386.img.xz, 4 GB
// unpacked - never held in memory or unpacked to a temporary file). The one
// place that touches the vendored LZMA SDK xz decoder (third_party/lzma-7z);
// the app never includes it. Without CHD support (ABLEEM_NO_CHD - the codecs
// are libchdr's lzma) every call answers false.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace ableem {

//******************
// XzFile
//******************
// decode() streams the unpacked bytes into `sink` a buffer at a time, every
// block's check (CRC32, CRC64 or SHA-256, whichever the file names) verified as
// it goes; concatenated streams are one file, as xz itself reads them.
// unpackedSize() reads the size from the streams' indexes at the end of the
// file, without decoding anything - what a flasher compares with the disk
// before it starts.
class XzFile {
public:
    // false stops the decode (decode() then fails with `error` = "stopped")
    using Sink = std::function<bool(const uint8_t *data, size_t size)>;
    // compressed bytes read so far, of the file's size
    using Progress = std::function<void(uint64_t read, uint64_t fileSize)>;

    static bool decode(const std::string &path, const Sink &sink, std::string &error,
                       const Progress &progress = nullptr);

    // the sum of every stream's index; false when the file is not a well-formed
    // .xz
    static bool unpackedSize(const std::string &path, uint64_t &size);
};

} // namespace ableem
