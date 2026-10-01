//
// ThemeColorDeriver - see the header.
//
#include "theme_color_deriver.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <unordered_map>

using namespace std;
using namespace themecolor;

namespace {

const double Pi = 3.14159265358979323846;

//******************
// sRGB, CIELAB
//******************
double lin(int c8) {
    const double c = c8 / 255.0;
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

double unlin(double v) {
    v = max(0.0, min(1.0, v));
    return 255.0 * (v <= 0.0031308 ? 12.92 * v : 1.055 * pow(v, 1.0 / 2.4) - 0.055);
}

// Python's round() (half to even) on the default rounding mode
int roundEven(double v) {
    return static_cast<int>(nearbyint(v));
}

double labF(double t) {
    return t > 216.0 / 24389.0 ? pow(t, 1.0 / 3.0) : (24389.0 / 27.0 * t + 16.0) / 116.0;
}

double labFinv(double t) {
    const double cube = pow(t, 3.0);
    return cube > 216.0 / 24389.0 ? cube : (116.0 * t - 16.0) / (24389.0 / 27.0);
}

ThemeRgb fromHexInt(int hex) {
    return ThemeRgb((hex >> 16) & 0xff, (hex >> 8) & 0xff, hex & 0xff);
}

string fmt(const char *format, double a) {
    char buf[160];
    snprintf(buf, sizeof(buf), format, a);
    return buf;
}

//******************
// Pillow's resize (Lanczos, 8-bit fixed point)
//******************
const int PrecisionBits = 32 - 8 - 2;

double sincFilter(double x) {
    if (x == 0.0)
        return 1.0;
    x = x * Pi;
    return sin(x) / x;
}

double lanczosFilter(double x) {
    if (-3.0 <= x && x < 3.0)
        return sincFilter(x) * sincFilter(x / 3);
    return 0.0;
}

struct Coeffs {
    int ksize = 0;
    vector<int> bounds; // min, count per output
    vector<int> kk;     // ksize per output, fixed point
};

Coeffs precomputeCoeffs(int inSize, int outSize) {
    Coeffs out;
    double scale = static_cast<double>(inSize) / outSize;
    double filterscale = scale < 1.0 ? 1.0 : scale;
    const double support = 3.0 * filterscale;
    out.ksize = static_cast<int>(ceil(support)) * 2 + 1;
    out.bounds.assign(outSize * 2, 0);
    out.kk.assign(static_cast<size_t>(outSize) * out.ksize, 0);
    vector<double> pre(static_cast<size_t>(outSize) * out.ksize, 0.0);
    const double invScale = 1.0 / filterscale;
    for (int xx = 0; xx < outSize; xx++) {
        const double center = (xx + 0.5) * scale;
        double ww = 0.0;
        int xmin = static_cast<int>(center - support + 0.5);
        if (xmin < 0)
            xmin = 0;
        int xmax = static_cast<int>(center + support + 0.5);
        if (xmax > inSize)
            xmax = inSize;
        xmax -= xmin;
        double *k = &pre[static_cast<size_t>(xx) * out.ksize];
        for (int x = 0; x < xmax; x++) {
            const double w = lanczosFilter((x + xmin - center + 0.5) * invScale);
            k[x] = w;
            ww += w;
        }
        if (ww != 0.0)
            for (int x = 0; x < xmax; x++)
                k[x] /= ww;
        out.bounds[xx * 2] = xmin;
        out.bounds[xx * 2 + 1] = xmax;
    }
    for (size_t i = 0; i < pre.size(); i++) {
        if (pre[i] < 0)
            out.kk[i] = static_cast<int>(-0.5 + pre[i] * (1 << PrecisionBits));
        else
            out.kk[i] = static_cast<int>(0.5 + pre[i] * (1 << PrecisionBits));
    }
    return out;
}

unsigned char clip8(long long v) {
    const long long s = v >> PrecisionBits;
    return static_cast<unsigned char>(s < 0 ? 0 : (s > 255 ? 255 : s));
}

// a packed 3-channel image
struct Image3 {
    int w = 0;
    int h = 0;
    vector<unsigned char> px;
};

Image3 resampleHorizontal(const Image3 &in, int outW) {
    const Coeffs c = precomputeCoeffs(in.w, outW);
    Image3 out;
    out.w = outW;
    out.h = in.h;
    out.px.assign(static_cast<size_t>(outW) * in.h * 3, 0);
    for (int y = 0; y < in.h; y++) {
        for (int xx = 0; xx < outW; xx++) {
            const int xmin = c.bounds[xx * 2];
            const int xmax = c.bounds[xx * 2 + 1];
            const int *k = &c.kk[static_cast<size_t>(xx) * c.ksize];
            for (int ch = 0; ch < 3; ch++) {
                long long ss = 1 << (PrecisionBits - 1);
                for (int x = 0; x < xmax; x++)
                    ss += static_cast<long long>(in.px[(static_cast<size_t>(y) * in.w + x + xmin) * 3 + ch]) * k[x];
                out.px[(static_cast<size_t>(y) * outW + xx) * 3 + ch] = clip8(ss);
            }
        }
    }
    return out;
}

Image3 resampleVertical(const Image3 &in, int outH) {
    const Coeffs c = precomputeCoeffs(in.h, outH);
    Image3 out;
    out.w = in.w;
    out.h = outH;
    out.px.assign(static_cast<size_t>(in.w) * outH * 3, 0);
    for (int yy = 0; yy < outH; yy++) {
        const int ymin = c.bounds[yy * 2];
        const int ymax = c.bounds[yy * 2 + 1];
        const int *k = &c.kk[static_cast<size_t>(yy) * c.ksize];
        for (int x = 0; x < in.w; x++) {
            for (int ch = 0; ch < 3; ch++) {
                long long ss = 1 << (PrecisionBits - 1);
                for (int y = 0; y < ymax; y++)
                    ss += static_cast<long long>(in.px[(static_cast<size_t>(y + ymin) * in.w + x) * 3 + ch]) * k[y];
                out.px[(static_cast<size_t>(yy) * in.w + x) * 3 + ch] = clip8(ss);
            }
        }
    }
    return out;
}

// Pillow's alpha_composite of a pixel over opaque black
unsigned char overBlack(int c, int a) {
    if (a == 0)
        return 0;
    const int coef1 = a * 255 * 255 * 128 / (a * 255 + 255 * (255 - a));
    int tmp = c * coef1 + (0x80 << 7);
    tmp = ((tmp >> 8) + tmp) >> 8;
    return static_cast<unsigned char>(tmp >> 7);
}

//******************
// Pillow's median cut (quantize, MEDIANCUT, 8-bit RGB)
//******************
// One distinct colour of the picture. Pillow's hash table keys on a hash of the channels, not on the channels,
// so two colours with the same hash are one entry (the first seen); it is ported the same way.
struct Colour {
    int r = 0;
    int g = 0;
    int b = 0;
    int count = 0;
    int value(int axis) const { return axis == 0 ? r : (axis == 1 ? g : b); }
};

uint32_t pixelHash(int r, int g, int b) {
    return static_cast<uint32_t>(r) * 463u ^ (static_cast<uint32_t>(g) << 8) * 10069u ^
           (static_cast<uint32_t>(b) << 16) * 64997u;
}

struct Box {
    vector<int> members; // indexes into the colours
    long long pixelCount = 0;
    int volume = -1;
    int left = -1;
    int right = -1;
};

int boxVolume(Box &box, const vector<Colour> &colours) {
    if (box.volume >= 0)
        return box.volume;
    if (box.members.empty()) {
        box.volume = 0;
        return 0;
    }
    int lo[3] = {255, 255, 255};
    int hi[3] = {0, 0, 0};
    for (int m : box.members)
        for (int a = 0; a < 3; a++) {
            lo[a] = min(lo[a], colours[m].value(a));
            hi[a] = max(hi[a], colours[m].value(a));
        }
    box.volume = (hi[0] - lo[0] + 1) * (hi[1] - lo[1] + 1) * (hi[2] - lo[2] + 1);
    return box.volume;
}

// Pillow's heap: 1-based, a max-heap on the boxes' pixel counts (the same swaps, so equal counts come out in the
// same order)
class BoxHeap {
public:
    BoxHeap(const vector<Box> &boxes) : boxes_(boxes), heap_(1, -1) {}

    void add(int box) {
        heap_.push_back(box);
        size_t k = heap_.size() - 1;
        while (k != 1) {
            if (cmp(box, heap_[k / 2]) <= 0)
                break;
            heap_[k] = heap_[k / 2];
            k >>= 1;
        }
        heap_[k] = box;
    }

    bool remove(int &out) {
        size_t count = heap_.size() - 1;
        if (count == 0)
            return false;
        out = heap_[1];
        const int v = heap_[count];
        count--;
        size_t k = 1;
        size_t l;
        for (; k * 2 <= count; k = l) {
            l = k * 2;
            if (l < count && cmp(heap_[l], heap_[l + 1]) < 0)
                l++;
            if (cmp(v, heap_[l]) > 0)
                break;
            heap_[k] = heap_[l];
        }
        heap_[k] = v;
        heap_.resize(count + 1);
        return true;
    }

private:
    int cmp(int a, int b) const {
        return static_cast<int>(boxes_[a].pixelCount) - static_cast<int>(boxes_[b].pixelCount);
    }

    const vector<Box> &boxes_;
    vector<int> heap_;
};

// splits a box along its weighted-longest axis at the pixel-count median (the higher half is the left one)
void splitBox(vector<Box> &boxes, int index, const vector<Colour> &colours) {
    int lo[3] = {255, 255, 255};
    int hi[3] = {0, 0, 0};
    for (int m : boxes[index].members)
        for (int a = 0; a < 3; a++) {
            lo[a] = min(lo[a], colours[m].value(a));
            hi[a] = max(hi[a], colours[m].value(a));
        }
    const int f[3] = {(hi[0] - lo[0]) * 77, (hi[1] - lo[1]) * 150, (hi[2] - lo[2]) * 29};
    int best = f[0];
    int axis = 0;
    for (int i = 1; i < 3; i++)
        if (best < f[i]) {
            best = f[i];
            axis = i;
        }

    vector<int> sorted = boxes[index].members;
    // highest first; the order inside one value does not matter (a value is never cut in two)
    stable_sort(sorted.begin(), sorted.end(),
                [&](int a, int b) { return colours[a].value(axis) > colours[b].value(axis); });
    const long long pixelCount = boxes[index].pixelCount;
    const size_t n = sorted.size();
    size_t i = 0;
    long long left = 0;
    long long n0 = 0;
    long long n1 = 0;
    while (i < n) {
        left += colours[sorted[i]].count;
        n0 += colours[sorted[i]].count;
        i++;
        if (left * 2 > pixelCount)
            break;
    }
    if (i < n) {
        const int splitValue = colours[sorted[i - 1]].value(axis);
        while (i < n && colours[sorted[i]].value(axis) == splitValue) {
            n0 += colours[sorted[i]].count;
            i++;
        }
    }
    size_t cut = i;
    for (size_t j = cut; j < n; j++)
        n1 += colours[sorted[j]].count;
    if (cut == n) {
        // everything went left: the lowest value goes right
        const int lowest = colours[sorted[n - 1]].value(axis);
        while (cut > 0 && colours[sorted[cut - 1]].value(axis) == lowest) {
            cut--;
            n0 -= colours[sorted[cut]].count;
            n1 += colours[sorted[cut]].count;
        }
    }

    Box l;
    Box r;
    l.members.assign(sorted.begin(), sorted.begin() + cut);
    r.members.assign(sorted.begin() + cut, sorted.end());
    l.pixelCount = n0;
    r.pixelCount = n1;
    boxes.push_back(l);
    boxes.push_back(r);
    boxes[index].left = static_cast<int>(boxes.size()) - 2;
    boxes[index].right = static_cast<int>(boxes.size()) - 1;
    boxes[index].members.clear();
}

void collectLeaves(const vector<Box> &boxes, int index, vector<int> &leaves) {
    const Box &b = boxes[index];
    if (b.left >= 0 && b.right >= 0) {
        collectLeaves(boxes, b.left, leaves);
        collectLeaves(boxes, b.right, leaves);
        return;
    }
    if (!b.members.empty())
        leaves.push_back(index);
}

int distSq(const ThemeRgb &a, int r, int g, int b) {
    return (a.r - r) * (a.r - r) + (a.g - g) * (a.g - g) + (a.b - b) * (a.b - b);
}

// the palette of a packed RGB picture: clusters (colour, pixel count after the remap), in palette order
vector<pair<ThemeRgb, int>> medianCut(const vector<unsigned char> &rgb, int nPixels, int nColors) {
    vector<Colour> colours;
    unordered_map<uint32_t, int> byHash;
    vector<int> classOf(nPixels);
    for (int i = 0; i < nPixels; i++) {
        const int r = rgb[i * 3];
        const int g = rgb[i * 3 + 1];
        const int b = rgb[i * 3 + 2];
        const uint32_t key = pixelHash(r, g, b);
        auto it = byHash.find(key);
        if (it == byHash.end()) {
            Colour c;
            c.r = r;
            c.g = g;
            c.b = b;
            c.count = 1;
            colours.push_back(c);
            it = byHash.emplace(key, static_cast<int>(colours.size()) - 1).first;
        } else {
            colours[it->second].count++;
        }
        classOf[i] = it->second;
    }

    vector<Box> boxes;
    boxes.reserve(static_cast<size_t>(nColors) * 2 + 2);
    Box root;
    root.pixelCount = nPixels;
    for (size_t i = 0; i < colours.size(); i++)
        root.members.push_back(static_cast<int>(i));
    boxes.push_back(root);
    BoxHeap heap(boxes);
    heap.add(0);
    for (int step = nColors - 1; step > 0; step--) {
        int node = -1;
        bool found = false;
        while (heap.remove(node)) {
            if (boxVolume(boxes[node], colours) != 1) {
                found = true;
                break;
            }
        }
        if (!found)
            break;
        splitBox(boxes, node, colours);
        heap.add(boxes[node].left);
        heap.add(boxes[node].right);
    }

    vector<int> leaves;
    collectLeaves(boxes, 0, leaves);
    vector<int> boxOfClass(colours.size(), 0);
    for (size_t li = 0; li < leaves.size(); li++)
        for (int m : boxes[leaves[li]].members)
            boxOfClass[m] = static_cast<int>(li);

    const int n = static_cast<int>(leaves.size());
    vector<long long> sum[3];
    for (auto &s : sum)
        s.assign(n, 0);
    vector<int> count(n, 0);
    for (int i = 0; i < nPixels; i++) {
        const int e = boxOfClass[classOf[i]];
        sum[0][e] += rgb[i * 3];
        sum[1][e] += rgb[i * 3 + 1];
        sum[2][e] += rgb[i * 3 + 2];
        count[e]++;
    }
    vector<ThemeRgb> pal(n);
    for (int e = 0; e < n; e++)
        pal[e] = ThemeRgb(static_cast<int>(.5 + static_cast<double>(sum[0][e]) / count[e]),
                          static_cast<int>(.5 + static_cast<double>(sum[1][e]) / count[e]),
                          static_cast<int>(.5 + static_cast<double>(sum[2][e]) / count[e]));

    // every pixel goes to the nearest palette entry, looking from its box's entry outward (Pillow's search)
    vector<vector<int>> order(n);
    vector<vector<int>> between(n, vector<int>(n, 0));
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++)
            between[i][j] = distSq(pal[i], pal[j].r, pal[j].g, pal[j].b);
        order[i].resize(n);
        for (int j = 0; j < n; j++)
            order[i][j] = j;
        stable_sort(order[i].begin(), order[i].end(), [&](int a, int b) { return between[i][a] < between[i][b]; });
    }
    vector<int> used(n, 0);
    for (int i = 0; i < nPixels; i++) {
        const int r = rgb[i * 3];
        const int g = rgb[i * 3 + 1];
        const int b = rgb[i * 3 + 2];
        const int own = boxOfClass[classOf[i]];
        int bestMatch = own;
        int bestDist = distSq(pal[own], r, g, b);
        const int limit = bestDist << 2;
        for (int j = 0; j < n; j++) {
            const int idx = order[own][j];
            if (between[own][idx] > limit)
                break;
            const int d = distSq(pal[idx], r, g, b);
            if (d < bestDist) {
                bestDist = d;
                bestMatch = idx;
            }
        }
        used[bestMatch]++;
    }
    vector<pair<ThemeRgb, int>> out;
    for (int e = 0; e < n; e++)
        if (used[e] > 0)
            out.push_back(make_pair(pal[e], used[e]));
    return out;
}

} // namespace

