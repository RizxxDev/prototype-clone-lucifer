#include "bot_stats.hpp"
#include "logger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <regex>
#include <iomanip>

using json = nlohmann::json;

BotStats::BotStats(const BotStats& other) {
    std::lock_guard<std::mutex> lock(other.mtx);
    level = other.level;
    gems = other.gems;
    played_hours = other.played_hours;
    age_days = other.age_days;
    total_played_seconds = other.total_played_seconds;
    first_seen_epoch = other.first_seen_epoch;
    session_start = other.session_start;
    is_session_active = other.is_session_active;
}

BotStats& BotStats::operator=(const BotStats& other) {
    if (this == &other) return *this;
    std::unique_lock<std::mutex> lock1(mtx, std::defer_lock);
    std::unique_lock<std::mutex> lock2(other.mtx, std::defer_lock);
    std::lock(lock1, lock2);

    level = other.level;
    gems = other.gems;
    played_hours = other.played_hours;
    age_days = other.age_days;
    total_played_seconds = other.total_played_seconds;
    first_seen_epoch = other.first_seen_epoch;
    session_start = other.session_start;
    is_session_active = other.is_session_active;
    return *this;
}

void BotStats::startSession() {
    std::lock_guard<std::mutex> lock(mtx);
    if (!is_session_active) {
        session_start = std::chrono::steady_clock::now();
        is_session_active = true;
    }
}

void BotStats::stopSession() {
    std::lock_guard<std::mutex> lock(mtx);
    if (is_session_active) {
        auto now = std::chrono::steady_clock::now();
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(now - session_start).count();
        if (sec > 0) {
            total_played_seconds += sec;
        }
        is_session_active = false;
    }
}

float BotStats::getEffectivePlayedHours() const {
    std::lock_guard<std::mutex> lock(mtx);
    float extra_hrs = 0.0f;
    if (is_session_active) {
        auto now = std::chrono::steady_clock::now();
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(now - session_start).count();
        if (sec > 0) extra_hrs = static_cast<float>(sec) / 3600.0f;
    }
    if (played_hours > 0.0f) {
        return played_hours + extra_hrs;
    }
    return (static_cast<float>(total_played_seconds) / 3600.0f) + extra_hrs;
}

int BotStats::getEffectiveAgeDays() const {
    std::lock_guard<std::mutex> lock(mtx);
    if (age_days > 0) return age_days;
    if (first_seen_epoch > 0) {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        int diff = static_cast<int>((now - first_seen_epoch) / 86400);
        return (diff > 0) ? diff : 0;
    }
    return 0;
}

std::string BotStats::getFormattedGems() const {
    std::lock_guard<std::mutex> lock(mtx);
    std::string s = std::to_string(gems);
    int n = static_cast<int>(s.length()) - 3;
    while (n > 0) {
        s.insert(n, ",");
        n -= 3;
    }
    return s;
}

std::string BotStats::getFormattedActiveTime() const {
    float hrs = getEffectivePlayedHours();
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << hrs << " hours";
    return ss.str();
}

