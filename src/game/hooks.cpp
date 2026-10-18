// Hooks for default.xbe (Double S.T.E.A.L. - The Second Clash, XDK 5849).
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>
#include <thread>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")

#include "core/log.h"

#include "kernel/kernel.h"
#include "xdk/d3d8.h"
#include "gpu/backend.h"
#include "xdk/hle.h"
#include "common/controls.h"

X86_EXTERN_FUNC(__imp__sub_00177B8E);

std::chrono::steady_clock::time_point g_last_tick;  // TickScene last ran
// button prompts in a row this frame: its y, the room their keys took so far (FUN_000EB610)
std::vector<std::pair<int, float>> g_row_rooms;  // row's y, room so far
std::vector<std::pair<int, float>> g_row_rooms_last;  // the last frame's, each row's whole
// the game's state (its frame FUN_00023800's object +0x4C): 9 a race being driven (its countdown too); 1 and 2 a
// cutscene, 7 a briefing, 8 the start of a mission, 10 the pause menu (logged with STEAL_STATE_LOG=1)
std::atomic<uint32_t> g_game_state{0};
constexpr uint32_t kStateRacing = 9, kStatePause = 10;

namespace ui {
extern bool g_skip_intros;  // Recomp Settings > GENERAL > SKIP INTROS
void OptionsShown();        // the game's OPTIONS page is up this frame: Y there opens Recomp Settings
// the RECOMP SETTINGS row put into the OPTIONS page: its y in the page's units, chosen, the page's fade
void OptionsRow(int y, bool chosen, float alpha);
void OpenPage();
bool HideOptions();  // Recomp Settings is drawn over the OPTIONS page: its rows and title go meanwhile
bool TakeOptionsReplay();  // Recomp Settings closed over the OPTIONS page: the page comes in again
bool TakeSound(uint32_t& id);
float PromptRoom(char button, std::string& key);  // the room the button's key takes, 0 to leave the prompt be
void AddPrompt(float x, float y, float h, uint32_t colour, const std::string& key, bool over);
void DimPrompts(float keep);  // the frame's prompts under a dialog's backdrop, keeping that much of their light
void PromptsFrame();  // a menu sound Recomp Settings asks for, for the game to play
}

// XMVDecoder_Play(decoder, flags, rect), __stdcall: the library's own player decodes the movie (LOGO1.XMV, the
// publisher logo) onto the overlay the SDK presents. SKIP INTROS or STEAL_SKIP_MOVIES=1 skips it as if it had finished.
X86_FUNC(sub_00177B8E) {
    if (ui::g_skip_intros || std::getenv("STEAL_SKIP_MOVIES")) {
        kernel::Return(ctx, 0, 3);
        return;
    }
    __imp__sub_00177B8E(ctx, base);
}

// The minimap keeps the 3x3 tiles around the player (cache at minimap + 0x14: slots at +0x388 of 12 bytes,
// column and row shorts, texture, loads in flight). A tile is drawn once its in-flight count is back to 0.
namespace {
constexpr uint32_t kNaviComplete = 0x000E85A0;  // (cache) takes finished reads, counts their tiles done
constexpr uint32_t kNaviReloadAll = 0x000E47D0;  // (minimap, position, wait) the whole 3x3 again
bool NaviOriginal() {
    static const bool original = std::getenv("STEAL_NAVI_ORIGINAL") != nullptr;
    return original;
}
bool NaviLog() {
    static const bool on = std::getenv("STEAL_NAVI_LOG") != nullptr;
    return on;
}
std::string NaviSlots(uint32_t cache) {
    std::string slots;
    for (int i = 0; i < 9; i++) {
        uint32_t v = kernel::R32(cache + 0x388 + i * 12);
        slots += " " + std::to_string(int16_t(v)) + "," + std::to_string(int16_t(v >> 16)) + "/" +
                 std::to_string(int32_t(kernel::R32(cache + 0x388 + i * 12 + 8)));
    }
    return slots;
}
}  // namespace

// STEAL_NAVI_LOG=1: the minimap's tile streaming (cell changes, the nine cached tiles with their loads in flight,
// every load); STEAL_NAVI_RELOAD_AT=<s> reloads the whole 3x3 without waiting at that time (a test).
X86_EXTERN_FUNC(__imp__sub_000E4D10);
X86_FUNC(sub_000E4D10) {
    uint32_t t = ctx.ecx, cache = t + 0x14;
    auto f = [&](uint32_t a) { uint32_t v = kernel::R32(a); float x; std::memcpy(&x, &v, 4); return x; };
    float scale = f(cache + 0x384);
    if (NaviLog()) {
        int px = int((f(t + 0x454) - f(cache + 0x370)) * scale), pz = int((0.0f - (f(t + 0x45C) - f(cache + 0x368))) * scale);
        int cw = int(kernel::R32(cache + 0x360)), ch = int(kernel::R32(cache + 0x364));
        static int last_c = -99, last_r = -99, calls = 0;
        bool moved = cw > 0 && ch > 0 && (px / cw != last_c || pz / ch != last_r);
        if (moved || ++calls % 120 == 0) {
            if (cw > 0 && ch > 0) last_c = px / cw, last_r = pz / ch;
            LOGE("navi: cell %d,%d rem %d,%d tiles%s", last_c, last_r, cw ? px % cw : 0, ch ? pz % ch : 0,
                 NaviSlots(cache).c_str());
        }
        static const double reload_at = std::getenv("STEAL_NAVI_RELOAD_AT") ? std::atof(std::getenv("STEAL_NAVI_RELOAD_AT")) : 0;
        static int reloaded = 0;
        static auto start = std::chrono::steady_clock::now();
        if (reload_at > 0 && !reloaded &&
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() > reload_at) {
            reloaded = 1;
            LOGE("navi: test reload");
            kernel::CallGuest(ctx, kNaviReloadAll, {t + 0x454, 0});
            LOGE("navi: after test reload tiles%s", NaviSlots(cache).c_str());
        }
    }
    // The next row or column of tiles loads once the player is 83 units (15 map pixels) into a new cell; until
    // then the far side of the minimap has no tiles and shows the world through. Load it 3 pixels in instead.
    uint32_t margin_at = t + 0x544, margin = kernel::R32(margin_at);
    if (!NaviOriginal() && scale > 0) {
        float near_margin = 3.5f / scale;
        uint32_t bits;
        std::memcpy(&bits, &near_margin, 4);
        kernel::W32(margin_at, bits);
    }
    __imp__sub_000E4D10(ctx, base);
    if (!NaviOriginal() && scale > 0) kernel::W32(margin_at, margin);
}

// A load takes the request slot numbered by the reads still queued. Reads here finish at once, so the next load
// (the 3x3 reloaded in one go after an event) dequeues the last and takes its slot again before its tiles were
// counted done: they stay "in flight" and the minimap is left empty until the next reload. The finished reads
// are taken first, as several frames of a console disc read would have.
X86_EXTERN_FUNC(__imp__sub_000E88D0);
X86_FUNC(sub_000E88D0) {
    int32_t c = int32_t(kernel::R32(ctx.esp + 4)), r = int32_t(kernel::R32(ctx.esp + 8));
    if (!NaviOriginal()) kernel::CallGuest(ctx, kNaviComplete, {});
    __imp__sub_000E88D0(ctx, base);
    if (NaviLog()) LOGE("navi: row load %d %d -> %u", c, r, ctx.eax);
}
X86_EXTERN_FUNC(__imp__sub_000E8B40);
X86_FUNC(sub_000E8B40) {
    int32_t c = int32_t(kernel::R32(ctx.esp + 4)), r = int32_t(kernel::R32(ctx.esp + 8));
    if (!NaviOriginal()) kernel::CallGuest(ctx, kNaviComplete, {});
    __imp__sub_000E8B40(ctx, base);
    if (NaviLog()) LOGE("navi: column load %d %d -> %u", c, r, ctx.eax);
}


