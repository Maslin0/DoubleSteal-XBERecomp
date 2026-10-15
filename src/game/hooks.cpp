// Hooks for default.xbe (Double S.T.E.A.L. - The Second Clash, XDK 5849).
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <string>

#include "core/log.h"

#include "kernel/kernel.h"
#include "xdk/hle.h"

X86_EXTERN_FUNC(__imp__sub_00177B8E);

namespace ui {
extern bool g_skip_intros;  // Recomp Settings > GENERAL > SKIP INTROS
void OptionsShown();        // the game's OPTIONS page is up this frame: Y there opens Recomp Settings
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

// The options menu, each frame it is up (main menu and pause menu): +0x18 the page shown (1 OPTIONS, 3 CONTROLLER
// SETTING), +0x20 its change (0 settled, 2 leaving).
X86_EXTERN_FUNC(__imp__sub_0010AB80);
X86_FUNC(sub_0010AB80) {
    uint32_t menu = ctx.ecx;
    __imp__sub_0010AB80(ctx, base);
    if (kernel::R32(menu + 0x18) == 1 && kernel::R32(menu + 0x20) == 0) ui::OptionsShown();
}
