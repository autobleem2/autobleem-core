#include "flac_encoder.h"

#include <algorithm>
#include <array>

using namespace std;

namespace {

const int MaxFixedOrder = 4;
const int MaxPartitionOrder = 8;
const int MaxRiceParameter = 14; // 15 is the escape code of the 4-bit method, never used here
const uint32_t SampleRateCode44100 = 9;

uint8_t crc8(const uint8_t *data, size_t size) {
    uint8_t crc = 0;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = static_cast<uint8_t>((crc & 0x80) ? (crc << 1) ^ 0x07 : crc << 1);
    }
    return crc;
}

const array<uint16_t, 256> &crc16Table() {
    static const array<uint16_t, 256> table = [] {
        array<uint16_t, 256> t;
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i << 8;
            for (int b = 0; b < 8; b++)
                c = (c & 0x8000) ? (c << 1) ^ 0x8005 : c << 1;
            t[i] = static_cast<uint16_t>(c);
        }
        return t;
    }();
    return table;
}

// CRC-16/BUYPASS (poly 0x8005, start 0), the one that closes a FLAC frame
uint16_t crc16(const uint8_t *data, size_t size) {
    const array<uint16_t, 256> &table = crc16Table();
    uint16_t crc = 0;
    for (size_t i = 0; i < size; i++)
        crc = static_cast<uint16_t>((crc << 8) ^ table[(crc >> 8) ^ data[i]]);
    return crc;
}

// most significant bit first
class Bits {
public:
    explicit Bits(vector<uint8_t> &out) : out_(out) {}
    void put(uint32_t value, int bits) {
        if (bits == 0)
            return;
        acc_ = (acc_ << bits) | (value & ((bits == 32) ? 0xffffffffu : ((1u << bits) - 1)));
        used_ += bits;
        while (used_ >= 8) {
            used_ -= 8;
            out_.push_back(static_cast<uint8_t>(acc_ >> used_));
        }
    }
    // 'zeros' zero bits and a one
    void unary(uint32_t zeros) {
        while (zeros >= 24) {
            put(0, 24);
            zeros -= 24;
        }
        put(1, static_cast<int>(zeros) + 1);
    }
    void align() {
        if (used_ != 0)
            put(0, 8 - used_);
    }

private:
    vector<uint8_t> &out_;
    uint64_t acc_ = 0;
    int used_ = 0;
};

enum SubframeType { Constant, Verbatim, Fixed };

struct Plan {
    SubframeType type = Verbatim;
    int order = 0;
    int partitionOrder = 0;
    array<uint8_t, 1 << MaxPartitionOrder> rice;
    uint64_t bits = 0;
};

inline uint32_t fold(int32_t r) {
    return (static_cast<uint32_t>(r) << 1) ^ static_cast<uint32_t>(r >> 31);
}

// the Rice parameter that makes 'count' residuals with the folded sum 'sum' shortest, and those bits
uint64_t bestRice(uint64_t sum, uint32_t count, int &k) {
    uint64_t best = ~0ull;
    for (int i = 0; i <= MaxRiceParameter; i++) {
        const uint64_t bits = static_cast<uint64_t>(count) * (i + 1) + (sum >> i);
        if (bits < best) {
            best = bits;
            k = i;
        }
    }
    return best;
}

// The fixed predictor of order o is the o-th difference of the samples, so each order's residual follows from
// the previous one's.
class Channel {
public:
    void plan(const int32_t *x, uint32_t n, int bps, Plan &best) {
        bool constant = true;
        for (uint32_t i = 1; i < n && constant; i++)
            constant = x[i] == x[0];
        if (constant) {
            best.type = Constant;
            best.bits = 8 + static_cast<uint64_t>(bps);
            return;
        }
        best.type = Verbatim;
        best.bits = 8 + static_cast<uint64_t>(n) * bps;

        res_[0].assign(x, x + n);
        for (int o = 1; o <= MaxFixedOrder && static_cast<uint32_t>(o) < n; o++) {
            res_[o].resize(n);
            for (uint32_t i = o; i < n; i++)
                res_[o][i] = res_[o - 1][i] - res_[o - 1][i - 1];
        }
        for (int o = 0; o <= MaxFixedOrder && static_cast<uint32_t>(o) < n; o++)
            tryOrder(n, bps, o, best);
    }
    const int32_t *residual(int order) const { return res_[order].data(); }

private:
    void tryOrder(uint32_t n, int bps, int o, Plan &best) {
        int maxP = 0;
        while (maxP < MaxPartitionOrder && n % (2u << maxP) == 0 && (n >> (maxP + 1)) > static_cast<uint32_t>(o))
            maxP++;
        const uint32_t size = n >> maxP;
        const uint32_t parts = 1u << maxP;
        array<uint64_t, 1 << MaxPartitionOrder> sums;
        array<uint32_t, 1 << MaxPartitionOrder> counts;
        for (uint32_t j = 0; j < parts; j++) {
            const uint32_t start = j == 0 ? o : j * size;
            uint64_t sum = 0;
            for (uint32_t i = start; i < (j + 1) * size; i++)
                sum += fold(res_[o][i]);
            sums[j] = sum;
            counts[j] = (j + 1) * size - start;
        }
        const uint64_t fixedBits = 8 + static_cast<uint64_t>(o) * bps + 2 + 4;
        for (int p = maxP; p >= 0; p--) {
            const uint32_t count = 1u << p;
            uint64_t total = fixedBits;
            array<uint8_t, 1 << MaxPartitionOrder> rice;
            for (uint32_t j = 0; j < count; j++) {
                int k = 0;
                total += 4 + bestRice(sums[j], counts[j], k);
                rice[j] = static_cast<uint8_t>(k);
            }
            if (total < best.bits) {
                best.type = Fixed;
                best.order = o;
                best.partitionOrder = p;
                best.rice = rice;
                best.bits = total;
            }
            for (uint32_t j = 0; j < count / 2; j++) { // merge the neighbours: the next coarser level
                sums[j] = sums[2 * j] + sums[2 * j + 1];
                counts[j] = counts[2 * j] + counts[2 * j + 1];
            }
        }
    }