// RECOMP SETTINGS as a row of the game's OPTIONS page, under CONTROLLER SETTING. The page is laid out by its xpr:
// each element's description holds its place and, for a row, its neighbours. The rows are moved closer (33 units
// apart to 30) to make room, the cursor passes through the new row in the page's input (the game's highlight is
// taken off CONTROLLER SETTING or RESET TO DEFAULT while it is on it), and the row itself is drawn by ui.cpp.
namespace {
constexpr uint32_t kOptionPageVtable = 0x001CA72C;
constexpr uint32_t kPageInput = 0x000EBD00;    // thiscall page (buttons): cursor, then the page's handlers
constexpr uint32_t kPageHighlight = 0x000EB5A0;  // thiscall page (chosen, left): their help and highlight animations
constexpr uint32_t kPlaySound = 0x0008B960;    // thiscall sound player (id)
constexpr uint32_t kSoundPlayer = 0x0042D708;
constexpr uint32_t kCursorSound = 0x2C03, kOkSound = 0x2001;
// page: +0x10 description (+4 elements), +0x14 elements, +0x24 the chosen one, +0x3C fade (0x100 shown), +0x44 the
// OK buttons, +0x48 the back buttons. Element: +0x10 description (+0 name, +0xC y), vtable +0x20 highlight (on).
constexpr int kRowY0 = 77, kRowStep = 33, kNewStep = 30, kRows = 9, kNewRow = 7;  // after CONTROLLER SETTING (6)

uint16_t& U16(uint32_t va) { return *static_cast<uint16_t*>(kernel::HostPtr(va)); }

uint32_t Element(uint32_t page, const char* name, int* index = nullptr) {
    uint32_t def = kernel::R32(page + 0x10);
    for (int i = 0; i < int(kernel::R32(def + 4)); i++) {
        uint32_t el = kernel::R32(kernel::R32(page + 0x14) + i * 4);
        if (std::strcmp(static_cast<const char*>(kernel::HostPtr(kernel::R32(kernel::R32(el + 0x10)))), name) == 0) {
            if (index) *index = i;
            return el;
        }
    }
    return 0;
}

uint32_t g_page = 0;
bool g_on_row = false;

// a row's y in the new layout: the rows from the new one down one place, all of them closer
bool NewY(uint16_t& y) {
    for (int r = 0; r < kRows; r++)
        for (int extra : {0, 3})  // values sit 3 lower than their row
            if (y == kRowY0 + r * kRowStep + extra) {
                y = uint16_t(kRowY0 + (r < kNewRow ? r : r + 1) * kNewStep + extra);
                return true;
            }
    return false;
}

// def: the page's description, +4 its elements, +8 theirs (0x24
// each, y at +0xC); +0x14 its timelines, +0x18 theirs (12 bytes, +8 their commands). A move command (type 3) holds
// where it takes its element from and to (x, y at +0x18, +0x1A and +0x1C, +0x1E), which the element keeps after.
// Done when RESET TO DEFAULT (row 7) is still in its place: a description loaded again comes as it was.
void Relayout(uint32_t def) {
    bool fresh = false;
    for (int i = 0; i < int(kernel::R32(def + 4)); i++) {
        uint32_t d = kernel::R32(def + 8) + i * 0x24;
        if (!std::strcmp(static_cast<const char*>(kernel::HostPtr(kernel::R32(d))), "ResetDefault"))
            fresh = U16(d + 0xC) == kRowY0 + 7 * kRowStep;
    }
    if (!fresh) return;
    for (int i = 0; i < int(kernel::R32(def + 4)); i++) NewY(U16(kernel::R32(def + 8) + i * 0x24 + 0xC));
    for (int t = 0; t < int(kernel::R32(def + 0x14)); t++) {
        uint32_t tl = kernel::R32(def + 0x18) + t * 12;  // +4 its commands, +8 pointers to them
        for (int c = 0; c < int(kernel::R32(tl + 4)); c++) {
            uint32_t cmd = kernel::R32(kernel::R32(tl + 8) + c * 4);
            uint32_t type = kernel::R32(cmd);
            if (type != 3 || kernel::R32(cmd + 0xC) != 0) continue;  // moves of the page's own elements
            NewY(U16(cmd + 0x1A));
            NewY(U16(cmd + 0x1E));
        }
    }
}

void Highlight(X86Context& ctx, uint32_t page, uint32_t el, int index, bool on) {
    uint32_t ecx = ctx.ecx;
    ctx.ecx = el;
    kernel::CallGuest(ctx, kernel::R32(kernel::R32(el) + 0x20), {on ? 1u : 0u});
    ctx.ecx = page;
    kernel::CallGuest(ctx, kPageHighlight, {on ? uint32_t(index) : ~0u, on ? ~0u : uint32_t(index)});
    ctx.ecx = ecx;
}

void Sound(X86Context& ctx, uint32_t id) {
    uint32_t ecx = ctx.ecx;
    ctx.ecx = kSoundPlayer;
    kernel::CallGuest(ctx, kPlaySound, {id});
    ctx.ecx = ecx;
}
}  // namespace

