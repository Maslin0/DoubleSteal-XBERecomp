// Test aids, the same as the native port's:
//   STEAL_AUTOPRESS = "secs:button,..."  presses a pad 1 button for 0.2 s at each time (a b x y start back
//                                        up down left right white black lt rt), "secs:hold-a-5" holds it 5 s
//   STEAL_CAPTURE_AT = "secs,..."        captures a frame (XBE_CAPTURE_DIR, default "captures") at each time
//   STEAL_THREADS_AT = "secs,..."        logs every guest thread's registers and stack at each time
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "core/input.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "kernel/kernel.h"

namespace debug {

void LogThreads();

namespace {

using Clock = std::chrono::steady_clock;
Clock::time_point g_start = Clock::now();

struct Press {
    double at, len;
    std::string button;
};
std::vector<Press> g_presses;

std::string Env(const char* name) {
    char buf[4096];
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    return n && n < sizeof(buf) ? std::string(buf, n) : std::string();
}

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t p = 0;
    while (p <= s.size()) {
        size_t q = s.find(sep, p);
        if (q == std::string::npos) q = s.size();
        if (q > p) out.push_back(s.substr(p, q - p));
        p = q + 1;
    }
    return out;
}

double Now() { return std::chrono::duration<double>(Clock::now() - g_start).count(); }

}  // namespace

void Init() {
    g_start = Clock::now();
    for (auto& item : Split(Env("STEAL_AUTOPRESS"), ',')) {
        size_t c = item.find(':');
        if (c == std::string::npos) continue;
        Press p{std::atof(item.substr(0, c).c_str()), 0.2, item.substr(c + 1)};
        if (p.button.rfind("hold-", 0) == 0) {
            size_t d = p.button.find('-', 5);
            p.len = d == std::string::npos ? 1.0 : std::atof(p.button.substr(d + 1).c_str());
            p.button = p.button.substr(5, d == std::string::npos ? std::string::npos : d - 5);
        }
        g_presses.push_back(p);
    }
    std::vector<double> dumps;
    for (auto& t : Split(Env("STEAL_THREADS_AT"), ',')) dumps.push_back(std::atof(t.c_str()));
    if (!dumps.empty())
        std::thread([dumps] {
            for (double t : dumps) {
                while (Now() < t) Sleep(20);
                LogThreads();
            }
        }).detach();
    std::vector<double> caps;
    for (auto& t : Split(Env("STEAL_CAPTURE_AT"), ',')) caps.push_back(std::atof(t.c_str()));
    if (!caps.empty()) {
        if (Env("XBE_CAPTURE_DIR").empty()) SetEnvironmentVariableA("XBE_CAPTURE_DIR", "captures");
        std::thread([caps] {
            for (double t : caps) {
                while (Now() < t) Sleep(20);
                gpu::g_capture = true;
            }
        }).detach();
    }
}

void LogThreads() {
    for (auto& t : kernel::Threads())
        if (t->ctx) {
            LOGI("thread %X", t->id);
            kernel::LogGuestStack(*t->ctx, 48);
        }
}

void Pad(int port, input::Pad& pad) {
    if (port != 0 || g_presses.empty()) return;
    double now = Now();
    pad.connected = true;  // a scripted run keeps pad 1 plugged in
    for (auto& p : g_presses) {
        if (now < p.at || now >= p.at + p.len) continue;
        static const char* kButtons[] = {"up", "down", "left", "right", "start", "back", "ls", "rs"};
        static const char* kAnalog[] = {"a", "b", "x", "y", "black", "white", "lt", "rt"};
        for (int i = 0; i < 8; i++) {
            if (p.button == kButtons[i]) pad.buttons |= uint16_t(1u << i);
            if (p.button == kAnalog[i]) pad.analog[i] = 255;
        }
    }
}

}  // namespace debug
