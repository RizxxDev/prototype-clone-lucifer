#include "gui.hpp"
#include "enet_client.hpp"
#include "world.hpp"
#include "bot_actions.hpp"
#include "cli.hpp"
#include "logger.hpp"
#include "env.hpp"
#include "lua_engine.hpp"

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
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

LuciferGui::LuciferGui(EnetClient& enet, WorldState& world, BotActions& actions,
                       AutoFarmController& autofarm, LuaEngine& lua_engine,
                       BotInfo& info, std::atomic<bool>& running)
    : enet_(enet), world_(world), actions_(actions),
      autofarm_(autofarm), lua_engine_(lua_engine), info_(info), running_(running) {
    g_current_gui = this;

    strncpy_s(cfg_account_name_, info_.account_name.c_str(), sizeof(cfg_account_name_) - 1);
    strncpy_s(cfg_server_ip_, info_.game_host.c_str(), sizeof(cfg_server_ip_) - 1);
    cfg_server_port_ = info_.game_port;

    const char* default_script =
        "-- Lucifer Lua Script v2.86\n"
        "-- Tekan tombol [Jalankan Script] di atas untuk mengeksekusi\n\n"
        "log(\"🚀 Lucifer Lua Script dimulai!\")\n"
        "local world = bot:getWorld()\n"
        "log(\"Nama World: \" .. world.name .. \" (\" .. world.width .. \"x\" .. world.height .. \")\")\n\n"
        "local me = bot:getLocal()\n"
        "log(\"Posisi Bot: Tile (\" .. me.tile_x .. \", \" .. me.tile_y .. \")\")\n\n"
        "-- Contoh: Bergerak dengan bot\n"
        "bot:moveTo(me.tile_x + 1, me.tile_y, 40)\n"
        "sleep(300)\n"
        "bot:moveTo(me.tile_x, me.tile_y, 40)\n"
        "log(\"✅ Script selesai!\")\n";
    strncpy_s(script_buffer_, default_script, sizeof(script_buffer_) - 1);
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
    colors[ImGuiCol_Button]                = ImVec4(0.75f, 0.18f, 0.22f, 1.00f);
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

    BOOL useDarkMode = TRUE;
    ::DwmSetWindowAttribute(hWnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));

    if (!createDeviceD3D(hWnd_)) {
        cleanupDeviceD3D();
        ::UnregisterClassW(wc_.lpszClassName, wc_.hInstance);
        return false;
    }

    ::ShowWindow(hWnd_, SW_SHOWDEFAULT);
    ::UpdateWindow(hWnd_);

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

        enet_.poll();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        renderTabs();

        ImGui::Render();
        const float clear_color[4] = { 0.08f, 0.09f, 0.12f, 1.00f };
        pd3dDeviceContext_->OMSetRenderTargets(1, &mainRenderTargetView_, nullptr);
        pd3dDeviceContext_->ClearRenderTargetView(mainRenderTargetView_, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        pSwapChain_->Present(1, 0); // VSync ~60 FPS
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
            if (ImGui::BeginTabItem("🚶 Gerak (Movement)")) {
                renderTabMovement();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("🎒 Inventory")) {
                renderTabInventory();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("🌍 World & Pemain")) {
                renderTabWorld();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("📜 Script Editor (Lua)")) {
                renderTabScriptEditor();
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

    ImGui::BeginChild("StatusCard", ImVec2(0, 220), true);
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
    ImGui::Text("Status Lua     : %s", lua_engine_.isRunning() ? "SEDANG BERJALAN" : "IDLE");

    ImGui::Columns(1);
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::BeginChild("QuickActionsCard", ImVec2(0, 150), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "⚡ QUICK CONTROLS");
    ImGui::Separator();
    ImGui::Spacing();

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

void LuciferGui::renderTabMovement() {
    float px = 0, py = 0;
    int tx = 0, ty = 0;
    world_.getLocalPos(px, py, tx, ty);

    ImGui::BeginChild("MovementControls", ImVec2(0, 360), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "🚶 KONTROL PERGERAKAN (TANDA PANAH)");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Posisi Bot Saat Ini: Tile (%d, %d) | Pixel (%.1f, %.1f)", tx, ty, px, py);
    ImGui::TextDisabled("Gunakan tombol panah di bawah atau tombol panah pada keyboard (Up, Down, Left, Right).");
    ImGui::Spacing();

    // Step size
    ImGui::Text("Langkah (Step):");
    ImGui::SameLine();
    if (ImGui::RadioButton("1 Tile", move_step_ == 1)) move_step_ = 1;
    ImGui::SameLine();
    if (ImGui::RadioButton("2 Tiles", move_step_ == 2)) move_step_ = 2;
    ImGui::SameLine();
    if (ImGui::RadioButton("3 Tiles", move_step_ == 3)) move_step_ = 3;
    ImGui::SameLine();
    if (ImGui::RadioButton("5 Tiles", move_step_ == 5)) move_step_ = 5;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Keyboard Arrow Keys navigation
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            actions_.moveTo(tx, ty - move_step_, 25);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            actions_.moveTo(tx, ty + move_step_, 25);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            actions_.moveTo(tx - move_step_, ty, 25);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            actions_.moveTo(tx + move_step_, ty, 25);
        }
    }

    // D-Pad Arrow Buttons Layout
    float btn_w = 110.0f;
    float btn_h = 45.0f;
    float center_x = 220.0f;

    // Up Arrow Button
    ImGui::SetCursorPosX(center_x);
    if (ImGui::Button(" ▲ ATAS ", ImVec2(btn_w, btn_h))) {
        actions_.moveTo(tx, ty - move_step_, 25);
    }

    ImGui::Spacing();

    // Left, Center, Right Arrow Buttons
    ImGui::SetCursorPosX(center_x - btn_w - 10.0f);
    if (ImGui::Button(" ◄ KIRI ", ImVec2(btn_w, btn_h))) {
        actions_.moveTo(tx - move_step_, ty, 25);
    }
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(btn_w, btn_h)); // Spacer tengah
    ImGui::SameLine();
    if (ImGui::Button(" KANAN ► ", ImVec2(btn_w, btn_h))) {
        actions_.moveTo(tx + move_step_, ty, 25);
    }

    ImGui::Spacing();

    // Down Arrow Button
    ImGui::SetCursorPosX(center_x);
    if (ImGui::Button(" ▼ BAWAH ", ImVec2(btn_w, btn_h))) {
        actions_.moveTo(tx, ty + move_step_, 25);
    }

    ImGui::EndChild();
}