// The options menu, each frame it is up (main menu and pause menu): +0x18 the page shown (1 OPTIONS, 3 CONTROLLER
// SETTING), +0x20 its change (0 settled, 2 leaving), +0x24 the OPTIONS page.
X86_EXTERN_FUNC(__imp__sub_0010AB80);
X86_FUNC(sub_0010AB80) {
    uint32_t menu = ctx.ecx;
    __imp__sub_0010AB80(ctx, base);
    uint32_t page = kernel::R32(menu + 0x24);
    bool up = kernel::R32(menu + 0x18) == 1 && page && kernel::R32(page) == kOptionPageVtable;
    // the pause menu's OPTIONS keep their rows: Recomp Settings is F10's there
    if (g_game_state == kStatePause) g_page = 0;
    if (!up || g_game_state == kStatePause) return;
    if (page != g_page) {
        g_page = page, g_on_row = false;
        if (std::getenv("STEAL_PAGE_DUMP")) {
            uint32_t def = kernel::R32(page + 0x10);
            for (int i = 0; i < int(kernel::R32(def + 4)); i++) {
                uint32_t el = kernel::R32(kernel::R32(page + 0x14) + i * 4);
                uint32_t d = kernel::R32(def + 8) + i * 0x24;
                std::string line;
                char b[16];
                for (int k = 0; k < 0x24; k += 4) std::snprintf(b, sizeof(b), " %08X", kernel::R32(d + k)), line += b;
                line += " |";
                for (int k = 0; k < 0x40; k += 4) std::snprintf(b, sizeof(b), " %08X", kernel::R32(el + k)), line += b;
                LOGW("page el %d %s vt %08X:%s", i, static_cast<const char*>(kernel::HostPtr(kernel::R32(d))),
                     kernel::R32(el), line.c_str());
            }
            for (int t = 0; t < int(kernel::R32(def + 0x14)); t++) {
                uint32_t tl = kernel::R32(def + 0x18) + t * 12;
                std::string line;
                char b[64];
                for (int c = 0; c < int(kernel::R32(tl + 4)); c++) {
                    uint32_t cmd = kernel::R32(kernel::R32(tl + 8) + c * 4);
                    line += " [";
                    for (int k = 0; k < 0x24; k += 4) std::snprintf(b, sizeof(b), "%08X ", kernel::R32(cmd + k)), line += b;
                    line += "]";
                }
                LOGW("page tl %d %08X:%s", t, kernel::R32(tl), line.c_str());
            }
        }
    }
    if (kernel::R32(menu + 0x20) == 0) ui::OptionsShown();
    // the page's first timeline (page +0x1C: its timelines; +4, +8 their time, +0xC running) brings it in
    if (ui::TakeOptionsReplay()) {
        uint32_t tl = kernel::R32(kernel::R32(page + 0x1C));
        kernel::W32(tl + 4, 0);
        kernel::W32(tl + 8, 0);
        *static_cast<uint8_t*>(kernel::HostPtr(tl + 0xC)) = 1;
    }
    // an element is drawn while its +0x24 is set; the background (Bg) and the A and B buttons stay
    static std::vector<std::pair<uint32_t, uint8_t>> hidden;
    if (ui::HideOptions() && hidden.empty()) {
        uint32_t def = kernel::R32(page + 0x10);
        for (int i = 0; i < int(kernel::R32(def + 4)); i++) {
            uint32_t el = kernel::R32(kernel::R32(page + 0x14) + i * 4);
            const char* name = static_cast<const char*>(kernel::HostPtr(kernel::R32(kernel::R32(el + 0x10))));
            if (!std::strcmp(name, "Bg") || !std::strcmp(name, "A") || !std::strcmp(name, "B")) continue;
            uint8_t& shown = *static_cast<uint8_t*>(kernel::HostPtr(el + 0x24));
            hidden.push_back({el, shown});
            shown = 0;
        }
    } else if (!ui::HideOptions() && !hidden.empty()) {
        for (auto& [el, shown] : hidden) *static_cast<uint8_t*>(kernel::HostPtr(el + 0x24)) = shown;
        hidden.clear();
    }
    // leaving (the menu's change state 2) the game's rows darken to nothing over 8 frames, evenly (measured): the row
    // with them; their colours and the page's fade stay as they were meanwhile
    static int leaving = 0;
    leaving = kernel::R32(menu + 0x20) == 2 ? leaving + 1 : 0;
    float light = std::max(0.0f, 1 - leaving / 8.0f);
    ui::OptionsRow(kRowY0 + kNewRow * kNewStep, g_on_row, light);
}

// FUN_000123A0 is the game's main loop, once a frame whatever the scene: it reads the pads and runs the scene's frame.
// Recomp Settings' sounds are played from here, on the game's thread (the presents of some screens, FUN_00031B30's
// among them, are not every frame's)
// the game's CONTROLLER TYPE for controller 1 (its options at 0x1D2410 + 0xDC, FUN_0001E3A0: +0 the type, 0 A, 1 B)
constexpr uint32_t kControllerType = 0x1D24EC;
X86_EXTERN_FUNC(__imp__sub_000123A0);
X86_FUNC(sub_000123A0) {
    ui::PromptsFrame();
    g_row_rooms_last.swap(g_row_rooms);
    g_row_rooms.clear();
    // the keys as the game's actions under its controller type; the menus' keys while no race is running
    bool racing = g_game_state == kStateRacing &&
                  std::chrono::steady_clock::now() - g_last_tick < std::chrono::milliseconds(250);
    controls::Update(int(kernel::R32(kControllerType) & 0xFFFF), racing);
    // the player reads its banks' table (+0x1A8): none yet early in the boot, the sounds then dropped
    for (uint32_t id; ui::TakeSound(id);)
        if (kernel::R32(kSoundPlayer + 0x1A8)) {
            LOGI("ui sound %04X", id);
            Sound(ctx, id);
        }
    __imp__sub_000123A0(ctx, base);
}

// A page's image element drawn (FUN_000EB610, thiscall element (?, frame)): a button prompt (an element named A, B,
// X or Y, its sprite the button and a word) shows its key with the keyboard used last. Its sprite (the sheet at
// element +0x14, +0x10 its sprites of 0x18 bytes: +4 u0, +6 v0, +8 u1, +0xA v1, +0xC w, +0xE h in texels), by the
// element's description +6, loses the button (its height wide and a gap) for this draw, and the element moves right
// by the key's room; ui.cpp letters the key there in the colour the game draws it (FUN_000EADD0).
namespace {
constexpr uint32_t kElementColour = 0x000EADD0;  // thiscall element (frame) -> ARGB
// an element moved by a timeline takes its place from its mover (element +4, FUN_000EA300): the prompt being drawn
// moves right there too
uint32_t g_shift_mover = 0;
int g_shift = 0;
bool g_drawing_bg = false;      // in a dialog's backdrop's draw (Sure_Bg, Timezone_Bg), drawn when g_bg_drawn
bool g_bg_drawn = false;
uint32_t g_bg_batch = 0;
bool g_drawing_prompt = false;  // in a prompt's draw: whether it got room for its vertices (it was drawn)
bool g_prompt_drawn = false;
uint32_t g_prompt_batch = 0;  // the sprite batch it went into: +0x2870 the batch's light (its pixel shader's alpha;
                              // a page fades with it, the batch is not drawn at 0)
bool g_moved = false;  // the prompt took its place from its mover, at g_moved_x, g_moved_y
int g_moved_x = 0, g_moved_y = 0;
// prompts side by side: each moves right by the rooms of those drawn before it in its row this frame
float RowRoom(int y, float room) {
    for (auto& [ry, sum] : g_row_rooms)
        if (ry == y) {
            float before = sum;
            sum += room;
            return before;
        }
    g_row_rooms.push_back({y, room});
    return 0;
}
// a row's whole room the last frame
float RowWhole(int y) {
    for (auto& [ry, sum] : g_row_rooms_last)
        if (ry == y) return sum;
    return 0;
}
// the button of a prompt element: A, B, X or Y alone or before its word (A_Ok, B_Back under TIME ZONE, A_Continue),
// the pause menu's Button (A OK B CONTINUE, its B in kPairs) and M_B (B BACK); 0 for any other element
char PromptButton(const char* name) {
    if (!name || !name[0]) return 0;
    if (!std::strcmp(name, "Button")) return 'A';
    if (!std::strcmp(name, "M_B")) return 'B';
    if (!std::strchr("ABXY", name[0]) || (name[1] && name[1] != '_')) return 0;
    return name[0];
}
}
// FUN_0006D7E0 (count, ...): room for a sprite's vertices in the frame's batch, none while the page is not being
// drawn (a black screen between pages runs its frames all the same)
X86_EXTERN_FUNC(__imp__sub_0006D7E0);
X86_FUNC(sub_0006D7E0) {
    uint32_t batch = ctx.ecx;
    __imp__sub_0006D7E0(ctx, base);
    if (g_drawing_prompt) g_prompt_drawn = ctx.eax != 0, g_prompt_batch = batch;
    if (g_drawing_bg) g_bg_drawn = ctx.eax != 0, g_bg_batch = batch;
}
X86_EXTERN_FUNC(__imp__sub_000EA300);
X86_FUNC(sub_000EA300) {  // thiscall mover (short* x, short* y) -> placed
    uint32_t mover = ctx.ecx, px = kernel::R32(ctx.esp + 4), py = kernel::R32(ctx.esp + 8);
    __imp__sub_000EA300(ctx, base);
    if (mover && mover == g_shift_mover && (ctx.eax & 0xFF)) {
        g_moved = true;
        g_moved_x = int16_t(U16(px));
        g_moved_y = int16_t(U16(py));
        U16(px) = uint16_t(g_moved_x + g_shift);
    }
}
// a button in a prompt's sprite: where the button starts and where the word after it does, in texels
struct Key {
    char button;
    int circle, word;
};
// sprites laid out otherwise, by their sheet's width and their row in it (texels, the word from 3 before its pixels):
// PauseMenu.xpr's A OK B CONTINUE and A JUMP B BACK, ReplayMenu.xpr's A OK B HIDE MENU with two buttons;
// Result.xpr's A OK, its button some texels into the sprite (lead: how far in the first button may start)
struct Pair {
    int sheet_w, row;
    Key keys[2];
    int lead = 8;
};
constexpr Pair kPairs[] = {{1024, 210, {{'A', 89, 116}, {'B', 162, 189}}},
                           {1024, 240, {{'A', 89, 115}, {'B', 187, 214}}},
                           {256, 126, {{'A', 28, 54}, {'B', 103, 127}}},
                           {512, 2, {{'A', 216, 241}, {0, 0, 0}}, 24}};

