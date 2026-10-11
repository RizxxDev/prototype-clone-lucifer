#include "gui.hpp"
#include "enet_client.hpp"
#include "world.hpp"
#include "bot_actions.hpp"
#include "cli.hpp"
#include "logger.hpp"
#include "env.hpp"
#include "lua_engine.hpp"
#include "anti_detection.hpp"
#include "reconnect_controller.hpp"
#include "socks5.hpp"
#include "bot_manager.hpp"
#include "item_database.hpp"
#include "http_client.hpp"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "TextEditor.h"

#include <dwmapi.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <deque>

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
                       BotInfo& info, std::atomic<bool>& running,
                       DeviceFingerprint& fingerprint, ReconnectController& reconnect_ctrl,
                       std::function<void()> reconnect_fn)
    : enet_(enet), world_(world), actions_(actions),
      autofarm_(autofarm), lua_engine_(lua_engine), info_(info), running_(running),
      fingerprint_(fingerprint), reconnect_ctrl_(reconnect_ctrl), reconnect_fn_(reconnect_fn) {
    g_current_gui = this;

    strncpy_s(cfg_account_name_, info_.account_name.c_str(), sizeof(cfg_account_name_) - 1);
    strncpy_s(cfg_server_ip_, info_.game_host.c_str(), sizeof(cfg_server_ip_) - 1);
    cfg_server_port_ = info_.game_port;
    if (!info_.account_name.empty()) {
        gui_stats_.loadFromFile(info_.account_name);
    }
    lua_engine_.setStats(&gui_stats_);

    text_editor_ = std::make_unique<TextEditor>();
    auto lang = TextEditor::LanguageDefinition::Lua();
    static const char* const custom_identifiers[] = {
        "addBot", "getBot", "getBots", "bot", "world", "log", "sleep",
        "GetLocal", "GetWorld", "GetInventory", "GetTiles", "GetTile",
        "GetObjectList", "GetPlayerList", "GetPlayer", "MakeRequest",
        "AddHook", "RemoveHook", "RemoveHooks", "SendPacket", "SendPacketRaw",
        "SendVariantList", "FindPath", "CheckPath", "LogToConsole"
    };
    for (const auto& id_name : custom_identifiers) {
        TextEditor::Identifier id;
        id.mDeclaration = std::string("Lucifer API: ") + id_name;
        lang.mIdentifiers.insert(std::make_pair(std::string(id_name), id));
    }
    text_editor_->SetLanguageDefinition(lang);

    TextEditor::Palette pal = TextEditor::GetDarkPalette();
    pal[(size_t)TextEditor::PaletteIndex::Background] = 0xff100803; // Dark navy #030810
    pal[(size_t)TextEditor::PaletteIndex::Cursor] = 0xffffff00; // Cyan cursor
    pal[(size_t)TextEditor::PaletteIndex::Selection] = 0x80905000; // Translucent blue
    pal[(size_t)TextEditor::PaletteIndex::LineNumber] = 0xff7a5838; // Soft slate blue #38587a
    pal[(size_t)TextEditor::PaletteIndex::CurrentLineFill] = 0x20301a08; // Subtle line highlight
    pal[(size_t)TextEditor::PaletteIndex::CurrentLineEdge] = 0x40603510;
    pal[(size_t)TextEditor::PaletteIndex::Keyword] = 0xffd69c56; // Vibrant orange/amber
    pal[(size_t)TextEditor::PaletteIndex::String] = 0xff70c070; // Soft green
    pal[(size_t)TextEditor::PaletteIndex::Number] = 0xff80e0e0; // Bright cyan
    pal[(size_t)TextEditor::PaletteIndex::Identifier] = 0xffe2e8f0; // Clean off-white
    pal[(size_t)TextEditor::PaletteIndex::KnownIdentifier] = 0xff60b0ff; // Neon sky blue
    pal[(size_t)TextEditor::PaletteIndex::Punctuation] = 0xffb0c0d0;
    text_editor_->SetPalette(pal);

    // Default kosong sesuai instruksi pengguna
    script_buffer_[0] = '\0';
    text_editor_->SetText("");
}

LuciferGui::~LuciferGui() {
    shutdown();
    g_current_gui = nullptr;
}

void LuciferGui::setBotManager(BotManager* mgr) {
    bot_manager_ = mgr;
    lua_engine_.setBotManager(mgr);
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

static bool ToggleSwitch(const char* str_id, bool* v) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    float height = 18.0f;
    float width = 34.0f;
    float radius = height * 0.5f;

    if (ImGui::InvisibleButton(str_id, ImVec2(width, height))) {
        *v = !*v;
    }

    ImU32 col_bg;
    if (*v) {
        col_bg = ImGui::IsItemHovered() ? IM_COL32(0, 185, 255, 255) : IM_COL32(0, 150, 235, 255);
    } else {
        col_bg = ImGui::IsItemHovered() ? IM_COL32(28, 52, 78, 255) : IM_COL32(18, 36, 56, 255);
    }

    draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), col_bg, radius);
    draw_list->AddRect(p, ImVec2(p.x + width, p.y + height), IM_COL32(35, 75, 115, 180), radius, 0, 1.0f);

    float t = *v ? 1.0f : 0.0f;
    float circle_x = p.x + radius + t * (width - 2 * radius);
    float circle_y = p.y + radius;
    ImU32 knob_col = *v ? IM_COL32(255, 255, 255, 255) : IM_COL32(130, 155, 185, 255);
    draw_list->AddCircleFilled(ImVec2(circle_x, circle_y), radius - 2.5f, knob_col);

    return *v;
}

void LuciferGui::applyLuciferTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 5.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 5.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 4.0f;
    style.WindowBorderSize  = 0.0f;
    style.ChildBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.ItemSpacing       = ImVec2(6.0f, 6.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style.WindowPadding     = ImVec2(8.0f, 8.0f);
    style.FramePadding      = ImVec2(6.0f, 4.0f);

    // Deep Space Cyan / Navy Blue Palette (Lucifer v2.85)
    colors[ImGuiCol_Text]                  = ImVec4(0.90f, 0.95f, 0.99f, 1.00f);
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.42f, 0.54f, 0.68f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.027f, 0.051f, 0.086f, 1.00f); // #070d16
    colors[ImGuiCol_ChildBg]               = ImVec4(0.043f, 0.090f, 0.149f, 0.96f); // #0b1726
    colors[ImGuiCol_PopupBg]               = ImVec4(0.040f, 0.080f, 0.135f, 0.98f);
    colors[ImGuiCol_Border]                = ImVec4(0.078f, 0.200f, 0.322f, 0.90f); // #143352
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.035f, 0.086f, 0.141f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.051f, 0.133f, 0.220f, 1.00f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.071f, 0.188f, 0.310f, 1.00f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.020f, 0.043f, 0.075f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.027f, 0.055f, 0.094f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.020f, 0.043f, 0.075f, 1.00f);
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.035f, 0.075f, 0.125f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.025f, 0.050f, 0.080f, 0.50f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.090f, 0.220f, 0.350f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.120f, 0.290f, 0.460f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.000f, 0.550f, 0.880f, 1.00f);
    colors[ImGuiCol_CheckMark]             = ImVec4(0.000f, 0.650f, 1.000f, 1.00f);
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.000f, 0.580f, 0.920f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.200f, 0.750f, 1.000f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.047f, 0.157f, 0.267f, 1.00f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.075f, 0.235f, 0.392f, 1.00f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.000f, 0.518f, 0.820f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.000f, 0.450f, 0.750f, 0.40f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.000f, 0.550f, 0.880f, 0.60f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.000f, 0.600f, 0.950f, 0.80f);
    colors[ImGuiCol_Separator]             = ImVec4(0.078f, 0.200f, 0.322f, 0.70f);
    colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.000f, 0.550f, 0.880f, 0.78f);
    colors[ImGuiCol_SeparatorActive]       = ImVec4(0.000f, 0.600f, 0.950f, 1.00f);
    colors[ImGuiCol_Tab]                   = ImVec4(0.039f, 0.086f, 0.145f, 1.00f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.075f, 0.235f, 0.392f, 0.85f);
    colors[ImGuiCol_TabActive]             = ImVec4(0.000f, 0.518f, 0.820f, 1.00f);
    colors[ImGuiCol_TabUnfocused]          = ImVec4(0.030f, 0.065f, 0.110f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.055f, 0.150f, 0.250f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.050f, 0.115f, 0.190f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.090f, 0.220f, 0.350f, 1.00f);
    colors[ImGuiCol_TableBorderLight]      = ImVec4(0.065f, 0.160f, 0.260f, 1.00f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.000f, 1.000f, 1.000f, 0.02f);
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
        L"Lucifer v2.85 [discord.gg/nuron]",
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
    if (bot_manager_) {
        bot_manager_->saveAccountsToFile(accounts_file_path_);
    }

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

        if (reconnect_fn_) {
            reconnect_ctrl_.update(reconnect_fn_);
        }

        if (bot_manager_) {
            bot_manager_->pollAll();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        renderMainLayout();

        // Trigger Bothax Lua OnDraw hook
        lua_engine_.triggerOnDraw(ImGui::GetIO().DeltaTime);

        ImGui::Render();
        const float clear_color[4] = { 0.027f, 0.051f, 0.086f, 1.00f };
        pd3dDeviceContext_->OMSetRenderTargets(1, &mainRenderTargetView_, nullptr);
        pd3dDeviceContext_->ClearRenderTargetView(mainRenderTargetView_, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        pSwapChain_->Present(1, 0); // VSync ~60 FPS
    }
}

void LuciferGui::renderMainLayout() {
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));

    if (ImGui::Begin("LuciferMainWindow", nullptr, window_flags)) {
        // Cache bot list once per frame — semua sub-render pakai cached_bots_
        refreshBotCache();

        renderTitleBar();
        renderTopNav();

        ImGui::Spacing();

        switch (current_top_tab_) {
        case LuciferTopTab::Bots:     renderTabBots(); break;
        case LuciferTopTab::List:     renderTabList(); break;
        case LuciferTopTab::Executor: renderTabExecutor(); break;
        case LuciferTopTab::Proxy:    renderTabProxy(); break;
        case LuciferTopTab::Switch:   renderTabSwitch(); break;
        case LuciferTopTab::Database: renderTabDatabase(); break;
        case LuciferTopTab::Settings: renderTabSettings(); break;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void LuciferGui::renderTitleBar() {
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    // Background bar
    draw->AddRectFilled(pos, ImVec2(pos.x + avail_w, pos.y + 24.0f), IM_COL32(8, 17, 28, 255), 3.0f);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);

    // Title centered
    const char* title_text = "Lucifer v2.85 [discord.gg/nuron]";
    float title_w = ImGui::CalcTextSize(title_text).x;
    ImGui::SetCursorPosX((avail_w - title_w) * 0.5f);
    ImGui::TextColored(ImVec4(0.85f, 0.92f, 0.98f, 1.0f), "%s", title_text);

    // Window action icons on far right
    ImGui::SameLine(avail_w - 96.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.15f, 0.20f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.20f, 0.25f, 1.0f));
    if (ImGui::Button("+##title_plus", ImVec2(18, 16))) {}
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    if (ImGui::Button("🔒##title_lock", ImVec2(18, 16))) {}

    ImGui::SameLine();
    if (ImGui::Button("🔄##title_reload", ImVec2(18, 16))) {
        if (reconnect_fn_) reconnect_fn_();
    }

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.15f, 0.18f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.20f, 0.22f, 1.0f));
    if (ImGui::Button("⏻##title_power", ImVec2(20, 16))) {
        running_ = false;
    }
    ImGui::PopStyleColor(2);

    ImGui::Spacing();
}

void LuciferGui::renderTopNav() {
    struct NavItem {
        const char* label;
        LuciferTopTab tab;
    };
    static const NavItem items[] = {
        { "🤖 Bots", LuciferTopTab::Bots },
        { "📋 List", LuciferTopTab::List },
        { "</> Executor", LuciferTopTab::Executor },
        { "🌐 Proxy", LuciferTopTab::Proxy },
        { "🔀 Switch", LuciferTopTab::Switch },
        { "🗄️ Database", LuciferTopTab::Database },
        { "⚙️ Settings", LuciferTopTab::Settings },
    };

    float tab_w = 118.0f;
    float tab_h = 28.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));

    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); ++i) {
        if (i > 0) ImGui::SameLine();

        bool is_active = (current_top_tab_ == items[i].tab);

        if (is_active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.42f, 0.70f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.50f, 0.82f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.035f, 0.080f, 0.135f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.065f, 0.160f, 0.260f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.58f, 0.72f, 0.88f, 1.0f));
        }

        if (ImGui::Button(items[i].label, ImVec2(tab_w, tab_h))) {
            current_top_tab_ = items[i].tab;
        }

        if (is_active) {
            ImVec2 p0 = ImGui::GetItemRectMin();
            ImVec2 p1 = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(p0.x, p1.y - 2.5f),
                ImVec2(p1.x, p1.y),
                IM_COL32(0, 185, 255, 255)
            );
        }

        ImGui::PopStyleColor(3);
    }

    ImGui::PopStyleVar(2);
    ImGui::Separator();
}

void LuciferGui::renderTabBots() {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float sidebar_w = 230.0f;

    renderSidebar(sidebar_w, avail.y);

    ImGui::SameLine();

    float content_w = avail.x - sidebar_w - 6.0f;
    ImGui::BeginChild("BotsContentArea", ImVec2(content_w, avail.y), false);

    struct SubItem {
        const char* label;
        LuciferSubTab tab;
    };
    static const SubItem sub_items[] = {
        { "Main", LuciferSubTab::Main },
        { "World", LuciferSubTab::World },
        { "Inventory", LuciferSubTab::Inventory },
        { "Console", LuciferSubTab::Console },
        { "Automation", LuciferSubTab::Automation },
        { "Rotation", LuciferSubTab::Rotation },
        { "Logs", LuciferSubTab::Logs },
        { "RID", LuciferSubTab::RID },
    };

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));

    float sub_w = 80.0f;
    float sub_h = 24.0f;

    for (size_t i = 0; i < sizeof(sub_items) / sizeof(sub_items[0]); ++i) {
        if (i > 0) ImGui::SameLine();

        bool is_active = (current_sub_tab_ == sub_items[i].tab);

        if (is_active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.38f, 0.65f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.48f, 0.78f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.038f, 0.082f, 0.138f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.065f, 0.155f, 0.245f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.68f, 0.82f, 1.0f));
        }

        if (ImGui::Button(sub_items[i].label, ImVec2(sub_w, sub_h))) {
            current_sub_tab_ = sub_items[i].tab;
        }

        if (is_active) {
            ImVec2 p0 = ImGui::GetItemRectMin();
            ImVec2 p1 = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(p0.x, p1.y - 2.0f),
                ImVec2(p1.x, p1.y),
                IM_COL32(0, 185, 255, 255)
            );
        }

        ImGui::PopStyleColor(3);
    }
    ImGui::PopStyleVar(2);

    ImGui::Spacing();

    ImVec2 sub_avail = ImGui::GetContentRegionAvail();

    switch (current_sub_tab_) {
    case LuciferSubTab::Main:
        renderSubTabMain(sub_avail.x, sub_avail.y);
        break;
    case LuciferSubTab::World:
        renderSubTabWorld();
        break;
    case LuciferSubTab::Inventory:
        renderSubTabInventory();
        break;
    case LuciferSubTab::Console:
        renderSubTabConsole();
        break;
    case LuciferSubTab::Automation:
        renderSubTabAutomation();
        break;
    case LuciferSubTab::Rotation:
        renderSubTabRotation();
        break;
    case LuciferSubTab::Logs:
        renderSubTabLogs();
        break;
    case LuciferSubTab::RID:
        renderSubTabRID();
        break;
    }

    ImGui::EndChild();
}

