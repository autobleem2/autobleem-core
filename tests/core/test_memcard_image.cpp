//
// ableem::MemcardImage: a PlayStation memory card image and its fifteen save slots.
//
#include "doctest/doctest.h"

#include "../support/string_maker.h"
#include "../support/temp_dir.h"

#include <ableem/engine/memcard_image.h>

#include <cstring>
#include <string>
#include <vector>

using ableem::MemcardImage;
using std::string;
using std::vector;

namespace {

const int Frame = 0x80;
const int Block = 0x2000;

// A card built in memory. Slot layout (see memcard_image.h): a directory frame per slot after the header
// frame, a block per slot after the first block.
struct CardBytes {
    CardBytes() : bytes(MemcardImage::Size, 0) {
        // every slot free
        for (int slot = 0; slot < MemcardImage::Slots; slot++) {
            dir(slot)[0] = 0xA0;
            dir(slot)[8] = 0xFF;
            checksum(slot);
        }
    }
    uint8_t *dir(int slot) { return &bytes[Frame + slot * Frame]; }
    uint8_t *block(int slot) { return &bytes[Block + slot * Block]; }

    void checksum(int slot) {
        uint8_t x = 0;
        for (int i = 0; i < 126; i++)
            x ^= dir(slot)[i];
        dir(slot)[127] = x;
    }

    // a save occupying `slots` (first is the top block), with its title, product code and game id, and an
    // icon whose first frame is solid palette colour 1 (pure red)
    void addSave(const vector<int> &slots, const string &title, const string &productCode, const string &gameId) {
        for (size_t i = 0; i < slots.size(); i++) {
            uint8_t *d = dir(slots[i]);
            bool last = i + 1 == slots.size();
            d[0] = i == 0 ? 0x51 : (last ? 0x53 : 0x52);
            int size = slots.size() * Block;
            d[4] = size & 0xFF;
            d[5] = (size >> 8) & 0xFF;
            d[6] = (size >> 16) & 0xFF;
            d[8] = last ? 0xFF : slots[i + 1];
            d[9] = last ? 0xFF : 0x00;
            if (i == 0) {
                memcpy(d + 12, productCode.c_str(), productCode.size());
                memcpy(d + 22, gameId.c_str(), gameId.size());
            }
            checksum(slots[i]);
        }
        uint8_t *b = block(slots[0]);
        b[0] = 'S';
        b[1] = 'C';
        memcpy(b + 4, title.c_str(), title.size());
        // palette entry 1: 15-bit BGR with red = 31 -> bytes 0x1F 0x00
        b[0x60 + 2] = 0x1F;
        b[0x60 + 3] = 0x00;
        // frame 0: every pixel index 1 (two pixels per byte)
        memset(b + 0x80, 0x11, 128);
    }

    vector<uint8_t> bytes;
};

// shiftjis.dat maps a Shift-JIS code to Unicode as big-endian 16-bit entries; this table has the ASCII
// range and nothing else, which is all a title in these tests uses
vector<uint8_t> asciiShiftJisTable() {
    vector<uint8_t> table(25088, 0);
    for (int c = 0; c < 128; c++) {
        table[c * 2] = 0;
        table[c * 2 + 1] = c;
    }
    return table;
}

MemcardImage imageOf(CardBytes &card) {
    MemcardImage image;
    image.setShiftJisTable(asciiShiftJisTable());
    image.setBytes(card.bytes.data());
    return image;
}

bool checksumValid(const MemcardImage &image, int slot) {
    const uint8_t *d = image.bytes() + Frame + slot * Frame;
    uint8_t x = 0;
    for (int i = 0; i < 126; i++)
        x ^= d[i];
    return d[127] == x;
}

} // namespace

TEST_CASE("an empty card is fifteen free slots with nothing to say") {
    CardBytes card;
    MemcardImage image = imageOf(card);
    for (int slot = 0; slot < MemcardImage::Slots; slot++) {
        CHECK(image.isFree(slot));
        CHECK_FALSE(image.isUsed(slot));
        CHECK(image.title(slot) == "");
        CHECK(image.nextSlot(slot) == 0xFF);
    }
    CHECK(image.findEmptySlots(3) == vector<int>{0, 1, 2});
}