    vector<int32_t> res_[MaxFixedOrder + 1];
};

void writeSubframe(Bits &bits, const Plan &plan, Channel &channel, const int32_t *x, uint32_t n, int bps) {
    if (plan.type == Constant) {
        bits.put(0, 8);
        bits.put(static_cast<uint32_t>(x[0]), bps);
    } else if (plan.type == Verbatim) {
        bits.put(1 << 1, 8);
        for (uint32_t i = 0; i < n; i++)
            bits.put(static_cast<uint32_t>(x[i]), bps);
    } else {
        const int o = plan.order;
        const int32_t *res = channel.residual(o);
        bits.put(static_cast<uint32_t>((0x08 | o) << 1), 8);
        for (int i = 0; i < o; i++)
            bits.put(static_cast<uint32_t>(x[i]), bps);
        bits.put(0, 2); // 4-bit Rice parameters
        bits.put(static_cast<uint32_t>(plan.partitionOrder), 4);
        const uint32_t parts = 1u << plan.partitionOrder;
        const uint32_t size = n >> plan.partitionOrder;
        for (uint32_t j = 0; j < parts; j++) {
            const int k = plan.rice[j];
            bits.put(static_cast<uint32_t>(k), 4);
            for (uint32_t i = j == 0 ? o : j * size; i < (j + 1) * size; i++) {
                const uint32_t u = fold(res[i]);
                bits.unary(u >> k);
                bits.put(u, k);
            }
        }
    }
}

// the frame number, UTF-8 coded as FLAC does for a fixed block size
void utf8Number(vector<uint8_t> &out, uint32_t v) {
    if (v < 0x80) {
        out.push_back(static_cast<uint8_t>(v));
        return;
    }
    int extra = 1;
    while (extra < 5 && v >= (1u << (5 * extra + 6)))
        extra++;
    out.push_back(static_cast<uint8_t>((0xff00u >> (extra + 1)) | (v >> (6 * extra))));
    for (int i = extra - 1; i >= 0; i--)
        out.push_back(static_cast<uint8_t>(0x80 | ((v >> (6 * i)) & 0x3f)));
}

} // namespace

vector<uint8_t> FlacEncoder::encode(const int16_t *interleaved, size_t samples, uint32_t blockSize) {
    vector<uint8_t> out;
    if (blockSize == 0 || blockSize > 65535)
        return out;
    Channel channels[4]; // left, right, mid, side
    vector<int32_t> sig[4];
    vector<uint8_t> frame;
    uint32_t frameNumber = 0;
    for (size_t at = 0; at < samples; at += blockSize, frameNumber++) {
        const uint32_t n = static_cast<uint32_t>(min<size_t>(blockSize, samples - at));
        for (int c = 0; c < 4; c++)
            sig[c].resize(n);
        for (uint32_t i = 0; i < n; i++) {
            const int32_t l = interleaved[(at + i) * 2], r = interleaved[(at + i) * 2 + 1];
            sig[0][i] = l;
            sig[1][i] = r;
            sig[2][i] = (l + r) >> 1;
            sig[3][i] = l - r;
        }
        Plan plans[4];
        const int bps[4] = {16, 16, 16, 17};
        for (int c = 0; c < 4; c++)
            channels[c].plan(sig[c].data(), n, bps[c], plans[c]);

        // channel assignment code, and which signals it carries, in order
        struct Mode {
            uint32_t code;
            int first, second;
        };
        const Mode modes[4] = {{1, 0, 1}, {8, 0, 3}, {9, 3, 1}, {10, 2, 3}};
        int best = 0;
        for (int m = 1; m < 4; m++)
            if (plans[modes[m].first].bits + plans[modes[m].second].bits <
                plans[modes[best].first].bits + plans[modes[best].second].bits)
                best = m;
        const Mode &mode = modes[best];

        frame.clear();
        frame.push_back(0xff);
        frame.push_back(0xf8); // fixed block size
        frame.push_back(static_cast<uint8_t>((7u << 4) | SampleRateCode44100)); // block size: 16 bits at the end
        frame.push_back(static_cast<uint8_t>((mode.code << 4) | (4u << 1)));    // 16 bits a sample
        utf8Number(frame, frameNumber);
        frame.push_back(static_cast<uint8_t>((n - 1) >> 8));
        frame.push_back(static_cast<uint8_t>(n - 1));
        frame.push_back(crc8(frame.data(), frame.size()));
        {
            Bits bits(frame);
            writeSubframe(bits, plans[mode.first], channels[mode.first], sig[mode.first].data(), n, bps[mode.first]);
            writeSubframe(bits, plans[mode.second], channels[mode.second], sig[mode.second].data(), n,
                          bps[mode.second]);
            bits.align();
        }
        const uint16_t crc = crc16(frame.data(), frame.size());
        frame.push_back(static_cast<uint8_t>(crc >> 8));
        frame.push_back(static_cast<uint8_t>(crc));
        out.insert(out.end(), frame.begin(), frame.end());
    }
    return out;
}