void LuciferGui::renderSidebar(float width, float height) {
    ImGui::BeginChild("BotsSidebar", ImVec2(width, height), true);

    // Row 1: Select All + Toggle Switch
    ImGui::Text("Select All");
    ImGui::SameLine(width - 48.0f);
    ToggleSwitch("SelectAllToggle", &select_all_);

    // Row 2: Enable Multi-Select
    ImGui::Checkbox("Enable Multi-Select", &enable_multiselect_);

    ImGui::Spacing();

    // Bot List Box
    float list_box_h = height - 165.0f;
    if (list_box_h < 100.0f) list_box_h = 100.0f;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.025f, 0.055f, 0.090f, 1.0f));
    ImGui::BeginChild("BotListBox", ImVec2(0, list_box_h), true);

    const auto& bots = cached_bots_;
    if (selected_bot_id_ <= 0 && !bots.empty() && bots[0]) {
        selected_bot_id_ = bots[0]->getId();
    }

    if (bots.empty()) {
        if (!info_.account_name.empty()) {
            bool is_online = enet_.isConnected();
            ImVec4 text_col = is_online ? ImVec4(0.18f, 0.95f, 0.40f, 1.0f) : ImVec4(0.60f, 0.70f, 0.80f, 1.0f);
            ImGui::TextColored(text_col, "%s", info_.account_name.c_str());
        } else {
            ImGui::Spacing();
            ImGui::TextDisabled(" Belum ada bot terdaftar.");
            ImGui::TextDisabled(" Klik '+ Add Bot' di bawah.");
        }
    } else {
        for (auto& bot : bots) {
            std::string name = bot->getAccountName();
            if (strlen(bot_search_) > 0) {
                std::string filter = bot_search_;
                std::string name_lower = name;
                std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
                std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
                if (name_lower.find(filter) == std::string::npos) continue;
            }

            bool is_selected = (selected_bot_id_ == bot->getId());
            bool is_online = bot->isOnline() || bot->isConnected();

            ImVec4 text_col = is_online ? ImVec4(0.18f, 0.95f, 0.40f, 1.0f) : ImVec4(0.65f, 0.70f, 0.75f, 1.0f);

            if (is_selected) {
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.06f, 0.28f, 0.48f, 0.90f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.08f, 0.35f, 0.58f, 0.95f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.05f, 0.15f, 0.25f, 0.50f));
            }

            ImGui::PushStyleColor(ImGuiCol_Text, text_col);

            std::string label = name + "##bot_" + std::to_string(bot->getId());
            if (ImGui::Selectable(label.c_str(), is_selected)) {
                selected_bot_id_ = bot->getId();
            }

            ImGui::PopStyleColor(3);
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // Search bar
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##BotSearch", "Search for bots..", bot_search_, sizeof(bot_search_));

    // Buttons: Load | Save
    float btn_w = (width - 24.0f) * 0.5f;
    if (ImGui::Button("Load", ImVec2(btn_w, 22.0f))) {
        if (bot_manager_) bot_manager_->loadAccountsFromFile(accounts_file_path_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save", ImVec2(btn_w, 22.0f))) {
        if (bot_manager_) bot_manager_->saveAccountsToFile(accounts_file_path_, true);
    }

    // Add Bot button
    if (ImGui::Button("Add Bot", ImVec2(-1, 22.0f))) {
        ImGui::OpenPopup("AddBotPopupModal");
    }

    // Remove Bot button
    if (ImGui::Button("Remove Bot", ImVec2(-1, 22.0f))) {
        if (bot_manager_ && selected_bot_id_ > 0) {
            bot_manager_->removeBot(selected_bot_id_);
            bot_manager_->saveAccountsToFile(accounts_file_path_);
            auto remaining = bot_manager_->getBots();
            if (!remaining.empty()) selected_bot_id_ = remaining[0]->getId();
            else selected_bot_id_ = 0;
        }
    }

    // Modal Add Bot
    if (ImGui::BeginPopupModal("AddBotPopupModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Tambah Bot Baru ke List:");
        ImGui::Separator();
        ImGui::InputText("GrowID / Username", new_bot_user_, sizeof(new_bot_user_));
        ImGui::InputText("Password", new_bot_pass_, sizeof(new_bot_pass_), ImGuiInputTextFlags_Password);
        ImGui::InputText("Target World", new_bot_world_, sizeof(new_bot_world_));

        ImGui::Spacing();
        if (ImGui::Button("Tambahkan", ImVec2(120, 0))) {
            if (strlen(new_bot_user_) > 0 && bot_manager_) {
                auto b = bot_manager_->addBot(new_bot_user_, new_bot_pass_, new_bot_world_);
                if (b) selected_bot_id_ = b->getId();
                bot_manager_->saveAccountsToFile(accounts_file_path_);
                new_bot_user_[0] = '\0';
                new_bot_pass_[0] = '\0';
                new_bot_world_[0] = '\0';
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Batal", ImVec2(100, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();
}

void LuciferGui::renderSubTabMain(float width, float height) {
    float card_w = (width - 8.0f) * 0.5f;
    float card_h = (height - 8.0f) * 0.5f;

    // Row 1: Bot Information (Top-Left) & Movement (Top-Right)
    renderCardBotInfo(card_w, card_h);
    ImGui::SameLine();
    renderCardMovement(card_w, card_h);

    ImGui::Spacing();

    // Row 2: Preferences (Bottom-Left) & Intervals (Bottom-Right)
    renderCardPreferences(card_w, card_h);
    ImGui::SameLine();
    renderCardIntervals(card_w, card_h);
}

void LuciferGui::renderCardBotInfo(float w, float h) {
    ImGui::BeginChild("CardBotInfo", ImVec2(w, h), true);

    ImGui::TextColored(ImVec4(0.85f, 0.95f, 1.0f, 1.0f), "👤 Bot Information");
    ImGui::Separator();
    ImGui::Spacing();

    std::shared_ptr<BotInstance> bot = (bot_manager_ && selected_bot_id_ > 0)
        ? bot_manager_->getBotById(selected_bot_id_) : nullptr;
    if (bot) {
        gui_stats_ = bot->getStats();
    }

    bool is_in_world = bot ? bot->isOnline() : (!world_.getWorldName().empty() && enet_.isConnected());
    bool is_connected = bot ? bot->isConnected() : enet_.isConnected();
    int ping = bot ? bot->getPing() : enet_.getPing();
    float px = 0, py = 0;
    int tx = 0, ty = 0;
    std::string wname;
    int gems = 0;

    if (bot) {
        bot->getWorld().getLocalPos(px, py, tx, ty);
        wname = bot->getWorld().getWorldName();
        gems = bot->getWorld().getItemCount(112);
    } else {
        world_.getLocalPos(px, py, tx, ty);
        wname = world_.getWorldName();
        gems = world_.getItemCount(112);
    }

    std::string wname_upper = wname.empty() ? "-" : wname;
    if (!wname.empty()) {
        std::transform(wname_upper.begin(), wname_upper.end(), wname_upper.begin(), ::toupper);
    }

    // 1. Status: Online [37?]
    ImGui::Text("Status: ");
    ImGui::SameLine();
    if (is_in_world) {
        ImGui::TextColored(ImVec4(0.18f, 0.95f, 0.40f, 1.0f), "Online (In World)");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.85f, 0.25f, 0.25f, 1.0f), " [%d]", ping > 0 ? ping : 37);
    } else if (is_connected) {
        ImGui::TextColored(ImVec4(0.20f, 0.75f, 1.0f, 1.0f), "Online (Lobby)");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.85f, 0.25f, 0.25f, 1.0f), " [%d]", ping > 0 ? ping : 37);
    } else {
        ImGui::TextColored(ImVec4(0.85f, 0.30f, 0.30f, 1.0f), "Offline");
    }

    // 2. Google: Idle (hanya saat online/terhubung)
    ImGui::Text("Google: ");
    ImGui::SameLine();
    if (is_connected) {
        ImGui::TextColored(ImVec4(0.18f, 0.95f, 0.40f, 1.0f), "Idle");
    } else {
        ImGui::TextColored(ImVec4(0.65f, 0.70f, 0.75f, 1.0f), "Offline");
    }

    int lvl = bot ? bot->getStats().level : gui_stats_.level;
    int gms = bot ? bot->getStats().gems : gui_stats_.gems;
    if (gms <= 0 && gems > 0) gms = gems;
    std::string gms_formatted = bot ? bot->getStats().getFormattedGems() : gui_stats_.getFormattedGems();
    if (gms_formatted == "0" && gms > 0) {
        gms_formatted = std::to_string(gms);
        int n = static_cast<int>(gms_formatted.length()) - 3;
        while (n > 0) {
            gms_formatted.insert(n, ",");
            n -= 3;
        }
    }
    float played_hrs = bot ? bot->getStats().getEffectivePlayedHours() : gui_stats_.getEffectivePlayedHours();
    int age_d = bot ? bot->getStats().getEffectiveAgeDays() : gui_stats_.getEffectiveAgeDays();

    // 3. Level: 1, Gems: 0
    ImGui::Text("Level: %d, Gems: %s", lvl, gms_formatted.c_str());

    // 4. World: NAME (X, Y)
    ImGui::Text("World: %s (%d, %d)", wname_upper.c_str(), tx, ty);

    // 5. Played 0.00 hours. Age 0 days.
    ImGui::Text("Played %.2f hours. Age %d days.", played_hrs, age_d);

    ImGui::Spacing();
    ImGui::Spacing();

    // Inputs: World Name + Leave + Warp
    ImGui::SetNextItemWidth(w - 150.0f);
    ImGui::InputTextWithHint("##WorldInput", "World Name", warp_input_, sizeof(warp_input_));
    ImGui::SameLine();
    if (ImGui::Button("Leave", ImVec2(60, 22))) {
        if (bot) bot->say("/quit");
        else actions_.say("/quit");
    }
    ImGui::SameLine();
    if (ImGui::Button("Warp", ImVec2(60, 22))) {
        if (strlen(warp_input_) > 0) {
            if (bot) {
                bot->warp(warp_input_);
                bot->setTargetWorld(warp_input_);
                if (bot_manager_) bot_manager_->saveAccountsToFile(accounts_file_path_);
            }
            else actions_.warp(warp_input_);
        }
    }

    ImGui::Spacing();

    // Connect & Disconnect buttons
    float btn_half = (w - 24.0f) * 0.5f;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.42f, 0.70f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.52f, 0.85f, 1.00f));
    if (ImGui::Button("Connect", ImVec2(btn_half, 26.0f))) {
        if (bot) bot->start();
        else if (reconnect_fn_) reconnect_fn_();
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.05f, 0.14f, 0.24f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.20f, 0.25f, 1.00f));
    if (ImGui::Button("Disconnect", ImVec2(btn_half, 26.0f))) {
        if (bot) bot->stop();
        else enet_.disconnect();
    }
    ImGui::PopStyleColor(2);

    ImGui::EndChild();
}

void LuciferGui::renderCardMovement(float w, float h) {
    ImGui::BeginChild("CardMovement", ImVec2(w, h), true);

    ImGui::TextColored(ImVec4(0.85f, 0.95f, 1.0f, 1.0f), "✥ Movement");
    ImGui::SameLine(w - 85.0f);
    if (ImGui::Button("Respawn", ImVec2(75.0f, 20.0f))) {
        std::shared_ptr<BotInstance> bot = (bot_manager_ && selected_bot_id_ > 0)
            ? bot_manager_->getBotById(selected_bot_id_) : nullptr;
        if (bot) bot->respawn();
        else actions_.respawn();
    }
    ImGui::Separator();
    ImGui::Spacing();

    std::shared_ptr<BotInstance> bot = (bot_manager_ && selected_bot_id_ > 0)
        ? bot_manager_->getBotById(selected_bot_id_) : nullptr;

    float px = 0, py = 0;
    int tx = 0, ty = 0;
    if (bot) bot->getWorld().getLocalPos(px, py, tx, ty);
    else world_.getLocalPos(px, py, tx, ty);

    // Centered Cross D-Pad
    float btn_size_w = 40.0f;
    float btn_size_h = 34.0f;
    float center_x = w * 0.5f;

    // Up:
    ImGui::SetCursorPosX(center_x - btn_size_w * 0.5f);
    if (ImGui::Button("↑##DpadUp", ImVec2(btn_size_w, btn_size_h))) {
        if (bot) bot->moveTo(tx, ty - move_step_);
        else actions_.moveTo(tx, ty - move_step_);
    }

    // Mid row: Left, Center (Door/Enter), Right
    ImGui::SetCursorPosX(center_x - btn_size_w * 1.5f - 4.0f);
    if (ImGui::Button("←##DpadLeft", ImVec2(btn_size_w, btn_size_h))) {
        if (bot) bot->moveTo(tx - move_step_, ty);
        else actions_.moveTo(tx - move_step_, ty);
    }
    ImGui::SameLine();
    if (ImGui::Button("🚪##DpadEnter", ImVec2(btn_size_w, btn_size_h))) {
        if (bot) bot->getActions().enter();
        else actions_.enter();
    }
    ImGui::SameLine();
    if (ImGui::Button("→##DpadRight", ImVec2(btn_size_w, btn_size_h))) {
        if (bot) bot->moveTo(tx + move_step_, ty);
        else actions_.moveTo(tx + move_step_, ty);
    }

    // Down:
    ImGui::SetCursorPosX(center_x - btn_size_w * 0.5f);
    if (ImGui::Button("↓##DpadDown", ImVec2(btn_size_w, btn_size_h))) {
        if (bot) bot->moveTo(tx, ty + move_step_);
        else actions_.moveTo(tx, ty + move_step_);
    }

    ImGui::Spacing();

    // Step selector below D-pad (small indicator, number 1, gear)
    ImGui::SetCursorPosX(center_x - 55.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.04f, 0.10f, 0.17f, 1.0f));
    ImGui::Button("■##sq", ImVec2(20, 20));
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(45.0f);
    ImGui::SliderInt("##Step", &move_step_, 1, 10);
    ImGui::SameLine();
    ImGui::Button("⚙️##step_gear", ImVec2(22, 20));

    ImGui::EndChild();
}

void LuciferGui::renderCardPreferences(float w, float h) {
    ImGui::BeginChild("CardPreferences", ImVec2(w, h), true);

    ImGui::TextColored(ImVec4(0.85f, 0.95f, 1.0f, 1.0f), "⚙️ Preferences");
    ImGui::Separator();
    ImGui::Spacing();

    // Row 1: Bypass Logon
    ImGui::Text("Bypass Logon");
    ImGui::SameLine(w - 75.0f);
    ImGui::SmallButton("⚙️##pref1");
    ImGui::SameLine();
    ToggleSwitch("BypassLogonToggle", &pref_bypass_logon_);

    ImGui::Spacing();

    // Row 2: Auto Reconnect [9.4]
    if (reconnect_ctrl_.getState() == ReconnectState::WaitingReconnect) {
        ImGui::Text("Auto Reconnect [%.1f]", (float)reconnect_ctrl_.getSecondsRemaining());
    } else {
        ImGui::Text("Auto Reconnect [9.4]");
    }
    ImGui::SameLine(w - 75.0f);
    ImGui::SmallButton("⚙️##pref2");
    ImGui::SameLine();
    ToggleSwitch("AutoReconnectToggle", &pref_auto_reconnect_);

    ImGui::Spacing();

    // Row 3: Auto Collect
    ImGui::Text("Auto Collect");
    ImGui::SameLine(w - 75.0f);
    ImGui::SmallButton("⚙️##pref3");
    ImGui::SameLine();
    ToggleSwitch("AutoCollectToggle", &pref_auto_collect_);

    ImGui::Spacing();

    // Row 4: Auto Accept
    ImGui::Text("Auto Accept");
    ImGui::SameLine(w - 75.0f);
    ImGui::SmallButton("⚙️##pref4");
    ImGui::SameLine();
    ToggleSwitch("AutoAcceptToggle", &pref_auto_accept_);

    ImGui::Spacing();

    // Row 5: Auto Disconnect On Mod
    ImGui::Text("Auto Disconnect On Mod");
    ImGui::SameLine(w - 75.0f);
    ImGui::SmallButton("⚙️##pref5");
    ImGui::SameLine();
    ToggleSwitch("AutoDisconnectModToggle", &pref_auto_mod_disconnect_);

    ImGui::EndChild();
}