TEST_CASE("a save is parsed from its directory frames: kind of block, chain, codes and title") {
    CardBytes card;
    card.addSave({0, 1}, "Hello", "BASCUS-941", "63TEKKEN3");
    MemcardImage image = imageOf(card);

    CHECK(image.isUsed(0));
    CHECK(image.isTop(0));
    CHECK(image.blockType(1) == MemcardImage::BlockType::LinkEnd);
    CHECK(image.nextSlot(0) == 1);
    CHECK(image.nextSlot(1) == 0xFF);
    CHECK(image.gameSlots(0) == vector<int>{0, 1});
    CHECK(image.productCode(0) == "BASCUS-941");
    CHECK(image.gameId(0) == "63TEKKEN3");
    CHECK(image.title(0) == "Hello");
    CHECK(image.title(1) == ""); // a link block has no title of its own
    CHECK(image.hasIcon(0));
    CHECK_FALSE(image.hasIcon(1));
    CHECK(image.isFree(2));
    CHECK(image.findEmptySlots(2) == vector<int>{2, 3});
}

TEST_CASE("a save exported from one card imports into the free slots of another, relinked") {
    CardBytes a;
    a.addSave({0, 1}, "Hello", "BASCUS-941", "63TEKKEN3");
    MemcardImage from = imageOf(a);

    CardBytes b;
    b.addSave({0}, "Other", "BASCUS-000", "00OTHER"); // slot 0 is taken; the import lands on 1 and 2
    MemcardImage to = imageOf(b);

    int size = from.exportSize(0);
    CHECK(size == Frame + 2 * Block);
    vector<uint8_t> buffer(size);
    from.exportGame(0, buffer.data());
    to.importGame(buffer.data(), size);

    CHECK(to.isTop(1));
    CHECK(to.blockType(2) == MemcardImage::BlockType::LinkEnd);
    CHECK(to.nextSlot(1) == 2);
    CHECK(to.nextSlot(2) == 0xFF);
    CHECK(to.title(1) == "Hello");
    CHECK(to.productCode(1) == "BASCUS-941");
    CHECK(to.title(0) == "Other"); // untouched
    CHECK(checksumValid(to, 1));
    CHECK(checksumValid(to, 2));
    CHECK(to.findEmptySlots(1) == vector<int>{3});
}

TEST_CASE("a save that does not fit is not imported at all") {
    CardBytes a;
    a.addSave({0, 1, 2}, "Big", "BASCUS-941", "63BIG");
    MemcardImage from = imageOf(a);

    CardBytes b;
    for (int slot = 0; slot < 13; slot++)
        b.addSave({slot}, "S", "BASCUS-000", "0"); // two slots left
    MemcardImage to = imageOf(b);

    vector<uint8_t> buffer(from.exportSize(0));
    from.exportGame(0, buffer.data());
    to.importGame(buffer.data(), buffer.size());

    CHECK(to.isFree(13));
    CHECK(to.isFree(14));
}

TEST_CASE("deleting a save frees its blocks but leaves the data, so it can be undeleted") {
    CardBytes card;
    card.addSave({0, 1}, "Hello", "BASCUS-941", "63TEKKEN3");
    MemcardImage image = imageOf(card);

    image.deleteGame(0);
    CHECK(image.isFree(0));
    CHECK(image.isFree(1));
    CHECK(image.isDeleted(0)); // the block still starts with "SC"
    CHECK_FALSE(image.isUsed(0));
    CHECK(checksumValid(image, 0));

    image.undeleteSlot(0);
    CHECK(image.isTop(0));
    CHECK(image.title(0) == "Hello");
    CHECK(checksumValid(image, 0));
}

