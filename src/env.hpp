#pragma once
#include <string>
#include <unordered_map>
#include <fstream>
#include <algorithm>
#include <cctype>

class EnvConfig {
public:
    static std::unordered_map<std::string, std::string> load(const std::string& path = ".env") {
        std::unordered_map<std::string, std::string> env;
        std::ifstream file(path);
        if (!file.is_open()) return env;

        std::string line;
        while (std::getline(file, line)) {
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            if (line[start] == '#') continue;

            size_t eq = line.find('=', start);
            if (eq == std::string::npos) continue;

            std::string key = line.substr(start, eq - start);
            size_t key_end = key.find_last_not_of(" \t\r\n");
            if (key_end != std::string::npos) key = key.substr(0, key_end + 1);

            size_t val_start = line.find_first_not_of(" \t\r\n", eq + 1);
            std::string val = "";
            if (val_start != std::string::npos) {
                size_t val_end = line.find_last_not_of(" \t\r\n");
                val = line.substr(val_start, val_end - val_start + 1);
                if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') ||
                                        (val.front() == '\'' && val.back() == '\''))) {
                    val = val.substr(1, val.size() - 2);
                }
            }
            env[key] = val;
        }
        return env;
    }

    static std::string get(const std::unordered_map<std::string, std::string>& env,
                           const std::string& key,
                           const std::string& def_val = "") {
        auto it = env.find(key);
        if (it != env.end() && !it->second.empty()) return it->second;
        return def_val;
    }

    static int getInt(const std::unordered_map<std::string, std::string>& env,
                      const std::string& key,
                      int def_val = 0) {
        auto it = env.find(key);
        if (it != env.end() && !it->second.empty()) {
            try {
                return std::stoi(it->second);
            } catch (...) {}
        }
        return def_val;
    }

    static bool getBool(const std::unordered_map<std::string, std::string>& env,
                       const std::string& key,
                       bool def_val = false) {
        auto it = env.find(key);
        if (it != env.end() && !it->second.empty()) {
            std::string s = it->second;
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            if (s == "true" || s == "1" || s == "yes") return true;
            if (s == "false" || s == "0" || s == "no") return false;
        }
        return def_val;
    }
};