void LuciferGui::renderCardIntervals(float w, float h) {
    ImGui::BeginChild("CardIntervals", ImVec2(w, h), true);

    ImGui::TextColored(ImVec4(0.85f, 0.95f, 1.0f, 1.0f), "🕒 Intervals");
    ImGui::SameLine(w - 140.0f);
    ImGui::TextDisabled("Dynamic Delay");
    ImGui::SameLine();
    ToggleSwitch("DynamicDelayToggle", &dynamic_delay_);

    ImGui::Separator();
    ImGui::Spacing();

    // 1. Move
    ImGui::Checkbox("##cb_move", &interval_move_enabled_);
    ImGui::SameLine();
    ImGui::Text("Move - %.3fs (+%.3fs)", interval_move_base_, interval_move_jitter_);

    ImGui::Spacing();

    // 2. Collect
    ImGui::Checkbox("##cb_collect", &interval_collect_enabled_);
    ImGui::SameLine();
    ImGui::Text("Collect - %.3fs (+%.3fs)", interval_collect_base_, interval_collect_jitter_);

    ImGui::Spacing();

    // 3. Hit
    ImGui::Checkbox("##cb_hit", &interval_hit_enabled_);
    ImGui::SameLine();
    ImGui::Text("Hit - %.3fs (+%.3fs)", interval_hit_base_, interval_hit_jitter_);

    ImGui::Spacing();

    // 4. Place
    ImGui::Checkbox("##cb_place", &interval_place_enabled_);
    ImGui::SameLine();
    ImGui::Text("Place - %.3fs (+%.3fs)", interval_place_base_, interval_place_jitter_);

    ImGui::EndChild();
}

void LuciferGui::renderTabList() {
    float avail_w = ImGui::GetContentRegionAvail().x;
    float avail_h = ImGui::GetContentRegionAvail().y;

    float footer_h = 52.0f;
    float table_h = avail_h - footer_h - 8.0f;
    if (table_h < 120.0f) table_h = 120.0f;

    // Kumpulkan daftar bot dan statistik
    const auto& bots = cached_bots_;

    int online_count = 0;
    int offline_count = 0;
    int total_bot_count = static_cast<int>(bots.size());
    long long total_gem_count = 0;

    for (const auto& b : bots) {
        if (b->isOnline() || b->isConnected()) {
            online_count++;
        } else {
            offline_count++;
        }
        total_gem_count += b->getStats().gems;
    }

    if (bots.empty() && !info_.account_name.empty()) {
        total_bot_count = 1;
        if (enet_.isConnected()) online_count = 1;
        else offline_count = 1;
        total_gem_count = gui_stats_.gems;
    }

    // 1. Tabel Daftar Bot (Lucifer v2.85 - ID, Name, Level, Ping, Status, Proxy, World, Position, Gems)
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.018f, 0.040f, 0.070f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));

    if (ImGui::BeginTable("LuciferBotListTable", 9,
                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                          ImVec2(0, table_h))) {

        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Ping", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Proxy", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("World", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthFixed, 85.0f);
        ImGui::TableSetupColumn("Gems", ImGuiTableColumnFlags_WidthFixed, 75.0f);

        ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImVec4(0.035f, 0.080f, 0.135f, 1.0f));
        ImGui::TableHeadersRow();
        ImGui::PopStyleColor();

        if (bots.empty()) {
            if (!info_.account_name.empty()) {
                ImGui::TableNextRow();
                bool is_selected = (selected_bot_id_ == 1);
                if (is_selected) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(10, 45, 75, 180));
                }

                // ID
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable("1##row_single", is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_bot_id_ = 1;
                }

                // Name
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", info_.account_name.c_str());

                // Level
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d", gui_stats_.level > 0 ? gui_stats_.level : 1);

                // Ping
                ImGui::TableSetColumnIndex(3);
                int png = enet_.isConnected() ? (enet_.getPing() > 0 ? enet_.getPing() : 37) : 0;
                ImGui::Text("%d", png);

                // Status
                ImGui::TableSetColumnIndex(4);
                if (enet_.isConnected()) {
                    ImGui::TextColored(ImVec4(0.18f, 0.95f, 0.40f, 1.0f), "Online");
                } else {
                    ImGui::TextColored(ImVec4(0.65f, 0.70f, 0.75f, 1.0f), "Offline");
                }

                // Proxy
                ImGui::TableSetColumnIndex(5);
                std::string pstr = main_proxy_.isValid() ? (main_proxy_.host + ":" + std::to_string(main_proxy_.port)) : "Direct";
                ImGui::Text("%s", pstr.c_str());

                // World
                ImGui::TableSetColumnIndex(6);
                std::string w = world_.getWorldName();
                if (w.empty()) w = "-";
                else std::transform(w.begin(), w.end(), w.begin(), ::toupper);
                ImGui::Text("%s", w.c_str());

                // Position
                ImGui::TableSetColumnIndex(7);
                float px = 0, py = 0; int tx = 0, ty = 0;
                world_.getLocalPos(px, py, tx, ty);
                ImGui::Text("%d:%d", tx, ty);

                // Gems
                ImGui::TableSetColumnIndex(8);
                ImGui::Text("%d", gui_stats_.gems);
            }
        } else {
            for (const auto& b : bots) {
                ImGui::TableNextRow();
                bool is_selected = (selected_bot_id_ == b->getId());
                if (is_selected) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(10, 45, 75, 180));
                }

                // ID
                ImGui::TableSetColumnIndex(0);
                char row_id[32];
                std::snprintf(row_id, sizeof(row_id), "%d##row_%d", b->getId(), b->getId());
                if (ImGui::Selectable(row_id, is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_bot_id_ = b->getId();
                }

                // Name
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", b->getAccountName().c_str());

                // Level
                ImGui::TableSetColumnIndex(2);
                int lvl = b->getStats().level > 0 ? b->getStats().level : 1;
                ImGui::Text("%d", lvl);

                // Ping
                ImGui::TableSetColumnIndex(3);
                int png = b->getPing();
                if (png <= 0 && (b->isOnline() || b->isConnected())) png = 37;
                ImGui::Text("%d", png);

                // Status
                ImGui::TableSetColumnIndex(4);
                if (b->isOnline() || b->isConnected()) {
                    ImGui::TextColored(ImVec4(0.18f, 0.95f, 0.40f, 1.0f), "Online");
                } else {
                    ImGui::TextColored(ImVec4(0.65f, 0.70f, 0.75f, 1.0f), "Offline");
                }

                // Proxy
                ImGui::TableSetColumnIndex(5);
                std::string pstr;
                if (b->getProxy().enabled && !b->getProxy().host.empty()) {
                    pstr = b->getProxy().host + ":" + std::to_string(b->getProxy().port);
                } else {
                    pstr = "Direct";
                }
                ImGui::Text("%s", pstr.c_str());

                // World
                ImGui::TableSetColumnIndex(6);
                std::string w = b->getWorld().getWorldName();
                if (w.empty()) w = b->getTargetWorld();
                if (w.empty()) w = "-";
                else std::transform(w.begin(), w.end(), w.begin(), ::toupper);
                ImGui::Text("%s", w.c_str());

                // Position
                ImGui::TableSetColumnIndex(7);
                float px = 0, py = 0; int tx = 0, ty = 0;
                b->getWorld().getLocalPos(px, py, tx, ty);
                ImGui::Text("%d:%d", tx, ty);

                // Gems
                ImGui::TableSetColumnIndex(8);
                ImGui::Text("%d", b->getStats().gems);
            }
        }

        ImGui::EndTable();
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Separator();

    // 2. Bottom Summary Bar (Footer): [Online Count] | [Offline Count] | [Total Bot Count] | [Total Gem Count]
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(6, 14, 24, 255));
    ImGui::BeginChild("ListFooterBar", ImVec2(0, footer_h), false);

    if (ImGui::BeginTable("ListFooterTable", 4, ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Col0", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Col1", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Col2", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Col3", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();

        // Col 0: Online Count
        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(ImVec4(0.60f, 0.72f, 0.85f, 1.0f), "Online Count");
        ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.98f, 1.0f), "%d", online_count);

        // Col 1: Offline Count
        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(ImVec4(0.60f, 0.72f, 0.85f, 1.0f), "Offline Count");
        ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.98f, 1.0f), "%d", offline_count);

        // Col 2: Total Bot Count
        ImGui::TableSetColumnIndex(2);
        ImGui::TextColored(ImVec4(0.60f, 0.72f, 0.85f, 1.0f), "Total Bot Count");
        ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.98f, 1.0f), "%d", total_bot_count);

        // Col 3: Total Gem Count
        ImGui::TableSetColumnIndex(3);
        ImGui::TextColored(ImVec4(0.60f, 0.72f, 0.85f, 1.0f), "Total Gem Count");
        ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.98f, 1.0f), "%lld / 0", total_gem_count);

        ImGui::EndTable();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void LuciferGui::addExecutorLog(const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    localtime_s(&tm_buf, &tt);
    char time_str[32];
    std::strftime(time_str, sizeof(time_str), "[%H:%M:%S] ", &tm_buf);
    std::string entry = std::string(time_str) + msg;

    std::lock_guard<std::mutex> lock(executor_logs_mtx_);
    executor_logs_.push_back(std::move(entry));
    // O(1) pop_front alih-alih erase O(N)
    while (executor_logs_.size() > 500) {
        executor_logs_.pop_front();
    }
}

void LuciferGui::clearExecutorLogs() {
    std::lock_guard<std::mutex> lock(executor_logs_mtx_);
    executor_logs_.clear();
}

// ---------------------------------------------------------------------------
// refreshBotCache — dipanggil SEKALI per frame di awal renderMainLayout agar
// semua sub-render function tidak perlu lock mutex BotManager berulang kali.
// ---------------------------------------------------------------------------
void LuciferGui::refreshBotCache() {
    if (bot_manager_) {
        cached_bots_ = bot_manager_->getBots();
    } else {
        cached_bots_.clear();
    }
}

LuaEngine& LuciferGui::getActiveExecutorEngine() {
    if (bot_manager_) {
        if (selected_bot_id_ > 0) {
            for (const auto& b : cached_bots_) {
                if (b && b->getId() == selected_bot_id_) {
                    return b->getLuaEngine();
                }
            }
        }
        if (!cached_bots_.empty() && cached_bots_[0]) {
            return cached_bots_[0]->getLuaEngine();
        }
    }
    lua_engine_.setTargetBotId(selected_bot_id_);
    return lua_engine_;
}

