#include "net/http.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace http {

namespace {

constexpr std::size_t kMaxHeaderBytes = 1u << 16;
constexpr std::size_t kMaxBodyBytes = 1u << 22;

std::string url_decode(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < s.size()) {
            auto hex = [](char h) -> int {
                if (h >= '0' && h <= '9')
                    return h - '0';
                if (h >= 'a' && h <= 'f')
                    return h - 'a' + 10;
                if (h >= 'A' && h <= 'F')
                    return h - 'A' + 10;
                return -1;
            };
            int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>(hi * 16 + lo);
                i += 2;
            } else {
                out += c;
            }
        } else {
            out += c;
        }
    }
    return out;
}

const char *status_text(int code) {
    switch (code) {
    case 200:
        return "OK";
    case 204:
        return "No Content";
    case 400:
        return "Bad Request";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 413:
        return "Payload Too Large";
    case 429:
        return "Too Many Requests";
    case 500:
        return "Internal Server Error";
    case 503:
        return "Service Unavailable";
    default:
        return "OK";
    }
}

bool send_all(int fd, const char *data, std::size_t n) {
    std::size_t sent = 0;
    while (sent < n) {
        ssize_t w = ::send(fd, data + sent, n - sent, MSG_NOSIGNAL);
        if (w <= 0) {
            if (w < 0 && errno == EINTR)
                continue;
            return false;
        }
        sent += static_cast<std::size_t>(w);
    }
    return true;
}

std::string trim(const std::string &s) {
    std::size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos)
        return "";
    std::size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
    for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void write_response(int fd, const Response &resp, const std::string &origin) {
    std::string head = "HTTP/1.1 ";
    head += std::to_string(resp.status);
    head += ' ';
    head += status_text(resp.status);
    head += "\r\nContent-Type: ";
    head += resp.content_type;
    head += "\r\nContent-Length: ";
    head += std::to_string(resp.body.size());
    head += "\r\nConnection: close\r\n";
    if (!resp.cache_control.empty()) {
        head += "Cache-Control: ";
        head += resp.cache_control;
        head += "\r\n";
    }
    if (!origin.empty()) {
        head +=
            "Access-Control-Allow-Origin: " + origin +
            "\r\nAccess-Control-Allow-Credentials: true\r\nVary: Origin\r\n";
    }
    for (const std::string &h : resp.extra_headers) {
        if (h.find('\r') != std::string::npos ||
            h.find('\n') != std::string::npos)
            continue;
        head += h;
        head += "\r\n";
    }
    head += "\r\n";
    if (send_all(fd, head.data(), head.size()))
        send_all(fd, resp.body.data(), resp.body.size());
}

} // namespace

std::string Request::param(const std::string &key) const {
    std::size_t i = 0;
    while (i < query.size()) {
        std::size_t amp = query.find('&', i);
        if (amp == std::string::npos)
            amp = query.size();
        std::size_t eq = query.find('=', i);
        if (eq != std::string::npos && eq < amp) {
            if (query.compare(i, eq - i, key) == 0)
                return url_decode(query.substr(eq + 1, amp - eq - 1));
        } else if (amp - i == key.size() &&
                   query.compare(i, amp - i, key) == 0) {
            return "";
        }
        i = amp + 1;
    }
    return "";
}

std::string Request::header(const std::string &name) const {
    const std::string want = lower(name);
    for (const auto &[k, v] : headers)
        if (k == want)
            return v;
    return "";
}

std::string Request::cookie(const std::string &name) const {
    const std::string jar = header("cookie");
    std::size_t i = 0;
    while (i < jar.size()) {
        std::size_t semi = jar.find(';', i);
        if (semi == std::string::npos)
            semi = jar.size();
        const std::string pair = trim(jar.substr(i, semi - i));
        const std::size_t eq = pair.find('=');
        if (eq != std::string::npos && pair.compare(0, eq, name) == 0)
            return pair.substr(eq + 1);
        i = semi + 1;
    }
    return "";
}

Server::Server(Options opts, Handler handler)
    : opts_(std::move(opts)), handler_(std::move(handler)) {
    if (opts_.workers < 1)
        opts_.workers = 1;
}

Server::~Server() { stop(); }

bool Server::start() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
        return false;

    int yes = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(opts_.port));
    addr.sin_addr.s_addr =
        htonl(opts_.loopback_only ? INADDR_LOOPBACK : INADDR_ANY);

    if (::bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof addr) <
        0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    if (::listen(listen_fd_, opts_.backlog) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    running_ = true;
    workers_.reserve(static_cast<std::size_t>(opts_.workers));
    for (int i = 0; i < opts_.workers; ++i)
        workers_.emplace_back([this] { worker_loop(); });
    acceptor_ = std::thread([this] { accept_loop(); });
    return true;
}

void Server::stop() {
    if (!running_.exchange(false))
        return;
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
    }
    {
        std::lock_guard<std::mutex> lk(mtx_);
    }
    cv_.notify_all();
    if (acceptor_.joinable())
        acceptor_.join();
    for (std::thread &t : workers_)
        if (t.joinable())
            t.join();
    workers_.clear();
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto &[fd, peer] : queue_)
        ::close(fd);
    queue_.clear();
    listen_fd_ = -1;
}