// A prompt element drawn by imp with sprite (its record in the sheet): its words without their buttons, a piece each,
// the key lettered before each.
void DrawPrompt(X86Context& ctx, uint8_t* base, void (*imp)(X86Context&, uint8_t*), const char* name, uint32_t sprite) {
    uint32_t el = ctx.ecx;
    char button = PromptButton(name);
    if (!button || !*static_cast<uint8_t*>(kernel::HostPtr(el + 0x24))) {
        imp(ctx, base);
        return;
    }
    uint16_t u0 = U16(sprite + 4), v0 = U16(sprite + 6), u1 = U16(sprite + 8), v1 = U16(sprite + 10);
    uint16_t w = U16(sprite + 0xC), h = U16(sprite + 0xE);
    // a sprite's u and v are 4096ths of its sheet
    float upt = float(u1 - u0) / std::max<int>(w, 1), vpt = float(v1 - v0) / std::max<int>(h, 1);
    Key keys[2];
    int n = 0;
    // the sheet's size from its texture's format (+0xC: log2 width in bits 20-23, height 24-27); a sprite may be drawn
    // larger than its texels (Result.xpr's A OK: 90 texels as 101 units)
    uint32_t tex = kernel::R32(sprite), format = tex ? kernel::R32(tex + 0xC) : 0;
    int sheet_w = 1 << ((format >> 20) & 15), sheet_h = 1 << ((format >> 24) & 15);
    if (tex && upt > 0 && vpt > 0 && u1 > u0) {
        float left = u0 * sheet_w / 4096.0f, span = (u1 - u0) * sheet_w / 4096.0f, scale = w / span;
        int row = int(std::lround(v0 * sheet_h / 4096.0f));
        for (const Pair& p : kPairs)
            if (p.sheet_w == sheet_w && std::abs(p.row - row) <= 4) {
                // those within the sprite (M_B is the B BACK of A JUMP B BACK), the sprite starting by a button (MainMenu.xpr
                // has its own A OK on such a row)
                for (const Key& k : p.keys)
                    if (k.button && k.circle - left >= -3 && k.circle - left < span)
                        keys[n++] = {k.button, int(std::lround(std::max(k.circle - left, 0.0f) * scale)),
                                     int(std::lround((k.word - left) * scale))};
                if (n && keys[0].circle > p.lead * scale) n = 0;
                break;
            }
    }
    // one button: a circle a little under the sprite's height (on Options.xpr: 30 high, the word from 28); beside two
    // rows of words (MISSION SELECT's Y, 60 high) the words from 29 (the C of CLEAR leaning left)
    if (!n) keys[n++] = {button, 0, std::min<int>({h - 2, 29, w - 1})};
    std::string key[2];
    float room[2], rooms = 0, growth = 0;
    int start[2], end[2];
    for (int i = 0, last = 0; i < n; i++) {
        room[i] = ui::PromptRoom(keys[i].button, key[i]);
        if (room[i] <= 0) {
            imp(ctx, base);
            return;
        }
        start[i] = std::clamp(keys[i].word, last, w - 1);
        end[i] = i + 1 < n ? std::clamp(keys[i + 1].circle - 2, start[i] + 1, int(w)) : w;
        rooms += room[i];
        growth += room[i] - (start[i] - last);
        last = end[i];
    }
    float& x = *static_cast<float*>(kernel::HostPtr(el + 0x18));
    float y = *static_cast<float*>(kernel::HostPtr(el + 0x1C));
    float at = x;
    // where the sprite hangs from its place (the description +8: bits 0-1 left, centre, right; bits 2-3 top, middle,
    // bottom): A_Ok and B_Back by their middle
    uint16_t anchor = U16(kernel::R32(el + 0x10) + 8);
    float ax = (anchor & 3) == 1 ? 0.5f : (anchor & 3) == 2 ? 1.0f : 0.0f;
    float ay = (anchor & 0xC) == 4 ? 0.5f : (anchor & 0xC) == 8 ? 1.0f : 0.0f;
    uint32_t frame = kernel::R32(ctx.esp + 8);
    uint32_t colour = kernel::CallGuest(ctx, kElementColour, {frame});
    if (std::getenv("STEAL_PROMPT_LOG")) {
        static std::vector<std::string> logged;
        std::string id = std::string(name) + std::to_string(sprite);
        if (std::find(logged.begin(), logged.end(), id) == logged.end() && (logged.push_back(id), true))
            LOGW("prompt %s sprite u0 %u v0 %u u1 %u v1 %u w %u h %u keys %d x %.1f y %.1f anchor %X colour %08X", name, u0, v0, u1,
                 v1, w, h, n, at, y, anchor, colour);
    }
    // prompts side by side move right by the keys before them in their row; a row hung by its middle (TIME ZONE's)
    // by what they grew, the row then back by its half, by the whole of it the last frame
    float before = ax > 0 ? RowRoom(int(y), growth) - ax * RowWhole(int(y)) : RowRoom(int(y), rooms);
    float cursor = at - ax * w + before, key_at[2];
    X86Context entry = ctx;
    bool drawn = false, moved = false;
    int moved_x = 0, moved_y = 0;
    for (int i = 0; i < n; i++) {
        key_at[i] = cursor;
        cursor += room[i];
        int piece = end[i] - start[i];
        U16(sprite + 4) = uint16_t(u0 + std::lround(start[i] * upt));
        U16(sprite + 8) = uint16_t(u0 + std::lround(end[i] * upt));
        U16(sprite + 0xC) = uint16_t(piece);
        float shift = cursor + ax * piece - at;
        x = at + shift;
        g_shift_mover = kernel::R32(el + 4);
        g_shift = int(std::lround(shift));
        g_moved = false;
        g_drawing_prompt = true;
        g_prompt_drawn = false;
        if (i) ctx = entry;
        imp(ctx, base);
        g_drawing_prompt = false;
        g_shift_mover = 0;
        x = at;
        if (!i) drawn = g_prompt_drawn, moved = g_moved, moved_x = g_moved_x, moved_y = g_moved_y;
        cursor += piece;
    }
    U16(sprite + 4) = u0;
    U16(sprite + 8) = u1;
    U16(sprite + 0xC) = w;
    if (drawn && g_prompt_batch) {
        float light;
        uint32_t u = kernel::R32(g_prompt_batch + 0x2870);
        std::memcpy(&light, &u, 4);
        light = std::clamp(light, 0.0f, 1.0f);
        colour = (colour & 0x00FFFFFF) | (uint32_t((colour >> 24) * light + 0.5f) << 24);
    }
    if (!drawn || !(colour >> 24)) return;  // not drawn: its keys neither
    // placed by its timeline: from there
    float dx = moved ? float(moved_x) - at : 0, dy = moved ? float(moved_y) - y : 0;
    // a dialog's own prompts (A_Ok, B_Back) are over its backdrop
    bool over = std::strchr(name, '_') != nullptr;
    for (int i = 0; i < n; i++) ui::AddPrompt(key_at[i] + dx, y - ay * h + dy, float(h), colour, key[i], over);
}