void LuciferGui::renderTabExecutor() {
    float avail_w = ImGui::GetContentRegionAvail().x;
    float avail_h = ImGui::GetContentRegionAvail().y;

    float top_bar_h = 28.0f;
    float bottom_bar_h = 32.0f;
    float console_h = 150.0f;
    if (avail_h < 450.0f) {
        console_h = 100.0f;
    }
    float editor_h = avail_h - top_bar_h - bottom_bar_h - console_h - 24.0f;
    if (editor_h < 120.0f) editor_h = 120.0f;

    // Kumpulkan bot yang tersedia dari cached_bots_
    std::shared_ptr<BotInstance> current_bot = nullptr;
    const auto& bots = cached_bots_;
    if (bot_manager_) {
        if (selected_bot_id_ > 0) {
            for (const auto& b : bots) {
                if (b && b->getId() == selected_bot_id_) {
                    current_bot = b;
                    break;
                }
            }
        }
        if (!current_bot && !bots.empty() && bots[0]) {
            current_bot = bots[0];
            selected_bot_id_ = current_bot->getId();
        }
    }
    lua_engine_.setTargetBotId(selected_bot_id_);

    // =========================================================================
    // 1. TOP HEADER BAR: [▼] Selected Bot: <name|none>      [Dialog] [Switch] [Next]
    // =========================================================================
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.015f, 0.035f, 0.060f, 0.0f));
    ImGui::BeginChild("ExecutorTopBarArea", ImVec2(0, top_bar_h), false);

    // Dropdown arrow indicator / button
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.02f, 0.06f, 0.12f, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.05f, 0.15f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.82f, 1.0f, 1.0f));
    if (ImGui::Button("▼##ExecArrowBtn", ImVec2(24, 22))) {
        ImGui::OpenPopup("ExecBotSelectMenu");
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine(0, 8.0f);

    std::string bot_label = "Selected Bot: none";
    if (current_bot) {
        bot_label = "Selected Bot: #" + std::to_string(current_bot->getId()) + " " + current_bot->getAccountName() +
                    (current_bot->isOnline() ? " (Online)" : " (Offline)");
    } else if (!info_.account_name.empty()) {
        bot_label = "Selected Bot: " + info_.account_name;
    }

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.04f, 0.10f, 0.20f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.93f, 1.0f, 1.0f));
    if (ImGui::Button(bot_label.c_str(), ImVec2(0, 22))) {
        ImGui::OpenPopup("ExecBotSelectMenu");
    }
    ImGui::PopStyleColor(3);

    if (ImGui::BeginPopup("ExecBotSelectMenu")) {
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Choose Target Bot:");
        ImGui::Separator();
        if (ImGui::Selectable("none", selected_bot_id_ == 0)) {
            selected_bot_id_ = 0;
            lua_engine_.setTargetBotId(0);
        }
        for (const auto& b : bots) {
            char item_name[128];
            snprintf(item_name, sizeof(item_name), "#%d %s (%s)",
                     b->getId(), b->getAccountName().c_str(),
                     b->isOnline() ? "Online" : "Offline");
            if (ImGui::Selectable(item_name, selected_bot_id_ == b->getId())) {
                selected_bot_id_ = b->getId();
                lua_engine_.setTargetBotId(selected_bot_id_);
            }
        }
        if (bots.empty() && !info_.account_name.empty()) {
            char item_name[128];
            snprintf(item_name, sizeof(item_name), "#1 %s (Single)", info_.account_name.c_str());
            if (ImGui::Selectable(item_name, selected_bot_id_ == 1)) {
                selected_bot_id_ = 1;
                lua_engine_.setTargetBotId(1);
            }
        }
        ImGui::EndPopup();
    }

    // Right Action Buttons: [ Dialog ] [ Switch ] [ Next ]
    float btn_w = 64.0f;
    float btn_h = 22.0f;
    float right_group_w = (btn_w * 3) + (6.0f * 2);
    ImGui::SameLine(avail_w - right_group_w - 4.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.04f, 0.10f, 0.18f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.08f, 0.20f, 0.35f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.28f, 0.50f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.93f, 1.0f, 1.0f));

    if (ImGui::Button("Dialog", ImVec2(btn_w, btn_h))) {
        show_dialog_modal_ = true;
    }
    ImGui::SameLine(0, 6.0f);
    if (ImGui::Button("Switch", ImVec2(btn_w, btn_h))) {
        if (!bots.empty()) {
            int cur_idx = -1;
            for (size_t i = 0; i < bots.size(); ++i) {
                if (bots[i]->getId() == selected_bot_id_) {
                    cur_idx = (int)i;
                    break;
                }
            }
            int next_idx = (cur_idx + 1) % bots.size();
            selected_bot_id_ = bots[next_idx]->getId();
            lua_engine_.setTargetBotId(selected_bot_id_);
            snprintf(executor_status_msg_, sizeof(executor_status_msg_),
                     "Switched to #%d %s", bots[next_idx]->getId(), bots[next_idx]->getAccountName().c_str());
        }
    }
    ImGui::SameLine(0, 6.0f);
    if (ImGui::Button("Next", ImVec2(btn_w, btn_h))) {
        if (!bots.empty()) {
            int cur_idx = -1;
            for (size_t i = 0; i < bots.size(); ++i) {
                if (bots[i]->getId() == selected_bot_id_) {
                    cur_idx = (int)i;
                    break;
                }
            }
            int next_idx = (cur_idx + 1) % bots.size();
            selected_bot_id_ = bots[next_idx]->getId();
            lua_engine_.setTargetBotId(selected_bot_id_);
            snprintf(executor_status_msg_, sizeof(executor_status_msg_),
                     "Selected #%d %s", bots[next_idx]->getId(), bots[next_idx]->getAccountName().c_str());
        }
    }

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();

    ImGui::EndChild();
    ImGui::PopStyleColor();

    // =========================================================================
    // 2. CENTRAL CODE EDITOR AREA (Syntax Highlighted, Line Numbers, Lucifer Theme)
    // =========================================================================
    if (text_editor_) {
        text_editor_->Render("ExecutorCodeEditor", ImVec2(avail_w, editor_h), true);
    } else {
        ImGui::BeginChild("ExecutorEditorContainer", ImVec2(0, editor_h), true);
        ImGui::InputTextMultiline("##exec_code", script_buffer_, sizeof(script_buffer_),
                                  ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_AllowTabInput);
        ImGui::EndChild();
    }

    ImGui::Spacing();

    // =========================================================================
    // 3. BOTTOM CONTROL BAR: [▶ Run] [■ Stop] [📁 Save] [📁 New] [🗑 Delete] [🔍 View Error] [🧹 Clear] [🗔 WinConsole]
    // =========================================================================
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 0.0f));

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.030f, 0.080f, 0.145f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.060f, 0.160f, 0.280f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.090f, 0.240f, 0.420f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.08f, 0.20f, 0.35f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.93f, 1.0f, 1.0f));

    float bbtn_h = 28.0f;

    if (ImGui::Button("▶ Run", ImVec2(0, bbtn_h))) {
        LuaEngine& engine = getActiveExecutorEngine();
        std::string target_name = current_bot ? current_bot->getAccountName() : (info_.account_name.empty() ? "Global" : info_.account_name);
        int target_id = current_bot ? current_bot->getId() : selected_bot_id_;

        // Pastikan callback log terpasang ke Executor Console
        engine.setOnLogCallback([this](const std::string& msg) {
            addExecutorLog(msg);
        });
        lua_engine_.setOnLogCallback([this](const std::string& msg) {
            addExecutorLog(msg);
        });

        std::string code = text_editor_ ? text_editor_->GetText() : script_buffer_;
        addExecutorLog("[Executor] 🚀 Menjalankan script di #" + std::to_string(target_id) + " (" + target_name + ")...");

        if (engine.runScript(code)) {
            snprintf(executor_status_msg_, sizeof(executor_status_msg_), "Running on #%d %s", target_id, target_name.c_str());
        } else {
            std::string err = engine.getLastError();
            if (err.empty()) err = "Script already running or failed to initialize Lua state";
            addExecutorLog("[Executor] ❌ " + err);
            strncpy_s(executor_status_msg_, "Failed to start", sizeof(executor_status_msg_) - 1);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("■ Stop", ImVec2(0, bbtn_h))) {
        getActiveExecutorEngine().stopScript();
        lua_engine_.stopScript();
        addExecutorLog("[Executor] ⏹ Script dihentikan oleh pengguna.");
        strncpy_s(executor_status_msg_, "Script stopped", sizeof(executor_status_msg_) - 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("📁 Save", ImVec2(0, bbtn_h))) {
        std::string code = text_editor_ ? text_editor_->GetText() : script_buffer_;
        std::error_code ec;
        std::filesystem::create_directories("cache/scripts", ec);
        std::ofstream out("cache/scripts/executor.lua");
        if (out) {
            out << code;
            addExecutorLog("[Executor] 💾 Script tersimpan ke cache/scripts/executor.lua");
            strncpy_s(executor_status_msg_, "Saved to cache/scripts/executor.lua", sizeof(executor_status_msg_) - 1);
        } else {
            addExecutorLog("[Executor] ❌ Gagal menyimpan script ke file.");
            strncpy_s(executor_status_msg_, "Failed to save file", sizeof(executor_status_msg_) - 1);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("📁 New", ImVec2(0, bbtn_h))) {
        if (text_editor_) text_editor_->SetText("");
        script_buffer_[0] = '\0';
        strncpy_s(executor_status_msg_, "New empty script buffer", sizeof(executor_status_msg_) - 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("🗑 Delete", ImVec2(0, bbtn_h))) {
        if (text_editor_) text_editor_->SetText("");
        script_buffer_[0] = '\0';
        strncpy_s(executor_status_msg_, "Buffer cleared", sizeof(executor_status_msg_) - 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("🔍 View Error", ImVec2(0, bbtn_h))) {
        show_error_modal_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("🧹 Clear", ImVec2(0, bbtn_h))) {
        clearExecutorLogs();
    }
    ImGui::SameLine();
    if (ImGui::Button("🗔 WinConsole", ImVec2(0, bbtn_h))) {
#ifdef _WIN32
        HWND hCon = ::GetConsoleWindow();
        if (hCon) {
            win_console_visible_ = !win_console_visible_;
            ::ShowWindow(hCon, win_console_visible_ ? SW_SHOW : SW_HIDE);
            snprintf(executor_status_msg_, sizeof(executor_status_msg_),
                     "WinConsole: %s", win_console_visible_ ? "Visible" : "Hidden");
        }
#endif
    }

    // Right-aligned status indicators
    LuaEngine& active_engine = getActiveExecutorEngine();
    bool is_active_running = active_engine.isRunning() || lua_engine_.isRunning();
    LuaEngine& rep_engine = active_engine.isRunning() ? active_engine : lua_engine_;
    if (is_active_running) {
        float elapsed = rep_engine.getExecutionTimeMs() / 1000.0f;
        char status_txt[64];
        snprintf(status_txt, sizeof(status_txt), "● Running (%.1fs)", elapsed);
        float sw = ImGui::CalcTextSize(status_txt).x;
        ImGui::SameLine(avail_w - sw - 4.0f);
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "%s", status_txt);
    } else if (executor_status_msg_[0] != '\0') {
        float sw = ImGui::CalcTextSize(executor_status_msg_).x;
        if (avail_w - sw - 4.0f > ImGui::GetCursorPosX()) {
            ImGui::SameLine(avail_w - sw - 4.0f);
        } else {
            ImGui::SameLine();
        }
        ImGui::TextColored(ImVec4(0.5f, 0.75f, 0.95f, 1.0f), "%s", executor_status_msg_);
    }

    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(3);

    ImGui::Spacing();

    // =========================================================================
    // 3b. EXECUTOR LIVE OUTPUT CONSOLE
    // =========================================================================
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.012f, 0.025f, 0.045f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.08f, 0.20f, 0.35f, 0.8f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);

    if (ImGui::BeginChild("ExecutorOutputConsoleBox", ImVec2(0, console_h), true)) {
        ImGui::TextColored(ImVec4(0.25f, 0.85f, 1.0f, 1.0f), "📟 Executor Console Output");
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &executor_auto_scroll_);
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear##ExecClearSm")) {
            clearExecutorLogs();
        }

        ImGui::Separator();

        std::lock_guard<std::mutex> lk(executor_logs_mtx_);
        if (executor_logs_.empty()) {
            ImGui::TextColored(ImVec4(0.40f, 0.50f, 0.60f, 1.0f), "Ready. Click '▶ Run' to execute script on target bot. Output will appear here.");
        } else {
            for (const auto& line : executor_logs_) {
                if (line.find("❌") != std::string::npos || line.find("Error") != std::string::npos || line.find("error") != std::string::npos) {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", line.c_str());
                } else if (line.find("✅") != std::string::npos || line.find("success") != std::string::npos) {
                    ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.45f, 1.0f), "%s", line.c_str());
                } else if (line.find("[Executor]") != std::string::npos || line.find("[Bothax]") != std::string::npos) {
                    ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "%s", line.c_str());
                } else {
                    ImGui::TextUnformatted(line.c_str());
                }
            }
            if (executor_auto_scroll_) {
                ImGui::SetScrollHereY(1.0f);
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    // =========================================================================
    // 4. POPUP MODALS: OnDialogRequest Inspector & Lua Error Log
    // =========================================================================
    if (show_dialog_modal_) {
        ImGui::OpenPopup("OnDialogRequest Inspector##DialogModal");
    }
    if (ImGui::BeginPopupModal("OnDialogRequest Inspector##DialogModal", &show_dialog_modal_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0.0f, 0.75f, 1.0f, 1.0f), "🔍 OnDialogRequest Inspector (GTPS Protocol)");
        ImGui::Separator();

        std::string current_dialog = "";
        std::string bname = "None";
        if (current_bot) {
            bname = current_bot->getAccountName();
            current_dialog = current_bot->getLastDialog();
        } else {
            bname = info_.account_name.empty() ? "Local Bot" : info_.account_name;
            current_dialog = fallback_last_dialog_;
        }

        ImGui::Text("Active Target Bot: %s", bname.c_str());

        if (current_dialog.empty()) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No dialog packet received yet from game server.");
        } else {
            ImGui::BeginChild("DialogBoxScroll", ImVec2(520, 240), true);
            ImGui::PushTextWrapPos(500.0f);
            ImGui::TextUnformatted(current_dialog.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
        }

        ImGui::Spacing();
        if (!current_dialog.empty()) {
            if (ImGui::Button("📋 Copy Raw Dialog", ImVec2(150, 0))) {
                ImGui::SetClipboardText(current_dialog.c_str());
            }
            ImGui::SameLine();
        }

        if (ImGui::Button("Tutup##CloseDialogModal", ImVec2(100, 0))) {
            show_dialog_modal_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (show_error_modal_) {
        ImGui::OpenPopup("Lua Execution Error Log##ErrorModal");
    }
    if (ImGui::BeginPopupModal("Lua Execution Error Log##ErrorModal", &show_error_modal_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "❌ Lua Script Engine Error");
        ImGui::Separator();

        std::string err = getActiveExecutorEngine().getLastError();
        if (err.empty()) err = lua_engine_.getLastError();
        if (err.empty()) {
            ImGui::TextColored(ImVec4(0.2f, 0.85f, 0.3f, 1.0f), "✅ No errors reported. Lua engine is clean.");
        } else {
            ImGui::BeginChild("ErrorBoxScroll", ImVec2(520, 180), true);
            ImGui::PushTextWrapPos(500.0f);
            ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "%s", err.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
        }

        ImGui::Spacing();
        if (!err.empty()) {
            if (ImGui::Button("📋 Copy Error", ImVec2(120, 0))) {
                ImGui::SetClipboardText(err.c_str());
            }
            ImGui::SameLine();
        }

        if (ImGui::Button("Tutup##CloseErrorModal", ImVec2(100, 0))) {
            show_error_modal_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void LuciferGui::renderTabProxy() {
    ImGui::BeginChild("ProxyManagementArea", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.85f, 0.95f, 1.0f, 1.0f), "🌐 SOCKS5 PROXY CONTROL CENTER (RFC 1928 & RFC 1929)");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Columns(2, "ProxyTabCols", false);
    ImGui::SetColumnWidth(0, 430.0f);

    // Kiri Atas: Proxy Tester & Quick Config
    ImGui::BeginChild("ProxyTesterBox", ImVec2(0, 360), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "🔍 SOCKS5 PROXY TESTER & QUICK INPUT");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Quick Paste Proxy:");
    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputTextWithHint("##TabQuickProxy", "host:port:user:pass atau socks5://...", tab_proxy_quick_, sizeof(tab_proxy_quick_));
    ImGui::SameLine();
    if (ImGui::Button("⚡ Parse##tab", ImVec2(70, 20))) {
        if (strlen(tab_proxy_quick_) > 0) {
            auto parsed = Socks5Config::parse(tab_proxy_quick_);
            if (parsed.isValid()) {
                strncpy_s(proxy_test_host_, parsed.host.c_str(), sizeof(proxy_test_host_) - 1);
                proxy_test_port_ = parsed.port;
                strncpy_s(proxy_test_user_, parsed.username.c_str(), sizeof(proxy_test_user_) - 1);
                strncpy_s(proxy_test_pass_, parsed.password.c_str(), sizeof(proxy_test_pass_) - 1);
            }
        }
    }

    ImGui::Spacing();
    ImGui::SetNextItemWidth(220);
    ImGui::InputText("Host Proxy", proxy_test_host_, sizeof(proxy_test_host_));
    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("Port Proxy", &proxy_test_port_);
    ImGui::SetNextItemWidth(180);
    ImGui::InputText("User (Opsional)", proxy_test_user_, sizeof(proxy_test_user_));
    ImGui::SetNextItemWidth(180);
    ImGui::InputText("Pass (Opsional)", proxy_test_pass_, sizeof(proxy_test_pass_), ImGuiInputTextFlags_Password);

    ImGui::Spacing();
    if (ImGui::Button("🔍 Uji Konektivitas Proxy SOCKS5", ImVec2(280, 28))) {
        Socks5Config tcfg;
        tcfg.enabled = true;
        tcfg.host = proxy_test_host_;
        tcfg.port = proxy_test_port_;
        tcfg.username = proxy_test_user_;
        tcfg.password = proxy_test_pass_;
        std::string res;
        Socks5Client::testProxy(tcfg, res);
        strncpy_s(proxy_test_result_, res.c_str(), sizeof(proxy_test_result_) - 1);
    }

    if (strlen(proxy_test_result_) > 0) {
        ImGui::Spacing();
        if (strstr(proxy_test_result_, "✅") != nullptr) {
            ImGui::TextColored(ImVec4(0.25f, 0.95f, 0.35f, 1.0f), "%s", proxy_test_result_);
        } else {
            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", proxy_test_result_);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Tombol Pasang ke Bot
    auto sel_b = bot_manager_ ? bot_manager_->getBotById(selected_bot_id_) : nullptr;
    std::string apply_bot_lbl = sel_b ? ("📌 Terapkan ke Bot #" + std::to_string(sel_b->getId()) + " (" + sel_b->getAccountName() + ")") : "📌 Terapkan ke Bot Terpilih";
    if (ImGui::Button(apply_bot_lbl.c_str(), ImVec2(360, 24))) {
        if (sel_b) {
            Socks5Config pcfg;
            pcfg.enabled = true;
            pcfg.host = proxy_test_host_;
            pcfg.port = proxy_test_port_;
            pcfg.username = proxy_test_user_;
            pcfg.password = proxy_test_pass_;
            sel_b->setProxy(pcfg);
            if (bot_manager_) bot_manager_->saveAccountsToFile(accounts_file_path_);
            Logger::log("[PROXY] 📌 Proxy ", pcfg.toString(), " diterapkan ke bot #", sel_b->getId(), " (", sel_b->getAccountName(), ")");
        }
    }

    if (ImGui::Button("🌐 Jadikan Default untuk Bot Baru", ImVec2(360, 24))) {
        if (bot_manager_) {
            Socks5Config pcfg;
            pcfg.enabled = true;
            pcfg.host = proxy_test_host_;
            pcfg.port = proxy_test_port_;
            pcfg.username = proxy_test_user_;
            pcfg.password = proxy_test_pass_;
            bot_manager_->default_proxy = pcfg;
            Logger::log("[PROXY] 🌐 Proxy default bot pool diatur ke: ", pcfg.toString());
        }
    }

    ImGui::EndChild();

    // Kiri Bawah: Batch Assign Proxy (Import List)
    ImGui::Spacing();
    ImGui::BeginChild("BatchProxyCard", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "📥 BATCH PROXY DISTRIBUTOR (MASS ASSIGN)");
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Tempel daftar proxy (1 per baris: host:port:user:pass):");
    ImGui::InputTextMultiline("##BatchProxies", batch_proxy_text_, sizeof(batch_proxy_text_), ImVec2(-1, 100));
    ImGui::Spacing();
    if (ImGui::Button("🔀 Distribusikan ke Semua Bot di Pool", ImVec2(320, 26))) {
        if (bot_manager_) {
            std::istringstream stream(batch_proxy_text_);
            std::string pline;
            std::vector<Socks5Config> plist;
            while (std::getline(stream, pline)) {
                while (!pline.empty() && (pline.back() == '\r' || pline.back() == '\n' || pline.back() == ' ')) pline.pop_back();
                if (pline.empty() || pline[0] == '#') continue;
                auto pcfg = Socks5Config::parse(pline);
                if (pcfg.isValid()) plist.push_back(pcfg);
            }
            if (!plist.empty()) {
                auto all_bots = bot_manager_->getBots();
                for (size_t i = 0; i < all_bots.size(); ++i) {
                    all_bots[i]->setProxy(plist[i % plist.size()]);
                }
                bot_manager_->saveAccountsToFile(accounts_file_path_);
                Logger::log("[PROXY] 🔀 Berhasil mendistribusikan ", plist.size(), " proxy ke ", all_bots.size(), " bot.");
            }
        }
    }
    ImGui::EndChild();

    ImGui::NextColumn();

    // Kanan: Tabel Status Proxy Pool
    ImGui::BeginChild("ProxyPoolTableCard", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "📋 STATUS PROXY PER-AKUN (MULTI-BOT POOL)");
    ImGui::Separator();
    ImGui::Spacing();

    if (bot_manager_) {
        const auto& bots = cached_bots_;
        if (ImGui::BeginTable("ProxyStatusTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("Akun", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Proxy Host:Port", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Auth", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Tindakan", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableHeadersRow();

            for (const auto& b : bots) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("#%d", b->getId());

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", b->getAccountName().c_str());

                ImGui::TableSetColumnIndex(2);
                if (b->getProxy().enabled) {
                    ImGui::TextColored(ImVec4(0.20f, 0.95f, 0.45f, 1.0f), "🌐 %s:%d",
                                       b->getProxy().host.c_str(), b->getProxy().port);
                } else {
                    ImGui::TextDisabled("Direct (Tanpa Proxy)");
                }

                ImGui::TableSetColumnIndex(3);
                if (b->getProxy().enabled && !b->getProxy().username.empty()) {
                    ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.30f, 1.0f), "%s", b->getProxy().username.c_str());
                } else if (b->getProxy().enabled) {
                    ImGui::TextDisabled("No Auth");
                } else {
                    ImGui::TextDisabled("-");
                }

                ImGui::TableSetColumnIndex(4);
                ImGui::PushID(b->getId());
                if (ImGui::SmallButton("Uji")) {
                    if (b->getProxy().enabled) {
                        std::string res;
                        Socks5Client::testProxy(b->getProxy(), res);
                        Logger::log("[PROXY #", b->getId(), "] ", res);
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Direct")) {
                    Socks5Config none;
                    b->setProxy(none);
                    bot_manager_->saveAccountsToFile(accounts_file_path_);
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    ImGui::EndChild();
    ImGui::Columns(1);
    ImGui::EndChild();
}

void LuciferGui::renderTabSwitch() {
    ImGui::BeginChild("SwitchCard", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.85f, 0.95f, 1.0f, 1.0f), "🔀 Sub-Server Redirection & World Switcher");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Peralihan Cepat Sub-Server / World:");
    ImGui::SetNextItemWidth(250.0f);
    ImGui::InputTextWithHint("##QuickSwitchWorld", "Target World...", warp_input_, sizeof(warp_input_));
    ImGui::SameLine();
    if (ImGui::Button("🚀 Switch World Sekarang", ImVec2(180, 24))) {
        if (strlen(warp_input_) > 0) {
            actions_.warp(warp_input_);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("Server Redirection Status:");
    ImGui::BulletText("Host Aktif: %s:%d", info_.game_host.c_str(), info_.game_port);
    ImGui::BulletText("Current Sub-Server Token: %d", enet_.isConnected() ? 1 : 0);
    ImGui::BulletText("Reconnect Status: %s", reconnect_ctrl_.getStateString().c_str());

    ImGui::Spacing();
    if (ImGui::Button("🔄 Force Disconnect & Fast Reconnect", ImVec2(280, 28))) {
        if (reconnect_fn_) reconnect_fn_();
    }
    ImGui::EndChild();
}

void LuciferGui::renderTabDatabase() {
    ImGui::BeginChild("DatabaseCard", ImVec2(0, 0), true);
    auto& db = ItemDatabase::instance();
    ImGui::TextColored(ImVec4(0.85f, 0.95f, 1.0f, 1.0f), "🗄️ Growtopia Items Database Cache (cache/items.dat)");
    ImGui::SameLine();
    if (db.isLoaded()) {
        ImGui::TextColored(ImVec4(0.25f, 0.95f, 0.35f, 1.0f), "[ %zu items loaded from %s ]", db.getItemCount(), db.getLoadedPath().c_str());
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.25f, 1.0f), "[ %zu builtin items | Menunggu items.dat dari server ]", db.getItemCount());
    }
    ImGui::Separator();
    ImGui::Spacing();

    static char search_item[64] = "";
    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputTextWithHint("##ItemSearch", "Search item name or ID...", search_item, sizeof(search_item));
    ImGui::SameLine();
    if (ImGui::Button("\xf0\x9f\x94\x84 Reload cache/items.dat")) {
        db.loadFromFile("cache/items.dat");
        // Paksa rebuild cache setelah reload
        db_search_cache_buf_[0] = '\1'; // invalidate
    }

    ImGui::Spacing();

    // Rebuild hanya saat filter berubah, bukan tiap frame
    if (std::strcmp(db_search_cache_buf_, search_item) != 0) {
        db_search_cache_ = db.findItems(search_item, 500);
        std::strncpy(db_search_cache_buf_, search_item, sizeof(db_search_cache_buf_) - 1);
        db_search_cache_buf_[sizeof(db_search_cache_buf_) - 1] = '\0';
    }
    const auto& matched = db_search_cache_;

    if (ImGui::BeginTable("ItemDbTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Item Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Grow Time", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Rarity", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Hits", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();

        for (const auto& itm : matched) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", itm.id);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ImVec4(0.20f, 0.95f, 0.45f, 1.0f), "%s", itm.name.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", itm.type.c_str());
            ImGui::TableSetColumnIndex(3);
            if (itm.grow_time > 0) ImGui::Text("%ds", itm.grow_time);
            else ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d", itm.rarity);
            ImGui::TableSetColumnIndex(5);
            if (itm.break_hits > 0) ImGui::Text("%d", itm.break_hits);
            else ImGui::TextDisabled("-");
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void LuciferGui::renderSubTabWorld() {
    renderTabWorld();
}

void LuciferGui::renderSubTabInventory() {
    renderTabInventory();
}

static void renderColoredGtText(const std::string& str, const ImVec4& def_color) {
    if (str.empty()) {
        ImGui::NewLine();
        return;
    }

    ImVec4 cur_color = def_color;
    size_t i = 0;
    std::string buffer;

    auto flush_buf = [&buffer, &cur_color]() {
        if (!buffer.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, cur_color);
            ImGui::TextUnformatted(buffer.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 0);
            buffer.clear();
        }
    };

    while (i < str.size()) {
        if (str[i] == '\r') {
            i++;
            continue;
        }
        if (str[i] == '\n') {
            flush_buf();
            ImGui::NewLine();
            i++;
            continue;
        }
        if (str[i] == '`' && i + 1 < str.size()) {
            char code = str[i + 1];
            bool is_color_code = true;
            ImVec4 new_col = def_color;

            switch (code) {
            case '0': new_col = ImVec4(0.25f, 0.25f, 0.25f, 1.0f); break; // Dark/Black
            case '1': new_col = ImVec4(0.40f, 0.70f, 1.00f, 1.0f); break; // Sky Blue
            case '2': new_col = ImVec4(0.35f, 0.95f, 0.35f, 1.0f); break; // Bright Green
            case '3': new_col = ImVec4(0.35f, 0.90f, 0.95f, 1.0f); break; // Cyan
            case '4': new_col = ImVec4(0.95f, 0.30f, 0.30f, 1.0f); break; // Red
            case '5': new_col = ImVec4(0.80f, 0.45f, 0.95f, 1.0f); break; // Purple
            case '6': new_col = ImVec4(0.95f, 0.65f, 0.20f, 1.0f); break; // Amber
            case '7': new_col = ImVec4(0.75f, 0.75f, 0.80f, 1.0f); break; // Light Grey
            case '8': new_col = ImVec4(1.00f, 0.60f, 0.15f, 1.0f); break; // Orange
            case '9': new_col = ImVec4(1.00f, 0.90f, 0.25f, 1.0f); break; // Yellow
            case 'w': case 'W': case 'c': case 'C':
                new_col = ImVec4(1.00f, 1.00f, 1.00f, 1.0f); break; // White
            case 'p': case 'P':
                new_col = ImVec4(1.00f, 0.45f, 0.80f, 1.0f); break; // Pink / Magenta
            case 'o': case 'O':
                new_col = ImVec4(1.00f, 0.65f, 0.15f, 1.0f); break; // Orange
            case 'b': case 'B':
                new_col = ImVec4(0.20f, 0.20f, 0.20f, 1.0f); break; // Black
            case 'q': case 'Q':
                new_col = ImVec4(0.20f, 0.70f, 0.95f, 1.0f); break; // Dark Cyan
            case 'e': case 'E':
                new_col = ImVec4(0.55f, 0.85f, 1.00f, 1.0f); break; // Light Blue
            case 'r': case 'R':
                new_col = ImVec4(0.30f, 0.90f, 0.40f, 1.0f); break; // Light Green
            case 't': case 'T':
                new_col = ImVec4(0.30f, 0.90f, 0.90f, 1.0f); break; // Turquoise
            case 'a': case 'A':
                new_col = ImVec4(0.65f, 0.65f, 0.70f, 1.0f); break; // Grey
            case 's': case 'S':
                new_col = ImVec4(0.85f, 0.85f, 0.90f, 1.0f); break; // Silver
            case '`': case ' ':
                new_col = def_color; break; // Reset
            default:
                is_color_code = false;
                break;
            }

            if (is_color_code) {
                flush_buf();
                cur_color = new_col;
                i += 2;
                continue;
            }
        }
        buffer.push_back(str[i]);
        i++;
    }

    flush_buf();
    ImGui::NewLine();
}

void LuciferGui::addFallbackConsoleMessage(ConsoleMsgType type, const std::string& msg) {
    if (msg.empty()) return;
    std::lock_guard<std::mutex> lock(fallback_console_mtx_);
    ConsoleEntry entry;
    entry.type = type;
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &t);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    entry.timestamp = buf;
    entry.text = msg;
    fallback_console_entries_.push_back(std::move(entry));
    while (fallback_console_entries_.size() > 500) {
        fallback_console_entries_.pop_front();
    }
}

void LuciferGui::renderSubTabConsole() {
    std::shared_ptr<BotInstance> bot = nullptr;
    if (bot_manager_) {
        if (selected_bot_id_ > 0) {
            for (const auto& b : cached_bots_) {
                if (b && b->getId() == selected_bot_id_) {
                    bot = b;
                    break;
                }
            }
        }
        if (!bot && !cached_bots_.empty()) {
            bot = cached_bots_.front();
        }
    }

    // 1. Top Control Bar: [Enable] (switch) | [Show Timestamps] (switch) | [Auto Scroll] (switch)
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Enable");
    ImGui::SameLine();
    ToggleSwitch("ConsoleEnableToggle", &console_enable_);

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    ImGui::Text("Show Timestamps");
    ImGui::SameLine();
    ToggleSwitch("ConsoleTimestampsToggle", &console_show_timestamps_);

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    ImGui::Text("Auto Scroll");
    ImGui::SameLine();
    ToggleSwitch("ConsoleAutoScrollToggle", &console_auto_scroll_);

    ImGui::Spacing();

    // 2. Filter Bar: [ 📋 All ] [ 🌐 World ] [ 💬 Private ] [ ⚙ System ]
    struct FilterButton {
        int id;
        const char* label;
    };
    const FilterButton filter_btns[] = {
        { 0, "📋 All" },
        { 1, "🌐 World" },
        { 2, "💬 Private" },
        { 3, "⚙ System" },
    };

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 0.0f));

    float fbtn_w = 90.0f;
    float fbtn_h = 24.0f;

    for (size_t i = 0; i < sizeof(filter_btns) / sizeof(filter_btns[0]); ++i) {
        if (i > 0) ImGui::SameLine();

        bool is_active = (console_filter_ == filter_btns[i].id);

        if (is_active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.38f, 0.65f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.48f, 0.78f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.038f, 0.082f, 0.138f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.065f, 0.155f, 0.245f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.72f, 0.85f, 1.0f));
        }

        if (ImGui::Button(filter_btns[i].label, ImVec2(fbtn_w, fbtn_h))) {
            console_filter_ = filter_btns[i].id;
        }

        if (is_active) {
            ImVec2 p0 = ImGui::GetItemRectMin();
            ImVec2 p1 = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(p0.x, p1.y - 2.0f),
                ImVec2(p1.x, p1.y),
                IM_COL32(0, 185, 255, 255)
            );
        }

        ImGui::PopStyleColor(3);
    }
    ImGui::PopStyleVar(2);

    ImGui::Spacing();

    // 3. Dark Console Viewport
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.012f, 0.025f, 0.045f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.08f, 0.22f, 0.38f, 0.70f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));

    float term_h = ImGui::GetContentRegionAvail().y - 32.0f;
    if (term_h < 100.0f) term_h = 100.0f;

    ImGui::BeginChild("LuciferConsoleTerminal", ImVec2(0, term_h), true, ImGuiWindowFlags_HorizontalScrollbar);

    // getConsoleSnapshot(): zero-copy jika tidak ada pesan baru (dirty=false)
    const std::vector<ConsoleEntry>* entries_ptr = nullptr;
    std::vector<ConsoleEntry> fallback_snap;
    if (bot) {
        entries_ptr = &bot->getConsoleSnapshot();
    } else {
        std::lock_guard<std::mutex> lock(fallback_console_mtx_);
        fallback_snap.assign(fallback_console_entries_.begin(), fallback_console_entries_.end());
        entries_ptr = &fallback_snap;
    }
    const std::vector<ConsoleEntry>& entries = *entries_ptr;

    if (entries.empty()) {
        ImGui::TextDisabled("Console ready. Waiting for bot connection/messages...");
    } else {
        for (const auto& entry : entries) {
            // Apply filter
            if (console_filter_ == 1 && entry.type != ConsoleMsgType::World) continue;
            if (console_filter_ == 2 && entry.type != ConsoleMsgType::Private) continue;
            if (console_filter_ == 3 && entry.type != ConsoleMsgType::System && entry.type != ConsoleMsgType::Task) continue;

            // Render Tag [S], [T], [W], [P]
            const char* tag_str = "[S]";
            ImVec4 tag_col = ImVec4(0.78f, 0.78f, 0.80f, 1.0f); // Default grey
            switch (entry.type) {
            case ConsoleMsgType::System:
                tag_str = "[S]";
                tag_col = ImVec4(0.78f, 0.78f, 0.80f, 1.0f); // Light grey/white
                break;
            case ConsoleMsgType::Task:
                tag_str = "[T]";
                tag_col = ImVec4(0.35f, 0.95f, 0.35f, 1.0f); // Green
                break;
            case ConsoleMsgType::World:
                tag_str = "[W]";
                tag_col = ImVec4(0.35f, 0.80f, 1.00f, 1.0f); // Cyan / light blue
                break;
            case ConsoleMsgType::Private:
                tag_str = "[P]";
                tag_col = ImVec4(1.00f, 0.45f, 0.80f, 1.0f); // Pink / magenta
                break;
            }

            ImGui::PushStyleColor(ImGuiCol_Text, tag_col);
            ImGui::TextUnformatted(tag_str);
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 0);

            if (console_show_timestamps_) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.70f, 0.75f, 1.0f));
                // Format langsung tanpa string allocation
                char ts_buf[16];
                std::snprintf(ts_buf, sizeof(ts_buf), "[%s] ", entry.timestamp.c_str());
                ImGui::TextUnformatted(ts_buf);
                ImGui::PopStyleColor();
                ImGui::SameLine(0, 0);
            } else {
                ImGui::TextUnformatted(" ");
                ImGui::SameLine(0, 0);
            }

            // Render colored text with Growtopia codes
            renderColoredGtText(entry.text, ImVec4(0.85f, 0.88f, 0.92f, 1.0f));
        }

        if (console_auto_scroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);

    ImGui::Spacing();

    // 4. Bottom Input Bar: [ Type a message to chat. ] [ Clear ]
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(avail_w - 75.0f);

    bool send_chat = ImGui::InputTextWithHint("##ConsoleChatInput", "Type a message to chat.",
        console_chat_input_, sizeof(console_chat_input_), ImGuiInputTextFlags_EnterReturnsTrue);

    if (send_chat && strlen(console_chat_input_) > 0) {
        if (bot) {
            bot->say(console_chat_input_);
        } else {
            actions_.say(console_chat_input_);
            addFallbackConsoleMessage(ConsoleMsgType::World, "<me> " + std::string(console_chat_input_));
        }
        console_chat_input_[0] = '\0';
        ImGui::SetKeyboardFocusHere(-1);
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(65.0f, 0))) {
        if (bot) {
            bot->clearConsoleMessages();
        } else {
            std::lock_guard<std::mutex> lock(fallback_console_mtx_);
            fallback_console_entries_.clear();
        }
    }
}