TEST_CASE("the product code and game id can be rewritten, with the frame's checksum kept right") {
    CardBytes card;
    card.addSave({0}, "Hello", "BASCUS-941", "63TEKKEN3");
    MemcardImage image = imageOf(card);

    image.setProductCode(0, "BESLES-123");
    image.setGameId(0, "45NEW");
    CHECK(image.productCode(0) == "BESLES-123");
    CHECK(image.gameId(0) == "45NEW");
    CHECK(checksumValid(image, 0));
}

TEST_CASE("icon frames come back as pixels: the save's own, dimmed for a link block, translucent for a free slot") {
    CardBytes card;
    card.addSave({0, 1}, "Hello", "BASCUS-941", "63TEKKEN3");
    MemcardImage image = imageOf(card);
    MemcardImage::Pixel px[MemcardImage::IconSize * MemcardImage::IconSize];

    image.iconPixels(0, 0, px);
    CHECK(px[0].r == 255); // palette red 31: white-scale 255
    CHECK(px[0].g == 0);
    CHECK(px[0].a == 255);
    CHECK(px[255].r == 255);

    image.iconPixels(1, 0, px); // the link block: the top's frame, a third as bright
    CHECK(px[0].r == 85);
    CHECK(px[0].a == 255);

    image.iconPixels(2, 0, px); // free
    CHECK(px[0].r == 0);
    CHECK(px[0].a == 127);

    image.deleteGame(0); // deleted: lightened
    image.iconPixels(0, 0, px);
    CHECK(px[0].r == 251); // 31 * 4 + 127
}

namespace {

// A synthetic card with a 1-, a 2- and a 3-frame save in slots 0, 1 and 2 (header byte 2 = 0x11/0x12/0x13).
// Every frame is a solid colour of its own (frame f = palette entry f + 1: red, green, blue) except the
// top-left pixel, which is palette entry 0 (raw 0x0000, the transparent colour). Slot 3 has a flag of 0x15, which
// is not a frame count. Palette entry 4 is white.
CardBytes animatedCard() {
    CardBytes card;
    const int flags[4] = {0x11, 0x12, 0x13, 0x15};
    for (int slot = 0; slot < 4; slot++) {
        card.addSave({slot}, "Save", "BASCUS-941", "63ANIM");
        uint8_t *b = card.block(slot);
        b[2] = flags[slot];
        b[0x60 + 2] = 0x1F; // entry 1: red
        b[0x60 + 3] = 0x00;
        b[0x60 + 4] = 0xE0; // entry 2: green (31 << 5)
        b[0x60 + 5] = 0x03;
        b[0x60 + 6] = 0x00; // entry 3: blue (31 << 10)
        b[0x60 + 7] = 0x7C;
        b[0x60 + 8] = 0xFF; // entry 4: white
        b[0x60 + 9] = 0x7F;
        for (int f = 0; f < 3; f++) {
            memset(b + 0x80 + f * 128, (f + 1) * 0x11, 128);
            b[0x80 + f * 128] = ((f + 1) << 4) | 0x00; // pixel (0,0) is entry 0, (1,0) is entry f + 1
        }
    }
    return card;
}

} // namespace

TEST_CASE("an icon animates over what header byte 2 says: 0x11 = 1 frame, 0x12 = 2, 0x13 = 3, anything else = 1") {
    CardBytes card = animatedCard();
    MemcardImage image = imageOf(card);
    CHECK(image.iconFrameCount(0) == 1);
    CHECK(image.iconFrameCount(1) == 2);
    CHECK(image.iconFrameCount(2) == 3);
    CHECK(image.iconFrameCount(3) == 1); // 0x15: not a frame count
    CHECK(image.iconFrameCount(4) == 1); // free

    CardBytes link;
    link.addSave({0, 1}, "Hello", "BASCUS-941", "63TEKKEN3");
    link.block(0)[2] = 0x13;
    MemcardImage linked = imageOf(link);
    CHECK(linked.iconFrameCount(0) == 3);
    CHECK(linked.iconFrameCount(1) == 1); // a link block is static
}

