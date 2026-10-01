//
// CarouselSession: the carousel's place as the small file a launcher restart hands on (BUG-40).
//
#include "doctest/doctest.h"

#include "../support/temp_dir.h"

#include "core/services/carousel_session.h"

#include <string>

using std::string;

namespace {

GameSetSelection sample() {
    GameSetSelection s;
    s.set = GameSet::RetroArch;
    s.ps1SelectState = Ps1SelectState::GamesSubdir;
    s.gameIndex = 17;
    s.usbGameDirIndex = 3;
    s.usbGameDirName = "Racing games";
    s.raPlaylistIndex = 2;
    s.raPlaylistName = "Sony - PlayStation.lpl";
    s.appCategory = AppCategory::Tools;
    return s;
}

void checkSame(const GameSetSelection &a, const GameSetSelection &b) {
    CHECK(a.set == b.set);
    CHECK(a.ps1SelectState == b.ps1SelectState);
    CHECK(a.gameIndex == b.gameIndex);
    CHECK(a.usbGameDirIndex == b.usbGameDirIndex);
    CHECK(a.usbGameDirName == b.usbGameDirName);
    CHECK(a.raPlaylistIndex == b.raPlaylistIndex);
    CHECK(a.raPlaylistName == b.raPlaylistName);
    CHECK(a.appCategory == b.appCategory);
}

} // namespace

TEST_CASE("CarouselSession: what is written is what is read back") {
    GameSetSelection back;
    CHECK(CarouselSession::parse(CarouselSession::serialize(sample()), back));
    checkSame(back, sample());

    GameSetSelection plain; // the defaults too
    GameSetSelection other = sample();
    CHECK(CarouselSession::parse(CarouselSession::serialize(plain), other));
    checkSame(other, plain);
}

TEST_CASE("CarouselSession: the text starts with its version and is one key=value per line") {
    const string text = CarouselSession::serialize(sample());
    CHECK(text.rfind("version=1\n", 0) == 0);
    CHECK(text.find("game=17\n") != string::npos);
    CHECK(text.find("usbdirname=Racing games\n") != string::npos);
}

TEST_CASE("CarouselSession: a name with a line break stays on its line") {
    GameSetSelection s;
    s.usbGameDirName = "a\nb\r\nc";
    GameSetSelection back;
    CHECK(CarouselSession::parse(CarouselSession::serialize(s), back));
    CHECK(back.usbGameDirName == "abc");
}

TEST_CASE("CarouselSession: a missing key keeps its default, an unknown one is ignored") {
    GameSetSelection back = sample();
    CHECK(CarouselSession::parse("version=1\nfuture=yes\ngame=4\r\nset=1\n", back));
    CHECK(back.set == GameSet::RetroArch);
    CHECK(back.gameIndex == 4);
    CHECK(back.ps1SelectState == Ps1SelectState::AllGames);
    CHECK(back.usbGameDirName.empty());
}

TEST_CASE("CarouselSession: a bad file is refused whole and leaves the selection alone") {
    GameSetSelection keep = sample();
    GameSetSelection s = keep;
    CHECK_FALSE(CarouselSession::parse("", s));
    CHECK_FALSE(CarouselSession::parse("game=4\n", s));            // no version
    CHECK_FALSE(CarouselSession::parse("version=2\ngame=4\n", s)); // another version
    CHECK_FALSE(CarouselSession::parse("version=1\ngame=x\n", s));
    CHECK_FALSE(CarouselSession::parse("version=1\ngame=-3\n", s));
    CHECK_FALSE(CarouselSession::parse("version=1\nset=9\n", s)); // past the last set
    CHECK_FALSE(CarouselSession::parse("version=1\nps1=9\n", s));
    CHECK_FALSE(CarouselSession::parse("version=1\nappcategory=9\n", s));
    checkSame(s, keep);
}

TEST_CASE("CarouselSession: take reads the file once, then it is gone") {
    TempDir tmp("carouselsession");
    const string path = tmp.path() + "/carousel.session";
    CHECK(CarouselSession::save(path, sample()));

    GameSetSelection back;
    CHECK(CarouselSession::take(path, back));
    checkSame(back, sample());
    CHECK_FALSE(ableem::DirEntry::exists(path));

    GameSetSelection again;
    CHECK_FALSE(CarouselSession::take(path, again)); // nothing left for a second start
}

TEST_CASE("CarouselSession: take removes a file it cannot use") {
    TempDir tmp("carouselsession");
    tmp.writeFile("carousel.session", "version=7\n");
    const string path = tmp.path() + "/carousel.session";
    GameSetSelection s;
    CHECK_FALSE(CarouselSession::take(path, s));
    CHECK_FALSE(ableem::DirEntry::exists(path));
}