void LuciferGui::renderSubTabAutomation() {
    ImGui::BeginChild("AutomationCard", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.85f, 0.95f, 1.0f, 1.0f), "⚡ Automation & Auto-Farm");
    ImGui::Separator();
    ImGui::Spacing();

    static int farm_tx = 0, farm_ty = 0;
    static int punch_count = 3;
    static int farm_block_id = 456;

    ImGui::Text("Status Auto-Farm: %s", autofarm_.isRunning() ? "ACTIVE (SEDANG MENAMBANG)" : "STOPPED");
    ImGui::Spacing();

    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("Tile X", &farm_tx);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("Tile Y", &farm_ty);
    ImGui::SameLine();
    if (ImGui::Button("📍 Ambil Posisi Bot", ImVec2(140, 22))) {
        float px, py;
        world_.getLocalPos(px, py, farm_tx, farm_ty);
    }

    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("Block Item ID", &farm_block_id);
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("Pukul per Siklus", &punch_count);

    ImGui::Spacing();
    if (!autofarm_.isRunning()) {
        if (ImGui::Button("▶️ Mulai Auto-Farm", ImVec2(180, 28))) {
            autofarm_.start(farm_tx, farm_ty, punch_count, farm_block_id);
        }
    } else {
        if (ImGui::Button("🛑 Hentikan Auto-Farm", ImVec2(180, 28))) {
            autofarm_.stop();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("Quick Automation:");
    if (ImGui::Button("🧲 Auto Collect Sekitar", ImVec2(200, 24))) {
        actions_.collectNearby(collect_radius_);
    }
    ImGui::SameLine();
    if (ImGui::Button("🚪 Auto Enter Pintu / Portal", ImVec2(220, 24))) {
        actions_.enter();
    }

    ImGui::EndChild();
}

void LuciferGui::renderSubTabRotation() {
    ImGui::BeginChild("RotationCard", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.85f, 0.95f, 1.0f, 1.0f), "🔄 World Rotation Manager");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SetNextItemWidth(180);
    ImGui::InputTextWithHint("##RotInput", "Nama World...", rotation_world_input_, sizeof(rotation_world_input_));
    ImGui::SameLine();
    if (ImGui::Button("➕ Tambah World", ImVec2(130, 22))) {
        if (strlen(rotation_world_input_) > 0) {
            rotation_worlds_.push_back(rotation_world_input_);
            rotation_world_input_[0] = '\0';
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("🧹 Clear List", ImVec2(100, 22))) {
        rotation_worlds_.clear();
    }

    ImGui::Spacing();
    ImGui::Text("Daftar World Rotasi (%zu World):", rotation_worlds_.size());
    ImGui::BeginChild("RotList", ImVec2(0, 160), true);
    for (size_t i = 0; i < rotation_worlds_.size(); ++i) {
        ImGui::BulletText("%zu. %s", i + 1, rotation_worlds_[i].c_str());
    }
    ImGui::EndChild();

    ImGui::Spacing();
    if (!rotation_running_) {
        if (ImGui::Button("▶️ Mulai Rotasi Massal", ImVec2(200, 28))) {
            rotation_running_ = true;
        }
    } else {
        if (ImGui::Button("🛑 Hentikan Rotasi", ImVec2(200, 28))) {
            rotation_running_ = false;
        }
    }
    ImGui::EndChild();
}

void LuciferGui::renderSubTabLogs() {
    renderTabConsole();
}

void LuciferGui::renderSubTabRID() {
    std::shared_ptr<BotInstance> bot = (bot_manager_ && selected_bot_id_ > 0)
        ? bot_manager_->getBotById(selected_bot_id_) : nullptr;

    std::string account_name = bot ? bot->getAccountName() : info_.account_name;
    DeviceFingerprint& fp = bot ? bot->getFingerprint() : fingerprint_;
    Socks5Config proxy = bot ? bot->getProxy() : main_proxy_;
    std::string game_host = bot ? bot->game_host : info_.game_host;
    int game_port = bot ? bot->game_port : info_.game_port;
    bool is_online = bot ? bot->isOnline() : (!world_.getWorldName().empty() && enet_.isConnected());

    static std::string status_feedback;

    // Header info bar
    ImGui::TextColored(ImVec4(0.00f, 0.73f, 1.00f, 1.0f), "🔑 IDENTITAS AKUN & NETWORK FINGERPRINT (RID)");
    ImGui::SameLine();
    ImGui::TextDisabled("— Akun:");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.95f, 1.0f, 1.0f), "%s", account_name.c_str());
    if (bot) {
        ImGui::SameLine();
        ImGui::TextDisabled("(Bot #%d)", bot->getId());
    }
    ImGui::SameLine();
    if (is_online) {
        ImGui::TextColored(ImVec4(0.18f, 0.95f, 0.40f, 1.0f), "● ONLINE");
    } else {
        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "○ OFFLINE");
    }

    ImGui::Separator();
    ImGui::Spacing();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float col_w = (avail.x - 12.0f) * 0.5f;
    if (col_w < 320.0f) col_w = avail.x;

    // Helper lambda for rendering a label + readonly input + copy button
    auto renderCopyRow = [](const char* label, const std::string& value, const char* id, float input_w) {
        ImGui::Text("%-15s:", label);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.035f, 0.075f, 0.125f, 1.0f));
        ImGui::SetNextItemWidth(input_w);
        char buf[512];
        strncpy_s(buf, value.c_str(), sizeof(buf) - 1);
        std::string input_id = std::string("##in_") + id;
        ImGui::InputText(input_id.c_str(), buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        std::string btn_label = std::string("📋 Copy##btn_") + id;
        if (ImGui::Button(btn_label.c_str(), ImVec2(65, 0))) {
            ImGui::SetClipboardText(value.c_str());
            status_feedback = std::string("✅ Disalin ke clipboard: ") + label;
        }
    };

    // Left Column: Network & IP Information
    ImGui::BeginChild("RID_NetworkCard", ImVec2(col_w, 290), true);
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.95f, 1.0f), "🌐 INFORMASI NETWORK & IP AKUN");
    ImGui::Separator();
    ImGui::Spacing();

    bool has_proxy = proxy.isValid();
    std::string ip_used;
    std::string ip_source_label;

    if (bot) {
        if (!bot->getServerReportedIp().empty()) {
            ip_used = bot->getServerReportedIp();
            ip_source_label = "✅ Terverifikasi dari Respon Server Game";
        } else {
            ip_used = bot->getEffectiveIp();
            ip_source_label = has_proxy ? "🌐 Terdeteksi via SOCKS5 Proxy Gateway" : "⚡ Terdeteksi dari IP Publik ISP Lokal";
        }
    } else {
        ip_used = has_proxy ? proxy.host : HttpClient::getCachedDirectIp();
        ip_source_label = has_proxy ? "🌐 SOCKS5 Proxy Host" : "⚡ IP Publik Lokal";
    }
    if (ip_used.empty()) {
        ip_used = has_proxy ? proxy.host : "182.9.36.244";
    }

    std::string port_used = has_proxy ? std::to_string(proxy.port) : std::to_string(game_port);
    std::string full_addr = ip_used + ":" + port_used;

    ImGui::Text("Status Mode    : ");
    ImGui::SameLine();
    if (has_proxy) {
        ImGui::TextColored(ImVec4(0.20f, 0.90f, 0.40f, 1.0f), "SOCKS5 PROXY (RFC 1928)");
    } else {
        ImGui::TextColored(ImVec4(0.20f, 0.80f, 1.00f, 1.0f), "DIRECT (IP PUBLIK LOKAL)");
    }

    ImGui::Spacing();
    renderCopyRow("IP Terbaca", ip_used, "ip", col_w - 240.0f);
    ImGui::SameLine();
    if (ImGui::Button("🔄##refresh_ip", ImVec2(28, 0))) {
        if (bot) {
            bot->refreshPublicIpAsync();
        } else {
            HttpClient::refreshDirectIpAsync();
        }
        status_feedback = "🔄 Mendeteksi ulang alamat IP publik di background...";
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Deteksi ulang alamat IP publik sekarang");
    }

    renderCopyRow("Port", port_used, "port", 100.0f);
    renderCopyRow("Alamat Lengkap", full_addr, "fulladdr", col_w - 200.0f);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Sumber IP      : ");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.18f, 0.95f, 0.50f, 1.0f), "%s", ip_source_label.c_str());

    if (has_proxy) {
        ImGui::Text("Auth User      : %s", proxy.username.empty() ? "(Tanpa Autentikasi)" : proxy.username.c_str());
        ImGui::Text("UDP Relay Mode : %s", proxy.udp_fallback_direct ? "Hybrid (TCP Proxy + UDP Fallback)" : "Full SOCKS5 Tunnel");
    } else {
        ImGui::Text("Keterangan     : Menggunakan IP Publik asli dari koneksi internet Anda");
    }
    ImGui::Text("Target Server  : %s:%d", game_host.c_str(), game_port);

    ImGui::EndChild();

    ImGui::SameLine();

    // Right Column: Hardware Identifier (RID / MAC / WK / Country)
    ImGui::BeginChild("RID_FingerprintCard", ImVec2(col_w, 290), true);
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.95f, 1.0f), "🛡️ HARDWARE IDENTIFIER (RID / MAC / WK)");
    ImGui::Separator();
    ImGui::Spacing();

    renderCopyRow("MAC Address", fp.mac, "mac", col_w - 200.0f);
    renderCopyRow("RID (UUID)", fp.rid, "rid", col_w - 200.0f);
    renderCopyRow("Country Code", fp.country, "country", 90.0f);
    renderCopyRow("WK (Token)", fp.wk, "wk", col_w - 200.0f);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Hash / Hash2   : %d / %d", fp.hash, fp.hash2);
    ImGui::Text("fhash / fz / zf: %d / %d / %d", fp.fhash, fp.fz, fp.zf);
    ImGui::Text("Device Version : %d", fp.device_version);

    int rid_lvl = bot ? bot->getStats().level : gui_stats_.level;
    int rid_gms = bot ? bot->getStats().gems : gui_stats_.gems;
    float rid_played = bot ? bot->getStats().getEffectivePlayedHours() : gui_stats_.getEffectivePlayedHours();
    int rid_age = bot ? bot->getStats().getEffectiveAgeDays() : gui_stats_.getEffectiveAgeDays();

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.20f, 0.85f, 1.0f, 1.0f), "Level: %d", rid_lvl);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.00f, 0.84f, 0.0f, 1.0f), " | Gems: %d", rid_gms);
    ImGui::SameLine();
    ImGui::TextDisabled(" | %.2fh (Age: %dd)", rid_played, rid_age);

    ImGui::EndChild();

    ImGui::Spacing();

    // Bottom Card: Actions & Tools
    ImGui::BeginChild("RID_ActionsCard", ImVec2(0, 150), true);
    ImGui::TextColored(ImVec4(0.85f, 0.95f, 1.0f, 1.0f), "⚡ MANAJEMEN IDENTITAS & AKSI CEPAT");
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("🎲 Acak Ulang Fingerprint Akun Ini (Regenerate)", ImVec2(340, 30))) {
        if (bot) {
            bot->getFingerprint() = DeviceFingerprint::generateRandom(bot->getFingerprint().country);
            bot->getFingerprint().saveToFile("cache/fingerprints/" + account_name + ".json");
            Logger::log("[FINGERPRINT] 🎲 Fingerprint diacak ulang untuk bot #", bot->getId(), " (", account_name, ")");
            Logger::log("              MAC: ", bot->getFingerprint().mac, " | RID: ", bot->getFingerprint().rid);
        } else {
            fingerprint_ = DeviceFingerprint::generateRandom(fingerprint_.country);
            fingerprint_.saveToFile("cache/fingerprints/" + info_.account_name + ".json");
            Logger::log("[FINGERPRINT] 🎲 Fingerprint diacak ulang untuk akun ", info_.account_name);
            Logger::log("              MAC: ", fingerprint_.mac, " | RID: ", fingerprint_.rid);
        }
        status_feedback = "🎲 Fingerprint baru berhasil dibuat dan disimpan ke cache/fingerprints/" + account_name + ".json";
    }

    ImGui::SameLine();

    if (ImGui::Button("💾 Simpan ke File JSON", ImVec2(170, 30))) {
        bool ok = fp.saveToFile("cache/fingerprints/" + account_name + ".json");
        if (ok) {
            status_feedback = "💾 Berhasil disimpan ke cache/fingerprints/" + account_name + ".json";
        } else {
            status_feedback = "❌ Gagal menyimpan file fingerprint!";
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("🔄 Sinkron Stats (/status)", ImVec2(190, 30))) {
        if (bot) {
            bot->queryServerStats();
        } else {
            actions_.say("/status");
        }
        status_feedback = "🔄 Mengirim /status ke server untuk menyinkronkan Level, Gems, Played, Age...";
    }

    ImGui::SameLine();

    if (ImGui::Button("📋 Salin Semua Info Akun", ImVec2(200, 30))) {
        std::ostringstream ss;
        ss << "=== INFORMASI AKUN & IDENTITAS GTPS ===" << "\n"
           << "Akun         : " << account_name << "\n"
           << "Level        : " << rid_lvl << "\n"
           << "Gems         : " << rid_gms << "\n"
           << "Played Time  : " << rid_played << " hours\n"
           << "Account Age  : " << rid_age << " days\n"
           << "IP Terbaca   : " << ip_used << " (" << (has_proxy ? ("SOCKS5 Proxy " + proxy.host + ":" + std::to_string(proxy.port)) : "Direct IP Publik") << ")\n"
           << "Port         : " << port_used << "\n"
           << "Alamat Lengkap: " << full_addr << "\n"
           << "MAC Address  : " << fp.mac << "\n"
           << "RID (UUID)   : " << fp.rid << "\n"
           << "Country      : " << fp.country << "\n"
           << "WK Token     : " << fp.wk << "\n"
           << "Hash / Hash2 : " << fp.hash << " / " << fp.hash2 << "\n"
           << "Target Serv  : " << game_host << ":" << game_port << "\n";
        ImGui::SetClipboardText(ss.str().c_str());
        status_feedback = "📋 Semua data akun, IP (" + ip_used + "), dan RID berhasil disalin ke clipboard!";
    }

    if (!status_feedback.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.20f, 0.95f, 0.40f, 1.0f), "%s", status_feedback.c_str());
    }

    ImGui::EndChild();
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
    ImGui::Text("Proxy SOCKS5   : %s", main_proxy_.isValid() ? main_proxy_.toString().c_str() : "Direct (Tanpa Proxy)");

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