TEST_CASE("the frame shown follows the real BIOS's pace: 320 ms a frame of 2, 220 ms of 3, from frame 0 and wrapping") {
    // a static icon never moves
    for (unsigned int t : {0u, 1000u, 123456u})
        CHECK(MemcardImage::iconFrameAt(1, t) == 0);

    CHECK(MemcardImage::iconFrameAt(2, 0) == 0); // the pencil has just entered the slot
    CHECK(MemcardImage::iconFrameAt(2, 319) == 0);
    CHECK(MemcardImage::iconFrameAt(2, 320) == 1);
    CHECK(MemcardImage::iconFrameAt(2, 639) == 1);
    CHECK(MemcardImage::iconFrameAt(2, 640) == 0);

    CHECK(MemcardImage::iconFrameAt(3, 0) == 0);
    CHECK(MemcardImage::iconFrameAt(3, 219) == 0);
    CHECK(MemcardImage::iconFrameAt(3, 220) == 1);
    CHECK(MemcardImage::iconFrameAt(3, 440) == 2);
    CHECK(MemcardImage::iconFrameAt(3, 659) == 2);
    CHECK(MemcardImage::iconFrameAt(3, 660) == 0);

    // a move tick later than the clock's zero: the frame counts from the move
    const unsigned int moveTick = 5000;
    CHECK(MemcardImage::iconFrameAt(3, 5000 + 230 - moveTick) == 1);
}

TEST_CASE("every frame of the synthetic card has its own colour, palette colour 0 is transparent, white is 255") {
    CardBytes card = animatedCard();
    MemcardImage image = imageOf(card);
    MemcardImage::Pixel px[MemcardImage::IconSize * MemcardImage::IconSize];

    const uint8_t expectRgb[3][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
    for (int f = 0; f < 3; f++) {
        image.iconPixels(2, f, px);
        CHECK(px[0].a == 0); // entry 0, raw 0x0000
        CHECK(px[1].a == 255);
        CHECK(px[1].r == expectRgb[f][0]);
        CHECK(px[1].g == expectRgb[f][1]);
        CHECK(px[1].b == expectRgb[f][2]);
        CHECK(px[255].a == 255);
    }

    // palette entry 4 is white: all channels 255
    card.block(0)[0x80] = 0x44;
    image = imageOf(card);
    image.iconPixels(0, 0, px);
    CHECK(px[0].r == 255);
    CHECK(px[0].g == 255);
    CHECK(px[0].b == 255);
    CHECK(px[0].a == 255);
}

TEST_CASE("a card is written and read back whole; a DexDrive file is read past its header; a short file is refused") {
    TempDir tmp("mcd");
    CardBytes card;
    card.addSave({0}, "Hello", "BASCUS-941", "63TEKKEN3");
    MemcardImage image = imageOf(card);

    REQUIRE(image.save(tmp.at("card.mcd")));
    MemcardImage back;
    back.setShiftJisTable(asciiShiftJisTable());
    REQUIRE(back.load(tmp.at("card.mcd")));
    CHECK(back.title(0) == "Hello");
    CHECK(memcmp(back.bytes(), image.bytes(), MemcardImage::Size) == 0);

    string dex(3904, '\0');
    dex.append(reinterpret_cast<const char *>(image.bytes()), MemcardImage::Size);
    tmp.writeFile("card.gme", dex);
    MemcardImage fromDex;
    fromDex.setShiftJisTable(asciiShiftJisTable());
    REQUIRE(fromDex.load(tmp.at("card.gme")));
    CHECK(fromDex.title(0) == "Hello");

    tmp.writeFile("short.mcd", "not a card");
    CHECK_FALSE(MemcardImage().load(tmp.at("short.mcd")));
    CHECK_FALSE(MemcardImage().load(tmp.at("missing.mcd")));
}

TEST_CASE("without a Shift-JIS table a title is as many U+0000s as it has bytes") {
    CardBytes card;
    card.addSave({0}, "Hi", "BASCUS-941", "63TEKKEN3");
    MemcardImage image;
    image.setBytes(card.bytes.data());
    CHECK(image.title(0) == string(2, '\0'));
}
