#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace http {

#ifdef _WIN32
using socket_t = std::uintptr_t;
inline constexpr socket_t kNoSocket = static_cast<socket_t>(-1);
#else
using socket_t = int;
inline constexpr socket_t kNoSocket = -1;
#endif

struct Request {
    std::string method;
    std::string path;
    std::string query;
    std::string body;

    std::vector<std::pair<std::string, std::string>> headers;

    std::string client_ip;

    std::string param(const std::string &key) const;
    std::string header(const std::string &name) const;
    std::string cookie(const std::string &name) const;
};

struct Response {
    int status = 200;
    std::string content_type = "text/plain; charset=utf-8";
    std::string cache_control = "no-store";
    std::string body;
    std::vector<std::string> extra_headers;
};

using Handler = std::function<Response(const Request &)>;

struct Options {
    int port = 7070;
    bool loopback_only = true;
    int workers = 16;
    std::size_t max_queued = 256;
    int backlog = 128;
    std::string client_ip_header;
    std::string allow_origin;
};

class Server {
  public:
    Server(Options opts, Handler handler);
    ~Server();

    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    bool start();
    void stop();

    int port() const { return opts_.port; }

  private:
    void accept_loop();
    void worker_loop();
    void serve(socket_t fd, const std::string &peer);

    Options opts_;
    Handler handler_;
    socket_t listen_fd_ = kNoSocket;
    std::atomic<bool> running_{false};
    std::thread acceptor_;
    std::vector<std::thread> workers_;

    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<std::pair<socket_t, std::string>> queue_;
};

} // namespace http
