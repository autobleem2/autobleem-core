//
// Publisher: one game put on a LAN server - LAN Share's "Publish" (pc-tools docs/lan-share-plan.md).
//   through the server's share, when one is given and reachable: the files copied into <share>/.uploading/
//     <game folder>/ (a dot folder, which the server never scans), moved into place under a free name, and a
//     rescan asked for - the server itself stays read only;
//   else over HTTP (LanClient): each file uploaded from what the server has staged already, then committed.
// Either way a game appears on the server whole or not at all.
//
#pragma once

#include <ableem/lanserver/lan_client.h>
#include <ableem/lanserver/lan_library.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ableem {

class Publisher {
public:
    struct File {
        std::string localPath; // where it is on this machine
        std::string name;      // what it is called in the game's folder on the server
    };
    struct Target {
        LanClient *client = nullptr; // the server (asked to rescan after a share copy; uploaded to otherwise)
        std::string shareDir;        // the server's games folder as this machine reaches it ("" = none)
        std::string library;         // the server's folder by name, when it serves several
    };
    struct Result {
        bool ok = false;
        std::string error;     // plain words, when !ok ("stopped" when progress said so)
        std::string folder;    // the folder the game became on the server
        bool viaShare = false; // copied to the share rather than uploaded
    };

    // progress(done, total) in bytes, over all the files; false stops (a share copy is removed, an upload
    // stays staged on the server for the next try)
    static Result publish(const std::vector<File> &files, const std::string &gameFolder, const Target &target,
                          const std::function<bool(uint64_t done, uint64_t total)> &progress);

    // what a game scanned here takes along: its discs, the files they name, the .sbi, its picture
    static std::vector<File> filesOf(const LanGame &game, const LanLibrary &library);
    // does the server have it already: by serial when both have one, else by title (any case)
    static bool serverHas(const LanClient::Status &status, const std::string &serial, const std::string &title);
    // a folder name any disk takes, from a title: what Windows refuses dropped (":" becomes " -"), no dot or
    // space at the end, "Game" when nothing is left
    static std::string folderNameFor(const std::string &title);
};

} // namespace ableem