void LuciferGui::renderTabInventory() {
    auto inv = world_.getInventory();

    ImGui::BeginChild("InvTopBar", ImVec2(0, 50), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "🎒 INVENTORY BOT (%zu ITEM DI TAS)", inv.size());
    ImGui::SameLine(ImGui::GetWindowWidth() - 250);
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##invfilter", "Cari Item ID...", inv_filter_, sizeof(inv_filter_));
    ImGui::EndChild();

    ImGui::Spacing();

    if (ImGui::BeginTable("InventoryTableClean", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("No", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Item ID", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Jumlah (Count)", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        std::string filter_str = inv_filter_;

        int display_idx = 1;
        for (size_t i = 0; i < inv.size(); ++i) {
            const auto& item = inv[i];

            std::string id_str = std::to_string(item.id);
            if (!filter_str.empty() && id_str.find(filter_str) == std::string::npos) {
                continue;
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("#%d", display_idx++);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.30f, 1.0f), "ID: %u", item.id);

            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.45f, 1.0f), "x%d", (int)item.count);

            ImGui::TableSetColumnIndex(3);
            if (item.flags) {
                ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "Equipped");
            } else {
                ImGui::TextDisabled("Di Tas");
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

void LuciferGui::renderTabScriptEditor() {
    bool is_running = lua_engine_.isRunning();

    ImGui::BeginChild("ScriptTopBar", ImVec2(0, 105), true);

    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "📜 LUCIFER LUA SCRIPT ENGINE v2.86");
    ImGui::SameLine();
    if (is_running) {
        float elapsed_sec = lua_engine_.getExecutionTimeMs() / 1000.0f;
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "● SEDANG BERJALAN (%.1fs)", elapsed_sec);
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "● IDLE (Siap dijalankan)");
    }

    ImGui::Spacing();

    if (is_running) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.25f, 0.25f, 1.0f));
        if (ImGui::Button("⏹ HENTIKAN SCRIPT", ImVec2(180, 32))) {
            lua_engine_.stopScript();
        }
        ImGui::PopStyleColor(2);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.75f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.88f, 0.35f, 1.0f));
        if (ImGui::Button("▶ JALANKAN SCRIPT", ImVec2(180, 32))) {
            lua_engine_.runScript(script_buffer_);
        }
        ImGui::PopStyleColor(2);
    }

    ImGui::SameLine();
    if (ImGui::Button("🧹 Bersihkan", ImVec2(90, 32))) {
        script_buffer_[0] = '\0';
    }

    ImGui::SameLine();
    ImGui::TextDisabled("| Template:");
    ImGui::SameLine();
    if (ImGui::SmallButton("Patroli Jalan")) {
        const char* t =
            "-- Template: Walk Patrol\n"
            "local me = bot:getLocal()\n"
            "log(\"Start Patrol from \" .. me.tile_x)\n"
            "bot:moveTo(me.tile_x + 3, me.tile_y, 40)\n"
            "sleep(500)\n"
            "bot:moveTo(me.tile_x, me.tile_y, 40)\n"
            "log(\"Patrol complete!\")\n";
        strncpy_s(script_buffer_, t, sizeof(script_buffer_) - 1);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Spam Chat")) {
        const char* t =
            "-- Template: Spam Chat\n"
            "for i = 1, 3 do\n"
            "    bot:say(\"Hello from Lucifer Lua Bot! #\" .. i)\n"
            "    sleep(1000)\n"
            "end\n";
        strncpy_s(script_buffer_, t, sizeof(script_buffer_) - 1);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Cek Tas & Pemain")) {
        const char* t =
            "-- Template: Inventory & Player Scanner\n"
            "local inv = bot:getInventory()\n"
            "log(\"Total item di tas: \" .. #inv)\n"
            "for i, item in ipairs(inv) do\n"
            "    log(\"  Item ID: \" .. item.id .. \" x\" .. item.count)\n"
            "end\n"
            "local players = bot:getPlayers()\n"
            "log(\"Total pemain di world: \" .. #players)\n"
            "for i, p in ipairs(players) do\n"
            "    log(\"  Pemain: \" .. p.name .. \" (NetID: \" .. p.net_id .. \")\")\n"
            "end\n";
        strncpy_s(script_buffer_, t, sizeof(script_buffer_) - 1);
    }

    std::string err = lua_engine_.getLastError();
    if (!err.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.95f, 0.25f, 0.25f, 1.0f), "❌ Error: %s", err.c_str());
    }

    ImGui::EndChild();

    ImGui::Spacing();

    ImGui::BeginChild("ScriptEditorArea", ImVec2(0, 0), false);
    ImGui::InputTextMultiline("##luascriptcode", script_buffer_, sizeof(script_buffer_),
                              ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_AllowTabInput);
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
