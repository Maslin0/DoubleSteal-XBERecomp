// Recomp Settings: a page drawn like the game's own OPTIONS screen (F1, or both sticks pressed on pad 1), and the
// frame rate counter. Every change is saved to settings.toml at once.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <imgui.h>

#include "app/app.h"
#include "core/input.h"

namespace debug {
void Pad(int port, input::Pad& pad);
}

namespace ui {

bool g_fps_counter = false;
bool g_skip_intros = false;

namespace {

using Clock = std::chrono::steady_clock;

// --- the page's look: the game's OPTIONS screen, measured on a 1440x810 frame -------------------------------------

constexpr float kW = 1440, kH = 810;
const ImU32 kGold = IM_COL32(214, 146, 22, 255);  // title, top lines
const ImU32 kLabel = IM_COL32(118, 118, 118, 255);  // rows not chosen
const ImU32 kChosen = IM_COL32(150, 222, 112, 255);  // the chosen row's name
const ImU32 kValue = IM_COL32(200, 200, 200, 255);
const ImU32 kValueChosen = IM_COL32(226, 206, 156, 255);
const ImU32 kArrow = IM_COL32(232, 84, 24, 255);
const ImU32 kHelp = IM_COL32(236, 162, 30, 255);
const ImU32 kLine = IM_COL32(150, 150, 150, 255);
const ImU32 kDiamond = IM_COL32(19, 19, 19, 255);

ImFont* g_heavy = nullptr;  // Arial Black, slanted when drawn: the menu lettering
ImFont* g_bold = nullptr;  // Arial Bold: the help line, the counter
constexpr float kFontPx = 64;

struct Frame {
    ImVec2 o;
    float s;
    ImVec2 P(float x, float y) const { return {o.x + x * s, o.y + y * s}; }
};

Frame Fit() {
    ImVec2 d = ImGui::GetIO().DisplaySize;
    float s = std::min(d.x / kW, d.y / kH);
    return {{(d.x - kW * s) / 2, (d.y - kH * s) / 2}, s};
}

enum Align { kStart, kCentre, kEnd };

// text at (x, y) = its left, centre or right edge and its vertical middle; slanted like the game's lettering
void Text(ImDrawList* dl, const Frame& f, ImFont* font, float px, float x, float y, ImU32 colour, const std::string& s,
          Align align = kStart, bool slant = true) {
    if (!font) font = ImGui::GetFont();
    float size = px * f.s;
    ImVec2 ext = font->CalcTextSizeA(size, FLT_MAX, 0, s.c_str());
    ImVec2 at = f.P(x, y);
    at.y -= ext.y / 2;
    if (align == kCentre) at.x -= ext.x / 2;
    if (align == kEnd) at.x -= ext.x;
    int first = dl->VtxBuffer.Size;
    // a dark edge under the letters, as the game's have
    dl->AddText(font, size, {at.x + 2 * f.s, at.y + 2 * f.s}, IM_COL32(0, 0, 0, 200), s.c_str());
    dl->AddText(font, size, at, colour, s.c_str());
    if (!slant) return;
    float base = at.y + ext.y;
    for (int i = first; i < dl->VtxBuffer.Size; i++) dl->VtxBuffer[i].pos.x += (base - dl->VtxBuffer[i].pos.y) * 0.22f;
}

void Polyline(ImDrawList* dl, const Frame& f, std::initializer_list<ImVec2> pts, ImU32 colour, float thick) {
    std::vector<ImVec2> p;
    for (ImVec2 v : pts) p.push_back(f.P(v.x, v.y));
    dl->AddPolyline(p.data(), int(p.size()), colour, 0, thick * f.s);
}

// the game's orange pointer, a triangle facing left (dir -1) or right (+1)
void Pointer(ImDrawList* dl, const Frame& f, float x, float y, int dir) {
    float w = 13 * dir, h = 10;
    dl->AddTriangleFilled(f.P(x - w, y - h), f.P(x + w, y), f.P(x - w, y + h), kArrow);
}

// the game's OPTIONS page was up within the last few frames
std::atomic<int64_t> g_options_seen{0};
bool OptionsUp() {
    int64_t now = Clock::now().time_since_epoch().count();
    return now - g_options_seen.load() < std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(150)).count();
}

// the RECOMP SETTINGS row in the game's OPTIONS page (hooks.cpp moves the game's rows to make room)
std::atomic<int> g_row_y{0};
std::atomic<bool> g_row_chosen{false};
std::atomic<float> g_row_alpha{0};

ImU32 Fade(ImU32 c, float a) { return (c & 0x00FFFFFF) | (ImU32(((c >> 24) & 0xFF) * a) << 24); }

void Button(ImDrawList* dl, const Frame& f, float x, ImU32 colour, const char* letter, const char* name) {
    dl->AddCircleFilled(f.P(x, 704), 17 * f.s, colour);
    dl->AddCircle(f.P(x, 704), 17 * f.s, IM_COL32(255, 255, 255, 90), 0, 1.5f * f.s);
    Text(dl, f, g_heavy, 26, x, 704, IM_COL32(255, 255, 255, 230), letter, kCentre, false);
    Text(dl, f, g_heavy, 30, x + 32, 704, IM_COL32(205, 205, 205, 255), name);
}

void Chrome(ImDrawList* dl, const Frame& f, const std::string& title, const std::string& help) {
    ImVec2 d = ImGui::GetIO().DisplaySize;
    dl->AddRectFilled({0, 0}, d, IM_COL32(0, 0, 0, 255));
    // the diamond weave behind the rows
    for (int r = 0; r < 10; r++)
        for (int c = 0; c < 15; c++) {
            float cx = 245 + c * 68 + (r & 1) * 34, cy = 165 + r * 46;
            dl->AddQuadFilled(f.P(cx, cy - 22), f.P(cx + 30, cy), f.P(cx, cy + 22), f.P(cx - 30, cy), kDiamond);
        }
    Polyline(dl, f, {{66, 83}, {1228, 83}, {1276, 47}, {1372, 47}}, kGold, 3);
    Polyline(dl, f, {{66, 128}, {1250, 128}, {1298, 92}, {1372, 92}}, kGold, 3);
    Text(dl, f, g_heavy, 54, 152, 105, kGold, title);
    Polyline(dl, f, {{66, 655}, {125, 655}, {155, 632}, {1285, 632}, {1315, 655}, {1372, 655}}, kLine, 2.5f);
    Text(dl, f, g_bold, 29, 720, 655, kHelp, help, kCentre, false);
    Polyline(dl, f, {{66, 678}, {1372, 678}}, kLine, 2.5f);
    Polyline(dl, f, {{66, 730}, {1372, 730}}, kLine, 2.5f);
    Button(dl, f, 167, IM_COL32(62, 166, 40, 255), "A", "OK");
    Button(dl, f, 309, IM_COL32(178, 34, 34, 255), "B", "BACK");
}

// --- what the page holds -------------------------------------------------------------------------------------------

struct Row {
    const char* name;
    const char* help;
    std::string (*value)();
    bool (*step)(int dir);  // -1 or +1; false when the value is already at that end
};

struct Display {
    std::string device;  // \.\DISPLAY1
    int number;          // as Windows numbers it
    app::Mode desktop;
};

// the connected monitors, by their numbers
std::vector<Display> Displays() {
    std::vector<Display> out;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR mon, HDC, LPRECT, LPARAM list) -> BOOL {
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (!GetMonitorInfoW(mon, &mi)) return TRUE;
        if (!EnumDisplaySettingsW(mi.szDevice, ENUM_REGISTRY_SETTINGS, &dm) || !dm.dmPelsWidth)
            EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm);
        Display d{std::string(), 0, {dm.dmPelsWidth, dm.dmPelsHeight}};
        for (const wchar_t* c = mi.szDevice; *c; c++) d.device += char(*c);
        size_t digits = d.device.find_last_not_of("0123456789") + 1;
        d.number = digits < d.device.size() ? std::atoi(d.device.c_str() + digits) : 0;
        reinterpret_cast<std::vector<Display>*>(list)->push_back(d);
        return TRUE;
    }, LPARAM(&out));
    std::sort(out.begin(), out.end(), [](const Display& a, const Display& b) { return a.number < b.number; });
    return out;
}

