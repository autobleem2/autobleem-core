//
// CddaPlayer - see the header.
//
#include "cdda_player.h"

#include <algorithm>
#include <cstring>

namespace {
const size_t HistoryFrames = 4096;
const uint32_t PrevRestartFrames = 3 * CddaPlayer::SectorsPerSecond; // prev() inside this goes to the track before
} // namespace

CddaPlayer::CddaPlayer(CdDrive &d, const CdToc &toc, int retriesMore, size_t bufferSectors)
    : drive(d), retries(retriesMore), capacity(std::max<size_t>(bufferSectors, 2)) {
    for (size_t i = 0; i < toc.tracks.size(); i++) {
        const CdTrack &t = toc.tracks[i];
        if (!t.audio)
            continue;
        uint32_t end = toc.leadout;
        if (i + 1 < toc.tracks.size()) {
            const CdTrack &n = toc.tracks[i + 1];
            end = n.start;
            if (n.pregap > 0 && n.pregap < n.start && n.start - n.pregap > t.start)
                end = n.start - n.pregap; // the next track's INDEX 00 is not this one's music
        }
        if (end <= t.start)
            continue;
        tracks.push_back({t.number, t.start, end - t.start});
        totalFrames += end - t.start;
    }
    ring.resize(capacity);
    for (Slot &s : ring)
        s.data.resize(SectorBytes);
    history.assign(HistoryFrames * 2, 0);
    rebuildOrder(0);
}

CddaPlayer::~CddaPlayer() {
    stop();
}

//******************
// order
//******************
void CddaPlayer::rebuildOrder(int first) {
    order.clear();
    for (size_t i = 0; i < tracks.size(); i++)
        order.push_back(static_cast<int>(i));
    if (!shuf || order.empty())
        return;
    first = std::max(0, std::min(first, static_cast<int>(order.size()) - 1));
    std::swap(order[0], order[static_cast<size_t>(first)]);
    std::shuffle(order.begin() + 1, order.end(), rng);
}

int CddaPlayer::orderPos(int index) const {
    for (size_t i = 0; i < order.size(); i++)
        if (order[i] == index)
            return static_cast<int>(i);
    return 0;
}

int CddaPlayer::successor(int index, bool manual) const {
    if (rep == Repeat::Track && !manual)
        return index;
    size_t pos = static_cast<size_t>(orderPos(index)) + 1;
    if (pos < order.size())
        return order[pos];
    if (rep == Repeat::Disc && !order.empty())
        return order[0];
    return -1;
}

//******************
// transport
//******************
void CddaPlayer::startAt(int index, uint32_t frame) {
    generation++;
    count = 0;
    head = 0;
    offset = 0;
    curIndex = index;
    curFrame = frame;
    nowIndex = index;
    nowFrame = frame;
    active = true;
    if (st == State::Stopped)
        st = State::Playing;
    if (!reader.joinable()) {
        quit = false;
        reader = std::thread(&CddaPlayer::run, this);
    }
    wake.notify_all();
}

bool CddaPlayer::play(int trackNumber) {
    std::unique_lock<std::mutex> lk(m);
    for (size_t i = 0; i < tracks.size(); i++) {
        if (tracks[i].number != trackNumber)
            continue;
        rebuildOrder(static_cast<int>(i));
        st = State::Playing;
        startAt(static_cast<int>(i), 0);
        return true;
    }
    return false;
}

void CddaPlayer::pause() {
    std::lock_guard<std::mutex> lk(m);
    if (st == State::Playing)
        st = State::Paused;
}

void CddaPlayer::resume() {
    std::lock_guard<std::mutex> lk(m);
    if (st == State::Paused)
        st = State::Playing;
}

void CddaPlayer::stop() {
    std::thread t;
    {
        std::lock_guard<std::mutex> lk(m);
        quit = true;
        active = false;
        generation++;
        st = State::Stopped;
        count = head = offset = 0;
        nowIndex = -1;
        nowFrame = 0;
        t = std::move(reader);
        wake.notify_all();
    }
    if (t.joinable())
        t.join();
}

bool CddaPlayer::next() {
    std::lock_guard<std::mutex> lk(m);
    if (st == State::Stopped || nowIndex < 0)
        return false;
    int n = successor(nowIndex, true);
    if (n < 0)
        return false;
    startAt(n, 0);
    return true;
}

bool CddaPlayer::prev() {
    std::lock_guard<std::mutex> lk(m);
    if (st == State::Stopped || nowIndex < 0)
        return false;
    int target = nowIndex;
    if (nowFrame < PrevRestartFrames) {
        int pos = orderPos(nowIndex);
        if (pos > 0)
            target = order[static_cast<size_t>(pos - 1)];
        else if (rep == Repeat::Disc && order.size() > 1)
            target = order.back();
    }
    startAt(target, 0);
    return true;
}

bool CddaPlayer::seek(uint32_t frames) {
    std::lock_guard<std::mutex> lk(m);
    if (st == State::Stopped || nowIndex < 0)
        return false;
    uint32_t last = tracks[static_cast<size_t>(nowIndex)].length - 1;
    startAt(nowIndex, std::min(frames, last));
    return true;
}

