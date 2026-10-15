// Hooks for default.xbe (Double S.T.E.A.L. - The Second Clash, XDK 5849).
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/log.h"

#include "kernel/kernel.h"
#include "xdk/hle.h"

X86_EXTERN_FUNC(__imp__sub_00177B8E);

// XMVDecoder_Play(decoder, flags, rect), __stdcall: the library's own player decodes the movie (LOGO1.XMV, the
// publisher logo) onto the overlay the SDK presents. STEAL_SKIP_MOVIES=1 skips it as if it had finished.
X86_FUNC(sub_00177B8E) {
    if (std::getenv("STEAL_SKIP_MOVIES")) {
        kernel::Return(ctx, 0, 3);
        return;
    }
    __imp__sub_00177B8E(ctx, base);
}

// STEAL_NAVI_LOG=1: the minimap's tile streaming (cell changes, the nine cached tiles, every load)
X86_EXTERN_FUNC(__imp__sub_000E4D10);
X86_FUNC(sub_000E4D10) {
    static const bool on = std::getenv("STEAL_NAVI_LOG") != nullptr;
    if (on) {
        uint32_t t = ctx.ecx, cache = t + 0x14;
        auto f = [&](uint32_t a) { uint32_t v = kernel::R32(a); float x; std::memcpy(&x, &v, 4); return x; };
        float scale = f(cache + 0x384);
        int px = int((f(t + 0x454) - f(cache + 0x370)) * scale), pz = int((0.0f - (f(t + 0x45C) - f(cache + 0x368))) * scale);
        int cw = int(kernel::R32(cache + 0x360)), ch = int(kernel::R32(cache + 0x364));
        static int last_c = -99, last_r = -99;
        if (cw > 0 && ch > 0 && (px / cw != last_c || pz / ch != last_r)) {
            last_c = px / cw;
            last_r = pz / ch;
            std::string slots;
            for (int i = 0; i < 9; i++) {
                uint32_t v = kernel::R32(cache + 0x388 + i * 12);
                slots += " " + std::to_string(int16_t(v)) + "," + std::to_string(int16_t(v >> 16));
            }
            LOGE("navi: cell %d,%d rem %d,%d threshold %.1f flag %u tiles%s", last_c, last_r, px % cw, pz % ch,
                 scale * f(t + 0x544), kernel::R32(cache + 0x38c), slots.c_str());
        }
    }
    // The next row or column of tiles loads once the player is 83 units (15 map pixels) into a new cell; until
    // then the far side of the minimap has no tiles and shows the world through. Load it 3 pixels in instead.
    // STEAL_NAVI_ORIGINAL=1 keeps the game's own distance.
    static const bool original = std::getenv("STEAL_NAVI_ORIGINAL") != nullptr;
    uint32_t margin_at = ctx.ecx + 0x544, margin = kernel::R32(margin_at);
    uint32_t scale_bits = kernel::R32(ctx.ecx + 0x14 + 0x384);
    float scale;
    std::memcpy(&scale, &scale_bits, 4);
    if (!original && scale > 0) {
        float near_margin = 3.5f / scale;
        uint32_t bits;
        std::memcpy(&bits, &near_margin, 4);
        kernel::W32(margin_at, bits);
    }
    __imp__sub_000E4D10(ctx, base);
    if (!original && scale > 0) kernel::W32(margin_at, margin);
}
X86_EXTERN_FUNC(__imp__sub_000E88D0);
X86_FUNC(sub_000E88D0) {
    static const bool on = std::getenv("STEAL_NAVI_LOG") != nullptr;
    int32_t c = int32_t(kernel::R32(ctx.esp + 4)), r = int32_t(kernel::R32(ctx.esp + 8));
    __imp__sub_000E88D0(ctx, base);
    if (on) LOGE("navi: row load %d %d -> %u", c, r, ctx.eax);
}
X86_EXTERN_FUNC(__imp__sub_000E8B40);
X86_FUNC(sub_000E8B40) {
    static const bool on = std::getenv("STEAL_NAVI_LOG") != nullptr;
    int32_t c = int32_t(kernel::R32(ctx.esp + 4)), r = int32_t(kernel::R32(ctx.esp + 8));
    __imp__sub_000E8B40(ctx, base);
    if (on) LOGE("navi: column load %d %d -> %u", c, r, ctx.eax);
}