void LuciferGui::renderTabMultiBot() {
    if (!bot_manager_) {
        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "⚠️ Multi-Bot Manager belum terpasang.");
        return;
    }

    // 1. Header Bar: Statistik & Tindakan Massal
    ImGui::BeginChild("MultiBotHeaderCard", ImVec2(0, 115), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "🤖 MULTI-BOT THREAD POOL ORCHESTRATOR");
    ImGui::SameLine();
    ImGui::TextDisabled("| Total: %zu Bot | Online: %zu | Threads: %zu Workers",
                       bot_manager_->getBotCount(), bot_manager_->getOnlineCount(),
                       bot_manager_->getThreadPool().getThreadCount());

    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("▶️ Start All (Staggered)", ImVec2(180, 28))) {
        bot_manager_->startAll(1500);
    }
    ImGui::SameLine();
    if (ImGui::Button("🛑 Stop All", ImVec2(120, 28))) {
        bot_manager_->stopAll();
    }
    ImGui::SameLine();
    if (ImGui::Button("🧹 Bersihkan Pool", ImVec2(140, 28))) {
        bot_manager_->clearBots();
    }

    ImGui::Spacing();
    ImGui::SetNextItemWidth(160);
    ImGui::InputTextWithHint("##mbwarp", "World...", multibot_warp_world_, sizeof(multibot_warp_world_));
    ImGui::SameLine();
    if (ImGui::Button("🚀 Warp Semua Bot", ImVec2(150, 0))) {
        if (strlen(multibot_warp_world_) > 0) {
            bot_manager_->warpAll(multibot_warp_world_);
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##mbsay", "Ketik chat massal...", multibot_say_text_, sizeof(multibot_say_text_));
    ImGui::SameLine();
    if (ImGui::Button("💬 Say Semua Bot", ImVec2(150, 0))) {
        if (strlen(multibot_say_text_) > 0) {
            bot_manager_->sayAll(multibot_say_text_);
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();

    // 2. Konten Utama: 2 Kolom (Kiri: Form Tambah & Import | Kanan: Tabel Bot)
    ImGui::Columns(2, "MultiBotCols", false);
    ImGui::SetColumnWidth(0, 360.0f);

    // Form Tambah Bot
    ImGui::BeginChild("AddBotCard", ImVec2(0, 340), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "➕ TAMBAH BOT BARU");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SetNextItemWidth(180);
    ImGui::InputText("GrowID / User", new_bot_user_, sizeof(new_bot_user_));
    ImGui::SetNextItemWidth(180);
    ImGui::InputText("Password", new_bot_pass_, sizeof(new_bot_pass_), ImGuiInputTextFlags_Password);
    ImGui::SetNextItemWidth(180);
    ImGui::InputText("Target World", new_bot_world_, sizeof(new_bot_world_));

    ImGui::Spacing();
    ImGui::Checkbox("Gunakan Proxy SOCKS5", &new_bot_use_proxy_);
    if (new_bot_use_proxy_) {
        ImGui::SetNextItemWidth(200);
        ImGui::InputTextWithHint("##QuickProxyNew", "host:port:user:pass", new_bot_proxy_quick_, sizeof(new_bot_proxy_quick_));
        ImGui::SameLine();
        if (ImGui::Button("⚡ Parse##new", ImVec2(55, 20))) {
            if (strlen(new_bot_proxy_quick_) > 0) {
                auto parsed = Socks5Config::parse(new_bot_proxy_quick_);
                if (parsed.isValid()) {
                    strncpy_s(new_bot_proxy_host_, parsed.host.c_str(), sizeof(new_bot_proxy_host_) - 1);
                    new_bot_proxy_port_ = parsed.port;
                    strncpy_s(new_bot_proxy_user_, parsed.username.c_str(), sizeof(new_bot_proxy_user_) - 1);
                    strncpy_s(new_bot_proxy_pass_, parsed.password.c_str(), sizeof(new_bot_proxy_pass_) - 1);
                }
            }
        }

        ImGui::SetNextItemWidth(160);
        ImGui::InputText("Proxy Host", new_bot_proxy_host_, sizeof(new_bot_proxy_host_));
        ImGui::SetNextItemWidth(90);
        ImGui::InputInt("Proxy Port", &new_bot_proxy_port_);
        ImGui::SetNextItemWidth(140);
        ImGui::InputText("Proxy User", new_bot_proxy_user_, sizeof(new_bot_proxy_user_));
        ImGui::SetNextItemWidth(140);
        ImGui::InputText("Proxy Pass", new_bot_proxy_pass_, sizeof(new_bot_proxy_pass_), ImGuiInputTextFlags_Password);

        static char new_bot_test_res[128] = "";
        if (ImGui::Button("🔍 Uji Proxy Ini##new", ImVec2(150, 22))) {
            Socks5Config tcfg;
            tcfg.enabled = true;
            tcfg.host = new_bot_proxy_host_;
            tcfg.port = new_bot_proxy_port_;
            tcfg.username = new_bot_proxy_user_;
            tcfg.password = new_bot_proxy_pass_;
            std::string tmsg;
            Socks5Client::testProxy(tcfg, tmsg);
            strncpy_s(new_bot_test_res, tmsg.c_str(), sizeof(new_bot_test_res) - 1);
        }
        if (strlen(new_bot_test_res) > 0) {
            ImGui::SameLine();
            ImGui::TextColored(strstr(new_bot_test_res, "✅") ? ImVec4(0.25f, 0.95f, 0.35f, 1.0f) : ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
                               "%s", new_bot_test_res);
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("➕ Tambahkan ke Pool", ImVec2(220, 30))) {
        if (strlen(new_bot_user_) > 0) {
            Socks5Config pcfg;
            if (new_bot_use_proxy_) {
                pcfg.enabled = true;
                pcfg.host = new_bot_proxy_host_;
                pcfg.port = new_bot_proxy_port_;
                pcfg.username = new_bot_proxy_user_;
                pcfg.password = new_bot_proxy_pass_;
            }
            bot_manager_->addBot(new_bot_user_, new_bot_pass_, new_bot_world_, pcfg);
            bot_manager_->saveAccountsToFile(accounts_file_path_);
            new_bot_user_[0] = '\0';
            new_bot_pass_[0] = '\0';
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();

    // Import / Export Card
    ImGui::BeginChild("ImportExportCard", ImVec2(0, 140), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "📁 IMPORT / EXPORT AKUN");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SetNextItemWidth(220);
    ImGui::InputText("File", accounts_file_path_, sizeof(accounts_file_path_));
    ImGui::Spacing();
    if (ImGui::Button("📥 Import Akun", ImVec2(120, 26))) {
        bot_manager_->loadAccountsFromFile(accounts_file_path_);
    }
    ImGui::SameLine();
    if (ImGui::Button("💾 Export Akun", ImVec2(120, 26))) {
        bot_manager_->saveAccountsToFile(accounts_file_path_, true);
    }
    ImGui::EndChild();

    ImGui::NextColumn();

    // Tabel Kanan: Daftar Bot
    ImGui::BeginChild("BotListCard", ImVec2(0, 340), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "📋 DAFTAR BOT DI THREAD POOL");
    ImGui::Separator();

    const auto& bots = cached_bots_;
    if (ImGui::BeginTable("MultiBotTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("GrowID", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Target World", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Proxy SOCKS5", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Tindakan", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableHeadersRow();

        for (const auto& b : bots) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("#%d", b->getId());

            ImGui::TableSetColumnIndex(1);
            if (b->getId() == selected_bot_id_) {
                ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.30f, 1.0f), "👉 %s", b->getAccountName().c_str());
            } else {
                ImGui::Text("%s", b->getAccountName().c_str());
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", b->getTargetWorld().empty() ? "-" : b->getTargetWorld().c_str());

            ImGui::TableSetColumnIndex(3);
            std::string st = b->getStatusString();
            if (b->isOnline()) {
                ImGui::TextColored(ImVec4(0.25f, 0.95f, 0.35f, 1.0f), "● %s", st.c_str());
            } else if (st.find("Retry") != std::string::npos || st.find("Connecting") != std::string::npos) {
                ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.25f, 1.0f), "⏳ %s", st.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.70f, 0.70f, 0.70f, 1.0f), "○ %s", st.c_str());
            }

            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%s", b->getProxy().enabled ? b->getProxy().toString().c_str() : "Direct");

            ImGui::TableSetColumnIndex(5);
            ImGui::PushID(b->getId());
            if (ImGui::SmallButton("Start")) {
                b->start();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Stop")) {
                b->stop();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Pilih")) {
                selected_bot_id_ = b->getId();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) {
                bot_manager_->removeBot(b->getId());
                bot_manager_->saveAccountsToFile(accounts_file_path_);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    // Card Bawah: Detail Bot Terpilih
    auto sel_bot = bot_manager_->getBotById(selected_bot_id_);
    if (sel_bot) {
        ImGui::Spacing();
        ImGui::BeginChild("SelectedBotDetail", ImVec2(0, 245), true);
        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.30f, 1.0f), "🔎 DETAIL & PENGATURAN BOT: #%d (%s)",
                           sel_bot->getId(), sel_bot->getAccountName().c_str());
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Status: %s | Ping: %d ms | Target World: %s | Proxy: %s",
                    sel_bot->getStatusString().c_str(), sel_bot->getPing(),
                    sel_bot->getTargetWorld().empty() ? "(none)" : sel_bot->getTargetWorld().c_str(),
                    sel_bot->getProxy().enabled ? sel_bot->getProxy().toString().c_str() : "Direct");
        ImGui::Text("Fingerprint MAC: %s | RID: %s... | Country: %s",
                    sel_bot->getFingerprint().mac.c_str(), sel_bot->getFingerprint().rid.substr(0, 8).c_str(),
                    sel_bot->getFingerprint().country.c_str());

        ImGui::Spacing();

        // Editor Password & Target World
        static char edit_pass[64] = "";
        static char edit_world[64] = "";
        static int last_edit_id = -1;
        if (last_edit_id != sel_bot->getId()) {
            last_edit_id = sel_bot->getId();
            strncpy_s(edit_pass, sel_bot->getPassword().c_str(), sizeof(edit_pass) - 1);
            strncpy_s(edit_world, sel_bot->getTargetWorld().c_str(), sizeof(edit_world) - 1);
            sel_use_proxy_ = sel_bot->getProxy().enabled;
            strncpy_s(sel_proxy_host_, sel_bot->getProxy().host.c_str(), sizeof(sel_proxy_host_) - 1);
            sel_proxy_port_ = sel_bot->getProxy().port > 0 ? sel_bot->getProxy().port : 1080;
            strncpy_s(sel_proxy_user_, sel_bot->getProxy().username.c_str(), sizeof(sel_proxy_user_) - 1);
            strncpy_s(sel_proxy_pass_, sel_bot->getProxy().password.c_str(), sizeof(sel_proxy_pass_) - 1);
            sel_bot_proxy_quick_[0] = '\0';
            sel_proxy_test_result_[0] = '\0';
        }

        ImGui::SetNextItemWidth(140);
        ImGui::InputText("Password", edit_pass, sizeof(edit_pass), ImGuiInputTextFlags_Password);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130);
        ImGui::InputText("Target World", edit_world, sizeof(edit_world));
        ImGui::SameLine();
        if (ImGui::Button("💾 Simpan Akun & Proxy", ImVec2(170, 22))) {
            sel_bot->setPassword(edit_pass);
            sel_bot->setTargetWorld(edit_world);
            Socks5Config pcfg;
            if (sel_use_proxy_) {
                pcfg.enabled = true;
                pcfg.host = sel_proxy_host_;
                pcfg.port = sel_proxy_port_;
                pcfg.username = sel_proxy_user_;
                pcfg.password = sel_proxy_pass_;
            }
            sel_bot->setProxy(pcfg);
            bot_manager_->saveAccountsToFile(accounts_file_path_);
            Logger::log("[MANAGER] ✅ Kredensial & Proxy bot '", sel_bot->getAccountName(), "' disimpan: ", pcfg.toString());
        }

        ImGui::Spacing();
        ImGui::Checkbox("Gunakan Proxy SOCKS5##sel", &sel_use_proxy_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220);
        ImGui::InputTextWithHint("##SelQuickProxy", "Paste: host:port:user:pass", sel_bot_proxy_quick_, sizeof(sel_bot_proxy_quick_));
        ImGui::SameLine();
        if (ImGui::Button("⚡ Parse##sel", ImVec2(60, 20))) {
            if (strlen(sel_bot_proxy_quick_) > 0) {
                auto parsed = Socks5Config::parse(sel_bot_proxy_quick_);
                if (parsed.isValid()) {
                    sel_use_proxy_ = true;
                    strncpy_s(sel_proxy_host_, parsed.host.c_str(), sizeof(sel_proxy_host_) - 1);
                    sel_proxy_port_ = parsed.port;
                    strncpy_s(sel_proxy_user_, parsed.username.c_str(), sizeof(sel_proxy_user_) - 1);
                    strncpy_s(sel_proxy_pass_, parsed.password.c_str(), sizeof(sel_proxy_pass_) - 1);
                }
            }
        }

        if (sel_use_proxy_) {
            ImGui::SetNextItemWidth(140);
            ImGui::InputText("Host##sel", sel_proxy_host_, sizeof(sel_proxy_host_));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70);
            ImGui::InputInt("Port##sel", &sel_proxy_port_);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::InputText("User##sel", sel_proxy_user_, sizeof(sel_proxy_user_));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::InputText("Pass##sel", sel_proxy_pass_, sizeof(sel_proxy_pass_), ImGuiInputTextFlags_Password);
            ImGui::SameLine();
            if (ImGui::Button("🔍 Uji##sel", ImVec2(60, 22))) {
                Socks5Config tcfg;
                tcfg.enabled = true;
                tcfg.host = sel_proxy_host_;
                tcfg.port = sel_proxy_port_;
                tcfg.username = sel_proxy_user_;
                tcfg.password = sel_proxy_pass_;
                std::string res;
                Socks5Client::testProxy(tcfg, res);
                strncpy_s(sel_proxy_test_result_, res.c_str(), sizeof(sel_proxy_test_result_) - 1);
            }
            if (strlen(sel_proxy_test_result_) > 0) {
                ImGui::SameLine();
                ImGui::TextColored(strstr(sel_proxy_test_result_, "✅") ? ImVec4(0.25f, 0.95f, 0.35f, 1.0f) : ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
                                   "%s", sel_proxy_test_result_);
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("🚀 Warp Bot Ini", ImVec2(130, 24))) {
            std::string tw = strlen(edit_world) > 0 ? edit_world : sel_bot->getTargetWorld();
            if (!tw.empty()) {
                sel_bot->setTargetWorld(tw);
                sel_bot->warp(tw);
                bot_manager_->saveAccountsToFile(accounts_file_path_);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("🧲 Collect Sekitar", ImVec2(130, 24))) {
            sel_bot->collectNearby(4.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("🔄 Respawn Bot Ini", ImVec2(130, 24))) {
            sel_bot->respawn();
        }
        ImGui::SameLine();
        if (ImGui::Button("❌ Set Direct (Hapus Proxy)", ImVec2(190, 24))) {
            sel_use_proxy_ = false;
            sel_proxy_host_[0] = '\0';
            sel_proxy_user_[0] = '\0';
            sel_proxy_pass_[0] = '\0';
            Socks5Config none;
            sel_bot->setProxy(none);
            bot_manager_->saveAccountsToFile(accounts_file_path_);
            Logger::log("[MANAGER] ❌ Proxy bot '", sel_bot->getAccountName(), "' dihapus (Direct mode).");
        }
        ImGui::EndChild();
    }

    ImGui::Columns(1);
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
            actions_.moveTo(tx, ty - move_step_, 120);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            actions_.moveTo(tx, ty + move_step_, 120);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            actions_.moveTo(tx - move_step_, ty, 120);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            actions_.moveTo(tx + move_step_, ty, 120);
        }
    }

    // D-Pad Arrow Buttons Layout
    float btn_w = 110.0f;
    float btn_h = 45.0f;
    float center_x = 220.0f;

    // Up Arrow Button
    ImGui::SetCursorPosX(center_x);
    if (ImGui::Button(" ▲ ATAS ", ImVec2(btn_w, btn_h))) {
        actions_.moveTo(tx, ty - move_step_, 120);
    }

    ImGui::Spacing();

    // Left, Center, Right Arrow Buttons
    ImGui::SetCursorPosX(center_x - btn_w - 10.0f);
    if (ImGui::Button(" ◄ KIRI ", ImVec2(btn_w, btn_h))) {
        actions_.moveTo(tx - move_step_, ty, 120);
    }
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(btn_w, btn_h)); // Spacer tengah
    ImGui::SameLine();
    if (ImGui::Button(" KANAN ► ", ImVec2(btn_w, btn_h))) {
        actions_.moveTo(tx + move_step_, ty, 120);
    }

    ImGui::Spacing();

    // Down Arrow Button
    ImGui::SetCursorPosX(center_x);
    if (ImGui::Button(" ▼ BAWAH ", ImVec2(btn_w, btn_h))) {
        actions_.moveTo(tx, ty + move_step_, 120);
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

void LuciferGui::renderTabAntiDetection() {
    ImGui::Columns(2, "AntiDetCols", false);
    ImGui::SetColumnWidth(0, 520.0f);

    // Card 1: Device Fingerprint
    ImGui::BeginChild("FingerprintCard", ImVec2(0, 360), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "🛡️ DEVICE FINGERPRINT (IDENTITAS HARDWARE)");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("MAC Address   : %s", fingerprint_.mac.c_str());
    ImGui::Text("RID (UUID)    : %s", fingerprint_.rid.c_str());
    ImGui::Text("WK (Token)    : %s", fingerprint_.wk.c_str());
    ImGui::Text("Hash          : %d", fingerprint_.hash);
    ImGui::Text("Hash2         : %d", fingerprint_.hash2);
    ImGui::Text("fhash         : %d", fingerprint_.fhash);
    ImGui::Text("fz / zf       : %d / %d", fingerprint_.fz, fingerprint_.zf);
    ImGui::Text("Country Code  : %s", fingerprint_.country.c_str());
    ImGui::Text("Device Version: %d", fingerprint_.device_version);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("🎲 Acak Ulang Fingerprint (Regenerate)", ImVec2(300, 32))) {
        fingerprint_ = DeviceFingerprint::generateRandom(fingerprint_.country);
        fingerprint_.saveToFile("cache/fingerprints/" + info_.account_name + ".json");
        Logger::log("[FINGERPRINT] 🎲 Fingerprint diacak ulang untuk akun ", info_.account_name);
        Logger::log("              MAC: ", fingerprint_.mac, " | RID: ", fingerprint_.rid.substr(0, 8), "...");
    }
    ImGui::TextDisabled("Catatan: Fingerprint baru akan otomatis tersimpan di cache/fingerprints/");

    ImGui::EndChild();

    ImGui::Spacing();

    // Card 2: Delay Randomization
    ImGui::BeginChild("DelayJitterCard", ImVec2(0, 180), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "⏱️ DELAY & RANDOMIZATION (HUMAN JITTER)");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Checkbox("Aktifkan Random Jitter pada Delay Tindakan", &AntiDetection::enable_random_delays);
    ImGui::TextDisabled("Memberikan variasi acak (±15-25%%) pada jeda gerak, pukul, dan autofarm.");

    ImGui::Spacing();
    ImGui::Checkbox("Aktifkan Simulasi Jeda Manusia (Micro-Pauses)", &AntiDetection::enable_human_pauses);
    ImGui::TextDisabled("Menambahkan jeda sesaat secara acak (150-350ms) seperti interaksi pemain manusia asli.");

    ImGui::EndChild();

    ImGui::Spacing();

    // Card 4: SOCKS5 Proxy Tester
    ImGui::BeginChild("Socks5TesterCard", ImVec2(0, 240), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "🌐 SOCKS5 PROXY TESTER (RFC 1928 / 1929)");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SetNextItemWidth(180);
    ImGui::InputText("Host Proxy", proxy_test_host_, sizeof(proxy_test_host_));
    ImGui::SetNextItemWidth(90);
    ImGui::InputInt("Port Proxy", &proxy_test_port_);
    ImGui::SetNextItemWidth(140);
    ImGui::InputText("User (Opsional)", proxy_test_user_, sizeof(proxy_test_user_));
    ImGui::SetNextItemWidth(140);
    ImGui::InputText("Pass (Opsional)", proxy_test_pass_, sizeof(proxy_test_pass_), ImGuiInputTextFlags_Password);

    ImGui::Spacing();
    if (ImGui::Button("🔍 Uji Konektivitas Proxy SOCKS5", ImVec2(240, 28))) {
        Socks5Config tcfg;
        tcfg.enabled = true;
        tcfg.host = proxy_test_host_;
        tcfg.port = proxy_test_port_;
        tcfg.username = proxy_test_user_;
        tcfg.password = proxy_test_pass_;
        std::string res;
        Socks5Client::testProxy(tcfg, res);
        strncpy_s(proxy_test_result_, res.c_str(), sizeof(proxy_test_result_) - 1);
    }

    if (strlen(proxy_test_result_) > 0) {
        ImGui::Spacing();
        if (strstr(proxy_test_result_, "✅") != nullptr) {
            ImGui::TextColored(ImVec4(0.25f, 0.95f, 0.35f, 1.0f), "%s", proxy_test_result_);
        } else {
            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", proxy_test_result_);
        }
    }
    ImGui::EndChild();

    ImGui::NextColumn();

    // Card 3: Auto-Reconnect with Exponential Backoff
    ImGui::BeginChild("ReconnectCard", ImVec2(0, 800), true);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "🔄 AUTO-RECONNECT (EXPONENTIAL BACKOFF)");
    ImGui::Separator();
    ImGui::Spacing();

    bool rec_enabled = reconnect_ctrl_.isEnabled();
    if (ImGui::Checkbox("Aktifkan Fitur Auto-Reconnect", &rec_enabled)) {
        reconnect_ctrl_.setEnabled(rec_enabled);
    }

    ImGui::Spacing();
    ImGui::Text("Status Reconnect : %s", reconnect_ctrl_.getStateString().c_str());
    ImGui::Text("Percobaan Gagal  : %d kali", reconnect_ctrl_.getRetryCount());
    ImGui::Text("Interval Delay   : %.1f detik", reconnect_ctrl_.getNextDelayMs() / 1000.0f);

    if (reconnect_ctrl_.getState() == ReconnectState::WaitingReconnect) {
        int sec_rem = reconnect_ctrl_.getSecondsRemaining();
        int total_sec = std::max(1, reconnect_ctrl_.getNextDelayMs() / 1000);
        float progress = 1.0f - (static_cast<float>(sec_rem) / static_cast<float>(total_sec));
        ImGui::Spacing();
        ImGui::ProgressBar(progress, ImVec2(-1, 24),
                           (std::to_string(sec_rem) + " detik tersisa...").c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("⚡ Reconnect Sekarang", ImVec2(180, 32))) {
        if (reconnect_fn_) {
            reconnect_ctrl_.triggerNow(reconnect_fn_);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("🛑 Batalkan Countdown", ImVec2(180, 32))) {
        reconnect_ctrl_.cancel();
    }

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.30f, 1.0f), "💡 Cara Kerja Exponential Backoff:");
    ImGui::BulletText("Percobaan 1: ~2 detik");
    ImGui::BulletText("Percobaan 2: ~4 detik (+-15%% jitter)");
    ImGui::BulletText("Percobaan 3: ~8 detik (+-15%% jitter)");
    ImGui::BulletText("Percobaan 4: ~16 detik (+-15%% jitter)");
    ImGui::BulletText("Maksimal    : 30 detik (menghindari spam server)");
    ImGui::BulletText("Counter otomatis reset ke 0 saat berhasil online di world!");

    ImGui::EndChild();

    ImGui::Columns(1);
}

