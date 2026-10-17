// The keyboard bound to the game's actions. The controller's buttons each action is on, by CONTROLLER TYPE, from the
// game's own diagram of them (ControllerSetting.xpr): TYPE B swaps the throttle and brake from the triggers to A and
// X and the gears the other way. The actions' keys become controller 1's controls' keys (input::Keys) for the SDK's
// keyboard, made again when the keys or the type change.
#include "common/controls.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <iterator>
#include <mutex>

#include <toml++/toml.hpp>

#include "core/input.h"
#include "core/log.h"

namespace controls {

namespace {

using input::Control;

struct Action {
    const char* id;
    const char* label;
    std::array<uint16_t, 2> keys;
    Control a, b;  // its control under TYPE A and TYPE B
};
const Action kActions[] = {
    {"steer_left", "STEER LEFT", {'A', VK_LEFT}, Control::kStickLeft, Control::kStickLeft},
    {"steer_right", "STEER RIGHT", {'D', VK_RIGHT}, Control::kStickRight, Control::kStickRight},
    {"accelerate", "ACCELERATE", {'W', VK_UP}, Control::kRightTrigger, Control::kA},
    {"brake", "BRAKE", {'S', VK_DOWN}, Control::kLeftTrigger, Control::kX},
    {"gear_up", "GEAR UP", {'E', 0}, Control::kA, Control::kRightTrigger},
    {"gear_down", "GEAR DOWN", {'Q', 0}, Control::kX, Control::kLeftTrigger},
    {"sidebrake", "REVERSE / SIDEBRAKE", {VK_SPACE, 0}, Control::kB, Control::kB},
    {"action", "ACTION", {'F', 0}, Control::kY, Control::kY},
    {"change_camera", "CHANGE CAMERA", {'C', 0}, Control::kDpadUp, Control::kDpadUp},
    {"rear_camera", "REAR CAMERA", {'R', 0}, Control::kDpadDown, Control::kDpadDown},
    {"zoom_camera", "ZOOM CAMERA", {'Z', 0}, Control::kWhite, Control::kWhite},
    {"map_scale", "MAP SCALE", {'M', 0}, Control::kBlack, Control::kBlack},
    {"camera_left", "CAMERA LEFT", {'J', 0}, Control::kRightStickLeft, Control::kRightStickLeft},
    {"camera_right", "CAMERA RIGHT", {'L', 0}, Control::kRightStickRight, Control::kRightStickRight},
    {"camera_up", "CAMERA UP", {'I', 0}, Control::kRightStickUp, Control::kRightStickUp},
    {"camera_down", "CAMERA DOWN", {'K', 0}, Control::kRightStickDown, Control::kRightStickDown},
    {"pause", "PAUSE", {VK_ESCAPE, 0}, Control::kStart, Control::kStart},
};
constexpr size_t kCount = std::size(kActions);

std::mutex g_lock;
std::array<std::array<uint16_t, 2>, kCount> g_keys = [] {
    std::array<std::array<uint16_t, 2>, kCount> k{};
    for (size_t i = 0; i < kCount; i++) k[i] = kActions[i].keys;
    return k;
}();
int g_type = -1;  // as last applied

// under g_lock: the controls' keys from the actions'
void Apply() {
    input::KeyMap map{};
    for (size_t i = 0; i < kCount; i++) {
        auto& slots = map[size_t(g_type == 1 ? kActions[i].b : kActions[i].a)];
        for (uint16_t vk : g_keys[i])
            for (auto& s : slots)
                if (vk && (!s || s == vk)) {
                    s = vk;
                    break;
                }
    }
    input::Keys() = map;
}

}  // namespace

size_t Count() { return kCount; }

const char* Label(size_t action) { return action < kCount ? kActions[action].label : ""; }

std::string Keys(size_t action) {
    std::lock_guard lock(g_lock);
    if (action >= kCount) return "";
    std::string first = input::KeyName(g_keys[action][0]), second = input::KeyName(g_keys[action][1]);
    if (first.empty()) first.swap(second);
    if (first.empty()) return "NONE";
    return second.empty() ? first : first + ", " + second;
}

void SetKey(size_t action, int slot, uint16_t vk) {
    std::lock_guard lock(g_lock);
    if (action >= kCount) return;
    auto& k = g_keys[action];
    k[size_t(slot & 1)] = vk;
    if (k[0] == k[1]) k[1] = 0;
    if (!k[0]) std::swap(k[0], k[1]);
    Apply();
}

bool ClearSecond(size_t action) {
    std::lock_guard lock(g_lock);
    if (action >= kCount || !g_keys[action][1]) return false;
    g_keys[action][1] = 0;
    Apply();
    return true;
}

bool Reset() {
    std::lock_guard lock(g_lock);
    bool changed = false;
    for (size_t i = 0; i < kCount; i++) {
        changed |= g_keys[i] != kActions[i].keys;
        g_keys[i] = kActions[i].keys;
    }
    if (changed) Apply();
    return changed;
}

void Update(int type, bool racing) {
    static int was = -1;
    if (int(racing) != was) {
        was = racing;
        LOGI("controls: %s, controller type %c", racing ? "racing" : "menus", type == 1 ? 'B' : 'A');
    }
    input::SetMenuKeys(!racing);
    std::lock_guard lock(g_lock);
    if (type == g_type) return;
    g_type = type;
    Apply();
}

void Load(const void* table) {
    const auto* t = static_cast<const toml::table*>(table);
    std::lock_guard lock(g_lock);
    if (const toml::table* c = (*t)["controls"].as_table()) {
        for (size_t i = 0; i < kCount; i++) {
            const toml::array* a = (*c)[kActions[i].id].as_array();
            if (!a) continue;
            for (size_t k = 0; k < 2; k++) g_keys[i][k] = uint16_t(k < a->size() ? (*a)[k].value_or(int64_t(0)) : 0);
        }
    }
    if (g_type < 0) g_type = 0;
    Apply();
}

void Save(void* table) {
    auto* t = static_cast<toml::table*>(table);
    toml::table c;
    std::lock_guard lock(g_lock);
    for (size_t i = 0; i < kCount; i++) {
        toml::array a;
        for (uint16_t vk : g_keys[i])
            if (vk) a.push_back(int64_t(vk));
        c.insert(kActions[i].id, a);
    }
    t->insert_or_assign("controls", c);
}

}  // namespace controls