//******************
// ThemeRgb
//******************
string ThemeRgb::hex() const {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02x%02x%02x", r & 0xff, g & 0xff, b & 0xff);
    return buf;
}

//******************
// colour maths
//******************
double ThemeColorDeriver::luminance(const ThemeRgb &c) {
    return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
}

double ThemeColorDeriver::contrast(const ThemeRgb &a, const ThemeRgb &b) {
    double la = luminance(a);
    double lb = luminance(b);
    if (la < lb)
        swap(la, lb);
    return (la + 0.05) / (lb + 0.05);
}

void ThemeColorDeriver::toLab(const ThemeRgb &c, double &L, double &a, double &b) {
    const double r = lin(c.r);
    const double g = lin(c.g);
    const double bl = lin(c.b);
    const double x = (0.4124564 * r + 0.3575761 * g + 0.1804375 * bl) / 0.95047;
    const double y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * bl;
    const double z = (0.0193339 * r + 0.1191920 * g + 0.9503041 * bl) / 1.08883;
    const double fx = labF(x);
    const double fy = labF(y);
    const double fz = labF(z);
    L = 116 * fy - 16;
    a = 500 * (fx - fy);
    b = 200 * (fy - fz);
}

bool ThemeColorDeriver::fromLab(double L, double a, double b, bool clip, ThemeRgb &out) {
    const double fy = (L + 16) / 116;
    const double fx = fy + a / 500;
    const double fz = fy - b / 200;
    const double x = labFinv(fx) * 0.95047;
    const double y = labFinv(fy);
    const double z = labFinv(fz) * 1.08883;
    const double r = 3.2404542 * x - 1.5371385 * y - 0.4985314 * z;
    const double g = -0.9692660 * x + 1.8760108 * y + 0.0415560 * z;
    const double bl = 0.0556434 * x - 0.2040259 * y + 1.0572252 * z;
    if (!clip && !(r >= -0.001 && r <= 1.001 && g >= -0.001 && g <= 1.001 && bl >= -0.001 && bl <= 1.001))
        return false;
    out = ThemeRgb(roundEven(unlin(r)), roundEven(unlin(g)), roundEven(unlin(bl)));
    return true;
}