// the chosen monitor's modes, smallest first
std::vector<app::Mode>& Modes() {
    static std::string of = "?";
    static std::vector<app::Mode> modes;
    if (of != app::Get().monitor) {
        of = app::Get().monitor;
        modes.clear();
        std::wstring device(of.begin(), of.end());
        std::vector<app::Mode>& m = modes;
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        for (DWORD i = 0; EnumDisplaySettingsW(device.empty() ? nullptr : device.c_str(), i, &dm); i++) {
            if (dm.dmBitsPerPel != 32 || dm.dmPelsWidth < 640 || dm.dmPelsHeight < 480) continue;
            if (std::none_of(m.begin(), m.end(),
                             [&](const app::Mode& o) { return o.width == dm.dmPelsWidth && o.height == dm.dmPelsHeight; }))
                m.push_back({dm.dmPelsWidth, dm.dmPelsHeight});
        }
        std::sort(m.begin(), m.end(), [](const app::Mode& a, const app::Mode& b) {
            return a.width != b.width ? a.width < b.width : a.height < b.height;
        });
    }
    return modes;
}

template <class T, size_t N>
bool Cycle(T& v, const T (&list)[N], int dir) {
    int i = int(std::find(std::begin(list), std::end(list), v) - std::begin(list));
    if (i == int(N)) i = 0;
    int j = std::clamp(i + dir, 0, int(N) - 1);
    if (list[j] == v) return false;
    v = list[j];
    return true;
}