void Server::accept_loop() {
    while (running_.load()) {
        sockaddr_in peer{};
        socklen_t plen = sizeof peer;
        int fd =
            ::accept(listen_fd_, reinterpret_cast<sockaddr *>(&peer), &plen);
        if (fd < 0) {
            if (!running_.load())
                break;
            if (errno == EINTR || errno == ECONNABORTED)
                continue;
            break;
        }

        char ipbuf[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, &peer.sin_addr, ipbuf, sizeof ipbuf);

        timeval tv{5, 0};
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);

        bool overloaded = false;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (queue_.size() >= opts_.max_queued) {
                overloaded = true;
            } else {
                queue_.emplace_back(fd, std::string(ipbuf));
            }
        }
        if (overloaded) {
            Response busy;
            busy.status = 503;
            busy.body = "server busy";
            write_response(fd, busy, opts_.allow_origin);
            ::close(fd);
            continue;
        }
        cv_.notify_one();
    }
}

void Server::worker_loop() {
    for (;;) {
        int fd = -1;
        std::string peer;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk,
                     [this] { return !queue_.empty() || !running_.load(); });
            if (queue_.empty()) {
                if (!running_.load())
                    return;
                continue;
            }
            std::tie(fd, peer) = queue_.front();
            queue_.pop_front();
        }
        serve(fd, peer);
        ::close(fd);
    }
}

void Server::serve(int fd, const std::string &peer) {
    std::string buf;
    char chunk[4096];
    std::size_t hdr_end;
    while ((hdr_end = buf.find("\r\n\r\n")) == std::string::npos &&
           buf.size() < kMaxHeaderBytes) {
        ssize_t r = ::recv(fd, chunk, sizeof chunk, 0);
        if (r <= 0)
            return;
        buf.append(chunk, static_cast<std::size_t>(r));
    }
    if (hdr_end == std::string::npos) {
        Response big;
        big.status = 413;
        big.body = "headers too large";
        write_response(fd, big, opts_.allow_origin);
        return;
    }

    const std::size_t line_end = buf.find("\r\n");
    const std::string line = buf.substr(0, line_end);

    const std::size_t sp1 = line.find(' ');
    const std::size_t sp2 =
        sp1 == std::string::npos ? std::string::npos : line.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) {
        Response bad;
        bad.status = 400;
        bad.body = "bad request";
        write_response(fd, bad, opts_.allow_origin);
        return;
    }

    Request req;
    req.method = line.substr(0, sp1);
    const std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    const std::size_t q = target.find('?');
    if (q == std::string::npos) {
        req.path = target;
    } else {
        req.path = target.substr(0, q);
        req.query = target.substr(q + 1);
    }

    std::size_t pos = line_end + 2;
    while (pos < hdr_end) {
        std::size_t eol = buf.find("\r\n", pos);
        if (eol == std::string::npos || eol > hdr_end)
            eol = hdr_end;
        const std::string h = buf.substr(pos, eol - pos);
        const std::size_t colon = h.find(':');
        if (colon != std::string::npos)
            req.headers.emplace_back(lower(trim(h.substr(0, colon))),
                                     trim(h.substr(colon + 1)));
        pos = eol + 2;
    }

    req.client_ip = peer;
    if (!opts_.client_ip_header.empty()) {
        const std::string v = req.header(opts_.client_ip_header);
        if (!v.empty()) {
            const std::size_t comma = v.find(',');
            const std::string first =
                trim(comma == std::string::npos ? v : v.substr(0, comma));
            if (!first.empty())
                req.client_ip = first;
        }
    }

    const std::string clen = req.header("content-length");
    if (!clen.empty()) {
        const std::size_t len = std::strtoul(clen.c_str(), nullptr, 10);
        if (len > kMaxBodyBytes) {
            Response big;
            big.status = 413;
            big.body = "request body too large";
            write_response(fd, big, opts_.allow_origin);
            return;
        }
        std::string body = buf.substr(hdr_end + 4);
        while (body.size() < len) {
            ssize_t r = ::recv(fd, chunk, sizeof chunk, 0);
            if (r <= 0)
                break;
            body.append(chunk, static_cast<std::size_t>(r));
        }
        if (body.size() > len)
            body.resize(len);
        req.body = std::move(body);
    }

    if (req.method == "OPTIONS") {
        Response pre;
        pre.status = 204;
        if (!opts_.allow_origin.empty()) {
            pre.extra_headers.push_back(
                "Access-Control-Allow-Methods: GET, POST, OPTIONS");
            pre.extra_headers.push_back(
                "Access-Control-Allow-Headers: Content-Type");
            pre.extra_headers.push_back("Access-Control-Max-Age: 86400");
        }
        write_response(fd, pre, opts_.allow_origin);
        return;
    }

    Response resp = handler_(req);
    write_response(fd, resp, opts_.allow_origin);
}

} // namespace http