X86_EXTERN_FUNC(__imp__sub_000EB610);
X86_FUNC(sub_000EB610) {
    uint32_t el = ctx.ecx, desc = kernel::R32(el + 0x10);
    const char* name = desc ? static_cast<const char*>(kernel::HostPtr(kernel::R32(desc))) : nullptr;
    if (std::getenv("STEAL_PROMPT_LOG") && name) {
        static std::vector<std::string> seen;
        if (std::find(seen.begin(), seen.end(), name) == seen.end()) {
            seen.push_back(name);
            LOGW("element drawn %s", name);
        }
    }
    // a dialog's backdrop (Sure_Bg under "Are you sure?", Timezone_Bg under TIME ZONE): a sheet of half black over
    // the page, its prompts under it whenever drawn (the batch is drawn by layers): their keys as dark
    if (name && (!std::strcmp(name, "Sure_Bg") || !std::strcmp(name, "Timezone_Bg"))) {
        uint32_t frame = kernel::R32(ctx.esp + 8), ecx = ctx.ecx;
        uint32_t colour = kernel::CallGuest(ctx, kElementColour, {frame});
        ctx.ecx = ecx;
        g_drawing_bg = true;
        g_bg_drawn = false;
        __imp__sub_000EB610(ctx, base);
        g_drawing_bg = false;
        if (g_bg_drawn && g_bg_batch) {
            float light;
            uint32_t u = kernel::R32(g_bg_batch + 0x2870);
            std::memcpy(&light, &u, 4);
            float alpha = std::clamp(light, 0.0f, 1.0f) * float(colour >> 24) / 255;
            if (std::getenv("STEAL_PROMPT_LOG")) {
                static std::string last;
                char line[128];
                std::snprintf(line, sizeof line, "backdrop %s colour %08X light %.2f", name, colour, light);
                if (last != line) LOGW("%s", (last = line).c_str());
            }
            if (alpha > 0) ui::DimPrompts(1 - 0.5f * alpha);
        }
        return;
    }
    uint32_t sprite = kernel::R32(kernel::R32(el + 0x14) + 0x10) + uint32_t(int16_t(U16(desc + 6))) * 0x18;
    DrawPrompt(ctx, base, __imp__sub_000EB610, name, sprite);
}

// An animated image element drawn (FUN_000EAF50, as FUN_000EB610): its sprite the one of its animation's frame (the
// sheet +0x18 the animations' tables by the description +6, a table +2 the frames' sprites; the frame its timeline's
// at element +0xC, or element +0x38): TIME ZONE's A OK and B BACK, MISSION SELECT's Y WATCH CLEAR CONDITION
X86_EXTERN_FUNC(__imp__sub_000EAF50);
X86_FUNC(sub_000EAF50) {
    uint32_t el = ctx.ecx, desc = kernel::R32(el + 0x10);
    const char* name = desc ? static_cast<const char*>(kernel::HostPtr(kernel::R32(desc))) : nullptr;
    if (std::getenv("STEAL_PROMPT_LOG") && name) {
        static std::vector<std::string> seen;
        if (std::find(seen.begin(), seen.end(), name) == seen.end()) {
            seen.push_back(name);
            LOGW("animated element drawn %s", name);
        }
    }
    if (!PromptButton(name) || !*static_cast<uint8_t*>(kernel::HostPtr(el + 0x24))) {
        __imp__sub_000EAF50(ctx, base);
        return;
    }
    uint32_t sheet = kernel::R32(el + 0x14);
    uint32_t table = kernel::R32(kernel::R32(sheet + 0x18) + uint32_t(int16_t(U16(desc + 6))) * 4);
    int32_t frame = int32_t(kernel::R32(el + 0x38));
    if (uint32_t line = kernel::R32(el + 0xC); line && *static_cast<uint8_t*>(kernel::HostPtr(line + 8)))
        frame = int16_t(U16(kernel::R32(kernel::R32(line + 4) + 0x14) + kernel::R32(line + 0x14) * 4));
    uint32_t sprite = kernel::R32(sheet + 0x10) + uint32_t(int16_t(U16(table + 2 + uint32_t(frame) * 2))) * 0x18;
    DrawPrompt(ctx, base, __imp__sub_000EAF50, name, sprite);
}

X86_EXTERN_FUNC(__imp__sub_000EBD00);
X86_FUNC(sub_000EBD00) {
    uint32_t page = ctx.ecx, buttons = kernel::R32(ctx.esp + 4);
    if (page != g_page || kernel::R32(page) != kOptionPageVtable || !buttons) {
        __imp__sub_000EBD00(ctx, base);
        return;
    }
    constexpr uint32_t kUp = 1, kDown = 2;
    int controller = -1, reset = -1;
    uint32_t controller_el = Element(page, "ControllerSetting", &controller);
    uint32_t reset_el = Element(page, "ResetDefault", &reset);
    int chosen = int(kernel::R32(page + 0x24));
    if (!controller_el || !reset_el) {
        __imp__sub_000EBD00(ctx, base);
        return;
    }
    if (!g_on_row) {
        // onto the new row from the rows either side of it
        if ((chosen == controller && (buttons & kDown)) || (chosen == reset && (buttons & kUp))) {
            Highlight(ctx, page, chosen == controller ? controller_el : reset_el, chosen, false);
            g_on_row = true;
            Sound(ctx, kCursorSound);
            kernel::Return(ctx, 0, 1);
            return;
        }
        __imp__sub_000EBD00(ctx, base);
        return;
    }
    // on the new row: the game's cursor still rests on the row it came from
    if (buttons & (kUp | kDown)) {
        bool down = buttons & kDown;
        uint32_t el = down ? reset_el : controller_el;
        int index = down ? reset : controller;
        kernel::W32(page + 0x24, uint32_t(index));
        Highlight(ctx, page, el, index, true);
        g_on_row = false;
        Sound(ctx, kCursorSound);
    } else if (buttons & kernel::R32(page + 0x44)) {
        Sound(ctx, kOkSound);
        ui::OpenPage();
    } else if (buttons & kernel::R32(page + 0x48)) {
        g_on_row = false;
        __imp__sub_000EBD00(ctx, base);  // back, as from any row
        return;
    }
    kernel::Return(ctx, 0, 1);
}

// a page set up from its description (thiscall page (description, xpr)): the elements take their places from it here
X86_EXTERN_FUNC(__imp__sub_000EC4D0);
X86_FUNC(sub_000EC4D0) {
    if (kernel::R32(ctx.ecx) == kOptionPageVtable && g_game_state != kStatePause) Relayout(kernel::R32(ctx.esp + 4));
    __imp__sub_000EC4D0(ctx, base);
}

