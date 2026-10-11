#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <d3d11.h>
#include <string>
#include <vector>
#include <deque>
#include <atomic>
#include <memory>
#include <functional>
#include "socks5.hpp"
#include "bot_stats.hpp"
#include "console_entry.hpp"
#include "item_database.hpp"

class EnetClient;
class WorldState;
class BotActions;
class AutoFarmController;
class LuaEngine;
struct BotInfo;
struct DeviceFingerprint;
class ReconnectController;
class BotManager;
class TextEditor;

// ============================================================
// Lucifer Native Desktop GUI (Dear ImGui + DirectX 11 + Win32)
// ============================================================

class LuciferGui {
public:
    LuciferGui(EnetClient& enet, WorldState& world, BotActions& actions,
               AutoFarmController& autofarm, LuaEngine& lua_engine,
               BotInfo& info, std::atomic<bool>& running,
               DeviceFingerprint& fingerprint, ReconnectController& reconnect_ctrl,
               std::function<void()> reconnect_fn = nullptr);
    ~LuciferGui();

    // Inisialisasi window Win32 & DirectX 11 device
    bool init(HINSTANCE hInstance, int width = 1040, int height = 700);

    // Loop render GUI
    void run();

    // Cleanup resources
    void shutdown();

    // Bot Manager Setter
    void setBotManager(BotManager* mgr);

    BotStats& getStats() { return gui_stats_; }
    const BotStats& getStats() const { return gui_stats_; }
    void addFallbackConsoleMessage(ConsoleMsgType type, const std::string& msg);
    void setFallbackLastDialog(const std::string& d) { fallback_last_dialog_ = d; }

    enum class LuciferTopTab {
        Bots = 0,
        List,
        Executor,
        Proxy,
        Switch,
        Database,
        Settings
    };

    enum class LuciferSubTab {
        Main = 0,
        World,
        Inventory,
        Console,
        Automation,
        Rotation,
        Logs,
        RID
    };

    void renderMainLayout();
    void renderTitleBar();
    void renderTopNav();
    void renderTabBots();
    void renderSidebar(float width, float height);
    void renderSubTabMain(float width, float height);
    void renderCardBotInfo(float w, float h);
    void renderCardMovement(float w, float h);
    void renderCardPreferences(float w, float h);
    void renderCardIntervals(float w, float h);

    void renderTabList();
    void renderTabExecutor();
    void renderTabProxy();
    void renderTabSwitch();
    void renderTabDatabase();
    void renderTabSettings();

    void renderTabStatus();
    void renderTabMultiBot();
    void renderTabMovement();
    void renderTabInventory();
    void renderTabWorld();
    void renderTabAntiDetection();
    void renderTabScriptEditor();
    void renderTabConsole();

    void renderSubTabWorld();
    void renderSubTabInventory();
    void renderSubTabConsole();
    void renderSubTabAutomation();
    void renderSubTabRotation();
    void renderSubTabLogs();
    void renderSubTabRID();

    void applyLuciferTheme();

    // D3D11 Helpers
    bool createDeviceD3D(HWND hWnd);
    void cleanupDeviceD3D();
    void createRenderTarget();
    void cleanupRenderTarget();

    // Win32 & D3D11 handles
    HWND hWnd_ = nullptr;
    WNDCLASSEXW wc_{};
    ID3D11Device* pd3dDevice_ = nullptr;
    ID3D11DeviceContext* pd3dDeviceContext_ = nullptr;
    IDXGISwapChain* pSwapChain_ = nullptr;
    ID3D11RenderTargetView* mainRenderTargetView_ = nullptr;

    // References
    EnetClient& enet_;
    WorldState& world_;
    BotActions& actions_;
    AutoFarmController& autofarm_;
    LuaEngine& lua_engine_;
    BotInfo& info_;
    std::atomic<bool>& running_;
    DeviceFingerprint& fingerprint_;
    ReconnectController& reconnect_ctrl_;
    std::function<void()> reconnect_fn_;
    BotManager* bot_manager_ = nullptr;
    BotStats gui_stats_;

    // Movement (Tanda Panah) Buffers
    int move_step_ = 1; // 1 tile per click/keypress

    // Navigation & Quick Actions
    char warp_input_[64] = "";
    char chat_input_[256] = "";
    float collect_radius_ = 4.0f;

    // Inventory Buffers
    char inv_filter_[64] = "";

    // Multi-Bot Buffers
    char new_bot_user_[64] = "";
    char new_bot_pass_[64] = "";
    char new_bot_world_[64] = "";
    bool new_bot_use_proxy_ = false;
    char new_bot_proxy_host_[128] = "";
    int new_bot_proxy_port_ = 1080;
    char new_bot_proxy_user_[64] = "";
    char new_bot_proxy_pass_[64] = "";
    char multibot_warp_world_[64] = "";
    char multibot_say_text_[256] = "";
    int selected_bot_id_ = 1;
    char accounts_file_path_[256] = "cache/accounts.txt";

