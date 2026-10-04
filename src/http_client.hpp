#pragma once
#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include "socks5.hpp"

struct LoginResponse {
    bool success = false;
    int http_status = 0;
    std::string body;
    std::string error;

    // Data yang di-parse dari response GTPS
    std::string token;
    std::string server_ip;
    int server_port = 0;
    std::string meta;
    std::string loginurl;
    std::string raw_json;
    std::map<std::string, std::string> kv;
};

class HttpClient {
public:
    HttpClient(const std::string& base_url, bool use_https, const Socks5Config& proxy = {});

    void setProxy(const Socks5Config& proxy) { proxy_ = proxy; }
    const Socks5Config& getProxy() const { return proxy_; }

    // Login ke GTPS
    LoginResponse login(const std::string& growid,
                        const std::string& password,
                        const std::string& version = "5.30",
                        const std::string& platform = "windows");

    // Request generic
    LoginResponse post(const std::string& path,
                       const std::string& body,
                       const std::map<std::string, std::string>& headers = {});

    // Deteksi IP Publik Aktif (Direct atau via SOCKS5 Proxy)
    static std::string fetchPublicIp(const Socks5Config& proxy = {}, int timeout_ms = 4000);
    static std::string getCachedDirectIp();
    static void refreshDirectIpAsync();

private:
    std::string base_url_;
    bool use_https_;
    Socks5Config proxy_;
};