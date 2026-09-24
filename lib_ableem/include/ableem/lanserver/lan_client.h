//
// LanClient: the other end of a LanServer - what LAN Share (pc-tools) uses to manage an abstored on the home
// network. Plain HTTP/1.1 over a socket, one connection per request, as the server speaks it; no TLS (a home
// network, like the server). It reads /status.json, asks for a rescan, and uploads a game: each file PUT to
// /upload/<game folder>/<file> from what the server has staged already (a stopped upload goes on), then a
// commit. Publisher (publisher.h) puts a whole game on a server with it, or through the server's share.
//
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ableem {

class LanClient {
public:
    struct Game {
        std::string id, title, serial;
        uint64_t size = 0;
        int discs = 0;
    };
    struct Problem {
        std::string path, what;
        bool error = true;
    };
    struct Library {
        std::string name; // "" when the server serves one folder
        uint64_t free = 0;
    };
    struct Status {
        bool ok = false;
        std::string error; // plain words, when !ok
        std::string name, version;
        bool uploads = false, hashing = false;
        std::vector<Game> games;
        std::vector<Problem> problems;
        std::vector<Library> libraries;
    };

    // "http://192.168.68.144:8126" - "/store.tsv" or "/" after it is fine, as the Store's source URL is
    // what the user will have to hand; token: the server's upload token ("" to only read)
    explicit LanClient(const std::string &url, std::string token = "");
    ~LanClient();
    LanClient(const LanClient &) = delete;
    LanClient &operator=(const LanClient &) = delete;

    bool valid() const { return !host_.empty(); } // the address could be read
    std::string baseUrl() const;                  // "http://host:port"

    Status status();
    bool rescan(std::string &error);

    // one file into the server's staging for gameFolder, from what is staged there already; progress(sent,
    // total) is asked as it goes - false stops (what was sent stays staged)
    bool upload(const std::string &gameFolder, const std::string &localFile, const std::string &name,
                const std::string &library, const std::function<bool(uint64_t sent, uint64_t total)> &progress,
                std::string &error);
    // the staged folder into the games; finalName: the folder it became ("Game (2)" when "Game" was taken)
    bool commit(const std::string &gameFolder, const std::string &library, std::string &finalName, std::string &error);
    bool drop(const std::string &gameFolder, const std::string &library, std::string &error);

    // what an HTTP exchange came to
    struct Reply {
        int status = 0; // 0: no answer at all (error says why)
        std::string body, error;
    };
    // one request; the body from `file` (from `offset`, `length` bytes) when file is not empty, else `body`
    Reply request(const std::string &method, const std::string &target, const std::string &body = "",
                  const std::string &file = "", uint64_t offset = 0, uint64_t length = 0,
                  const std::function<bool(uint64_t sent)> &sent = nullptr);

private:
    std::string query(const std::string &library, const std::string &more = "") const;
    std::string host_;
    int port_ = 80;
    std::string token_;
};

} // namespace ableem
