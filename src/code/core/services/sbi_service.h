//
// SbiService: the .sbi files of a LibCrypt disc, fetched from our site into the game's save folder before a disc is
// played from a USB CD drive (docs/disc-drive-plan.md, piece 10). The emulator reads <CdromId>.sbi (or
// <CdromId>.1.sbi, .2.sbi ... for a disc with several versions) from that folder to defeat the copy protection.
//
#pragma once

#include "downloader.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

//******************
// SbiService
//******************
// <site>/sbi/index.json = {"SCES-02835": ["SCES-02835.1.sbi", "SCES-02835.2.sbi"], ...}, the files at <site>/sbi/<file>.
// The index is fetched at most once per instance (a failed fetch is remembered too: no second wait in one run) and
// only by fetchFor(). Never throws; the downloader's own command timeout is the only wait.
class SbiService {
public:
    enum class Result {
        Fetched,     // at least one file was written
        Present,     // every file was there already
        NotLibCrypt, // the serial is not in the index (or is empty)
        Offline,     // the index could not be fetched
        Error        // the index is unreadable, a file failed or was not an .sbi, or the folder cannot be written
    };

    // siteUrl: "https://autobleem.retromenele.pl" (a trailing "/" is fine); scratchDir: where the index is
    // fetched to (made if missing; the file is removed after being read)
    SbiService(Downloader &downloader, std::string siteUrl, std::string scratchDir)
        : downloader_(downloader), siteUrl_(std::move(siteUrl)), scratchDir_(std::move(scratchDir)) {}

    // serial as DiscIdentity gives it ("SCES-02835"); saveFolder: the game's save-state folder (made only when
    // there is something to write). A file already there that starts with "SBI\0" is not fetched again (the index
    // carries no sizes to compare). A file is fetched to <name>.dl, checked, and renamed into place; one that is
    // not an .sbi is dropped.
    Result fetchFor(const std::string &serial, const std::string &saveFolder);

    // for a log line: "fetched", "present", "not a LibCrypt serial", "offline", "error"
    static const char *describe(Result result);

    // "SCES-02835" -> "SCES02835" (what the emulator calls CdromId)
    static std::string cdromId(const std::string &serial);
    // the files a serial's versions are saved as: one -> {<ID>.sbi}; several -> {<ID>.1.sbi, <ID>.2.sbi, ...}
    static std::vector<std::string> targetNames(const std::string &serial, size_t versions);
    // the index text read into serial -> file names; false when it is not that shape
    static bool parseIndex(const std::string &text, std::map<std::string, std::vector<std::string>> &index);
    // "SBI\0" at the start of the file
    static bool looksLikeSbi(const std::string &path);

private:
    enum class Index { Untried, Ok, Offline, Bad };
    Index loadIndex();

    Downloader &downloader_;
    std::string siteUrl_, scratchDir_;
    Index index_ = Index::Untried;
    std::map<std::string, std::vector<std::string>> entries_;
};