    // Proxy Test & Quick Buffers
    char new_bot_proxy_quick_[256] = "";
    char sel_bot_proxy_quick_[256] = "";
    char tab_proxy_quick_[256] = "";
    char sel_proxy_host_[128] = "";
    int sel_proxy_port_ = 1080;
    char sel_proxy_user_[64] = "";
    char sel_proxy_pass_[64] = "";
    bool sel_use_proxy_ = false;
    char sel_proxy_test_result_[256] = "";
    char batch_proxy_text_[4096] = "";
    Socks5Config main_proxy_{};

    void setMainProxy(const Socks5Config& p) { main_proxy_ = p; }
    const Socks5Config& getMainProxy() const { return main_proxy_; }

    char proxy_test_host_[128] = "127.0.0.1";
    int proxy_test_port_ = 1080;
    char proxy_test_user_[64] = "";
    char proxy_test_pass_[64] = "";
    char proxy_test_result_[256] = "";

    // Script Editor Buffers (Lucifer v2.85 Executor)
    char script_buffer_[16384]{};
    std::unique_ptr<TextEditor> text_editor_;
    bool show_dialog_modal_ = false;
    bool show_error_modal_ = false;
    bool win_console_visible_ = true;
    char executor_status_msg_[128] = "";
    std::string fallback_last_dialog_;
    // Ganti vector dengan deque: pop_front O(1) vs erase-from-front O(N)
    std::deque<std::string> executor_logs_;
    mutable std::mutex executor_logs_mtx_;
    bool executor_auto_scroll_ = true;
    void addExecutorLog(const std::string& msg);
    void clearExecutorLogs();
    LuaEngine& getActiveExecutorEngine();

    // Cache daftar bot agar tidak lock setiap sub-fungsi render di satu frame
    std::vector<std::shared_ptr<class BotInstance>> cached_bots_;
    void refreshBotCache(); // Dipanggil 1x di awal setiap frame render

    // Cache item database search (hanya rebuild saat filter berubah)
    char db_search_cache_buf_[64] = "";
    std::vector<ItemInfo> db_search_cache_;

    // Console & Log Buffers
    char log_filter_[128] = "";
    bool auto_scroll_log_ = true;

    // Dedicated In-Game Console (Lucifer v2.85)
    bool console_enable_ = true;
    bool console_show_timestamps_ = true;
    bool console_auto_scroll_ = true;
    int console_filter_ = 0; // 0: All, 1: World, 2: Private, 3: System
    char console_chat_input_[256] = "";
    mutable std::mutex fallback_console_mtx_;
    std::deque<ConsoleEntry> fallback_console_entries_;

    // Active Navigation Tabs
    LuciferTopTab current_top_tab_ = LuciferTopTab::Bots;
    LuciferSubTab current_sub_tab_ = LuciferSubTab::Main;

    // Sidebar & Multi-Select Buffers
    bool select_all_ = false;
    bool enable_multiselect_ = false;
    char bot_search_[64] = "";
    std::vector<int> selected_bot_ids_;

    // Preferences Toggles (Lucifer v2.85)
    bool pref_bypass_logon_ = false;
    bool pref_auto_reconnect_ = true;
    bool pref_auto_collect_ = false;
    bool pref_auto_accept_ = false;
    bool pref_auto_mod_disconnect_ = true;

    // Intervals (Lucifer v2.85)
    bool dynamic_delay_ = true;
    bool interval_move_enabled_ = true;
    float interval_move_base_ = 0.235f;
    float interval_move_jitter_ = 0.111f;
    bool interval_collect_enabled_ = true;
    float interval_collect_base_ = 0.100f;
    float interval_collect_jitter_ = 0.047f;
    bool interval_hit_enabled_ = true;
    float interval_hit_base_ = 0.150f;
    float interval_hit_jitter_ = 0.071f;
    bool interval_place_enabled_ = true;
    float interval_place_base_ = 0.150f;
    float interval_place_jitter_ = 0.071f;

    // Rotation Buffers
    char rotation_world_input_[64] = "";
    std::vector<std::string> rotation_worlds_;
    bool rotation_running_ = false;

    // Settings Buffers
    char cfg_login_url_[256] = "";
    char cfg_server_ip_[128] = "";
    int cfg_server_port_ = 17091;
    char cfg_account_name_[64] = "";
    char cfg_account_pass_[64] = "";
    char cfg_target_world_[64] = "";
};
