//
// LanClient - see the header.
//
#include <ableem/lanserver/lan_client.h>
#include <ableem/lanserver/lan_library.h>

#include <ableem/engine/filesystem.h>
#include <ableem/engine/log.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <json.h>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#define AB_CLOSE_SOCKET closesocket
#define AB_SEND_FLAGS 0
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#define AB_CLOSE_SOCKET close
#define AB_SEND_FLAGS MSG_NOSIGNAL
#endif

using namespace std;

namespace ableem {

namespace {

const int TimeoutSeconds = 30;

bool sendAll(int s, const char *data, size_t length) {
    while (length > 0) {
        const int n = static_cast<int>(send(s, data, static_cast<int>(min<size_t>(length, 1 << 20)), AB_SEND_FLAGS));
        if (n <= 0)
            return false;
        data += n;
        length -= static_cast<size_t>(n);
    }
    return true;
}

string plain(const string &text) { // a server's one-line answer, without its newline
    string t = text;
    while (!t.empty() && (t.back() == '\n' || t.back() == '\r'))
        t.pop_back();
    return t;
}

} // namespace

//*******************************
// LanClient::LanClient
//*******************************
LanClient::LanClient(const string &url, string token) : token_(std::move(token)) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    string rest = url;
    while (!rest.empty() && rest.front() == ' ')
        rest.erase(0, 1);
    while (!rest.empty() && rest.back() == ' ')
        rest.pop_back();
    if (rest.compare(0, 7, "http://") == 0)
        rest = rest.substr(7);
    else if (rest.find("://") != string::npos)
        return; // https and the rest: not what a LAN server speaks
    rest = rest.substr(0, rest.find('/'));
    const size_t colon = rest.rfind(':');
    host_ = rest.substr(0, colon);
    if (colon != string::npos) {
        port_ = atoi(rest.substr(colon + 1).c_str());
        if (port_ <= 0 || port_ > 65535)
            host_.clear();
    }
}

LanClient::~LanClient() {
#ifdef _WIN32
    WSACleanup();
#endif
}

string LanClient::baseUrl() const {
    return "http://" + host_ + ":" + to_string(port_);
}

string LanClient::query(const string &library, const string &more) const {
    string q = more;
    if (!library.empty())
        q += (q.empty() ? "" : "&") + string("library=") + LanLibrary::urlPath(library);
    return q.empty() ? "" : "?" + q;
}

//*******************************
// LanClient::request
//*******************************
LanClient::Reply LanClient::request(const string &method, const string &target, const string &body, const string &file,
                                    uint64_t offset, uint64_t length, const function<bool(uint64_t)> &sent) {
    Reply r;
    if (host_.empty()) {
        r.error = "not a server address (http://<address>:<port>)";
        return r;
    }
    addrinfo hints{}, *found = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host_.c_str(), to_string(port_).c_str(), &hints, &found) != 0 || found == nullptr) {
        r.error = "no such server: " + host_;
        return r;
    }
    const int s = static_cast<int>(socket(found->ai_family, found->ai_socktype, found->ai_protocol));
#ifdef _WIN32
    DWORD timeout = TimeoutSeconds * 1000;
#else
    timeval timeout{TimeoutSeconds, 0};
#endif
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
    const bool connected = s >= 0 && connect(s, found->ai_addr, static_cast<int>(found->ai_addrlen)) == 0;
    freeaddrinfo(found);
    if (!connected) {
        if (s >= 0)
            AB_CLOSE_SOCKET(s);
        r.error = "the server at " + baseUrl() + " does not answer - is it running?";
        return r;
    }

    const uint64_t contentLength = file.empty() ? body.size() : length;
    string head = method + " " + target + " HTTP/1.1\r\nHost: " + host_ + ":" + to_string(port_) +
                  "\r\nConnection: close\r\nContent-Length: " + to_string(contentLength) + "\r\n";
    if (!token_.empty())
        head += "X-AB-Token: " + token_ + "\r\n";
    head += "\r\n";
    bool ok = sendAll(s, head.data(), head.size());
    if (ok && file.empty()) {
        ok = sendAll(s, body.data(), body.size());
    } else if (ok) {
        ifstream in(file, ios::binary);
        in.seekg(static_cast<streamoff>(offset));
        vector<char> chunk(1 << 20);
        uint64_t done = 0;
        while (ok && done < length && in) {
            in.read(chunk.data(), static_cast<streamsize>(min<uint64_t>(chunk.size(), length - done)));
            const streamsize got = in.gcount();
            if (got <= 0)
                break;
            ok = sendAll(s, chunk.data(), static_cast<size_t>(got));
            done += static_cast<uint64_t>(got);
            if (ok && sent && !sent(done)) {
                AB_CLOSE_SOCKET(s);
                r.error = "stopped";
                return r;
            }
        }
        ok = ok && done == length;
    }
    // the reply, whole: the server closes the connection after it
    string reply;
    char buffer[16384];
    int got;
    while ((got = static_cast<int>(recv(s, buffer, sizeof(buffer), 0))) > 0)
        reply.append(buffer, static_cast<size_t>(got));
    AB_CLOSE_SOCKET(s);
    if (reply.compare(0, 9, "HTTP/1.1 ") != 0) {
        r.error = ok ? "the server's answer made no sense" : "the connection to the server broke";
        return r;
    }
    r.status = atoi(reply.c_str() + 9);
    const size_t at = reply.find("\r\n\r\n");
    r.body = at == string::npos ? "" : reply.substr(at + 4);
    return r;
}