bool Flag(bool& v, int dir) {
    bool want = dir > 0;
    if (v == want) return false;
    v = want;
    return true;
}

std::string Size(uint32_t w, uint32_t h) { return std::to_string(w) + "x" + std::to_string(h); }

Clock::time_point g_apply_at{};  // a display change goes out once the choice rests
void ApplySoon() { g_apply_at = Clock::now() + std::chrono::milliseconds(700); }

const Row kDisplay[] = {
    {"MONITOR", "The monitor the game is shown on",
     [] {
         auto ds = Displays();
         for (const Display& d : ds)
             if (d.device == app::Get().monitor) return std::to_string(d.number) + " (" + Size(d.desktop.width, d.desktop.height) + ")";
         return std::string("PRIMARY");
     },
     [](int dir) {
         auto ds = Displays();
         // PRIMARY first, then each monitor by number
         int i = 0;
         for (size_t k = 0; k < ds.size(); k++)
             if (ds[k].device == app::Get().monitor) i = int(k) + 1;
         int j = std::clamp(i + dir, 0, int(ds.size()));
         if (j == i) return false;
         app::Get().monitor = j ? ds[j - 1].device : std::string();
         app::Get().width = app::Get().height = 0;  // that monitor's desktop size
         ApplySoon();
         return true;
     }},
    {"DISPLAY MODE", "Fullscreen, a borderless window over the desktop or a window",
     [] {
         static const char* k[] = {"FULLSCREEN", "BORDERLESS", "WINDOWED"};
         return std::string(k[uint32_t(app::Get().display_mode)]);
     },
     [](int dir) {
         static const app::DisplayMode k[] = {app::DisplayMode::Fullscreen, app::DisplayMode::Borderless,
                                              app::DisplayMode::Windowed};
         bool moved = Cycle(app::Get().display_mode, k, dir);
         if (moved) ApplySoon();
         return moved;
     }},
    {"RESOLUTION", "The size of the picture on the screen",
     [] {
         const app::Settings& s = app::Get();
         return s.width ? Size(s.width, s.height) : "DESKTOP (" + Size(app::Desktop().width, app::Desktop().height) + ")";
     },
     [](int dir) {
         app::Settings& s = app::Get();
         auto& m = Modes();
         // DESKTOP first, then the monitor's modes from the smallest
         int i = 0;
         for (size_t k = 0; k < m.size(); k++)
             if (m[k].width == s.width && m[k].height == s.height) i = int(k) + 1;
         int j = std::clamp(i + dir, 0, int(m.size()));
         if (j == i) return false;
         s.width = j ? m[j - 1].width : 0;
         s.height = j ? m[j - 1].height : 0;
         ApplySoon();
         return true;
     }},
    {"RENDER RESOLUTION", "The size the game is drawn at, against the screen's",
     [] {
         uint32_t p = app::Get().render_percent;
         app::Mode o = app::Output();
         return std::to_string(p) + "% (" + Size(o.width * p / 100, o.height * p / 100) + ")";
     },
     [](int dir) {
         static const uint32_t k[] = {25, 50, 75, 100, 125, 150, 175, 200};
         return Cycle(app::Get().render_percent, k, dir);
     }},
    {"VERTICAL SYNC", "Waits for the screen's refresh: no tearing",
     [] { return std::string(app::Get().vsync ? "ON" : "OFF"); },
     [](int dir) { return Flag(app::Get().vsync, dir); }},
    {"MAX FRAME RATE", "The game itself runs at 30 frames per second",
     [] {
         uint32_t c = app::Get().fps_cap;
         return c ? std::to_string(c) : std::string("UNLIMITED");
     },
     [](int dir) {
         static const uint32_t k[] = {30, 60, 75, 120, 144, 165, 240, 0};
         return Cycle(app::Get().fps_cap, k, dir);
     }},
    {"FPS COUNTER", "Shows the frame rate in the top left corner",
     [] { return std::string(g_fps_counter ? "ON" : "OFF"); },
     [](int dir) { return Flag(g_fps_counter, dir); }},
};

