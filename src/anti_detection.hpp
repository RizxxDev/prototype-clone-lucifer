#pragma once
#include <string>
#include <cstdint>
#include <random>

// ============================================================
// Lucifer Anti-Detection Engine
// Device Fingerprint Generator & Action Delay Randomizer
// ============================================================

struct DeviceFingerprint {
    std::string mac;
    std::string rid;
    std::string wk;
    int32_t hash = -16777216;
    int32_t hash2 = -129482910;
    int32_t fhash = -716928004;
    int32_t fz = 1331849031;
    int32_t zf = -1331849031;
    std::string country = "id";
    int device_version = 0;

    // Generate identitas perangkat unik baru secara acak
    static DeviceFingerprint generateRandom(const std::string& country = "id");

    // Dapatkan fingerprint tersimpan untuk akun ini, atau buat baru dan simpan
    static DeviceFingerprint getOrCreateForAccount(const std::string& account_name,
                                                  const std::string& cache_dir = "cache/fingerprints");

    // Simpan & muat ke/dari file JSON
    bool saveToFile(const std::string& filepath) const;
    bool loadFromFile(const std::string& filepath);

    // Format string handshake ENet standar Growtopia
    std::string toHandshakeString(const std::string& meta = "") const;
};

class AntiDetection {
public:
    static void init();

    // Dapatkan delay acak berbasis normal/uniform distribution dengan batas min/max
    static int getRandomDelay(int base_ms, int jitter_ms, int min_ms = 5);

    // Sleep dengan jitter acak untuk simulasi reaksi manusia
    static void sleepWithJitter(int base_ms, int jitter_ms, int min_ms = 5);

    // Simulasi jeda acak manusia (micro-pause)
    static void maybeHumanPause(float chance = 0.03f, int min_pause_ms = 150, int max_pause_ms = 400);

    // Pengaturan global
    static bool enable_random_delays;
    static bool enable_human_pauses;

    static std::mt19937& rng() { return rng_; }

    friend struct DeviceFingerprint;

private:
    static std::mt19937 rng_;
};