//*******************************
// LanClient::status / rescan
//*******************************
LanClient::Status LanClient::status() {
    using nlohmann::json;
    Status s;
    const Reply r = request("GET", "/status.json");
    if (r.status != 200) {
        s.error = r.status == 0 ? r.error
                                : "that is not an AutoBleem LAN server (it answered " + to_string(r.status) +
                                      " for /status.json - an abstored older than LAN Share?)";
        return s;
    }
    const json j = json::parse(r.body, nullptr, false);
    if (!j.is_object() || !j.contains("schema")) {
        s.error = "that is not an AutoBleem LAN server (no status at /status.json)";
        return s;
    }
    s.name = j.value("name", "");
    s.version = j.value("version", "");
    s.uploads = j.value("uploads", false);
    s.hashing = j.value("hashing", false);
    if (j.contains("games") && j["games"].is_array())
        for (const json &g : j["games"])
            s.games.push_back({g.value("id", ""), g.value("title", ""), g.value("serial", ""),
                               g.value("size", static_cast<uint64_t>(0)), g.value("discs", 0)});
    if (j.contains("problems") && j["problems"].is_array())
        for (const json &p : j["problems"])
            s.problems.push_back({p.value("path", ""), p.value("what", ""), p.value("error", true)});
    if (j.contains("libraries") && j["libraries"].is_array())
        for (const json &l : j["libraries"])
            s.libraries.push_back({l.value("name", ""), l.value("free", static_cast<uint64_t>(0))});
    s.ok = true;
    return s;
}

bool LanClient::rescan(string &error) {
    const Reply r = request("GET", "/rescan");
    if (r.status == 200 || r.status == 303)
        return true;
    error = r.status == 0 ? r.error : "the server refused a rescan (" + to_string(r.status) + ")";
    return false;
}

//*******************************
// LanClient::upload / commit / drop
//*******************************
bool LanClient::upload(const string &gameFolder, const string &localFile, const string &name, const string &library,
                       const function<bool(uint64_t, uint64_t)> &progress, string &error) {
    const long long size = DirEntry::fileSize(localFile);
    if (size < 0) {
        error = "cannot read " + localFile;
        return false;
    }
    const uint64_t total = static_cast<uint64_t>(size);
    const string target = "/upload/" + LanLibrary::urlPath(gameFolder) + "/" + LanLibrary::urlPath(name);
    // what the server has of it already
    Reply have = request("GET", target + query(library));
    if (have.status != 200) {
        error = have.status == 0 ? have.error : plain(have.body);
        return false;
    }
    uint64_t offset = strtoull(have.body.c_str(), nullptr, 10);
    if (offset > total)
        offset = 0; // a different file of that name was staged: start it again
    if (offset == total && total > 0) {
        if (progress)
            progress(total, total);
        return true;
    }
    const Reply r = request("PUT", target + query(library, "offset=" + to_string(offset)), "", localFile, offset,
                            total - offset, [&](uint64_t sent) { return !progress || progress(offset + sent, total); });
    if (r.status == 200)
        return true;
    error = r.status == 0 ? r.error : plain(r.body);
    return false;
}

bool LanClient::commit(const string &gameFolder, const string &library, string &finalName, string &error) {
    const Reply r = request("POST", "/upload/" + LanLibrary::urlPath(gameFolder) + query(library, "commit"));
    if (r.status == 200) {
        finalName = plain(r.body);
        return true;
    }
    error = r.status == 0 ? r.error : plain(r.body);
    return false;
}

bool LanClient::drop(const string &gameFolder, const string &library, string &error) {
    const Reply r = request("DELETE", "/upload/" + LanLibrary::urlPath(gameFolder) + query(library));
    if (r.status == 200)
        return true;
    error = r.status == 0 ? r.error : plain(r.body);
    return false;
}

bool LanClient::remove(const string &gameId, string &error) {
    const Reply r = request("DELETE", "/games/" + LanLibrary::urlPath(gameId));
    if (r.status == 200)
        return true;
    error = r.status == 0 ? r.error : plain(r.body);
    return false;
}

} // namespace ableem