double ThemeColorDeriver::chroma(const ThemeRgb &c) {
    double L, a, b;
    toLab(c, L, a, b);
    return hypot(a, b);
}

ThemeRgb ThemeColorDeriver::withLightness(const ThemeRgb &c, double L, double chromaCap) {
    double l0, a, b;
    toLab(c, l0, a, b);
    const double ch = hypot(a, b);
    if (chromaCap >= 0.0 && ch > chromaCap) {
        a = a * chromaCap / ch;
        b = b * chromaCap / ch;
    }
    double k = 1.0;
    ThemeRgb out;
    while (true) {
        if (fromLab(L, a * k, b * k, false, out))
            return out;
        k -= 0.05;
        if (k <= 0) {
            fromLab(L, 0, 0, true, out);
            return out;
        }
    }
}

ThemeRgb ThemeColorDeriver::mix(const ThemeRgb &a, const ThemeRgb &b, double t) {
    return ThemeRgb(roundEven(a.r + (b.r - a.r) * t), roundEven(a.g + (b.g - a.g) * t),
                    roundEven(a.b + (b.b - a.b) * t));
}

bool ThemeColorDeriver::liftUntil(const ThemeRgb &c, const ThemeRgb &sheet, double minimum, ThemeRgb &out) {
    double L, a, b;
    toLab(c, L, a, b);
    ThemeRgb cur = c;
    while (contrast(cur, sheet) < minimum) {
        L += LiftStep;
        if (L > LiftMaxL)
            return false;
        cur = withLightness(c, L);
    }
    out = cur;
    return true;
}

