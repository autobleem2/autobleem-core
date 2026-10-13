//
// ZipBuilder: a stored (uncompressed) .zip written from a few in-memory files, with the Unix mode in the
// central directory the way Info-ZIP records it (made-by host 3, mode in the upper 16 bits of the external
// attributes). miniz's writer records no mode, so the tests of the mode handling build their zips here.
//
#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <miniz.h>

namespace test_support {

class ZipBuilder {
public:
    // mode 0 = a zip from a DOS-style archiver: made-by host 0, no Unix attributes
    ZipBuilder &file(const std::string &name, const std::string &data, unsigned mode = 0644) {
        Item item;
        item.name = name;
        item.data = data;
        item.mode = mode;
        items_.push_back(item);
        return *this;
    }

    std::string bytes() const {
        std::string local, central;
        for (const Item &item : items_) {
            const uint32_t offset = static_cast<uint32_t>(local.size());
            const uint32_t crc = static_cast<uint32_t>(mz_crc32(
                MZ_CRC32_INIT, reinterpret_cast<const unsigned char *>(item.data.data()), item.data.size()));
            const uint32_t size = static_cast<uint32_t>(item.data.size());
            put32(local, 0x04034b50);
            put16(local, 10);
            put16(local, 0);
            put16(local, 0); // stored
            put16(local, 0);
            put16(local, 0x21); // 1980-01-01
            put32(local, crc);
            put32(local, size);
            put32(local, size);
            put16(local, static_cast<uint16_t>(item.name.size()));
            put16(local, 0);
            local += item.name;
            local += item.data;

            put32(central, 0x02014b50);
            put16(central, item.mode != 0 ? 0x031E : 0x000A); // made by: Unix, or DOS
            put16(central, 10);
            put16(central, 0);
            put16(central, 0);
            put16(central, 0);
            put16(central, 0x21);
            put32(central, crc);
            put32(central, size);
            put32(central, size);
            put16(central, static_cast<uint16_t>(item.name.size()));
            put16(central, 0);
            put16(central, 0);
            put16(central, 0);
            put16(central, 0);
            put32(central, item.mode != 0 ? ((0100000u | item.mode) << 16) : 0);
            put32(central, offset);
            central += item.name;
        }
        std::string out = local;
        const uint32_t centralOffset = static_cast<uint32_t>(out.size());
        out += central;
        put32(out, 0x06054b50);
        put16(out, 0);
        put16(out, 0);
        put16(out, static_cast<uint16_t>(items_.size()));
        put16(out, static_cast<uint16_t>(items_.size()));
        put32(out, static_cast<uint32_t>(central.size()));
        put32(out, centralOffset);
        put16(out, 0);
        return out;
    }

    bool write(const std::string &path) const {
        std::ofstream out(path, std::ios::binary);
        const std::string data = bytes();
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        return static_cast<bool>(out);
    }

private:
    struct Item {
        std::string name;
        std::string data;
        unsigned mode = 0;
    };
    static void put16(std::string &s, uint16_t v) {
        s.push_back(static_cast<char>(v & 0xff));
        s.push_back(static_cast<char>(v >> 8));
    }
    static void put32(std::string &s, uint32_t v) {
        for (int i = 0; i < 4; i++)
            s.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
    }
    std::vector<Item> items_;
};

} // namespace test_support
