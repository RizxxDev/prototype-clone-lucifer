#include "anti_detection.hpp"
#include "logger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <chrono>
#include <thread>
#include <algorithm>

namespace fs = std::filesystem;
using json = nlohmann::json;

bool AntiDetection::enable_random_delays = true;
bool AntiDetection::enable_human_pauses = true;
std::mt19937 AntiDetection::rng_{std::random_device{}()};

void AntiDetection::init() {
    rng_.seed(static_cast<unsigned int>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
}

int AntiDetection::getRandomDelay(int base_ms, int jitter_ms, int min_ms) {
    if (!enable_random_delays || jitter_ms <= 0) {
        return std::max(min_ms, base_ms);
    }
    int low = std::max(min_ms, base_ms - jitter_ms);
    int high = std::max(low, base_ms + jitter_ms);
    std::uniform_int_distribution<int> dist(low, high);
    return dist(rng_);
}

void AntiDetection::sleepWithJitter(int base_ms, int jitter_ms, int min_ms) {
    int delay = getRandomDelay(base_ms, jitter_ms, min_ms);
    if (delay > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
}

void AntiDetection::maybeHumanPause(float chance, int min_pause_ms, int max_pause_ms) {
    if (!enable_human_pauses || chance <= 0.0f) return;
    std::uniform_real_distribution<float> prob(0.0f, 1.0f);
    if (prob(rng_) < chance) {
        std::uniform_int_distribution<int> pause_dist(min_pause_ms, max_pause_ms);
        int pause_ms = pause_dist(rng_);
        std::this_thread::sleep_for(std::chrono::milliseconds(pause_ms));
    }
}

static std::string generateHex(size_t length, bool uppercase = true) {
    static const char hex_chars_upper[] = "0123456789ABCDEF";
    static const char hex_chars_lower[] = "0123456789abcdef";
    const char* chars = uppercase ? hex_chars_upper : hex_chars_lower;

    std::uniform_int_distribution<int> dist(0, 15);
    std::string res;
    res.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        res.push_back(chars[dist(AntiDetection::rng())]);
    }
    return res;
}

DeviceFingerprint DeviceFingerprint::generateRandom(const std::string& country) {
    DeviceFingerprint fp;
    fp.country = country.empty() ? "id" : country;
    fp.device_version = 0;

    // MAC Address acak dengan format XX:XX:XX:XX:XX:XX
    // Set Locally Administered bit (bit 1 byte pertama) dan Unicast (bit 0 = 0)
    std::uniform_int_distribution<int> byte_dist(0, 255);
    std::ostringstream mac_ss;
    for (int i = 0; i < 6; ++i) {
        int b = byte_dist(AntiDetection::rng());
        if (i == 0) {
            b = (b & 0xFE) | 0x02; // locally administered unicast
        }
        mac_ss << std::hex << std::setw(2) << std::setfill('0') << std::nouppercase << b;
        if (i < 5) mac_ss << ":";
    }
    fp.mac = mac_ss.str();

    // RID: 32 hex uppercase (format UUID tanpa strip)
    fp.rid = generateHex(32, true);

    // WK: 32 hex uppercase
    fp.wk = generateHex(32, true);

    // Realistic hashes
    std::uniform_int_distribution<int32_t> hash_dist(-2000000000, -10000000);
    fp.hash = hash_dist(AntiDetection::rng());
    fp.hash2 = hash_dist(AntiDetection::rng());
    fp.fhash = hash_dist(AntiDetection::rng());

    // fz dan zf (zf = -fz)
    std::uniform_int_distribution<int32_t> fz_dist(1000000000, 2000000000);
    fp.fz = fz_dist(AntiDetection::rng());
    fp.zf = -fp.fz;

    return fp;
}

DeviceFingerprint DeviceFingerprint::getOrCreateForAccount(const std::string& account_name,
                                                          const std::string& cache_dir) {
    std::string safe_name = account_name.empty() ? "default" : account_name;
    for (char& c : safe_name) {
        if (!isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') c = '_';
    }

    try {
        if (!fs::exists(cache_dir)) {
            fs::create_directories(cache_dir);
        }
    } catch (...) {}

    std::string file_path = cache_dir + "/" + safe_name + ".json";
    DeviceFingerprint fp;
    if (fp.loadFromFile(file_path)) {
        Logger::log("[FINGERPRINT] 🛡️ Memuat profil fingerprint tersimpan untuk akun '", account_name, "'");
        return fp;
    }

    // Periksa migrasi dari folder legacy .fingerprints/
    std::string legacy_path = ".fingerprints/" + safe_name + ".json";
    if (fs::exists(legacy_path) && fp.loadFromFile(legacy_path)) {
        fp.saveToFile(file_path);
        Logger::log("[FINGERPRINT] 📦 Migrasi profil fingerprint dari ", legacy_path, " ke ", file_path);
        return fp;
    }

    fp = generateRandom("id");
    fp.saveToFile(file_path);
    Logger::log("[FINGERPRINT] ✨ Membuat fingerprint perangkat baru untuk akun '", account_name, "'");
    Logger::log("              MAC: ", fp.mac, " | RID: ", fp.rid.substr(0, 8), "...");
    return fp;
}

bool DeviceFingerprint::saveToFile(const std::string& filepath) const {
    try {
        json j;
        j["mac"] = mac;
        j["rid"] = rid;
        j["wk"] = wk;
        j["hash"] = hash;
        j["hash2"] = hash2;
        j["fhash"] = fhash;
        j["fz"] = fz;
        j["zf"] = zf;
        j["country"] = country;
        j["device_version"] = device_version;

        std::ofstream out(filepath);
        if (out.is_open()) {
            out << j.dump(4);
            return true;
        }
    } catch (const std::exception& e) {
        Logger::log("[FINGERPRINT] ⚠️ Gagal simpan fingerprint ke ", filepath, ": ", e.what());
    }
    return false;
}

bool DeviceFingerprint::loadFromFile(const std::string& filepath) {
    try {
        std::ifstream in(filepath);
        if (!in.is_open()) return false;

        json j = json::parse(in);
        mac = j.value("mac", "");
        rid = j.value("rid", "");
        wk = j.value("wk", "");
        hash = j.value("hash", -16777216);
        hash2 = j.value("hash2", -129482910);
        fhash = j.value("fhash", -716928004);
        fz = j.value("fz", 1331849031);
        zf = j.value("zf", -1331849031);
        country = j.value("country", "id");
        device_version = j.value("device_version", 0);

        return !mac.empty() && !rid.empty();
    } catch (...) {
        return false;
    }
}

std::string DeviceFingerprint::toHandshakeString(const std::string& meta) const {
    std::string s;
    s += "hash2|" + std::to_string(hash2) + "\n";
    if (!meta.empty()) {
        s += "meta|" + meta + "\n";
    }
    s += "fhash|" + std::to_string(fhash) + "\n";
    s += "platformID|0\n";
    s += "deviceVersion|" + std::to_string(device_version) + "\n";
    s += "country|" + country + "\n";
    s += "hash|" + std::to_string(hash) + "\n";
    s += "mac|" + mac + "\n";
    s += "rid|" + rid + "\n";
    s += "wk|" + wk + "\n";
    s += "fz|" + std::to_string(fz) + "\n";
    s += "zf|" + std::to_string(zf) + "\n";
    return s;
}
