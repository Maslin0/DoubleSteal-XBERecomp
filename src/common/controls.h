#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// The keyboard bound to the game's actions (Recomp Settings > CONTROLS) rather than to the controller's buttons: each
// action is pressed on controller 1 where the game's CONTROLLER TYPE puts it
namespace controls {

size_t Count();
const char* Label(size_t action);
std::string Keys(size_t action);  // its keys' names, comma separated, "NONE" for none
// slot 0 its key, 1 its second; the other slot gives it up if it has it
void SetKey(size_t action, int slot, uint16_t vk);
bool ClearSecond(size_t action);  // false when it had none
bool Reset();                     // every action's keys as at first; false when they already were

// game thread, each frame: the game's CONTROLLER TYPE (0 A, 1 B) and whether a race is running (the keys are the
// game's then, its menus' arrows, Enter, Space, Backspace and Escape otherwise)
void Update(int type, bool racing);

// the settings file's [controls]: action = [key, key] (virtual-key codes)
void Load(const void* table);
void Save(void* table);

}  // namespace controls