bool BotStats::parseFromText(const std::string& raw_text) {
    if (raw_text.empty()) return false;
    std::string text = Logger::stripColorCodes(raw_text);
    text.erase(std::remove(text.begin(), text.end(), '`'), text.end());
    bool updated = false;

    std::lock_guard<std::mutex> lock(mtx);

    // ==========================================
    // 1. Level parsing
    // ==========================================
    try {
        // Cocokkan: "Level: 15", "Level: 15 / 125", "(Level 15)", "level|15", "Lvl: 15"
        std::regex re_lvl(R"((?:level|lvl)\s*[:|=]?\s*([0-9]+))", std::regex_constants::icase);
        std::smatch m;
        if (std::regex_search(text, m, re_lvl) && m.size() > 1) {
            int lvl = std::stoi(m[1].str());
            if (lvl > 0 && lvl < 250) {
                level = lvl;
                updated = true;
            }
        }
    } catch (...) {}

    // Fallback scanner untuk Level
    if (!updated) {
        auto find_ci = [](const std::string& str, const std::string& sub) -> size_t {
            auto it = std::search(
                str.begin(), str.end(),
                sub.begin(), sub.end(),
                [](char c1, char c2) { return std::tolower(static_cast<unsigned char>(c1)) == std::tolower(static_cast<unsigned char>(c2)); }
            );
            return (it != str.end()) ? std::distance(str.begin(), it) : std::string::npos;
        };
        size_t lpos = find_ci(text, "level");
        if (lpos != std::string::npos) {
            size_t num_start = lpos + 5;
            while (num_start < text.size() && (text[num_start] == ':' || text[num_start] == ' ' || text[num_start] == '|')) {
                num_start++;
            }
            if (num_start < text.size() && std::isdigit(static_cast<unsigned char>(text[num_start]))) {
                try {
                    int lvl = std::stoi(text.substr(num_start));
                    if (lvl > 0 && lvl < 250) {
                        level = lvl;
                        updated = true;
                    }
                } catch (...) {}
            }
        }
    }

    // ==========================================
    // 2. Gems parsing
    // ==========================================
    try {
        // Cocokkan: "You have 50,000 Gems", "50,000 Gems", "Gems: 50,000", "gems|50000"
        std::regex re_gems1(R"(([0-9]{1,3}(?:,[0-9]{3})+|[0-9]+)\s*gems?)", std::regex_constants::icase);
        std::regex re_gems2(R"(gems?\s*[:|=]?\s*([0-9]{1,3}(?:,[0-9]{3})+|[0-9]+))", std::regex_constants::icase);
        std::smatch m;
        std::string match_str;
        if (std::regex_search(text, m, re_gems1) && m.size() > 1) {
            match_str = m[1].str();
        } else if (std::regex_search(text, m, re_gems2) && m.size() > 1) {
            match_str = m[1].str();
        }

        if (!match_str.empty()) {
            std::string clean_num;
            for (char c : match_str) {
                if (c != ',') clean_num += c;
            }
            int g = std::stoi(clean_num);
            if (g >= 0) {
                gems = g;
                updated = true;
            }
        }
    } catch (...) {}

    // ==========================================
    // 3. Played Hours parsing
    // ==========================================
    try {
        // Pola 1: Wajib mengandung kata "played" / "playtime" / "time played"
        // Contoh: "Total time played is 237.20 hours", "time played is 14.85 hours", "played for 12.50 hours", "playtime: 12.50 hours"
        std::regex re_played1(R"((?:total\s+time\s+played|time\s+played|total\s+playtime|played\s+for|playtime|played)\s*(?:is|:|=|\s)?\s*([0-9]+(?:\.[0-9]+)?)\s*hours?)", std::regex_constants::icase);
        // Pola 2: "Playtime: 12.50" atau "Played: 237"
        std::regex re_played2(R"(play(?:ed|time)?\s*[:|=]\s*([0-9]+(?:\.[0-9]+)?))", std::regex_constants::icase);

        std::smatch m;
        std::string match_str;
        if (std::regex_search(text, m, re_played1) && m.size() > 1) {
            match_str = m[1].str();
        } else if (std::regex_search(text, m, re_played2) && m.size() > 1) {
            match_str = m[1].str();
        }

        if (!match_str.empty()) {
            float hrs = std::stof(match_str);
            // Playtime hanya boleh bertambah atau sama, TIDAK BOLEH berkurang (mencegah false positive dari pesan server lain)
            if (hrs >= played_hours || played_hours <= 0.0f) {
                played_hours = hrs;
                updated = true;
            }
        }
    } catch (...) {}

    // Fallback scanner langsung untuk format standar: "Total time played is X hours"
    if (played_hours <= 0.0f) {
        std::string text_lower = text;
        std::transform(text_lower.begin(), text_lower.end(), text_lower.begin(), ::tolower);
        size_t ppos = text_lower.find("played is ");
        if (ppos == std::string::npos) ppos = text_lower.find("played for ");
        if (ppos == std::string::npos) ppos = text_lower.find("played: ");
        if (ppos == std::string::npos) ppos = text_lower.find("playtime: ");
        if (ppos != std::string::npos) {
            size_t num_start = text_lower.find_first_of("0123456789", ppos);
            if (num_start != std::string::npos) {
                try {
                    float hrs = std::stof(text.substr(num_start));
                    if (hrs >= played_hours || played_hours <= 0.0f) {
                        played_hours = hrs;
                        updated = true;
                    }
                } catch (...) {}
            }
        }
    }

    // ==========================================
    // 4. Age Days parsing
    // ==========================================
    try {
        // Pola 1: "This account was created 5 days ago", "account was created 5 days ago", "created 5 days ago"
        std::regex re_age_created(R"((?:account\s+was\s+created|was\s+created|created)\s+([0-9]+)\s*days?\s*ago)", std::regex_constants::icase);
        // Pola 2: "5 days ago"
        std::regex re_age_ago(R"(([0-9]+)\s*days?\s*ago)", std::regex_constants::icase);
        // Pola 3: "Your account is 35 days old", "35 days old"
        std::regex re_age_old(R"((?:account\s+is\s+)?([0-9]+)\s*days?\s+old)", std::regex_constants::icase);
        // Pola 4: "Account Age: 35 Days", "Age: 35 days", "Age: 35"
        std::regex re_age_colon(R"((?:account\s+age|age)\s*[:|=]?\s*([0-9]+))", std::regex_constants::icase);

        std::smatch m;
        std::string match_str;
        if (std::regex_search(text, m, re_age_created) && m.size() > 1) {
            match_str = m[1].str();
        } else if (std::regex_search(text, m, re_age_ago) && m.size() > 1) {
            match_str = m[1].str();
        } else if (std::regex_search(text, m, re_age_old) && m.size() > 1) {
            match_str = m[1].str();
        } else if (std::regex_search(text, m, re_age_colon) && m.size() > 1) {
            match_str = m[1].str();
        }

        if (!match_str.empty()) {
            int a = std::stoi(match_str);
            // Umur akun hanya boleh bertambah, tidak boleh berkurang
            if (a >= age_days || age_days <= 0) {
                age_days = a;
                updated = true;
            }
        }
    } catch (...) {}

    // Fallback scanner langsung untuk format standar: "This account was created 5 days ago"
    if (age_days <= 0) {
        std::string text_lower = text;
        std::transform(text_lower.begin(), text_lower.end(), text_lower.begin(), ::tolower);
        size_t apos = text_lower.find("created ");
        if (apos != std::string::npos) {
            size_t num_start = text_lower.find_first_of("0123456789", apos);
            if (num_start != std::string::npos) {
                try {
                    int a = std::stoi(text.substr(num_start));
                    if (a >= age_days || age_days <= 0) {
                        age_days = a;
                        updated = true;
                    }
                } catch (...) {}
            }
        }
    }

    return updated;
}

