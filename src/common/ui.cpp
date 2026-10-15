// The settings panel (F1, or both sticks pressed on pad 1) and the frame rate counter; every change is saved to
// settings.toml at once.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <vector>

#include <imgui.h>

#include "app/app.h"
#include "core/input.h"

namespace debug {
void Pad(int port, input::Pad& pad);
}

namespace ui {

bool g_fps_counter = false;

namespace {

std::atomic<bool> g_open{false};
uint16_t g_last_buttons = 0;

constexpr uint16_t kLeftThumb = 0x40, kRightThumb = 0x80;

// the monitor's modes, largest first
std::vector<app::Mode> Modes() {
    std::vector<app::Mode> modes;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm); i++) {
        if (dm.dmBitsPerPel != 32 || dm.dmPelsWidth < 640 || dm.dmPelsHeight < 480) continue;
        app::Mode m{dm.dmPelsWidth, dm.dmPelsHeight};
        if (std::none_of(modes.begin(), modes.end(), [&](const app::Mode& o) { return o.width == m.width && o.height == m.height; }))
            modes.push_back(m);
    }
    std::sort(modes.begin(), modes.end(), [](const app::Mode& a, const app::Mode& b) {
        return a.width != b.width ? a.width > b.width : a.height > b.height;
    });
    return modes;
}

// ImGui's font and sizes are made for about 720 lines: larger windows scale them up
void Scale() {
    static float scale = 0;
    float want = std::max(1.0f, std::round(ImGui::GetIO().DisplaySize.y / 720.0f * 4) / 4);
    if (want == scale) return;
    scale = want;
    ImGui::GetStyle() = ImGuiStyle();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetIO().FontGlobalScale = scale;
}

void Counter(double fps) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.0f FPS", fps);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float px = ImGui::GetFontSize() * 1.25f, pad = px * 0.3f;
    ImVec2 size = ImGui::GetFont()->CalcTextSizeA(px, FLT_MAX, 0, text), at{pad * 2, pad * 1.5f};
    dl->AddRectFilled({at.x - pad, at.y - pad * 0.5f}, {at.x + size.x + pad, at.y + size.y + pad * 0.5f}, IM_COL32(0, 0, 0, 150), pad);
    dl->AddText(ImGui::GetFont(), px, at, IM_COL32(255, 255, 255, 255), text);
}

void Panel() {
    app::Settings& s = app::Get();
    bool changed = false, display = false;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f}, ImGuiCond_Appearing, {0.5f, 0.5f});
    bool open = true;
    if (!ImGui::Begin("Recomp Settings", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    ImGui::PushItemWidth(ImGui::GetFontSize() * 14);

    static const char* kDisplay[] = {"Fullscreen", "Borderless", "Windowed"};
    int mode = int(s.display_mode);
    if (ImGui::Combo("Display mode", &mode, kDisplay, 3)) {
        s.display_mode = app::DisplayMode(mode);
        changed = display = true;
    }

    static std::vector<app::Mode> modes = Modes();
    char current[32];
    if (s.width && s.height) std::snprintf(current, sizeof(current), "%u x %u", s.width, s.height);
    else std::snprintf(current, sizeof(current), "Desktop");
    if (ImGui::BeginCombo("Resolution", current)) {
        if (ImGui::Selectable("Desktop", !s.width)) s.width = s.height = 0, changed = display = true;
        for (const app::Mode& m : modes) {
            char name[32];
            std::snprintf(name, sizeof(name), "%u x %u", m.width, m.height);
            if (ImGui::Selectable(name, s.width == m.width && s.height == m.height))
                s.width = m.width, s.height = m.height, changed = display = true;
        }
        ImGui::EndCombo();
    }

    int render = int(s.render_percent);
    if (ImGui::SliderInt("Render resolution", &render, 25, 200, "%d%%")) {
        s.render_percent = uint32_t(render);
        changed = true;
    }

    static const uint32_t kCaps[] = {30, 60, 75, 120, 144, 165, 240, 0};
    static const char* kCapNames[] = {"30", "60", "75", "120", "144", "165", "240", "Unlimited"};
    int cap = int(std::find(std::begin(kCaps), std::end(kCaps), s.fps_cap) - std::begin(kCaps));
    if (cap == int(std::size(kCaps))) cap = 1;
    if (ImGui::Combo("Max frame rate", &cap, kCapNames, int(std::size(kCaps)))) {
        s.fps_cap = kCaps[cap];
        changed = true;
    }
    changed |= ImGui::Checkbox("VSync", &s.vsync);
    changed |= ImGui::Checkbox("FPS counter", &g_fps_counter);

    static const uint32_t kAniso[] = {0, 2, 4, 8, 16};
    static const char* kAnisoNames[] = {"As the game asks", "2x", "4x", "8x", "16x"};
    int aniso = int(std::find(std::begin(kAniso), std::end(kAniso), s.aniso) - std::begin(kAniso));
    if (aniso == int(std::size(kAniso))) aniso = 0;
    if (ImGui::Combo("Anisotropic filtering", &aniso, kAnisoNames, int(std::size(kAniso)))) {
        s.aniso = kAniso[aniso];
        changed = true;
    }

    ImGui::PopItemWidth();
    ImGui::Separator();
    ImGui::TextDisabled("F1 or both sticks: close.  Saved to settings.toml.");
    ImGui::End();

    if (changed) app::Save();
    if (display) app::ApplyDisplay();
    if (!open) g_open = false;
}

}  // namespace

void Toggle() { g_open = !g_open; }

void Draw(double fps) {
    Scale();
    if (g_fps_counter) Counter(fps);
    if (g_open) Panel();
}

// F1 opens and closes the panel
bool Message(void*, uint32_t msg, uintptr_t wp, intptr_t, intptr_t*) {
    if (msg == WM_KEYDOWN && wp == VK_F1) {
        std::lock_guard lock(app::UiLock());
        Toggle();
        return true;
    }
    return false;
}

// both sticks pressed open and close it; while it is up, pad 1 moves through it and the game gets nothing
void Pad(int port, input::Pad& pad) {
    debug::Pad(port, pad);
    if (port != 0) return;
    uint16_t both = kLeftThumb | kRightThumb;
    bool pressed = (pad.buttons & both) == both && (g_last_buttons & both) != both;
    g_last_buttons = pad.buttons;
    if (pressed) {
        std::lock_guard lock(app::UiLock());
        Toggle();
    }
    if (!g_open) return;
    {
        std::lock_guard lock(app::UiLock());
        if (ImGui::GetCurrentContext()) {
            ImGuiIO& io = ImGui::GetIO();
            io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
            io.AddKeyEvent(ImGuiKey_GamepadDpadUp, (pad.buttons & 0x01) || pad.ly > 16000);
            io.AddKeyEvent(ImGuiKey_GamepadDpadDown, (pad.buttons & 0x02) || pad.ly < -16000);
            io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, (pad.buttons & 0x04) || pad.lx < -16000);
            io.AddKeyEvent(ImGuiKey_GamepadDpadRight, (pad.buttons & 0x08) || pad.lx > 16000);
            io.AddKeyEvent(ImGuiKey_GamepadFaceDown, pad.analog[0] > 30);  // A: activate
            io.AddKeyEvent(ImGuiKey_GamepadFaceRight, pad.analog[1] > 30);  // B: back
        }
    }
    bool connected = pad.connected;
    pad = input::Pad{};
    pad.connected = connected;
}

}  // namespace ui
