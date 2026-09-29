#include "gui.hpp"
#include "enet_client.hpp"
#include "world.hpp"
#include "bot_actions.hpp"
#include "cli.hpp"
#include "logger.hpp"
#include "env.hpp"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <dwmapi.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LuciferGui* g_current_gui = nullptr;

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        // Resize swap chain
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

LuciferGui::LuciferGui(EnetClient& enet, WorldState& world, BotActions& actions,
                       AutoFarmController& autofarm, BotInfo& info, std::atomic<bool>& running)
    : enet_(enet), world_(world), actions_(actions),
      autofarm_(autofarm), info_(info), running_(running) {
    g_current_gui = this;

    // Inisialisasi buffer dari info akun
    strncpy_s(cfg_account_name_, info_.account_name.c_str(), sizeof(cfg_account_name_) - 1);
    strncpy_s(cfg_server_ip_, info_.game_host.c_str(), sizeof(cfg_server_ip_) - 1);
    cfg_server_port_ = info_.game_port;
}

LuciferGui::~LuciferGui() {
    shutdown();
    g_current_gui = nullptr;
}

bool LuciferGui::createDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };

    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
        &pSwapChain_, &pd3dDevice_, &featureLevel, &pd3dDeviceContext_);

    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
            featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
            &pSwapChain_, &pd3dDevice_, &featureLevel, &pd3dDeviceContext_);
    }

    if (res != S_OK) return false;

    createRenderTarget();
    return true;
}

void LuciferGui::cleanupDeviceD3D() {
    cleanupRenderTarget();
    if (pSwapChain_) { pSwapChain_->Release(); pSwapChain_ = nullptr; }
    if (pd3dDeviceContext_) { pd3dDeviceContext_->Release(); pd3dDeviceContext_ = nullptr; }
    if (pd3dDevice_) { pd3dDevice_->Release(); pd3dDevice_ = nullptr; }
}

void LuciferGui::createRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    pSwapChain_->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        pd3dDevice_->CreateRenderTargetView(pBackBuffer, nullptr, &mainRenderTargetView_);
        pBackBuffer->Release();
    }
}

void LuciferGui::cleanupRenderTarget() {
    if (mainRenderTargetView_) {
        mainRenderTargetView_->Release();
        mainRenderTargetView_ = nullptr;
    }
}

void LuciferGui::applyLuciferTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    style.WindowRounding    = 8.0f;
    style.FrameRounding     = 5.0f;
    style.PopupRounding     = 5.0f;
    style.ScrollbarRounding = 5.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 6.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.ItemSpacing       = ImVec2(8.0f, 8.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 6.0f);
    style.WindowPadding     = ImVec2(12.0f, 12.0f);
    style.FramePadding      = ImVec2(8.0f, 5.0f);

    // Lucifer Crimson Red Dark Palette
    colors[ImGuiCol_Text]                  = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.52f, 0.58f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.09f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_ChildBg]               = ImVec4(0.12f, 0.13f, 0.17f, 1.00f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.11f, 0.12f, 0.16f, 0.98f);
    colors[ImGuiCol_Border]                = ImVec4(0.20f, 0.22f, 0.28f, 1.00f);
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.15f, 0.16f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.22f, 0.24f, 0.32f, 1.00f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.28f, 0.30f, 0.40f, 1.00f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.08f, 0.09f, 0.12f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.14f, 0.15f, 0.20f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.08f, 0.09f, 0.12f, 1.00f);
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.12f, 0.13f, 0.17f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.08f, 0.09f, 0.12f, 0.50f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.25f, 0.27f, 0.35f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.35f, 0.38f, 0.48f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.45f, 0.48f, 0.60f, 1.00f);
    colors[ImGuiCol_CheckMark]             = ImVec4(0.92f, 0.22f, 0.28f, 1.00f);
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.85f, 0.20f, 0.25f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.95f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.75f, 0.18f, 0.22f, 1.00f); // Lucifer Crimson Red
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.88f, 0.22f, 0.27f, 1.00f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.65f, 0.15f, 0.18f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.75f, 0.18f, 0.22f, 0.40f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.85f, 0.20f, 0.25f, 0.60f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.95f, 0.22f, 0.28f, 0.80f);
    colors[ImGuiCol_Separator]             = ImVec4(0.20f, 0.22f, 0.28f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.85f, 0.20f, 0.25f, 0.78f);
    colors[ImGuiCol_SeparatorActive]       = ImVec4(0.85f, 0.20f, 0.25f, 1.00f);
    colors[ImGuiCol_Tab]                   = ImVec4(0.14f, 0.15f, 0.20f, 1.00f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.70f, 0.18f, 0.22f, 0.80f);
    colors[ImGuiCol_TabActive]             = ImVec4(0.85f, 0.20f, 0.25f, 1.00f);
    colors[ImGuiCol_TabUnfocused]          = ImVec4(0.11f, 0.12f, 0.16f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.20f, 0.22f, 0.28f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.16f, 0.18f, 0.24f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.24f, 0.26f, 0.34f, 1.00f);
    colors[ImGuiCol_TableBorderLight]      = ImVec4(0.18f, 0.20f, 0.26f, 1.00f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
}

