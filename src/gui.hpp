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

class EnetClient;
class WorldState;
class BotActions;
class AutoFarmController;
class LuaEngine;
struct BotInfo;

// ============================================================
// Lucifer Native Desktop GUI (Dear ImGui + DirectX 11 + Win32)
// ============================================================

class LuciferGui {
public:
    LuciferGui(EnetClient& enet, WorldState& world, BotActions& actions,
               AutoFarmController& autofarm, LuaEngine& lua_engine,
               BotInfo& info, std::atomic<bool>& running);
    ~LuciferGui();

    // Inisialisasi window Win32 & DirectX 11 device
    bool init(HINSTANCE hInstance, int width = 1040, int height = 700);

    // Loop render GUI
    void run();

    // Cleanup resources
    void shutdown();

private:
    void renderTabs();
    void renderTabStatus();
    void renderTabMovement();
    void renderTabInventory();
    void renderTabWorld();
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

    // Movement (Tanda Panah) Buffers
    int move_step_ = 1; // 1 tile per click/keypress

    // Navigation & Quick Actions
    char warp_input_[64] = "riziq";
    char chat_input_[256] = "";
    float collect_radius_ = 4.0f;

    // Inventory Buffers
    char inv_filter_[64] = "";

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