const Row kGraphics[] = {
    {"SMAA", "Smooths jagged edges over the whole picture",
     [] { return std::string(app::Get().smaa ? "ON" : "OFF"); },
     [](int dir) { return Flag(app::Get().smaa, dir); }},
    {"ANISOTROPIC FILTERING", "Sharper road and ground textures far away",
     [] {
         uint32_t a = app::Get().aniso;
         return a ? std::to_string(a) + "x" : std::string("OFF (DEFAULT)");
     },
     [](int dir) {
         static const uint32_t k[] = {0, 2, 4, 8, 16};
         return Cycle(app::Get().aniso, k, dir);
     }},
};

const Row kGeneral[] = {
    {"SKIP INTROS", "Leaves out the logo movie at start, from the next launch",
     [] { return std::string(g_skip_intros ? "ON" : "OFF"); },
     [](int dir) { return Flag(g_skip_intros, dir); }},
    {"PAUSE ON FOCUS LOSS", "The game and its sound stop while the window is in the background",
     [] { return std::string(app::Get().pause_on_focus_loss ? "ON" : "OFF"); },
     [](int dir) { return Flag(app::Get().pause_on_focus_loss, dir); }},
};

struct Section {
    const char* name;
    const char* help;
    const Row* rows;
    int count;
};
const Section kSections[] = {
    {"DISPLAY", "Display mode, resolution, frame rate", kDisplay, int(std::size(kDisplay))},
    {"GRAPHICS", "Antialiasing, texture filtering", kGraphics, int(std::size(kGraphics))},
    {"GENERAL", "Start up, background pause", kGeneral, int(std::size(kGeneral))},
};
constexpr int kSectionCount = int(std::size(kSections));

// all under UiLock: the window thread (keys), the input thread (pad), the render thread (drawing)
std::atomic<bool> g_open{false};
bool g_swallow = false;  // closed from the pad: the game sees nothing until every button is let go
int g_section = -1;  // -1: the list of sections
int g_row = 0;

// --- input ---------------------------------------------------------------------------------------------------------

enum Key { kUp, kDown, kLeft, kRight, kOk, kBack, kKeys };

void Open() {
    g_section = -1;
    g_row = 0;
    g_open = true;
}

void Close() {
    g_open = false;
    g_swallow = true;
    app::Save();
}

void Press(Key k) {
    int rows = g_section < 0 ? kSectionCount + 1 : kSections[g_section].count;
    switch (k) {
        case kUp: g_row = (g_row + rows - 1) % rows; break;
        case kDown: g_row = (g_row + 1) % rows; break;
        case kLeft:
        case kRight:
            if (g_section >= 0 && kSections[g_section].rows[g_row].step(k == kRight ? 1 : -1)) app::Save();
            break;
        case kOk:
            if (g_section < 0) {
                if (g_row == kSectionCount) Close();
                else g_section = g_row, g_row = 0;
            } else {
                // A moves the value on, from the last back to the first
                const Row& r = kSections[g_section].rows[g_row];
                if (!r.step(1))
                    while (r.step(-1)) {
                    }
                app::Save();
            }
            break;
        case kBack:
            if (g_section < 0) Close();
            else g_row = g_section, g_section = -1;
            break;
        default: break;
    }
}

