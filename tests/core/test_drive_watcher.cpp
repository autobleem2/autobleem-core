//
// DriveWatcher over a fake drive: insert, read, ready, eject, a disc that is not PS1, one that cannot be read, a
// drive that comes and goes, pause/resume, the worker thread; the hub walk over a temp sysfs tree; the
// AB_FAKE_DRIVE switch over a real image; the emulator's -cdfile.
//
#include "doctest/doctest.h"
#include "support/fake_game.h"
#include "support/temp_dir.h"

#include "core/services/drive_watcher.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <unistd.h>
#endif

using namespace std;

namespace {

const uint32_t S = DiscReader::SectorSize;

// What the "drive" holds, shared by the test and the handles the backend gives out
struct Tray {
    bool plugged = true;  // the drive is on the bus
    bool disc = false;    // a disc is in and ready
    bool audio = false;   // ... an audio CD
    bool pc = false;      // ... a data disc without SYSTEM.CNF
    bool unreadable = false;
    int opens = 0;
    int open = 0;         // handles alive
    int identifies = 0;   // TOC reads
};

struct TrayDrive : CdDrive {
    explicit TrayDrive(Tray &t) : tray(t) {}
    bool readToc(CdToc &toc, string &error) override {
        tray.identifies++;
        if (tray.unreadable) {
            error = "no";
            return false;
        }
        toc = CdToc();
        if (tray.audio) {
            toc.tracks = {{1, true, 0, 0}, {2, true, 5000, 0}};
            toc.leadout = 9000;
        } else {
            toc.tracks = {{1, false, 0, 0}};
            toc.leadout = 100;
        }
        return true;
    }
    bool readSector(uint32_t lba, bool audio, uint8_t *data, uint8_t *) override {
        if (audio)
            return false;
        memset(data, 0, S);
        if (lba == 16) { // the PVD; the root's records are not needed: no SYSTEM.CNF and a PS label gives the serial
            data[15] = 1;
            memcpy(data + 16, "\x01" "CD001\x01", 7);
            memset(data + 16 + 40, ' ', 32);
            memcpy(data + 16 + 40, tray.pc ? "INSTALL" : "SCES_028.35", tray.pc ? 7 : 11);
            // root directory record: an extent whose sector reads as zeros - no SYSTEM.CNF there
            data[16 + 156] = 34;
            data[16 + 158] = 20;
            data[16 + 166] = 0;
            data[16 + 167] = 8; // 2048
            data[16 + 181] = 2;
        } else {
            data[15] = 1;
        }
        return true;
    }
    bool hasSubchannel() const override { return false; }
    Tray &tray;
};

struct TrayHandle : DriveWatcher::Handle {
    explicit TrayHandle(Tray &t) : tray(t), cd(t) { tray.open++; }
    ~TrayHandle() override { tray.open--; }
    bool ready() override { return tray.plugged && tray.disc; }
    CdDrive &drive() override { return cd; }
    Tray &tray;
    TrayDrive cd;
};

DriveWatcher::Backend backendFor(Tray &tray) {
    DriveWatcher::Backend b;
    b.findDrives = [&tray] { return tray.plugged ? vector<int>{3, 4} : vector<int>{}; };
    b.open = [&tray](int sg, string &error) -> unique_ptr<DriveWatcher::Handle> {
        if (!tray.plugged || sg != 3) {
            error = "no such drive";
            return nullptr;
        }
        tray.opens++;
        return unique_ptr<DriveWatcher::Handle>(new TrayHandle(tray));
    };
    return b;
}

} // namespace

TEST_CASE("no drive, then a drive, a disc, and the eject") {
    Tray tray;
    tray.plugged = false;
    DriveWatcher w(backendFor(tray));
    CHECK(w.state() == DriveWatcher::State::NoDrive);
    const unsigned c0 = w.changeCount();

    w.pollOnce();
    CHECK(w.state() == DriveWatcher::State::NoDrive);
    CHECK(w.changeCount() == c0); // nothing new

    tray.plugged = true; // the drive appears, empty
    w.pollOnce();
    CHECK(w.state() == DriveWatcher::State::Empty);
    CHECK(w.status().sgIndex == 3); // the first one found
    const unsigned c1 = w.changeCount();
    CHECK(c1 > c0);
    w.pollOnce();
    CHECK(w.changeCount() == c1);

    tray.disc = true; // a PS1 disc goes in: read once, Ready
    w.pollOnce();
    DriveWatcher::Status s = w.status();
    CHECK(s.state == DriveWatcher::State::Ready);
    CHECK(s.disc.kind == DiscIdentity::Kind::Ps1Game);
    CHECK(s.disc.serial == "SCES-02835");
    CHECK(s.changes > c1);
    const int reads = tray.identifies;
    w.pollOnce();
    w.pollOnce();
    CHECK(tray.identifies == reads); // once per inserted disc

    tray.disc = false; // eject
    w.pollOnce();
    CHECK(w.state() == DriveWatcher::State::Empty);
    tray.disc = true; // and the same disc back in: read again
    w.pollOnce();
    CHECK(w.state() == DriveWatcher::State::Ready);
    CHECK(tray.identifies == reads + 1);

    tray.plugged = false; // pulled out
    tray.disc = false;
    w.pollOnce();
    CHECK(w.state() == DriveWatcher::State::NoDrive);
    CHECK(tray.open == 0);
}

