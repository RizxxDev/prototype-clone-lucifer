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
#include <atomic>
#include <memory>
#include <functional>

class EnetClient;
class WorldState;
class BotActions;
class AutoFarmController;
class LuaEngine;
struct BotInfo;
struct DeviceFingerprint;
class ReconnectController;
class BotManager;

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
    void setBotManager(BotManager* mgr) { bot_manager_ = mgr; }

private:
    void renderTabs();
    void renderTabStatus();
    void renderTabMultiBot();
    void renderTabMovement();
    void renderTabInventory();
    void renderTabWorld();
    void renderTabAntiDetection();
    void renderTabScriptEditor();
    void renderTabConsole();
    void renderTabSettings();

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

    // Movement (Tanda Panah) Buffers
    int move_step_ = 1; // 1 tile per click/keypress

    // Navigation & Quick Actions
    char warp_input_[64] = "riziq";
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
    char accounts_file_path_[256] = "accounts.txt";

    // Proxy Test Buffers
    char proxy_test_host_[128] = "127.0.0.1";
    int proxy_test_port_ = 1080;
    char proxy_test_user_[64] = "";
    char proxy_test_pass_[64] = "";
    char proxy_test_result_[256] = "";

    // Script Editor Buffers
    char script_buffer_[16384]{};

    // Console Buffers
    char log_filter_[128] = "";
    bool auto_scroll_log_ = true;

    // Settings Buffers
    char cfg_login_url_[256] = "";
    char cfg_server_ip_[128] = "";
    int cfg_server_port_ = 17091;
    char cfg_account_name_[64] = "";
    char cfg_account_pass_[64] = "";
    char cfg_target_world_[64] = "";
};
