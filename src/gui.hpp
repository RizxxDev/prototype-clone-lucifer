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
struct BotInfo;

// ============================================================
// Lucifer Native Desktop GUI (Dear ImGui + DirectX 11 + Win32)
// ============================================================

class LuciferGui {
public:
    LuciferGui(EnetClient& enet, WorldState& world, BotActions& actions,
               AutoFarmController& autofarm, BotInfo& info, std::atomic<bool>& running);
    ~LuciferGui();

    // Inisialisasi window Win32 & DirectX 11 device
    bool init(HINSTANCE hInstance, int width = 1040, int height = 720);

    // Loop render GUI (akan berjalan hingga user menutup window atau running == false)
    void run();

    // Cleanup resources
    void shutdown();

private:
    void renderTabs();
    void renderTabStatus();
    void renderTabActions();
    void renderTabInventory();
    void renderTabWorld();
    void renderTabAutoFarm();
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
    BotInfo& info_;
    std::atomic<bool>& running_;

    // UI Input Buffers
    char warp_input_[64] = "riziq";
    char chat_input_[256] = "";
    int move_x_ = 0;
    int move_y_ = 0;
    int punch_x_ = 0;
    int punch_y_ = 0;
    int place_id_ = 2; // Default Dirt
    int place_x_ = 0;
    int place_y_ = 0;
    int wrench_x_ = 0;
    int wrench_y_ = 0;
    float collect_radius_ = 4.0f;
    int drop_id_ = 2;
    int drop_count_ = 1;
    int trash_id_ = 2;
    int trash_count_ = 1;

    // Auto-Farm Buffers
    int farm_id_ = 2;
    int farm_x_ = 0;
    int farm_y_ = 0;
    int farm_delay_ = 180;
    int farm_hits_ = 4;

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
