// MCPX audio processor registers. XACT reads the APU's global sample counter (NV_PAPU_XGSCNT, 48 kHz)
// straight from the hardware to time its sounds, so the register page exists in guest memory and the
// counter follows real time.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <thread>

#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace apu {

constexpr uint32_t kBase = 0xFE800000, kSize = 0x80000;
constexpr uint32_t kSampleCount = kBase + 0x200C;

void Init() {
    if (!mem::Commit(kBase, kSize)) return;
    std::thread([] {
        LARGE_INTEGER f, start, now;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&start);
        for (;;) {
            QueryPerformanceCounter(&now);
            kernel::W32(kSampleCount, uint32_t((now.QuadPart - start.QuadPart) * 48000 / f.QuadPart));
            Sleep(1);
        }
    }).detach();
}

}  // namespace apu