void LuciferGui::renderTabScriptEditor() {
    bool is_running = lua_engine_.isRunning();

    ImGui::BeginChild("ScriptTopBar", ImVec2(0, 140), true);

    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.0f), "📜 SCRIPT ENGINE (Bothax Lua Docs & Lucifer v2.86 Supported)");
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
    ImGui::TextDisabled("| Bothax:");
    ImGui::SameLine();
    if (ImGui::SmallButton("Block Dialog")) {
        const char* t =
            "-- Bothax Hook: Block Dialog & Log\n"
            "AddHook('OnVariant', 'BlockDialog', function(var, netid, delay)\n"
            "    if var[0] == 'OnDialogRequest' then\n"
            "        LogToConsole('🛑 Blocked Dialog from NetID: ' .. tostring(netid))\n"
            "        return true\n"
            "    end\n"
            "end)\n"
            "LogToConsole('Bothax Dialog Blocker Hook Aktif!')\n";
        strncpy_s(script_buffer_, t, sizeof(script_buffer_) - 1);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Scan Tas")) {
        const char* t =
            "-- Bothax: Scan Inventory\n"
            "local inv = GetInventory()\n"
            "LogToConsole('🎒 Total item di tas: ' .. #inv)\n"
            "for _, itm in pairs(inv) do\n"
            "    local info = GetItemByIDSafe(itm.id)\n"
            "    LogToConsole('Item ID: ' .. itm.id .. ' (' .. info.name .. ') x' .. itm.amount .. ' [Flags: ' .. itm.flags .. ']')\n"
            "end\n";
        strncpy_s(script_buffer_, t, sizeof(script_buffer_) - 1);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("World & Player")) {
        const char* t =
            "-- Bothax: Scan World & Players\n"
            "local w = GetWorld()\n"
            "LogToConsole('🌍 World: ' .. w.name .. ' (' .. w.width .. 'x' .. w.height .. ')')\n"
            "local me = GetLocal()\n"
            "LogToConsole('👤 Local Bot: ' .. me.name .. ' di (' .. me.tile_x .. ', ' .. me.tile_y .. ')')\n"
            "local plrs = GetPlayerList()\n"
            "LogToConsole('👥 Pemain lain di world: ' .. #plrs)\n"
            "for _, p in pairs(plrs) do\n"
            "    LogToConsole(' - ' .. p.name .. ' di (' .. (p.pos.x // 32) .. ', ' .. (p.pos.y // 32) .. ')')\n"
            "end\n";
        strncpy_s(script_buffer_, t, sizeof(script_buffer_) - 1);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Cari WL")) {
        const char* t =
            "-- Bothax: Scan World Locks (GetTiles)\n"
            "local tiles = GetTiles()\n"
            "LogToConsole('Total tiles loaded: ' .. #tiles)\n"
            "for _, tile in pairs(tiles) do\n"
            "    if tile.fg == 242 then\n"
            "        LogToConsole('Found World Lock at (' .. (tile.pos.x // 32) .. ', ' .. (tile.pos.y // 32) .. ')')\n"
            "    end\n"
            "end\n";
        strncpy_s(script_buffer_, t, sizeof(script_buffer_) - 1);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("HTTP Request")) {
        const char* t =
            "-- Bothax: HTTP MakeRequest\n"
            "local res = MakeRequest('https://growtopiagame.com/detail', 'GET')\n"
            "LogToConsole('HTTP Status: ' .. res.status)\n"
            "local online = res.content:match('\"online_user\":\"(%d+)\"')\n"
            "if online then\n"
            "    LogToConsole('Current User Online: ' .. online)\n"
            "else\n"
            "    LogToConsole('Response Content Length: ' .. #res.content)\n"
            "end\n";
        strncpy_s(script_buffer_, t, sizeof(script_buffer_) - 1);
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Lucifer Templates:");
    ImGui::SameLine();
    if (ImGui::SmallButton("Lucifer: Patrol")) {
        const char* t =
            "-- Template: Walk Patrol (Lucifer API)\n"
            "local me = bot:getLocal()\n"
            "log(\"Start Patrol from \" .. me.tile_x)\n"
            "bot:moveTo(me.tile_x + 3, me.tile_y, 40)\n"
            "sleep(500)\n"
            "bot:moveTo(me.tile_x, me.tile_y, 40)\n"
            "log(\"Patrol complete!\")\n";
        strncpy_s(script_buffer_, t, sizeof(script_buffer_) - 1);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Lucifer: Spam Chat")) {
        const char* t =
            "-- Template: Spam Chat\n"
            "for i = 1, 3 do\n"
            "    bot:say(\"Hello from Lucifer Lua Bot! #\" .. i)\n"
            "    sleep(1000)\n"
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

    if (ImGui::CollapsingHeader("📖 Referensi Cepat Bothax Lua API & Structures")) {
        ImGui::BeginChild("BothaxDocsRef", ImVec2(0, 140), true);
        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.30f, 1.0f), "Fungsi Bothax Utama:");
        ImGui::BulletText("GetLocal() -> NetAvatar { pos={x,y}, name, netid, userid, country, isleft, tile_x, tile_y }");
        ImGui::BulletText("GetWorld() -> World { name, width, height, tilecount, objectcount, lastoid }");
        ImGui::BulletText("GetInventory() -> Inventory[] { id, amount, flags }");
        ImGui::BulletText("GetTile(x, y) / GetTiles() -> Tile[] { fg, bg, collidable, x, y, pos={x,y}, coltype, flags, extra }");
        ImGui::BulletText("GetObjectList() -> WorldObject[] { id, pos={x,y}, amount, flags, oid }");
        ImGui::BulletText("GetPlayerList() -> NetAvatar[] | GetPlayer(netid) -> NetAvatar");
        ImGui::BulletText("GetCamera() -> WorldCamera | GetClient() -> ENetClient { address, port, ping }");
        ImGui::BulletText("FindPath(x, y) | CheckPath(x, y) -> boolean");
        ImGui::BulletText("SendPacket(type, text) | SendPacketRaw(to_client, packet) | SendVariantList(var, netid, delay)");
        ImGui::BulletText("AddHook(BothaxHook, label, callback) | RemoveHook(label) | RemoveHooks()");
        ImGui::BulletText("Hooks: 'OnVariant', 'OnSendPacket', 'OnSendPacketRaw', 'OnProcessTankUpdate', 'OnWorldTouch', 'OnDraw', 'OnInput'");
        ImGui::BulletText("Sleep(ms) | LogToConsole(str) | MakeRequest(url, method) | Hash32/64 | Encrypt/LoadEncrypt");
        ImGui::EndChild();
        ImGui::Spacing();
    }

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
