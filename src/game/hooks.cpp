// Hooks for default.xbe (Double S.T.E.A.L. - The Second Clash, XDK 5849).
#include <cstdlib>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>

#include "core/log.h"

#include "kernel/kernel.h"
#include "xdk/d3d8.h"
#include "xdk/hle.h"

X86_EXTERN_FUNC(__imp__sub_00177B8E);

namespace ui {
extern bool g_skip_intros;  // Recomp Settings > GENERAL > SKIP INTROS
void OptionsShown();        // the game's OPTIONS page is up this frame: Y there opens Recomp Settings
// the RECOMP SETTINGS row put into the OPTIONS page: its y in the page's units, chosen, the page's fade
void OptionsRow(int y, bool chosen, float alpha);
void OpenPage();
bool HideOptions();  // Recomp Settings is drawn over the OPTIONS page: its rows and title go meanwhile
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

// each description once: it outlives the page. def: the page's description, +4 its elements, +8 theirs (0x24
// each, y at +0xC); +0x14 its timelines, +0x18 theirs (12 bytes, +8 their commands). A move command (type 3) holds
// where it takes its element from and to (x, y at +0x18, +0x1A and +0x1C, +0x1E), which the element keeps after.
void Relayout(uint32_t def) {
    static std::vector<uint32_t> done;
    if (std::find(done.begin(), done.end(), def) != done.end()) return;
    done.push_back(def);
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
    if (!up) return;
    if (page != g_page) g_page = page, g_on_row = false;
    if (kernel::R32(menu + 0x20) == 0) ui::OptionsShown();
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
    ui::OptionsRow(kRowY0 + kNewRow * kNewStep, g_on_row, std::min(1.0f, int(kernel::R32(page + 0x3C)) / 256.0f));
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
    if (kernel::R32(ctx.ecx) == kOptionPageVtable) Relayout(kernel::R32(ctx.esp + 4));
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

// The frame's glow. Its bright pass (FUN_0003E840 for cutscenes and menus at 0x3FAED, FUN_0003D6B0's at 0x3D74A)
// sets the gain of what goes over the threshold as constant 0's alpha (0.094, 0.15); the rest comes from a material
// glow in the scene's alpha. The glow comes out about a third as strong as xemu's on the same frames, for a reason
// not found, so the gain of those two calls is raised to match (STEAL_BLOOM=<factor> overrides, 1 keeps the game's).
namespace {
constexpr uint32_t kBrightPassFrom[] = {0x3FAF2, 0x3D74F};  // the calls' return addresses
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
