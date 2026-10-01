#include "http_client.hpp"
#include "logger.hpp"
#include <nlohmann/json.hpp>
#include <httplib.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

static std::string winHttpPost(const std::string& host_str, int port, const std::string& path_str, const std::string& body, bool is_https, int& out_status) {
    out_status = 0;
    std::wstring w_host(host_str.begin(), host_str.end());
    std::wstring w_path(path_str.begin(), path_str.end());

    HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    HINTERNET hConnect = WinHttpConnect(hSession, w_host.c_str(), (INTERNET_PORT)port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }

    DWORD flags = is_https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", w_path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }

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

HttpClient::HttpClient(const std::string& base_url, bool use_https)
    : base_url_(base_url), use_https_(use_https) {}

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

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        httplib::Client cli(scheme + host);
#else
        httplib::Client cli(scheme + host);
#endif
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
    // Klien asli pakai field: growId, password, protocol, etc.
    // GTPS biasanya modifikasi, jadi kita coba format umum.
    std::string body =
        "growId=" + growid +
        "&password=" + password +
        "&version=" + version +
        "&platform=" + platform +
        "&protocol=177";

    Logger::log("[LOGIN] Mencoba login sebagai: ", growid);

    // POST ke login URL
    std::string host = base_url_;
    std::string path = "/api/login";
    // Pisah host dari path (kalau ada)
    auto slash = host.find('/', host.find("://") + 3);
    if (slash != std::string::npos) {
        path = host.substr(slash);
        host = host.substr(0, slash);
    }

    // Reconstruct
    HttpClient tmp(host, use_https_);
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
            std::string val_res = winHttpPost(dash_host, 443, "/player/growid/login/validate", val_body, true, status);
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

            // Coba beberapa field umum GTPS
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