TEST_CASE("an audio CD is Ready, a data disc without SYSTEM.CNF is NotPs1") {
    Tray tray;
    tray.disc = true;
    tray.audio = true;
    DriveWatcher w(backendFor(tray));
    w.pollOnce();
    CHECK(w.state() == DriveWatcher::State::Ready);
    CHECK(w.status().disc.kind == DiscIdentity::Kind::AudioCd);
    CHECK(w.status().disc.trackCount == 2);

    Tray pc;
    pc.disc = true;
    pc.pc = true;
    DriveWatcher w2(backendFor(pc));
    w2.pollOnce();
    CHECK(w2.state() == DriveWatcher::State::NotPs1);
    CHECK(w2.status().disc.kind == DiscIdentity::Kind::OtherDisc);
    CHECK(w2.status().disc.volumeLabel == "INSTALL");
}

TEST_CASE("an unreadable disc is tried ReadTries times, then Unreadable, and not again until it is ejected") {
    Tray tray;
    tray.disc = true;
    tray.unreadable = true;
    DriveWatcher w(backendFor(tray));
    for (int i = 1; i < DriveWatcher::ReadTries; i++) {
        w.pollOnce();
        CHECK(w.state() == DriveWatcher::State::Reading);
    }
    w.pollOnce();
    CHECK(w.state() == DriveWatcher::State::Unreadable);
    CHECK(tray.identifies == DriveWatcher::ReadTries);
    w.pollOnce();
    CHECK(tray.identifies == DriveWatcher::ReadTries);

    // a spinning-up drive: the second try reads
    Tray slow;
    slow.disc = true;
    slow.unreadable = true;
    DriveWatcher w2(backendFor(slow));
    w2.pollOnce();
    CHECK(w2.state() == DriveWatcher::State::Reading);
    slow.unreadable = false;
    w2.pollOnce();
    CHECK(w2.state() == DriveWatcher::State::Ready);
}

TEST_CASE("pause closes the drive, resume opens it and reads the disc again without a spurious change") {
    Tray tray;
    tray.disc = true;
    DriveWatcher w(backendFor(tray));
    w.pollOnce();
    REQUIRE(w.state() == DriveWatcher::State::Ready);
    CHECK(tray.open == 1);
    const unsigned changes = w.changeCount();

    w.pause();
    CHECK(w.paused());
    CHECK(tray.open == 0); // the emulator owns the drive
    w.pollOnce();          // a paused watcher does not touch it
    CHECK(tray.open == 0);
    CHECK(tray.opens == 1);

    w.resume();
    CHECK_FALSE(w.paused());
    w.pollOnce();
    CHECK(tray.open == 1);
    CHECK(w.state() == DriveWatcher::State::Ready);
    CHECK(w.changeCount() == changes); // the same disc: nothing for the UI to redraw

    // another disc while it was away
    w.pause();
    tray.audio = true;
    w.resume();
    w.pollOnce();
    CHECK(w.status().disc.kind == DiscIdentity::Kind::AudioCd);
    CHECK(w.changeCount() > changes);
}

TEST_CASE("the worker thread polls on its own, and stops") {
    Tray tray;
    tray.disc = true;
    {
        DriveWatcher w(backendFor(tray), 10);
        w.start();
        for (int i = 0; i < 300 && w.state() != DriveWatcher::State::Ready; i++)
            this_thread::sleep_for(chrono::milliseconds(10));
        CHECK(w.state() == DriveWatcher::State::Ready);
        w.pause();
        CHECK(tray.open == 0);
        w.resume();
        tray.disc = false;
        for (int i = 0; i < 300 && w.state() != DriveWatcher::State::Empty; i++)
            this_thread::sleep_for(chrono::milliseconds(10));
        CHECK(w.state() == DriveWatcher::State::Empty);
        w.stop();
    }
    CHECK(tray.open == 0);
}