//******************
// the picture
//******************
vector<unsigned char> ThemeColorDeriver::downscale(const unsigned char *pixels, int width, int height, int channels,
                                                   int outW, int outH) {
    Image3 img;
    img.w = width;
    img.h = height;
    img.px.resize(static_cast<size_t>(width) * height * 3);
    for (size_t i = 0; i < static_cast<size_t>(width) * height; i++) {
        if (channels == 4) {
            const int a = pixels[i * 4 + 3];
            for (int c = 0; c < 3; c++)
                img.px[i * 3 + c] = overBlack(pixels[i * 4 + c], a);
        } else {
            for (int c = 0; c < 3; c++)
                img.px[i * 3 + c] = pixels[i * 3 + c];
        }
    }
    const bool horizontalFirst = !((height - outH) > 0 && (height - outH) > (width - outW) * 2);
    const bool needH = outW != width;
    const bool needV = outH != height;
    if (horizontalFirst) {
        if (needH)
            img = resampleHorizontal(img, outW);
        if (needV)
            img = resampleVertical(img, outH);
    } else {
        if (needV)
            img = resampleVertical(img, outH);
        if (needH)
            img = resampleHorizontal(img, outW);
    }
    return img.px;
}

vector<ThemeColorDeriver::PaletteEntry> ThemeColorDeriver::palette(const unsigned char *pixels, int width, int height,
                                                                   int channels) {
    vector<PaletteEntry> out;
    if (!pixels || width <= 0 || height <= 0)
        return out;
    const vector<unsigned char> small =
        downscale(pixels, width, height, channels == 4 ? 4 : 3, PaletteWidth, PaletteHeight);
    const int total = PaletteWidth * PaletteHeight;
    const vector<pair<ThemeRgb, int>> clusters = medianCut(small, total, PaletteColors);
    for (const auto &c : clusters) {
        PaletteEntry e;
        e.color = c.first;
        e.share = static_cast<double>(c.second) / total;
        out.push_back(e);
    }
    stable_sort(out.begin(), out.end(), [](const PaletteEntry &a, const PaletteEntry &b) { return a.share > b.share; });
    return out;
}

