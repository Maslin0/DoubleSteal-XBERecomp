#include "app/app.h"
#include "core/input.h"

namespace apu {
void Init();
}
namespace debug {
void Init();
}
namespace ui {
extern bool g_fps_counter, g_skip_intros;
void Window(void* hwnd);
bool Message(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);
void Draw(double fps);
void Pad(int port, input::Pad& pad);
}

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"Double S.T.E.A.L. - The Second Clash",               // window and splash
    .short_title = L"Double S.T.E.A.L.",               // message boxes
    .name = L"DoubleSteal",                      // window classes, crash dumps, handover event
    .log = "dsteal",                        // log file prefix
    .settings = "settings.toml",          // settings file beside the exe
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .images = {{"default.xbe", "DoubleSteal"}},  // each recompiled xbe and its exe, for title launches
    .options = {{"fps_counter", &ui::g_fps_counter, nullptr}, {"skip_intros", &ui::g_skip_intros, nullptr}},                      // game settings kept in the settings file
    .load = nullptr,                      // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
    .init = debug::Init,                     // after the settings load, before the window and boot
    .loaded = apu::Init,                  // image in memory, before its entry point runs
    .window = ui::Window,                  // the game window is up
    .message = ui::Message,                 // window messages first; true takes one
    .ui = ui::Draw,                      // ImGui drawing each frame
    .pad = ui::Pad,                        // changes a polled pad
    .rumble = nullptr,                    // changes a rumble before it is set
};
