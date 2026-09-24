//
// ParamSfo - see the header.
//
#include <ableem/engine/param_sfo.h>

using namespace std;

namespace ableem {

namespace {

uint32_t le32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}

uint16_t le16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0] | p[1] << 8);
}

} // namespace

//*******************************
// ParamSfo::parse
//*******************************
// header: "\0PSF", version, key table offset, data table offset, entry count; then 16 bytes an entry: key
// offset (u16), format (u16: 0x0204 utf-8 string, 0x0004 utf-8 special, 0x0404 u32), length, max length, data
// offset - every number little-endian
bool ParamSfo::parse(const uint8_t *data, size_t size, map<string, string> &values) {
    if (size < 20 || data[0] != 0 || data[1] != 'P' || data[2] != 'S' || data[3] != 'F')
        return false;
    const uint32_t keys = le32(data + 8);
    const uint32_t datas = le32(data + 12);
    const uint32_t count = le32(data + 16);
    if (count > 1024 || 20 + static_cast<uint64_t>(count) * 16 > size || keys >= size || datas > size)
        return false;
    map<string, string> out;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = data + 20 + i * 16;
        const uint64_t keyAt = static_cast<uint64_t>(keys) + le16(e);
        const uint16_t format = le16(e + 2);
        const uint32_t length = le32(e + 4);
        const uint64_t valueAt = static_cast<uint64_t>(datas) + le32(e + 12);
        if (keyAt >= size || valueAt + length > size)
            return false;
        string key;
        for (uint64_t k = keyAt; k < size && data[k] != 0; k++)
            key += static_cast<char>(data[k]);
        string value;
        if (format == 0x0404 && length >= 4) {
            value = to_string(le32(data + valueAt));
        } else {
            value.assign(reinterpret_cast<const char *>(data + valueAt), length);
            const size_t nul = value.find('\0');
            if (nul != string::npos)
                value.resize(nul);
        }
        out[key] = value;
    }
    values.swap(out);
    return true;
}

} // namespace ableem