// STEAL_AUDIO_TRACE: the game's XACT requests. IXACTSoundBank_PlayEx (bank, cue, source; a cue on no sound source
// plays in 2D, through no submix: its parameters' fields logged raw) and IXACTSoundBank_Stop (bank, cue, flags, cue).
namespace {
bool XactTrace() {
    static const bool on = std::getenv("STEAL_AUDIO_TRACE") != nullptr;
    return on;
}
}  // namespace
// The game's sounds (FUN_0008BDE0, and FUN_0008B960 through the PlayEx wrapper FUN_0008AED0) ask XACT for a sound
// source first. With XACT's voices all taken (a pile-up of crashes) there is none, and the cue plays in 2D anyway;
// the game keeps no hold of a cue without its source and never stops it: crash loops pile up, a drone until the
// mission ends. Such a cue is refused as XACT refuses a voice.
constexpr uint32_t kPlay3dReturn = 0x8BE60;
constexpr uint32_t kWrapperReturn = 0x8AF06;      // FUN_0008AED0's call of PlayEx
constexpr uint32_t kWrapperCallerSlot = 0x20;     // its caller's return address, from PlayEx's esp
constexpr uint32_t kPlaySoundReturn = 0x8BAB3;    // FUN_0008B960's call of FUN_0008AED0
constexpr uint32_t kXactFail = 0x80004005;  // E_FAIL
bool UntrackedCue(const X86Context& ctx) {
    uint32_t ret = kernel::R32(ctx.esp);
    return ret == kPlay3dReturn ||
           (ret == kWrapperReturn && kernel::R32(ctx.esp + kWrapperCallerSlot) == kPlaySoundReturn);
}
X86_EXTERN_FUNC(__imp__sub_0016CDE3);
X86_FUNC(sub_0016CDE3) {
    uint32_t bank = kernel::R32(ctx.esp + 4), params = kernel::R32(ctx.esp + 8), out = kernel::R32(ctx.esp + 12);
    uint32_t p[4] = {};
    for (int i = 0; i < 4 && params; i++) p[i] = kernel::R32(params + i * 4);
    if (params && !p[2] && UntrackedCue(ctx)) {
        if (XactTrace()) LOGE("xact play bank %08X cue %X refused: no sound source", bank, p[1]);
        kernel::Return(ctx, kXactFail, 3);
        return;
    }
    __imp__sub_0016CDE3(ctx, base);
    if (XactTrace())
        LOGE("xact play bank %08X params %08X %08X %08X %08X cue out %08X -> %08X (cue %08X)", bank, p[0], p[1], p[2], p[3],
             out, ctx.eax, out ? kernel::R32(out) : 0);
}
X86_EXTERN_FUNC(__imp__sub_0016CE16);
X86_FUNC(sub_0016CE16) {
    uint32_t a = kernel::R32(ctx.esp + 4), b = kernel::R32(ctx.esp + 8), c = kernel::R32(ctx.esp + 12),
             d = kernel::R32(ctx.esp + 16);
    __imp__sub_0016CE16(ctx, base);
    if (XactTrace()) LOGE("xact stop %08X %08X %08X %08X -> %08X", a, b, c, d, ctx.eax);
}
// A contact's scrape (FUN_000E05B0, thiscall (contacts, &handle, surface, ?, &position, &velocity, ?)): a looping cue
// while the sliding speed is over the threshold at 0x1C0568, stopped below it. Pressed against a tank the speed is
// mostly just under it and over it now and then, and each stop lets the loop play out its pass: dozens of copies a
// second, a loud drone that takes every voice. A stop here waits until the speed has stayed low for a moment; a
// sound no longer asked about is stopped after a moment. Sounds are known by their id, not by the handle's address:
// the car's contact slots (FUN_000DDE10) hand the handles from slot to slot. A contact without a sound gets a fresh
// slot every frame and reads as a hit, so a stopped scrape would start again at once while the cars still touch: the
// car's scrapes on that surface stay quiet until it has had no such contact for a moment.
namespace {
constexpr uint32_t kScrapeThreshold = 0x1C0568;
constexpr uint32_t kSoundStop = 0x8B4D0;  // thiscall sound player (id)
constexpr auto kScrapeHeld = std::chrono::milliseconds(400);
constexpr auto kScrapeForgotten = std::chrono::milliseconds(500);
struct Scrape {
    uint32_t id, at;                                    // the sound, where its handle was last
    std::chrono::steady_clock::time_point high, seen;  // the last call over the threshold, the last call
    bool held;                                          // a stop of it was held back
};
std::vector<Scrape> g_scrapes;
struct Quiet {
    uint32_t contacts, surface;
    std::chrono::steady_clock::time_point seen;
};
std::vector<Quiet> g_quiet;  // scrapes stopped while their contact went on

void StopSound(X86Context& ctx, uint32_t id) {
    uint32_t ecx = ctx.ecx;
    ctx.ecx = kSoundPlayer;
    kernel::CallGuest(ctx, kSoundStop, {id});
    ctx.ecx = ecx;
}
}  // namespace
X86_EXTERN_FUNC(__imp__sub_000E05B0);
X86_FUNC(sub_000E05B0) {
    uint32_t slot = kernel::R32(ctx.esp + 4), velocity = kernel::R32(ctx.esp + 20);
    auto now = std::chrono::steady_clock::now();
    int32_t id = int32_t(kernel::R32(slot));
    // sounds no longer asked about: stopped if a stop of theirs was held back and their handle is still where it was
    for (size_t i = 0; i < g_scrapes.size();) {
        Scrape& s = g_scrapes[i];
        if (int32_t(s.id) != id && now - s.seen > kScrapeForgotten) {
            if (s.held && kernel::R32(s.at) == s.id) {
                StopSound(ctx, s.id);
                kernel::W32(s.at, ~0u);
            }
            g_scrapes.erase(g_scrapes.begin() + i);
        } else {
            i++;
        }
    }
    float v[3];
    for (int i = 0; i < 3; i++) {
        uint32_t u = kernel::R32(velocity + i * 4);
        std::memcpy(&v[i], &u, 4);
    }
    uint32_t t = kernel::R32(kScrapeThreshold);
    float threshold;
    std::memcpy(&threshold, &t, 4);
    bool low = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) < threshold;
    uint32_t contacts = ctx.ecx, surface = kernel::R32(ctx.esp + 8);
    std::erase_if(g_quiet, [&](const Quiet& q) { return now - q.seen > kScrapeForgotten; });
    auto quiet = std::find_if(g_quiet.begin(), g_quiet.end(),
                              [&](auto& q) { return q.contacts == contacts && q.surface == surface; });
    if (quiet != g_quiet.end() && id < 0) {
        quiet->seen = now;
        kernel::Return(ctx, ctx.eax, 6);
        return;
    }
    auto it = std::find_if(g_scrapes.begin(), g_scrapes.end(), [&](auto& s) { return int32_t(s.id) == id; });
    if (low && id >= 0 && it != g_scrapes.end()) {
        if (now - it->high < kScrapeHeld) {
            it->at = slot;
            it->seen = now;
            it->held = true;
            kernel::Return(ctx, ctx.eax, 6);
            return;
        }
        g_scrapes.erase(it);  // stopped now
        if (quiet == g_quiet.end()) g_quiet.push_back({contacts, surface, now});
    }
    __imp__sub_000E05B0(ctx, base);
    id = int32_t(kernel::R32(slot));
    if (!low && id >= 0) {  // playing, perhaps started just now
        it = std::find_if(g_scrapes.begin(), g_scrapes.end(), [&](auto& s) { return int32_t(s.id) == id; });
        if (it == g_scrapes.end()) it = g_scrapes.insert(g_scrapes.end(), {uint32_t(id), slot, now, now, false});
        it->at = slot;
        it->high = it->seen = now;
    }
}
// A car's contact sounds (FUN_000DDE10, thiscall (car, contact, ?, &position, &velocity, ?)): ten slots a frame,
// counted at +0x138, each keeping its sound (+0x13C) while the same contact (+0x140) comes back. With more contacts
// than that (against a tank) each further one takes the loudest slot and stops its sound, which is started again
// next frame: sixty starts a second, a drone. The further contacts are left out instead.
constexpr uint32_t kContactCount = 0x138;
constexpr int32_t kContactSlots = 10;
X86_EXTERN_FUNC(__imp__sub_000DDE10);
X86_FUNC(sub_000DDE10) {
    if (int32_t(kernel::R32(ctx.ecx + kContactCount)) >= kContactSlots) {
        kernel::Return(ctx, ctx.eax, 5);
        return;
    }
    __imp__sub_000DDE10(ctx, base);
}

