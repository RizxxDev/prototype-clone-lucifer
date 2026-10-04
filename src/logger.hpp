#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <iostream>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>
#include <mutex>
#include <algorithm>

class Logger {
public:
    static bool verbose;
    static bool hexdump;
    static std::mutex mtx;
    static std::vector<std::string> history;
    static const size_t MAX_HISTORY = 1000;

    static std::string ts() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        std::ostringstream ss;
        ss << std::put_time(&tm, "%H:%M:%S")
           << '.' << std::setw(3) << std::setfill('0') << ms.count();
        return ss.str();
    }

    template<typename... Args>
    static void log(Args&&... args) {
        std::lock_guard<std::mutex> lock(mtx);
        std::ostringstream ss;
        ss << "[" << ts() << "] ";
        (ss << ... << args);
        std::string line = ss.str();

        std::cout << line << std::endl;

        if (history.size() >= MAX_HISTORY) {
            history.erase(history.begin(), history.begin() + 100);
        }
        history.push_back(line);
    }

    template<typename... Args>
    static void logDebug(Args&&... args) {
        if (!verbose) return;
        log(std::forward<Args>(args)...);
    }

    static std::string stripColorCodes(const std::string& input) {
        std::string res;
        res.reserve(input.size());
        for (size_t i = 0; i < input.size(); ++i) {
            if (input[i] == '`') {
                // Lewati '`' dan karakter kode warna setelahnya jika ada (termasuk `` untuk reset)
                if (i + 1 < input.size()) {
                    i++;
                }
                continue;
            }
            if (input[i] != '\r') {
                res += input[i];
            }
        }
        return res;
    }

    static void hexdumpBytes(const std::string& tag,
                             const std::vector<uint8_t>& data) {
        if (!hexdump) return;
        std::lock_guard<std::mutex> lock(mtx);
        std::cout << "[" << ts() << "] " << tag << " ("
                  << data.size() << "B)\n";
        for (size_t i = 0; i < data.size(); i += 16) {
            std::cout << "  " << std::hex << std::setw(4)
                      << std::setfill('0') << i << "  ";
            size_t end = (std::min)(i + 16, data.size());
            for (size_t j = i; j < end; ++j) {
                std::cout << std::setw(2) << (int)data[j] << " ";
            }
            for (size_t j = end; j < i + 16; ++j) std::cout << "   ";
            std::cout << " |";
            for (size_t j = i; j < end; ++j) {
                char c = (char)data[j];
                std::cout << (c >= 32 && c < 127 ? c : '.');
            }
            std::cout << "|\n";
        }
        std::cout << std::dec << std::flush;
    }
};

// Inline definitions
inline bool Logger::verbose = false;
inline bool Logger::hexdump = false;
inline std::mutex Logger::mtx;
inline std::vector<std::string> Logger::history;