bool BotStats::saveToFile(const std::string& account_name, const std::string& cache_dir) const {
    if (account_name.empty()) return false;
    try {
        if (!std::filesystem::exists(cache_dir)) {
            std::filesystem::create_directories(cache_dir);
        }

        std::lock_guard<std::mutex> lock(mtx);
        json j;
        j["level"] = level;
        j["gems"] = gems;
        j["played_hours"] = played_hours;
        j["age_days"] = age_days;
        j["total_played_seconds"] = total_played_seconds;
        j["first_seen_epoch"] = first_seen_epoch;

        std::string filepath = cache_dir + "/" + account_name + ".json";
        std::ofstream out(filepath);
        if (out.is_open()) {
            out << j.dump(4);
            return true;
        }
    } catch (...) {}
    return false;
}

bool BotStats::loadFromFile(const std::string& account_name, const std::string& cache_dir) {
    if (account_name.empty()) return false;
    std::string filepath = cache_dir + "/" + account_name + ".json";
    try {
        if (std::filesystem::exists(filepath)) {
            std::ifstream in(filepath);
            if (in.is_open()) {
                json j;
                in >> j;
                std::lock_guard<std::mutex> lock(mtx);
                level = j.value("level", 1);
                gems = j.value("gems", 0);
                played_hours = j.value("played_hours", 0.0f);
                age_days = j.value("age_days", 0);
                total_played_seconds = j.value("total_played_seconds", (int64_t)0);
                first_seen_epoch = j.value("first_seen_epoch", (int64_t)0);
            }
        }
    } catch (...) {}

    // Inisialisasi first_seen_epoch jika belum ada
    if (first_seen_epoch <= 0) {
        try {
            std::string fp_file = "cache/fingerprints/" + account_name + ".json";
            if (std::filesystem::exists(fp_file)) {
                auto ftime = std::filesystem::last_write_time(fp_file);
                auto s_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
                );
                first_seen_epoch = std::chrono::system_clock::to_time_t(s_time);
            }
        } catch (...) {}
        if (first_seen_epoch <= 0) {
            first_seen_epoch = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        }
    }
    return true;
}