// the game's 3D sounds (FUN_0008CC90 matches each frame's wanted sounds to its slots by id): a slot's sound started
// for a wanted one (slot: id, cue, source, cue word; wanted: +0xC id, +0x10 cue word), and stopped by id
X86_EXTERN_FUNC(__imp__sub_0008BDE0);
X86_FUNC(sub_0008BDE0) {
    uint32_t player = ctx.ecx, slot = kernel::R32(ctx.esp + 4), wanted = kernel::R32(ctx.esp + 8);
    __imp__sub_0008BDE0(ctx, base);
    // STEAL_AUDIO_COUNT: XACT's free voices (engine +0x80), at most every 2 s
    static const bool count = std::getenv("STEAL_AUDIO_COUNT") != nullptr;
    static auto counted = std::chrono::steady_clock::time_point();
    if (count && std::chrono::steady_clock::now() - counted > std::chrono::seconds(2)) {
        counted = std::chrono::steady_clock::now();
        uint32_t engine = kernel::R32(player + 8);
        LOGE("xact free voices %d, game sounds %d", engine ? int32_t(kernel::R32(engine + 0x80)) : -1,
             int32_t(kernel::R32(player + 0x1AC)));
    }
    if (XactTrace() && wanted) {
        std::string w;
        char b[16];
        for (int i = 0; i < 0x38; i += 4) {
            std::snprintf(b, sizeof(b), " %08X", kernel::R32(wanted + i));
            w += b;
        }
        LOGE("snd3d play %08X slot %08X wanted %08X:%s -> %08X", player, slot, wanted, w.c_str(), ctx.eax);
    }
}
X86_EXTERN_FUNC(__imp__sub_0008B4D0);
X86_FUNC(sub_0008B4D0) {
    uint32_t id = kernel::R32(ctx.esp + 4), ret = kernel::R32(ctx.esp);
    __imp__sub_0008B4D0(ctx, base);
    if (XactTrace()) LOGE("snd3d stop id %08X from %08X -> %u", id, ret, ctx.eax);
}

// The cutscenes' glow. Their bright pass (FUN_0003E840, the call at 0x3FAED) sets the gain of what goes over the
// threshold as constant 0's alpha (0.094); the rest comes from a material glow in the scene's alpha. The glow comes
// out about a third as strong as xemu's on the same frames, for a reason not found, so that gain is raised to match
// (STEAL_BLOOM=<factor> overrides, 1 keeps the game's). Driving's pass (FUN_0003D6B0 at 0x3D74A, 0.15) is left
// alone: raised, it darkens the frame.
namespace {
constexpr uint32_t kBrightPassFrom[] = {0x3FAF2};  // the call's return address
constexpr float kGlowBoost = 1.8f;
float GlowBoost() {
    static const float boost = [] {
        const char* env = std::getenv("STEAL_BLOOM");
        return env ? std::clamp(float(std::atof(env)), 0.0f, 10.0f) : kGlowBoost;
    }();
    return boost;
}
void BoostGlow(uint32_t from, uint32_t reg, float* value) {
    if (reg != 0 || std::find(std::begin(kBrightPassFrom), std::end(kBrightPassFrom), from) == std::end(kBrightPassFrom)) {
        return;
    }
    value[3] = std::min(value[3] * GlowBoost(), 1.0f);
}
const bool g_glow_boost = (d3d::SetPixelConstantFilter(BoostGlow), true);
}  // namespace

// The camera's projection (FUN_0002D250 and FUN_0002D140, cdecl (matrix, fov, near, far, width, height); FUN_000275D0
// builds the players' cameras with them, near 0.5 and far 1200 to 3840 by the scene). STEAL_DRAW_DISTANCE=<factor>
// moves the far plane out.
namespace {
float FarFactor() {
    static const float factor = [] {
        const char* env = std::getenv("STEAL_DRAW_DISTANCE");
        return env ? std::clamp(float(std::atof(env)), 1.0f, 16.0f) : 1.0f;
    }();
    return factor;
}
void ScaleFar(X86Context& ctx) {
    constexpr uint32_t kFarArg = 16;
    float plane;
    uint32_t u = kernel::R32(ctx.esp + kFarArg);
    std::memcpy(&plane, &u, 4);
    plane *= FarFactor();
    std::memcpy(&u, &plane, 4);
    kernel::W32(ctx.esp + kFarArg, u);
}
}  // namespace
X86_EXTERN_FUNC(__imp__sub_0002D250);
X86_FUNC(sub_0002D250) {
    ScaleFar(ctx);
    __imp__sub_0002D250(ctx, base);
}
X86_EXTERN_FUNC(__imp__sub_0002D140);
X86_FUNC(sub_0002D140) {
    ScaleFar(ctx);
    __imp__sub_0002D140(ctx, base);
}