// --- drawing -------------------------------------------------------------------------------------------------------

void Page() {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    Frame f = Fit();
    if (g_section < 0) {
        Chrome(dl, f, "RECOMP SETTINGS", g_row < kSectionCount ? kSections[g_row].help : "Back to the game");
        for (int i = 0; i <= kSectionCount; i++) {
            bool on = i == g_row;
            const char* name = i < kSectionCount ? kSections[i].name : "EXIT";
            float y = 250 + i * 62;
            Text(dl, f, g_heavy, 42, 720, y, on ? kChosen : kLabel, name, kCentre);
            if (on) {
                float w = (g_heavy ? g_heavy : ImGui::GetFont())->CalcTextSizeA(42 * f.s, FLT_MAX, 0, name).x / f.s;
                Pointer(dl, f, 720 - w / 2 - 36, y, 1);
                Pointer(dl, f, 720 + w / 2 + 44, y, -1);
            }
        }
        return;
    }
    const Section& sec = kSections[g_section];
    Chrome(dl, f, sec.name, sec.rows[g_row].help);
    float top = 177 + (6 - sec.count) * 25.5f;  // fewer rows sit lower, around the middle
    for (int i = 0; i < sec.count; i++) {
        bool on = i == g_row;
        float y = top + i * 51;
        Text(dl, f, g_heavy, 38, 660, y, on ? kChosen : kLabel, sec.rows[i].name, kEnd);
        Text(dl, f, g_heavy, 38, 700, y, on ? kChosen : kLabel, ":", kCentre, false);
        std::string v = sec.rows[i].value();
        Text(dl, f, g_heavy, 36, 778, y, on ? kValueChosen : kValue, v);
        if (on) {
            float w = (g_heavy ? g_heavy : ImGui::GetFont())->CalcTextSizeA(36 * f.s, FLT_MAX, 0, v.c_str()).x / f.s;
            Pointer(dl, f, 750, y, -1);
            Pointer(dl, f, 778 + w + 36, y, 1);
        }
    }
}

// the row as the game's rows are drawn: CONTROLLER SETTING's lettering, green when chosen, then its help line.
// The page's units to the 1440x810 frame: GUIDE MAP's y 77 sits at 177, 1.53 a unit
void OptionsRowDraw() {
    float a = g_row_alpha;
    if (a <= 0) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    Frame f = Fit();
    float y = 177 + (g_row_y - 77) * 1.53f;
    bool on = g_row_chosen;
    Text(dl, f, g_heavy, 46, 720, y, Fade(on ? IM_COL32(84, 206, 52, 255) : IM_COL32(100, 100, 100, 255), a), "RECOMP SETTINGS", kCentre);
    if (on) Text(dl, f, g_bold, 29, 720, 655, Fade(kHelp, a), "Display, graphics and other settings of this port", kCentre, false);
}

void Counter(double fps) {
    char text[32];
    std::snprintf(text, sizeof(text), "FPS: %.1f", fps);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float px = std::max(16.0f, ImGui::GetIO().DisplaySize.y / 38);
    ImFont* font = g_bold ? g_bold : ImGui::GetFont();
    ImVec2 at{px * 0.8f, px * 0.6f};
    dl->AddText(font, px, {at.x + 1.5f, at.y + 1.5f}, IM_COL32(0, 0, 0, 220), text);
    dl->AddText(font, px, at, IM_COL32(255, 255, 255, 255), text);
}

}  // namespace

// the window is up and the DX12 side has not built ImGui's font texture yet: the menu fonts from Windows
void OptionsRow(int y, bool chosen, float alpha) {
    g_row_y = y;
    g_row_chosen = chosen;
    g_row_alpha = alpha;
}

void OpenPage() {
    std::lock_guard lock(app::UiLock());
    Open();
}