bool LuciferGui::init(HINSTANCE hInstance, int width, int height) {
    wc_.cbSize = sizeof(WNDCLASSEXW);
    wc_.style = CS_CLASSDC;
    wc_.lpfnWndProc = WndProc;
    wc_.hInstance = hInstance;
    wc_.lpszClassName = L"LuciferBotWindowClass";
    ::RegisterClassExW(&wc_);

    hWnd_ = ::CreateWindowW(
        wc_.lpszClassName,
        L"Lucifer Bot — GTPS Client Suite",
        WS_OVERLAPPEDWINDOW,
        100, 100, width, height,
        nullptr, nullptr, wc_.hInstance, nullptr);

    if (!hWnd_) return false;

    // Enable Windows 10/11 Dark Titlebar
    BOOL useDarkMode = TRUE;
    ::DwmSetWindowAttribute(hWnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));

    if (!createDeviceD3D(hWnd_)) {
        cleanupDeviceD3D();
        ::UnregisterClassW(wc_.lpszClassName, wc_.hInstance);
        return false;
    }

    ::ShowWindow(hWnd_, SW_SHOWDEFAULT);
    ::UpdateWindow(hWnd_);

    // Init ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    applyLuciferTheme();

    ImGui_ImplWin32_Init(hWnd_);
    ImGui_ImplDX11_Init(pd3dDevice_, pd3dDeviceContext_);

    return true;
}

void LuciferGui::shutdown() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    cleanupDeviceD3D();
    if (hWnd_) {
        ::DestroyWindow(hWnd_);
        ::UnregisterClassW(wc_.lpszClassName, wc_.hInstance);
        hWnd_ = nullptr;
    }
}

void LuciferGui::run() {
    while (running_) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                running_ = false;
            }
        }
        if (!running_) break;

        // Poll ENet packet handling
        enet_.poll();

        // New ImGui Frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Render Lucifer GUI Suite
        renderTabs();

        ImGui::Render();
        const float clear_color[4] = { 0.08f, 0.09f, 0.12f, 1.00f };
        pd3dDeviceContext_->OMSetRenderTargets(1, &mainRenderTargetView_, nullptr);
        pd3dDeviceContext_->ClearRenderTargetView(mainRenderTargetView_, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        pSwapChain_->Present(1, 0); // VSync
    }
}

