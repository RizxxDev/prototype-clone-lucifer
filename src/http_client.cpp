#include "http_client.hpp"
#include "logger.hpp"
#include <nlohmann/json.hpp>
#include <httplib.h>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

static std::string winHttpPost(const std::string& host_str, int port, const std::string& path_str,
                              const std::string& body, bool is_https, int& out_status,
                              const Socks5Config& proxy = {}) {
    out_status = 0;
    std::wstring w_host(host_str.begin(), host_str.end());
    std::wstring w_path(path_str.begin(), path_str.end());

    HINTERNET hSession = NULL;
    if (proxy.isValid()) {
        std::wstring w_proxy = L"socks=" + std::wstring(proxy.host.begin(), proxy.host.end()) + L":" + std::to_wstring(proxy.port);
        hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
                               WINHTTP_ACCESS_TYPE_NAMED_PROXY,
                               w_proxy.c_str(), WINHTTP_NO_PROXY_BYPASS, 0);
    } else {
        hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
                               WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!hSession) return "";

    HINTERNET hConnect = WinHttpConnect(hSession, w_host.c_str(), (INTERNET_PORT)port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }

    DWORD flags = is_https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", w_path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }

    if (proxy.isValid() && !proxy.username.empty()) {
        std::wstring w_user(proxy.username.begin(), proxy.username.end());
        std::wstring w_pass(proxy.password.begin(), proxy.password.end());
        WinHttpSetOption(hRequest, WINHTTP_OPTION_PROXY_USERNAME, (LPVOID)w_user.c_str(), static_cast<DWORD>(w_user.size() * sizeof(wchar_t)));
        WinHttpSetOption(hRequest, WINHTTP_OPTION_PROXY_PASSWORD, (LPVOID)w_pass.c_str(), static_cast<DWORD>(w_pass.size() * sizeof(wchar_t)));
    }

    DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));

    LPCWSTR headers = L"Content-Type: application/x-www-form-urlencoded\r\n";
    std::string response;
    if (WinHttpSendRequest(hRequest, headers, (DWORD)-1L, (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0)) {
        if (WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD statusCode = 0;
            DWORD size = sizeof(statusCode);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
            out_status = (int)statusCode;

            DWORD bytesRead = 0;
            char buf[4096];
            while (WinHttpReadData(hRequest, buf, sizeof(buf), &bytesRead) && bytesRead > 0) {
                response.append(buf, bytesRead);
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return response;
}

static std::string base64Encode(const std::string& in) {
    static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(b64[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(b64[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}
#endif

using json = nlohmann::json;

HttpClient::HttpClient(const std::string& base_url, bool use_https, const Socks5Config& proxy)
    : base_url_(base_url), use_https_(use_https), proxy_(proxy) {}

LoginResponse HttpClient::post(
        const std::string& path,
        const std::string& body,
        const std::map<std::string, std::string>& headers) {

    LoginResponse resp;

    try {
        // Parse base_url untuk ambil host & port
        std::string host = base_url_;
        std::string scheme = use_https_ ? "https://" : "http://";

        if (host.rfind(scheme, 0) == 0) host = host.substr(scheme.size());
        else if (host.rfind("http://", 0) == 0) host = host.substr(7);
        else if (host.rfind("https://", 0) == 0) host = host.substr(8);

        // Buang trailing slash
        while (!host.empty() && host.back() == '/') host.pop_back();

        int target_port = use_https_ ? 443 : 80;
        std::string target_host = host;
        size_t cpos = host.find(':');
        if (cpos != std::string::npos) {
            target_host = host.substr(0, cpos);
            try { target_port = std::stoi(host.substr(cpos + 1)); } catch (...) {}
        }

        // Jika SOCKS5 diaktifkan dan plain HTTP: Gunakan koneksi SOCKS5 CONNECT langsung
        if (proxy_.isValid() && !use_https_) {
            Logger::logDebug("[HTTP-SOCKS5] Mengirim POST melalui proxy ", proxy_.toString(), " ke ", target_host, ":", target_port);
            std::string err;
            SOCKET sock = Socks5Client::createConnectedSocket(target_host, static_cast<uint16_t>(target_port), proxy_, 8000, err);
            if (sock == INVALID_SOCKET) {
                resp.error = "Gagal terhubung via proxy SOCKS5: " + err;
                Logger::log("[HTTP] ❌ ", resp.error);
                return resp;
            }

            std::string req = "POST " + path + " HTTP/1.1\r\n";
            req += "Host: " + host + "\r\n";
            req += "User-Agent: GTPSBot/1.0\r\n";
            req += "Content-Type: application/x-www-form-urlencoded\r\n";
            req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
            req += "Connection: close\r\n";
            for (const auto& [k, v] : headers) {
                req += k + ": " + v + "\r\n";
            }
            req += "\r\n";
            req += body;

            send(sock, req.data(), static_cast<int>(req.size()), 0);

            std::string raw_resp;
            char buf[4096];
            while (true) {
                int r = recv(sock, buf, sizeof(buf), 0);
                if (r <= 0) break;
                raw_resp.append(buf, r);
            }
            closesocket(sock);

            size_t header_end = raw_resp.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                std::string header_part = raw_resp.substr(0, header_end);
                resp.body = raw_resp.substr(header_end + 4);

                size_t first_line_end = header_part.find("\r\n");
                std::string status_line = (first_line_end != std::string::npos) ? header_part.substr(0, first_line_end) : header_part;
                std::stringstream ss(status_line);
                std::string http_ver;
                int status_code = 0;
                ss >> http_ver >> status_code;
                resp.http_status = status_code;
                if (status_code >= 200 && status_code < 300) {
                    resp.success = true;
                } else {
                    resp.error = "HTTP " + std::to_string(status_code);
                }
            } else {
                resp.error = "Respon HTTP tidak valid dari server via SOCKS5";
            }
            return resp;
        }

#ifdef _WIN32
        if (proxy_.isValid() && use_https_) {
            int status = 0;
            std::string rbody = winHttpPost(target_host, target_port, path, body, true, status, proxy_);
            resp.http_status = status;
            resp.body = rbody;
            if (status >= 200 && status < 300) {
                resp.success = true;
            } else {
                resp.error = "HTTPS via SOCKS5 status " + std::to_string(status);
            }
            return resp;
        }
#endif

        httplib::Client cli(scheme + host);
        cli.set_connection_timeout(10, 0);
        cli.set_read_timeout(15, 0);

        httplib::Headers h;
        h.emplace("User-Agent", "GTPSBot/1.0");
        h.emplace("Content-Type", "application/x-www-form-urlencoded");
        for (auto& [k, v] : headers) h.emplace(k, v);

        Logger::logDebug("[HTTP] POST ", scheme, host, path);

        auto res = cli.Post(path.c_str(), h, body,
                            "application/x-www-form-urlencoded");

        if (!res) {
            resp.error = "No response from server";
            Logger::log("[HTTP] Gagal konek: ", resp.error);
            return resp;
        }

        resp.http_status = res->status;
        resp.body = res->body;

        Logger::logDebug("[HTTP] Status: ", res->status,
                         " | Body: ", res->body.substr(0, 200));

        if (res->status >= 200 && res->status < 300) {
            resp.success = true;
        } else {
            resp.error = "HTTP " + std::to_string(res->status);
        }

    } catch (const std::exception& e) {
        resp.error = e.what();
        Logger::log("[HTTP] Exception: ", resp.error);
    }

    return resp;
}

LoginResponse HttpClient::login(
        const std::string& growid,
        const std::string& password,
        const std::string& version,
        const std::string& platform) {

    LoginResponse resp;
    resp.raw_json = "";

    // Format login Growtopia (form-urlencoded)
    std::string body =
        "growId=" + growid +
        "&password=" + password +
        "&version=" + version +
        "&platform=" + platform +
        "&protocol=177";

    Logger::log("[LOGIN] Mencoba login sebagai: ", growid, (proxy_.isValid() ? (" via Proxy " + proxy_.toString()) : " (Direct)"));

    // POST ke login URL
    std::string host = base_url_;
    std::string path = "/api/login";
    // Pisah host dari path (kalau ada)
    auto slash = host.find('/', host.find("://") + 3);
    if (slash != std::string::npos) {
        path = host.substr(slash);
        host = host.substr(0, slash);
    }

    // Reconstruct dengan proxy yang sama
    HttpClient tmp(host, use_https_, proxy_);
    resp = tmp.post(path, body);

    if (!resp.success) {
        Logger::log("[LOGIN] Gagal: ", resp.error);
        return resp;
    }

    // 1. Coba parse format pipe-delimited standar Growtopia (server_data.php)
    if (resp.body.find('|') != std::string::npos) {
        std::istringstream stream(resp.body);
        std::string line;
        while (std::getline(stream, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
                line.pop_back();
            }
            auto pipe_pos = line.find('|');
            if (pipe_pos != std::string::npos) {
                std::string k = line.substr(0, pipe_pos);
                std::string v = line.substr(pipe_pos + 1);
                resp.kv[k] = v;

                if (k == "server") {
                    resp.server_ip = v;
                } else if (k == "port") {
                    try { resp.server_port = std::stoi(v); } catch (...) {}
                } else if (k == "meta") {
                    resp.meta = v;
                } else if (k == "loginurl") {
                    resp.loginurl = v;
                } else if (k == "token" || k == "ltoken") {
                    resp.token = v;
                }
            }
        }

        if (!resp.server_ip.empty()) {
            Logger::log("[LOGIN] Server Data: ", resp.server_ip, ":", resp.server_port, (!resp.meta.empty() ? (" | " + resp.meta) : ""));
        }

        // 1b. Jika server menyediakan loginurl (seperti login.creativeps.eu), validasi akun via Web API untuk mendapatkan ltoken
        if (!resp.loginurl.empty() && resp.token.empty()) {
            Logger::log("[LOGIN] Web Validate via: ", resp.loginurl);
            std::string dash_host = resp.loginurl;
            auto ppos = dash_host.find("://");
            if (ppos != std::string::npos) dash_host = dash_host.substr(ppos + 3);
            while (!dash_host.empty() && dash_host.back() == '/') dash_host.pop_back();

            std::string token_raw = "f=1&protocol=225&game_version=" + version;
            if (!resp.meta.empty()) {
                token_raw += "&meta=" + resp.meta;
            }
            std::string b64_token = base64Encode(token_raw);
            std::string val_body = "_token=" + b64_token + "&growId=" + growid + "&password=" + password;
            int status = 0;
#ifdef _WIN32
            std::string val_res = winHttpPost(dash_host, 443, "/player/growid/login/validate", val_body, true, status, proxy_);
            if (status >= 200 && status < 300 && !val_res.empty()) {
                try {
                    auto j = json::parse(val_res);
                    if (j.value("status", "") == "success") {
                        resp.token = j.value("token", "");
                        Logger::log("[LOGIN] ✅ Web Validate Berhasil! Token login diperoleh.");
                    } else {
                        Logger::log("[LOGIN] ⚠️ Web Validate response: ", j.value("message", ""));
                    }
                } catch (const std::exception& e) {
                    Logger::log("[LOGIN] Gagal parse JSON validate: ", e.what());
                }
            } else {
                Logger::log("[LOGIN] Web Validate HTTP status: ", status);
            }
#endif
        }
    }

    // 2. Coba parse JSON response (jika GTPS menggunakan custom REST API)
    if (resp.server_ip.empty()) {
        try {
            auto j = json::parse(resp.body);
            resp.raw_json = resp.body;

            if (j.contains("token"))       resp.token = j["token"];
            else if (j.contains("ltoken")) resp.token = j["ltoken"];

            if (j.contains("server"))      resp.server_ip = j["server"];
            else if (j.contains("ip"))     resp.server_ip = j["ip"];

            if (j.contains("port"))        resp.server_port = j["port"];

            Logger::log("[LOGIN] Token: ",
                        resp.token.empty() ? "(kosong)" : resp.token.substr(0, 16) + "...");
            Logger::log("[LOGIN] Server: ", resp.server_ip, ":",
                        resp.server_port);

        } catch (const std::exception& e) {
            // Bukan JSON dan bukan error fatal jika sudah terisi dari pipe
        }
    }

    return resp;
}

static std::string s_cached_direct_ip = "182.9.36.244";
static std::atomic<bool> s_refreshing_direct_ip{false};

std::string HttpClient::getCachedDirectIp() {
    refreshDirectIpAsync();
    return s_cached_direct_ip;
}

void HttpClient::refreshDirectIpAsync() {
    if (s_refreshing_direct_ip.exchange(true)) return;
    std::thread([]() {
        std::string ip = fetchPublicIp();
        if (!ip.empty()) {
            s_cached_direct_ip = ip;
        }
        s_refreshing_direct_ip = false;
    }).detach();
}

std::string HttpClient::fetchPublicIp(const Socks5Config& proxy, int timeout_ms) {
    if (proxy.isValid()) {
        std::string err;
        SOCKET sock = Socks5Client::createConnectedSocket("api.ipify.org", 80, proxy, timeout_ms, err);
        if (sock == INVALID_SOCKET) {
            sock = Socks5Client::createConnectedSocket("icanhazip.com", 80, proxy, timeout_ms, err);
        }
        if (sock != INVALID_SOCKET) {
            std::string req = "GET / HTTP/1.1\r\nHost: api.ipify.org\r\nUser-Agent: curl/7.84.0\r\nConnection: close\r\n\r\n";
            send(sock, req.data(), static_cast<int>(req.size()), 0);
            std::string raw;
            char buf[1024];
            while (true) {
                int r = recv(sock, buf, sizeof(buf) - 1, 0);
                if (r <= 0) break;
                raw.append(buf, r);
            }
            closesocket(sock);
            size_t pos = raw.find("\r\n\r\n");
            if (pos != std::string::npos) {
                std::string body = raw.substr(pos + 4);
                while (!body.empty() && (body.back() == '\r' || body.back() == '\n' || body.back() == ' ')) body.pop_back();
                while (!body.empty() && body.front() == ' ') body.erase(body.begin());
                if (!body.empty() && body.find('.') != std::string::npos) {
                    return body;
                }
            }
        }
        return proxy.host;
    }

    // Direct mode
    try {
        httplib::Client cli("http://api.ipify.org");
        cli.set_connection_timeout(timeout_ms / 1000 > 0 ? timeout_ms / 1000 : 2, 0);
        cli.set_read_timeout(timeout_ms / 1000 > 0 ? timeout_ms / 1000 : 2, 0);
        auto res = cli.Get("/");
        if (res && res->status == 200 && !res->body.empty()) {
            std::string ip = res->body;
            while (!ip.empty() && (ip.back() == '\r' || ip.back() == '\n' || ip.back() == ' ')) ip.pop_back();
            while (!ip.empty() && ip.front() == ' ') ip.erase(ip.begin());
            if (ip.find('.') != std::string::npos) return ip;
        }
    } catch (...) {}

    try {
        httplib::Client cli("http://icanhazip.com");
        cli.set_connection_timeout(timeout_ms / 1000 > 0 ? timeout_ms / 1000 : 2, 0);
        cli.set_read_timeout(timeout_ms / 1000 > 0 ? timeout_ms / 1000 : 2, 0);
        auto res = cli.Get("/");
        if (res && res->status == 200 && !res->body.empty()) {
            std::string ip = res->body;
            while (!ip.empty() && (ip.back() == '\r' || ip.back() == '\n' || ip.back() == ' ')) ip.pop_back();
            while (!ip.empty() && ip.front() == ' ') ip.erase(ip.begin());
            if (ip.find('.') != std::string::npos) return ip;
        }
    } catch (...) {}

    return s_cached_direct_ip.empty() ? "182.9.36.244" : s_cached_direct_ip;
}