void OptionsShown() { g_options_seen = Clock::now().time_since_epoch().count(); }

void Window(void*) {
    std::lock_guard lock(app::UiLock());
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontDefault();
    wchar_t dir[MAX_PATH];
    GetWindowsDirectoryW(dir, MAX_PATH);
    auto load = [&](const wchar_t* name) -> ImFont* {
        char path[MAX_PATH * 2];
        std::snprintf(path, sizeof(path), "%ls\\Fonts\\%ls", dir, name);
        return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES ? io.Fonts->AddFontFromFileTTF(path, kFontPx) : nullptr;
    };
    g_heavy = load(L"ariblk.ttf");
    g_bold = load(L"arialbd.ttf");
}

void Draw(double fps) {
    if (g_apply_at != Clock::time_point{} && Clock::now() >= g_apply_at) {
        g_apply_at = {};
        app::ApplyDisplay();
    }
    if (g_open) {
        Page();
    } else if (OptionsUp()) {
        OptionsRowDraw();
    }
    if (g_fps_counter) Counter(fps);
}

// F1 opens and closes the page; with it up the arrows, Enter and Esc work it
bool Message(void*, uint32_t msg, uintptr_t wp, intptr_t, intptr_t*) {
    if (msg != WM_KEYDOWN) return false;
    std::lock_guard lock(app::UiLock());
    if (wp == VK_F1) {
        if (g_open) Close();
        else Open();
        return true;
    }
    if (!g_open) return false;
    switch (wp) {
        case VK_UP: Press(kUp); return true;
        case VK_DOWN: Press(kDown); return true;
        case VK_LEFT: Press(kLeft); return true;
        case VK_RIGHT: Press(kRight); return true;
        case VK_RETURN:
        case VK_SPACE: Press(kOk); return true;
        case VK_ESCAPE:
        case VK_BACK: Press(kBack); return true;
    }
    return false;
}

// both sticks pressed open and close it; while it is up pad 1 works it and the game gets nothing
void Pad(int port, input::Pad& pad) {
    debug::Pad(port, pad);
    if (port != 0) return;
    static uint16_t last_buttons = 0;
    constexpr uint16_t kBothSticks = 0xC0;
    bool sticks = (pad.buttons & kBothSticks) == kBothSticks && (last_buttons & kBothSticks) != kBothSticks;
    last_buttons = pad.buttons;
    std::lock_guard lock(app::UiLock());
    bool now[kKeys] = {
        (pad.buttons & 0x01) || pad.ly > 16000,  (pad.buttons & 0x02) || pad.ly < -16000,
        (pad.buttons & 0x04) || pad.lx < -16000, (pad.buttons & 0x08) || pad.lx > 16000,
        pad.analog[0] > 30 || (pad.buttons & 0x10),  // A or START
        pad.analog[1] > 30 || (pad.buttons & 0x20),  // B or BACK
    };
    static bool held[kKeys] = {};
    static Clock::time_point repeat[kKeys];
    bool y = pad.analog[3] > 30;
    static bool y_held = false;
    bool y_pressed = y && !y_held;
    y_held = y;
    if (sticks) {
        if (g_open) Close();
        else Open();
    } else if (!g_open && y_pressed && OptionsUp()) {
        Open();
        g_swallow = true;
    } else if (g_open) {
        auto t = Clock::now();
        for (int k = 0; k < kKeys; k++) {
            if (now[k] && !held[k]) {
                Press(Key(k));
                repeat[k] = t + std::chrono::milliseconds(400);
            } else if (now[k] && k <= kRight && t >= repeat[k]) {
                Press(Key(k));
                repeat[k] = t + std::chrono::milliseconds(110);
            }
        }
    }
    std::copy(std::begin(now), std::end(now), std::begin(held));
    if (g_swallow) {
        bool any = pad.buttons || pad.lx > 16000 || pad.lx < -16000 || pad.ly > 16000 || pad.ly < -16000;
        for (uint8_t a : pad.analog) any |= a > 30;
        g_swallow = any;
    }
    if (!g_open && !g_swallow) return;
    bool connected = pad.connected;
    pad = input::Pad{};
    pad.connected = connected;
}

}  // namespace ui