TEST_CASE("the emulator's -cdfile") {
#ifndef _WIN32
    unsetenv("AB_FAKE_DRIVE");
#endif
    CHECK(DriveWatcher::cdfileFor(2) == "cdrom://drive2.cue");
    CHECK(DriveWatcher::cdfileFor(0) == "cdrom://drive0.cue");
}

#ifndef _WIN32
TEST_CASE("hub walk: every USB hub above the drive, nearest first, and 'on' written to each") {
    TempDir tmp("hubs");
    // devices/usb1 (root hub) / 1-1 (a hub) / 1-1.2 (the drive's USB device) / 1-1.2:1.0 / host / target / sg
    const string dev = tmp.makeSubDir("devices/pci/usb1/1-1/1-1.2/1-1.2:1.0/host6/target6:0:0/6:0:0:0");
    tmp.makeSubDir("class/scsi_generic/sg3");
    REQUIRE(symlink(dev.c_str(), tmp.at("class/scsi_generic/sg3/device").c_str()) == 0);
    tmp.writeFile("devices/pci/usb1/bDeviceClass", "09\n");
    tmp.makeSubDir("devices/pci/usb1/power");
    tmp.writeFile("devices/pci/usb1/power/control", "auto\n");
    tmp.writeFile("devices/pci/usb1/1-1/bDeviceClass", "09\n");
    tmp.makeSubDir("devices/pci/usb1/1-1/power");
    tmp.writeFile("devices/pci/usb1/1-1/power/control", "auto\n");
    tmp.writeFile("devices/pci/usb1/1-1/1-1.2/bDeviceClass", "00\n"); // the drive itself: not a hub
    tmp.makeSubDir("devices/pci/usb1/1-1/1-1.2/power");
    tmp.writeFile("devices/pci/usb1/1-1/1-1.2/power/control", "auto\n");

    const string root = tmp.at("class/scsi_generic");
    vector<string> files = DriveWatcher::hubControlFiles(root, 3);
    REQUIRE(files.size() == 2);
    CHECK(files[0].find("1-1/power/control") != string::npos);
    CHECK(files[1].find("usb1/power/control") != string::npos);
    CHECK(files[1].find("1-1") == string::npos);

    CHECK(DriveWatcher::keepHubsOn(root, 3));
    CHECK(tmp.readFile("devices/pci/usb1/1-1/power/control") == "on");
    CHECK(tmp.readFile("devices/pci/usb1/power/control") == "on");
    CHECK(tmp.readFile("devices/pci/usb1/1-1/1-1.2/power/control") == "auto\n");

    CHECK(DriveWatcher::hubControlFiles(root, 9).empty()); // no such sg
}

TEST_CASE("AB_FAKE_DRIVE: an image is the disc, AB_FAKE_DRIVE_EJECT opens the tray") {
    TempDir tmp("fakedrive");
    tmp.makeSubDir("games");
    test_support::makeFakeGame(tmp.at("games"), "Fake");
    const string cue = tmp.at("games/Fake/Fake.cue");
    const string eject = tmp.at("tray-open");
    setenv("AB_FAKE_DRIVE", cue.c_str(), 1);
    setenv("AB_FAKE_DRIVE_EJECT", eject.c_str(), 1);
    CHECK(DriveWatcher::fakeCue() == cue);
    CHECK(DriveWatcher::cdfileFor(5) == cue); // the emulator reads the image itself

    {
        DriveWatcher w(DriveWatcher::defaultBackend(tmp.at("no-sysfs")));
        w.pollOnce();
        CHECK(w.state() == DriveWatcher::State::Ready);
        CHECK(w.status().disc.kind == DiscIdentity::Kind::Ps1Game);
        CHECK(w.status().disc.serial == "SLUS-01234");

        tmp.writeFile("tray-open", "");
        w.pollOnce();
        CHECK(w.state() == DriveWatcher::State::Empty);

        ::unlink(eject.c_str());
        w.pollOnce();
        CHECK(w.state() == DriveWatcher::State::Ready);
    }

    setenv("AB_FAKE_DRIVE", (tmp.path() + "/missing.cue").c_str(), 1); // a cue that is not there: no drive
    {
        DriveWatcher w(DriveWatcher::defaultBackend(tmp.at("no-sysfs")));
        w.pollOnce();
        CHECK(w.state() == DriveWatcher::State::NoDrive);
    }
    unsetenv("AB_FAKE_DRIVE");
    unsetenv("AB_FAKE_DRIVE_EJECT");
}
#endif
