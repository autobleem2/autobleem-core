//
// Ports for the tests that run a real LanServer / HttpServer on 127.0.0.1.
//
// A port taken from the clock collides with another test process when ctest runs the suites in parallel ("port N is
// in use or not allowed"). Pick a random port of 20000-39999 and try the next random one when the bind is refused.
//
#pragma once

#include <ableem/lanserver/http_server.h>
#include <ableem/lanserver/lan_server.h>

#include <memory>
#include <random>
#include <string>

namespace testsupport {

constexpr int portAttempts = 25;

inline int randomPort() {
    static std::mt19937 gen{std::random_device{}()};
    return 20000 + static_cast<int>(gen() % 20000);
}

// listens on a free port, which is left in `port`
inline bool listenOnFreePort(ableem::HttpServer &server, int &port, std::string &error,
                             const std::string &bindAddress) {
    for (int attempt = 0; attempt < portAttempts; ++attempt) {
        port = randomPort();
        if (server.listen(port, error, bindAddress))
            return true;
    }
    return false;
}

// a LanServer started on a free port, which is left in `config.port`; null (and `error`) when none could be bound
inline std::unique_ptr<ableem::LanServer> startOnFreePort(ableem::LanServer::Config &config, std::string &error) {
    for (int attempt = 0; attempt < portAttempts; ++attempt) {
        config.port = randomPort();
        auto server = std::make_unique<ableem::LanServer>(config);
        if (server->start(error))
            return server;
    }
    return nullptr;
}

} // namespace testsupport