//******************
// the derivation
//******************
ThemeColorRoles ThemeColorDeriver::derive(const ThemeColorInput &in) {
    const bool haveBackground = in.pixels && in.width > 0 && in.height > 0;
    vector<PaletteEntry> pal;
    if (haveBackground)
        pal = palette(in.pixels, in.width, in.height, in.channels);
    return fromPalette(pal, haveBackground, in);
}

ThemeColorRoles ThemeColorDeriver::fromPalette(const vector<PaletteEntry> &pal, bool haveBackground,
                                               const ThemeColorInput &in) {
    ThemeColorRoles roles;
    if (!haveBackground)
        roles.fallbacks.push_back("no readable background image: palette unavailable");

    // --- sheet
    ThemeRgb base;
    string sheetSource;
    if (in.hasMainBg && max(in.mainBg.r, max(in.mainBg.g, in.mainBg.b)) > StockBlackMax) {
        base = in.mainBg;
        sheetSource = "Main_bg";
    } else {
        vector<PaletteEntry> dominant;
        for (const auto &e : pal)
            if (e.share >= DominantShare)
                dominant.push_back(e);
        if (dominant.empty())
            dominant = pal;
        if (!dominant.empty()) {
            const PaletteEntry *darkest = &dominant[0];
            double darkestL = 1e9;
            for (const auto &e : dominant) {
                double L, a, b;
                toLab(e.color, L, a, b);
                if (L < darkestL) {
                    darkestL = L;
                    darkest = &e;
                }
            }
            base = darkest->color;
            sheetSource = "background: darkest dominant cluster";
        } else {
            base = ThemeRgb(16, 16, 16);
            sheetSource = "fixed neutral (no palette)";
            roles.fallbacks.push_back("sheet: fixed #101010");
        }
    }
    double baseL, baseA, baseB;
    toLab(base, baseL, baseA, baseB);
    const double sheetL = min(max(baseL, SheetLMin), SheetLMax);
    const ThemeRgb sheet = withLightness(base, sheetL, SheetChromaMax);
    roles.notes.push_back("sheet <- " + sheetSource);

    // --- accent
    bool haveAccent = false;
    ThemeRgb accent;
    string accentSource;
    if (in.hasSecondary && chroma(in.secondary) >= OwnChromaMin) {
        accent = in.secondary;
        haveAccent = true;
        accentSource = "colors.ini sec";
    } else if (in.hasText && chroma(in.text) >= OwnChromaMin) {
        accent = in.text;
        haveAccent = true;
        accentSource = "colors.ini fg / Text_fg";
    }
    if (!haveAccent && !pal.empty()) {
        bool haveCandidate = false;
        double bestScore = 0.0;
        ThemeRgb bestColor;
        for (const auto &e : pal) {
            double L, a, b;
            toLab(e.color, L, a, b);
            const double ch = hypot(a, b);
            if (e.share >= AccentShare && L >= AccentLMin && L <= AccentLMax && ch >= AccentMinChroma) {
                const double score = e.share * ch;
                // Python's max() over (score, colour) tuples
                const bool better =
                    !haveCandidate || score > bestScore ||
                    (score == bestScore &&
                     (e.color.r > bestColor.r ||
                      (e.color.r == bestColor.r &&
                       (e.color.g > bestColor.g || (e.color.g == bestColor.g && e.color.b > bestColor.b)))));
                if (better) {
                    haveCandidate = true;
                    bestScore = score;
                    bestColor = e.color;
                }
            }
        }
        if (haveCandidate) {
            accent = bestColor;
            haveAccent = true;
            accentSource = "background: most saturated frequent cluster";
            double L, a, b;
            toLab(accent, L, a, b);
            const double ch = hypot(a, b);
            if (ch < AccentBoostChroma) {
                double k = AccentBoostChroma / ch;
                ThemeRgb boosted;
                bool ok = fromLab(L, a * k, b * k, false, boosted);
                double k2 = k;
                while (!ok && k2 > 1.0) {
                    k2 -= 0.1;
                    ok = fromLab(L, a * k2, b * k2, false, boosted);
                }
                if (ok)
                    accent = boosted;
                accentSource += " (chroma boosted)";
            }
        }
    }
    bool accentFallback = false;
    if (!haveAccent) {
        accentFallback = true;
        accent = fromHexInt(DefaultSecondary);
        roles.fallbacks.push_back("accent: no chromatic colour in the theme's data or background (monochrome) -> "
                                  "default theme's secondary #646464");
        accentSource = "default theme's secondary";
    }
    ThemeRgb edge;
    if (!liftUntil(accent, sheet, EdgeMinContrast, edge)) {
        edge = ThemeRgb(255, 255, 255);
        roles.fallbacks.push_back("edge: no lightness of the accent reaches 3:1 -> white");
    }
    roles.notes.push_back("accent <- " + accentSource);

    // --- text
    string textSource = "derived: near-white tinted toward the accent";
    ThemeRgb text = mix(ThemeRgb(255, 255, 255), edge, TextTint);
    if (in.hasText) {
        if (contrast(in.text, sheet) >= TextMinContrast) {
            text = in.text;
            textSource = "1.0 colors.ini fg / Text_fg";
        } else {
            roles.fallbacks.push_back(
                "text: 1.0 fg " + in.text.hex() +
                fmt(" is %.1f:1 vs the sheet (< 4.5) -> derived near-white", contrast(in.text, sheet)));
        }
    }
    if (accentFallback && !in.hasText) {
        text = fromHexInt(DefaultText);
        textSource = "default theme's text (no accent to tint with)";
    }
    roles.notes.push_back("text <- " + textSource);

    auto ensureText = [&](const string &name, const ThemeRgb &rgb) {
        if (contrast(rgb, sheet) >= TextMinContrast)
            return rgb;
        ThemeRgb lifted;
        if (!liftUntil(rgb, sheet, TextMinContrast, lifted)) {
            roles.fallbacks.push_back(name + ": cannot reach 4.5:1 -> default role");
            return fromHexInt(DefaultSecondary);
        }
        return lifted;
    };

    const ThemeRgb row = ensureText("row", mix(sheet, text, RowMix));
    const ThemeRgb grey(roundEven((text.r + text.g + text.b) / 3.0), roundEven((text.r + text.g + text.b) / 3.0),
                        roundEven((text.r + text.g + text.b) / 3.0));
    const ThemeRgb description = ensureText("description", mix(sheet, grey, DescMix));
    const ThemeRgb heading =
        accentFallback ? ensureText("heading", ThemeRgb(MonochromeHeading, MonochromeHeading, MonochromeHeading))
                       : ensureText("heading", edge);

    roles.sheet = sheet;
    roles.sheetAlpha = SheetAlpha;
    roles.accent = edge;
    roles.edge = edge;
    roles.selectionBand = edge;
    roles.text = text;
    roles.rowSelected = text;
    roles.footer = text;
    roles.row = row;
    roles.value = row;
    roles.description = description;
    roles.secondary = description;
    roles.hint = description;
    roles.heading = heading;
    roles.disabled = sheet;
    roles.disabledAlpha = DisabledAlpha;
    roles.monochrome = accentFallback;

    // the same check the prototype ends with
    string bad;
    const ThemeRgb *textRoles[] = {&roles.text,  &roles.secondary,   &roles.hint,
                                   &roles.row,   &roles.rowSelected, &roles.heading,
                                   &roles.value, &roles.description, &roles.footer};
    const char *textNames[] = {"text",    "secondary", "hint",        "row",   "rowSelected",
                               "heading", "value",     "description", "footer"};
    for (int i = 0; i < 9; i++)
        if (contrast(*textRoles[i], sheet) < TextMinContrast)
            bad += (bad.empty() ? "" : ", ") + string(textNames[i]);
    if (contrast(roles.selectionBand, sheet) < EdgeMinContrast)
        bad += (bad.empty() ? "" : ", ") + string("selectionBand");
    if (contrast(roles.edge, sheet) < EdgeMinContrast)
        bad += (bad.empty() ? "" : ", ") + string("edge");
    if (!bad.empty())
        roles.fallbacks.push_back("still below threshold: " + bad);
    return roles;
}
