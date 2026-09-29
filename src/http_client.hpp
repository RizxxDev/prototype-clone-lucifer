#pragma once
#include <string>
#include <map>
#include <vector>
#include <cstdint>

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
    HttpClient(const std::string& base_url, bool use_https);

    // Login ke GTPS
    LoginResponse login(const std::string& growid,
                        const std::string& password,
                        const std::string& version = "5.30",
                        const std::string& platform = "windows");

    // Request generic
    LoginResponse post(const std::string& path,
                       const std::string& body,
                       const std::map<std::string, std::string>& headers = {});

private:
    std::string base_url_;
    bool use_https_;
};