void CddaPlayer::setRepeat(Repeat r) {
    std::lock_guard<std::mutex> lk(m);
    rep = r;
    wake.notify_all();
}

void CddaPlayer::setShuffle(bool on) {
    std::lock_guard<std::mutex> lk(m);
    if (on == shuf)
        return;
    shuf = on;
    rebuildOrder(nowIndex >= 0 ? nowIndex : 0);
}

//******************
// for the UI
//******************
CddaPlayer::State CddaPlayer::state() const {
    std::lock_guard<std::mutex> lk(m);
    return st;
}

CddaPlayer::Repeat CddaPlayer::repeat() const {
    std::lock_guard<std::mutex> lk(m);
    return rep;
}

bool CddaPlayer::shuffle() const {
    std::lock_guard<std::mutex> lk(m);
    return shuf;
}

int CddaPlayer::track() const {
    std::lock_guard<std::mutex> lk(m);
    return st == State::Stopped || nowIndex < 0 ? 0 : tracks[static_cast<size_t>(nowIndex)].number;
}

uint32_t CddaPlayer::position() const {
    std::lock_guard<std::mutex> lk(m);
    return st == State::Stopped || nowIndex < 0 ? 0 : nowFrame;
}

uint32_t CddaPlayer::trackLength(int trackNumber) const {
    for (const Track &t : tracks)
        if (t.number == trackNumber)
            return t.length;
    return 0;
}

uint32_t CddaPlayer::buffered() const {
    std::lock_guard<std::mutex> lk(m);
    return static_cast<uint32_t>(count);
}

//******************
// the sound side
//******************
size_t CddaPlayer::read(int16_t *out, size_t frames) {
    uint8_t *bytes = reinterpret_cast<uint8_t *>(out);
    size_t want = frames * 4, got = 0;
    {
        std::lock_guard<std::mutex> lk(m);
        while (st == State::Playing && got < want) {
            if (count == 0) {
                if (!active)
                    st = State::Stopped; // the order is over and everything has been played
                break;                   // else: a buffer underrun
            }
            Slot &s = ring[head];
            size_t n = std::min<size_t>(want - got, SectorBytes - offset);
            memcpy(bytes + got, s.data.data() + offset, n);
            got += n;
            offset += static_cast<uint32_t>(n);
            nowIndex = s.index;
            nowFrame = s.frame;
            if (offset == SectorBytes) {
                offset = 0;
                head = (head + 1) % capacity;
                count--;
                wake.notify_all(); // room for the reader
            }
        }
        // the analyser's copy
        size_t frs = got / 4;
        const int16_t *src = out;
        for (size_t i = 0; i < frs; i++) {
            history[historyPos * 2] = src[2 * i];
            history[historyPos * 2 + 1] = src[2 * i + 1];
            historyPos = (historyPos + 1) % HistoryFrames;
        }
        historyFill = std::min(HistoryFrames, historyFill + frs);
    }
    if (got < want)
        memset(bytes + got, 0, want - got);
    return got / 4;
}

size_t CddaPlayer::lastFrames(int16_t *out, size_t frames) const {
    std::lock_guard<std::mutex> lk(m);
    size_t n = std::min(frames, historyFill);
    size_t start = (historyPos + HistoryFrames - n) % HistoryFrames;
    for (size_t i = 0; i < n; i++) {
        size_t p = (start + i) % HistoryFrames;
        out[2 * i] = history[2 * p];
        out[2 * i + 1] = history[2 * p + 1];
    }
    return n;
}

//******************
// the reader thread
//******************
void CddaPlayer::readSector(uint32_t lba, uint8_t *data) {
    for (int attempt = 0; attempt <= retries; attempt++)
        if (drive.readSector(lba, true, data, nullptr))
            return;
    memset(data, 0, SectorBytes); // silence, and on
    bad++;
}

void CddaPlayer::run() {
    std::vector<uint8_t> buf(SectorBytes);
    std::unique_lock<std::mutex> lk(m);
    while (!quit) {
        if (!active || count == capacity) {
            wake.wait(lk);
            continue;
        }
        const uint64_t gen = generation;
        const int index = curIndex;
        const uint32_t frame = curFrame;
        const uint32_t lba = tracks[static_cast<size_t>(index)].start + frame;
        lk.unlock();
        readSector(lba, buf.data());
        lk.lock();
        if (gen != generation || quit)
            continue; // a jump happened while the drive was reading: this sector is from the old place
        Slot &s = ring[(head + count) % capacity];
        s.data.swap(buf);
        buf.resize(SectorBytes);
        s.index = index;
        s.frame = frame;
        count++;
        if (frame + 1 < tracks[static_cast<size_t>(index)].length) {
            curFrame = frame + 1;
        } else {
            int n = successor(index, false);
            if (n < 0) {
                active = false;
            } else {
                curIndex = n;
                curFrame = 0;
            }
        }
    }
}