// FRAME RATE 60. The game's logic steps at 60 Hz: its frame (FUN_00023800) sets the scene up, runs up to two steps
// (TickScene FUN_00019180, thiscall with the step's index 0 or 1 on the stack, or a state's own), each closed by
// FUN_00031560(0), then draws and swaps on every second vblank. At 60 a frame runs one step and swaps on the next
// vblank; a second step follows only while the logic is a step or more behind real time, so a slower PC keeps the
// game's speed with fewer frames. The steps' indices go on alternating as on the console.
namespace ui {
extern uint32_t g_frame_rate;  // Recomp Settings > DISPLAY > FRAME RATE: 30 or 60
}
namespace {
using StepClock = std::chrono::steady_clock;
constexpr uint32_t kStepDone = 0x23A8E;  // FUN_00031560's return into the frame's step loop
constexpr double kStepHz = 60.0;
constexpr double kMaxBehind = 4.0;  // steps: more than this (loads, pauses) is let go
bool g_smooth_frame = false;        // in a frame run at 60
bool g_clock_running = false;
StepClock::time_point g_clock_start;
double g_steps = 0;    // the logic's steps since the clock started
uint32_t g_index = 0;  // the next TickScene's index
}  // namespace
uint32_t g_frame_object = 0;  // FUN_00023800's object, +0x4C the game's state
// XBE_PERF=1: the game's own time each second (its frame FUN_00023800, its logic steps TickScene, its scene's draw
// FUN_00016730), on its thread
double g_frame_time = 0, g_tick_time = 0, g_draw_time = 0;
int g_ticks = 0;
void GuestTime(double& sum, std::chrono::steady_clock::time_point start) {
    sum += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
void GuestTimes() {
    static const bool on = std::getenv("XBE_PERF") != nullptr;
    static auto last = std::chrono::steady_clock::now();
    static int frames = 0;
    frames++;
    auto now = std::chrono::steady_clock::now();
    if (!on || now - last < std::chrono::seconds(1)) return;
    LOGI("perf: game per frame %.2f ms (logic %.2f in %.2f steps, scene draw %.2f)", g_frame_time / frames,
         g_tick_time / frames, double(g_ticks) / frames, g_draw_time / frames);
    g_frame_time = g_tick_time = g_draw_time = 0;
    g_ticks = frames = 0;
    last = now;
}
// STEAL_PROFILE=<from s>,<for s>: the game thread's host instruction pointer sampled each millisecond while its scene
// draws, into profile.txt (llvm-symbolizer with DoubleSteal.pdb names them; the image base is its first line)
std::atomic<bool> g_in_draw{false};
void StartProfiler() {
    const char* env = std::getenv("STEAL_PROFILE");
    if (!env) return;
    double from = std::atof(env), span = std::strchr(env, ',') ? std::atof(std::strchr(env, ',') + 1) : 10;
    HANDLE me = nullptr;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &me, 0, FALSE, DUPLICATE_SAME_ACCESS);
    // STEAL_PROFILE_THREAD=render: the render thread instead, all the time
    bool render = std::getenv("STEAL_PROFILE_THREAD") && !std::strcmp(std::getenv("STEAL_PROFILE_THREAD"), "render");
    std::thread([me, from, span, render]() mutable {
        auto t0 = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::duration<double>(from));
        if (render) me = static_cast<HANDLE>(gpu::RenderThread());
        if (!me) return;
        std::vector<uint64_t> rips;
        timeBeginPeriod(1);
        while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < from + span) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (!render && !g_in_draw) continue;
            if (SuspendThread(me) == DWORD(-1)) break;
            CONTEXT c{};
            c.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(me, &c)) {
                // the first frames up the stack (a sample in a DLL: who called it), 0 after each sample
                rips.push_back(c.Rip);
                for (int i = 0; i < 4; i++) {
                    DWORD64 image = 0;
                    PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &image, nullptr);
                    if (!fn) {
                        c.Rip = *reinterpret_cast<DWORD64*>(c.Rsp);
                        c.Rsp += 8;
                    } else {
                        void* data;
                        DWORD64 frame;
                        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, c.Rip, fn, &c, &data, &frame, nullptr);
                    }
                    rips.push_back(c.Rip);
                }
                rips.push_back(0);
            }
            ResumeThread(me);
        }
        timeEndPeriod(1);
        if (FILE* f = std::fopen("profile.txt", "w")) {
            std::fprintf(f, "base %p\n", static_cast<void*>(GetModuleHandleW(nullptr)));
            for (uint64_t r : rips) {
                HMODULE m = nullptr;
                char name[MAX_PATH] = "?";
                if (r && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                            reinterpret_cast<LPCSTR>(r), &m))
                    GetModuleFileNameA(m, name, MAX_PATH);
                const char* leaf = std::strrchr(name, '\\') ? std::strrchr(name, '\\') + 1 : name;
                std::fprintf(f, "%llx %s+%llx\n", static_cast<unsigned long long>(r), leaf,
                             static_cast<unsigned long long>(r - reinterpret_cast<uint64_t>(m)));
            }
            std::fclose(f);
        }
        LOGW("profile: %zu samples", rips.size());
    }).detach();
}
X86_EXTERN_FUNC(__imp__sub_00016730);
X86_FUNC(sub_00016730) {
    static bool started = (StartProfiler(), true);
    (void)started;
    g_in_draw = true;
    auto start = std::chrono::steady_clock::now();
    __imp__sub_00016730(ctx, base);
    g_in_draw = false;
    GuestTime(g_draw_time, start);
}
X86_EXTERN_FUNC(__imp__sub_00023800);
X86_FUNC(sub_00023800) {
    g_game_state = kernel::R32(ctx.ecx + 0x4C);
    g_frame_object = ctx.ecx;
    if (std::getenv("STEAL_STATE_LOG")) {
        static uint32_t was = ~0u, scene_was = ~0u;
        uint32_t state = kernel::R32(ctx.ecx + 0x4C), scene = kernel::R32(ctx.ecx + 0x0C);
        uint32_t sstate = scene ? kernel::R32(scene + 0x1F4) : ~0u;
        if (state != was || sstate != scene_was) LOGW("state game %u scene %u", state, sstate);
        was = state, scene_was = sstate;
    }
    g_smooth_frame = ui::g_frame_rate >= 60;
    if (!g_smooth_frame) g_clock_running = false;
    d3d::SetIntervalOverride(g_smooth_frame ? 1 : 0);
    auto start = StepClock::now();
    __imp__sub_00023800(ctx, base);
    GuestTime(g_frame_time, start);
    GuestTimes();
    d3d::SetIntervalOverride(0);
    g_smooth_frame = false;
}
X86_EXTERN_FUNC(__imp__sub_00019180);
X86_FUNC(sub_00019180) {
    g_last_tick = std::chrono::steady_clock::now();
    constexpr uint32_t kIndexArg = 4;
    if (g_smooth_frame) kernel::W32(ctx.esp + kIndexArg, g_index);
    g_index = (kernel::R32(ctx.esp + kIndexArg) + 1) & 1;
    auto start = StepClock::now();
    __imp__sub_00019180(ctx, base);
    GuestTime(g_tick_time, start);
    g_ticks++;
}
X86_EXTERN_FUNC(__imp__sub_00031560);
X86_FUNC(sub_00031560) {
    bool step_done = g_smooth_frame && kernel::R32(ctx.esp) == kStepDone;
    __imp__sub_00031560(ctx, base);
    if (!step_done) return;
    auto now = StepClock::now();
    if (!g_clock_running) {
        g_clock_running = true;
        g_clock_start = now;
        g_steps = 0;
    }
    g_steps += 1;
    double due = std::chrono::duration<double>(now - g_clock_start).count() * kStepHz;
    if (due - g_steps > kMaxBehind) g_steps = std::floor(due);
    // ebx counts the loop's steps: the loop ends after this one unless the logic is behind, or the game's state
    // changed in it: the next state's first step readies what the frame draws (leaving a mission the race's scene is
    // gone, drawing it before that step reads freed objects), as at 30 where every frame has two
    bool changed = kernel::R32(g_frame_object + 0x4C) != g_game_state;
    if (ctx.ebx == 0 && due < g_steps + 1 && !changed) ctx.ebx = 1;
    // STEAL_STEP_LOG=1: the logic's steps and the frames each second into steps.log (60 and 60 at full speed)
    static const bool log = std::getenv("STEAL_STEP_LOG") != nullptr;
    static StepClock::time_point last = now;
    static int steps = 0, frames = 0;
    steps++;
    frames += ctx.ebx == 1;  // the loop ends after this step
    if (log && now - last >= std::chrono::seconds(1)) {
        static FILE* f = std::fopen("steps.log", "w");
        if (f) {
            std::fprintf(f, "%d steps %d frames in %.2f s\n", steps, frames, std::chrono::duration<double>(now - last).count());
            std::fflush(f);
        }
        last = now;
        steps = frames = 0;
    }
}