void LuciferGui::renderTabs() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("LuciferMainWindow", nullptr, window_flags)) {
        // Lucifer Header Banner
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.25f, 0.30f, 1.00f));
        ImGui::Text("🔥 LUCIFER PROTOTYPE");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("— Growtopia Private Server Bot Suite");

        ImGui::SameLine(ImGui::GetWindowWidth() - 250);
        if (enet_.isConnected()) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "● ONLINE");
            ImGui::SameLine();
            ImGui::TextDisabled("| %s", info_.account_name.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "● OFFLINE");
            ImGui::SameLine();
            ImGui::TextDisabled("| Disconnected");
        }

        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::BeginTabBar("LuciferTabs", ImGuiTabBarFlags_None)) {
            if (ImGui::BeginTabItem("🏠 Status")) {
                renderTabStatus();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("🎮 Aksi & Gerak")) {
                renderTabActions();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("🎒 Inventory")) {
                renderTabInventory();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("🌍 World & Radar")) {
                renderTabWorld();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("🚜 Auto-Farm")) {
                renderTabAutoFarm();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("📜 Console Logs")) {
                renderTabConsole();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("⚙️ Pengaturan")) {
                renderTabSettings();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void LuciferGui::renderTabStatus() {
    float px = 0, py = 0;
    int tx = 0, ty = 0;
    world_.getLocalPos(px, py, tx, ty);
    std::string wname = world_.getWorldName();

    ImGui::BeginChild("StatusCard", ImVec2(0, 240), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "📊 INFORMASI BOT & KONEKSI");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Columns(2, "StatusCols", false);
    ImGui::SetColumnWidth(0, 360.0f);

    ImGui::Text("Akun           : %s", info_.account_name.c_str());
    ImGui::Text("Versi Client   : %s (Protocol: 225)", info_.version.c_str());
    ImGui::Text("Game Server    : %s:%d", info_.game_host.c_str(), info_.game_port);
    ImGui::Text("NetID Bot      : %d", world_.getLocalNetId());

    ImGui::NextColumn();

    ImGui::Text("World Saat Ini : %s", wname.empty() ? "(Belum masuk world)" : wname.c_str());
    ImGui::Text("Posisi Tile    : X: %d, Y: %d", tx, ty);
    ImGui::Text("Posisi Pixel   : X: %.1f, Y: %.1f", px, py);
    ImGui::Text("Status AutoFarm: %s", autofarm_.isRunning() ? "SEDANG BERJALAN" : "IDLE");

    ImGui::Columns(1);
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::BeginChild("QuickActionsCard", ImVec2(0, 160), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "⚡ QUICK CONTROLS");
    ImGui::Separator();
    ImGui::Spacing();

    // Warp input
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("Nama World", warp_input_, sizeof(warp_input_));
    ImGui::SameLine();
    if (ImGui::Button("🚀 Warp Sekarang", ImVec2(140, 0))) {
        if (strlen(warp_input_) > 0) {
            actions_.warp(warp_input_);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("🔄 Respawn", ImVec2(100, 0))) {
        actions_.respawn();
    }

    ImGui::Spacing();
    if (ImGui::Button("🚪 Masuk Pintu / Portal di Posisi Bot", ImVec2(280, 0))) {
        actions_.enter();
    }
    ImGui::SameLine();
    if (ImGui::Button("🧲 Collect Item Sekitar", ImVec2(200, 0))) {
        actions_.collectNearby(collect_radius_);
    }

    ImGui::EndChild();
}

void LuciferGui::renderTabActions() {
    float px = 0, py = 0;
    int tx = 0, ty = 0;
    world_.getLocalPos(px, py, tx, ty);

    ImGui::BeginChild("MovementCard", ImVec2(0, 190), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "🚶 PERGERAKAN (MOVEMENT)");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Posisi Bot Saat Ini: Tile (%d, %d)", tx, ty);
    ImGui::Spacing();

    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("Target Tile X##move", &move_x_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("Target Tile Y##move", &move_y_);
    ImGui::SameLine();
    if (ImGui::Button("Gerak Halus (Smooth Walk)", ImVec2(180, 0))) {
        actions_.moveTo(move_x_, move_y_, 35);
    }
    ImGui::SameLine();
    if (ImGui::Button("Set Pos Bot ke Sini", ImVec2(150, 0))) {
        move_x_ = tx;
        move_y_ = ty;
    }

    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::BeginChild("TileActionsCard", ImVec2(0, 230), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "🧱 AKSI TILE (PUNCH / PLACE / WRENCH)");
    ImGui::Separator();
    ImGui::Spacing();

    // Punch
    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("Tile X##punch", &punch_x_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("Tile Y##punch", &punch_y_);
    ImGui::SameLine();
    if (ImGui::Button("👊 Pukul (Punch Tile)", ImVec2(160, 0))) {
        actions_.hit(punch_x_, punch_y_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Pukul Tile Bot", ImVec2(130, 0))) {
        actions_.hit(tx, ty);
    }

    ImGui::Spacing();
    // Place
    ImGui::SetNextItemWidth(90);
    ImGui::InputInt("Item ID##place", &place_id_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::InputInt("Tile X##place", &place_x_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::InputInt("Tile Y##place", &place_y_);
    ImGui::SameLine();
    if (ImGui::Button("🧱 Pasang (Place Block)", ImVec2(160, 0))) {
        actions_.place(static_cast<uint16_t>(place_id_), place_x_, place_y_);
    }

    ImGui::Spacing();
    // Wrench
    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("Tile X##wrench", &wrench_x_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("Tile Y##wrench", &wrench_y_);
    ImGui::SameLine();
    if (ImGui::Button("🔧 Wrench Tile", ImVec2(160, 0))) {
        actions_.wrench(wrench_x_, wrench_y_);
    }

    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::BeginChild("ChatCard", ImVec2(0, 110), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "💬 CHAT IN-GAME");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SetNextItemWidth(ImGui::GetWindowWidth() - 170);
    ImGui::InputText("##chatinput", chat_input_, sizeof(chat_input_));
    ImGui::SameLine();
    if (ImGui::Button("Kirim Pesan", ImVec2(140, 0))) {
        if (strlen(chat_input_) > 0) {
            actions_.say(chat_input_);
            chat_input_[0] = '\0';
        }
    }
    ImGui::EndChild();
}

void LuciferGui::renderTabInventory() {
    auto inv = world_.getInventory();
    uint32_t slots = world_.getInventorySlots();

    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "🎒 BACKPACK (%zu / %u SLOTS)", inv.size(), slots);
    ImGui::SameLine(ImGui::GetWindowWidth() - 360);

    // Drop & Trash quick input
    ImGui::SetNextItemWidth(70);
    ImGui::InputInt("ID##drop", &drop_id_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    ImGui::InputInt("Qty##drop", &drop_count_);
    ImGui::SameLine();
    if (ImGui::Button("Drop")) {
        actions_.drop(static_cast<uint16_t>(drop_id_), drop_count_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Trash")) {
        actions_.trash(static_cast<uint16_t>(drop_id_), drop_count_);
    }

    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::BeginTable("InventoryTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Slot #", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Item ID", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Jumlah", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Aksi Cepat", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < inv.size(); ++i) {
            const auto& item = inv[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("#%zu", i + 1);

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", item.id);

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("x%d", (int)item.count);

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%s", item.flags ? "Equipped" : "-");

            ImGui::TableSetColumnIndex(4);
            std::string drop_lbl = "Drop 1##" + std::to_string(i);
            std::string drop_all_lbl = "Drop All##" + std::to_string(i);
            std::string farm_lbl = "Pilih ke AutoFarm##" + std::to_string(i);

            if (ImGui::SmallButton(drop_lbl.c_str())) {
                actions_.drop(item.id, 1);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(drop_all_lbl.c_str())) {
                actions_.drop(item.id, item.count);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(farm_lbl.c_str())) {
                farm_id_ = item.id;
                place_id_ = item.id;
            }
        }
        ImGui::EndTable();
    }
}

void LuciferGui::renderTabWorld() {
    auto players = world_.getPlayers();
    auto dropped = world_.getDroppedItems();
    std::string wname = world_.getWorldName();

    ImGui::Columns(2, "WorldCols", false);
    ImGui::SetColumnWidth(0, 320.0f);

    ImGui::BeginChild("WorldDetails", ImVec2(0, 200), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "🌍 INFORMASI WORLD");
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("Nama World   : %s", wname.empty() ? "(none)" : wname.c_str());
    ImGui::Text("Dimensi      : %d x %d", world_.getWidth(), world_.getHeight());
    ImGui::Text("Total Pemain : %zu", players.size());
    ImGui::Text("Item Jatuh   : %zu objek", dropped.size());

    ImGui::Spacing();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("Radius", &collect_radius_, 1.0f, 10.0f, "%.1f tile");
    if (ImGui::Button("🧲 Ambil Semua Drop di Sekitar", ImVec2(240, 0))) {
        actions_.collectNearby(collect_radius_);
    }
    ImGui::EndChild();

    ImGui::NextColumn();

    ImGui::BeginChild("PlayersTableCard", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "👥 DAFTAR PEMAIN DI WORLD (%zu)", players.size());
    ImGui::Separator();

    if (ImGui::BeginTable("PlayersTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("NetID", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Nama", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("UserID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Country", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Pos (Tile)", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableHeadersRow();

        for (const auto& p : players) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", p.net_id);

            ImGui::TableSetColumnIndex(1);
            std::string clean = WorldState::stripColorCodes(p.name);
            if (p.is_local) {
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "%s (BOT KITA)", clean.c_str());
            } else {
                ImGui::Text("%s", clean.c_str());
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", p.user_id);

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%s", p.country.c_str());

            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d, %d", (int)(p.x / 32.0f), (int)(p.y / 32.0f));
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::Columns(1);
}

void LuciferGui::renderTabAutoFarm() {
    bool is_farming = autofarm_.isRunning();

    ImGui::BeginChild("AutoFarmControls", ImVec2(0, 260), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "🚜 LUCIFER AUTO-FARM ENGINE");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Columns(2, "FarmCols", false);
    ImGui::SetColumnWidth(0, 420.0f);

    ImGui::SetNextItemWidth(140);
    ImGui::InputInt("Item ID Blok / Seed", &farm_id_);

    float px = 0, py = 0;
    int tx = 0, ty = 0;
    world_.getLocalPos(px, py, tx, ty);

    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("Tile X##farm", &farm_x_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("Tile Y##farm", &farm_y_);
    ImGui::SameLine();
    if (ImGui::Button("Gunakan Posisi Bot")) {
        farm_x_ = tx;
        farm_y_ = ty + 1; // 1 tile di bawah bot
    }

    ImGui::SetNextItemWidth(220);
    ImGui::SliderInt("Delay Aksi (ms)", &farm_delay_, 100, 400);

    ImGui::SetNextItemWidth(220);
    ImGui::SliderInt("Pukulan per Blok", &farm_hits_, 1, 8);

    ImGui::NextColumn();

    ImGui::Text("Status Mesin Auto-Farm:");
    if (is_farming) {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "● AKTIF - SEDANG MEMANEN & MEMASANG");
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.25f, 0.25f, 1.0f));
        if (ImGui::Button("⏹ HENTIKAN AUTO-FARM", ImVec2(240, 50))) {
            autofarm_.stop();
        }
        ImGui::PopStyleColor(2);
    } else {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "● NONAKTIF (IDLE)");
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.75f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.88f, 0.35f, 1.0f));
        if (ImGui::Button("▶ MULAI AUTO-FARM", ImVec2(240, 50))) {
            autofarm_.start(static_cast<uint16_t>(farm_id_), farm_x_, farm_y_, farm_delay_, farm_hits_);
        }
        ImGui::PopStyleColor(2);
    }

    int current_stock = world_.getItemCount(static_cast<uint16_t>(farm_id_));
    ImGui::Spacing();
    ImGui::Text("Sisa Stok di Backpack: %d item", current_stock);

    ImGui::Columns(1);
    ImGui::EndChild();
}

void LuciferGui::renderTabConsole() {
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "📜 REALTIME CONSOLE LOGS");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(250);
    ImGui::InputTextWithHint("##logfilter", "Cari log...", log_filter_, sizeof(log_filter_));
    ImGui::SameLine();
    if (ImGui::Button("Hapus Log")) {
        std::lock_guard<std::mutex> lock(Logger::mtx);
        Logger::history.clear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-Scroll", &auto_scroll_log_);

    ImGui::Separator();
    ImGui::Spacing();

    ImGui::BeginChild("ConsoleScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    std::vector<std::string> logs_copy;
    {
        std::lock_guard<std::mutex> lock(Logger::mtx);
        logs_copy = Logger::history;
    }

    std::string filter_str = log_filter_;
    std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);

    for (const auto& line : logs_copy) {
        if (!filter_str.empty()) {
            std::string line_lower = line;
            std::transform(line_lower.begin(), line_lower.end(), line_lower.begin(), ::tolower);
            if (line_lower.find(filter_str) == std::string::npos) {
                continue;
            }
        }

        // Color coding
        if (line.find("[+]") != std::string::npos || line.find("ONLINE") != std::string::npos) {
            ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.45f, 1.0f), "%s", line.c_str());
        } else if (line.find("[!]") != std::string::npos || line.find("ERROR") != std::string::npos) {
            ImGui::TextColored(ImVec4(0.95f, 0.30f, 0.30f, 1.0f), "%s", line.c_str());
        } else if (line.find("[*]") != std::string::npos || line.find("ACTION") != std::string::npos) {
            ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.35f, 1.0f), "%s", line.c_str());
        } else if (line.find("[ENet]") != std::string::npos) {
            ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.95f, 1.0f), "%s", line.c_str());
        } else {
            ImGui::TextUnformatted(line.c_str());
        }
    }

    if (auto_scroll_log_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
}

void LuciferGui::renderTabSettings() {
    ImGui::BeginChild("SettingsCard", ImVec2(0, 340), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "⚙️ KONFIGURASI (.ENV)");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SetNextItemWidth(360);
    ImGui::InputText("Server Host", cfg_server_ip_, sizeof(cfg_server_ip_));

    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("Port", &cfg_server_port_);

    ImGui::SetNextItemWidth(200);
    ImGui::InputText("GrowID / Akun", cfg_account_name_, sizeof(cfg_account_name_));

    ImGui::Spacing();
    ImGui::Checkbox("Verbose Logging", &Logger::verbose);
    ImGui::SameLine();
    ImGui::Checkbox("Hexdump Logging", &Logger::hexdump);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Catatan: Perubahan konfigurasi server akan berlaku pada saat koneksi ulang.");

    ImGui::EndChild();
}
