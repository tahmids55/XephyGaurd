#include "xg/http.hpp"

#include <cstring>
#include <sstream>

#include "xg/util.hpp"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif

namespace xg::net {

bool parse_http_response(const std::string& raw, HttpResponse& out, size_t* consumed) {
    const size_t hdr_end = raw.find("\r\n\r\n");
    if (hdr_end == std::string::npos) return false;
    std::istringstream hs(raw.substr(0, hdr_end));
    std::string line;
    std::getline(hs, line);
    // HTTP/1.1 200 OK
    auto sp = line.find(' ');
    if (sp == std::string::npos) return false;
    out.status = std::atoi(line.c_str() + sp + 1);
    out.headers.clear();
    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto c = line.find(':');
        if (c == std::string::npos) continue;
        out.headers[to_lower(trim(line.substr(0, c)))] = trim(line.substr(c + 1));
    }
    size_t pos = hdr_end + 4;
    auto te = out.headers.find("transfer-encoding");
    if (te != out.headers.end() && icontains(te->second, "chunked")) {
        std::string body;
        while (true) {
            size_t le = raw.find("\r\n", pos);
            if (le == std::string::npos) return false;
            size_t len = std::stoul(raw.substr(pos, le - pos), nullptr, 16);
            pos = le + 2;
            if (len == 0) {
                // Skip optional trailers up to the final CRLF.
                size_t end = raw.find("\r\n", pos);
                if (end == std::string::npos) return false;
                while (end != pos) {
                    pos = end + 2;
                    end = raw.find("\r\n", pos);
                    if (end == std::string::npos) return false;
                }
                pos += 2;
                break;
            }
            if (raw.size() < pos + len + 2) return false;
            body.append(raw, pos, len);
            pos += len + 2;
        }
        out.body = std::move(body);
    } else if (auto cl = out.headers.find("content-length"); cl != out.headers.end()) {
        size_t len = std::stoul(cl->second);
        if (raw.size() < pos + len) return false;
        out.body = raw.substr(pos, len);
        pos += len;
    } else if (out.status == 204 || out.status == 304 || (out.status >= 100 && out.status < 200)) {
        out.body.clear();
    } else {
        return false;  // read until close; caller handles
    }
    if (consumed) *consumed = pos;
    return true;
}

HttpsClient::HttpsClient(std::string host, uint16_t port, TlsOptions opts)
    : host_(std::move(host)), port_(port), opts_(std::move(opts)) {}
HttpsClient::~HttpsClient() = default;

void HttpsClient::reset() { conn_.reset(); }

HttpResponse HttpsClient::request(const std::string& method, const std::string& path, const std::string& body,
                                  const std::map<std::string, std::string>& headers) {
    // Retry once on a stale keep-alive connection.
    const bool reused = conn_ && conn_->is_open();
    HttpResponse r = do_request(method, path, body, headers);
    if (r.status == 0 && reused) {
        conn_.reset();
        r = do_request(method, path, body, headers);
    }
    return r;
}

HttpResponse HttpsClient::do_request(const std::string& method, const std::string& path, const std::string& body,
                                     const std::map<std::string, std::string>& headers) {
    HttpResponse resp;
    if (!conn_ || !conn_->is_open()) {
        conn_ = std::make_unique<TlsConnection>();
        if (!conn_->connect(host_, port_, opts_, &resp.error)) {
            conn_.reset();
            return resp;
        }
    }
    std::ostringstream req;
    req << method << ' ' << path << " HTTP/1.1\r\n"
        << "Host: " << host_ << ':' << port_ << "\r\n"
        << "User-Agent: xephyguard-agent/" << XG_VERSION << "\r\n"
        << "Accept: application/json\r\n"
        << "Connection: keep-alive\r\n";
    if (!bearer_.empty()) req << "Authorization: Bearer " << bearer_ << "\r\n";
    for (const auto& [k, v] : headers) req << k << ": " << v << "\r\n";
    if (!body.empty() || method == "POST" || method == "PUT") {
        if (!headers.contains("Content-Type")) req << "Content-Type: application/json\r\n";
        req << "Content-Length: " << body.size() << "\r\n";
    }
    req << "\r\n" << body;
    const std::string s = req.str();
    if (!conn_->write_all(s.data(), s.size())) {
        resp.error = "write failed";
        conn_.reset();
        return resp;
    }
    std::string raw;
    char buf[16384];
    size_t consumed = 0;
    while (true) {
        if (parse_http_response(raw, resp, &consumed)) break;
        long n = conn_->read_some(buf, sizeof buf);
        if (n <= 0) {
            // Connection closed: accept "read until close" bodies.
            size_t he = raw.find("\r\n\r\n");
            if (n == 0 && he != std::string::npos) {
                HttpResponse tmp;
                std::string patched = raw;
                // Parse headers via a synthetic content-length.
                size_t blen = raw.size() - (he + 4);
                patched.insert(he, "\r\nContent-Length: " + std::to_string(blen));
                if (parse_http_response(patched, tmp, nullptr)) { resp = tmp; conn_.reset(); return resp; }
            }
            resp = HttpResponse{};
            resp.error = n == 0 ? "connection closed" : "read failed/timeout";
            conn_.reset();
            return resp;
        }
        raw.append(buf, size_t(n));
        if (raw.size() > 32 * 1024 * 1024) { resp = HttpResponse{}; resp.error = "response too large"; conn_.reset(); return resp; }
    }
    if (auto c = resp.headers.find("connection"); c != resp.headers.end() && icontains(c->second, "close")) conn_.reset();
    return resp;
}

HttpResponse http_get_plain(const std::string& host, uint16_t port, const std::string& path, int timeout_ms) {
    HttpResponse resp;
    long long s = tcp_connect(host, port, timeout_ms, &resp.error);
    if (s < 0) return resp;
    std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\nUser-Agent: xephyguard-agent\r\n\r\n";
#ifdef _WIN32
    ::send(static_cast<SOCKET>(s), req.data(), int(req.size()), 0);
#else
    (void)!::write(int(s), req.data(), req.size());
#endif
    std::string raw;
    char buf[8192];
    while (true) {
#ifdef _WIN32
        int n = ::recv(static_cast<SOCKET>(s), buf, sizeof buf, 0);
#else
        long n = ::read(int(s), buf, sizeof buf);
#endif
        if (n <= 0) break;
        raw.append(buf, size_t(n));
        if (raw.size() > 4 * 1024 * 1024) break;
        size_t consumed;
        if (parse_http_response(raw, resp, &consumed)) { socket_close(s); return resp; }
    }
    socket_close(s);
    size_t he = raw.find("\r\n\r\n");
    if (he != std::string::npos) {
        raw.insert(he, "\r\nContent-Length: " + std::to_string(raw.size() - he - 4));
        if (parse_http_response(raw, resp, nullptr)) return resp;
    }
    resp = HttpResponse{};
    resp.error = "no HTTP response";
    return resp;
}

}  // namespace xg::net
