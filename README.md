# Double S.T.E.A.L. - The Second Clash - XBE Recomp

An unofficial native PC port of the original Xbox version of Double S.T.E.A.L. - The Second Clash (Bunkasha, 2003), with 60 FPS, keyboard support, extended graphics options and other enhancements.

This port was created using the [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp) tool.

> [!NOTE]
> This project does not include any game assets. You must provide the files from your own legally acquired copy of the game to install or build.

## Installation

- Put your game files in the `assets` folder. This can be an ISO, or the extracted data files (`default.xbe` with the game's folders beside it).
- Run `DoubleSteal.exe`.
- Press **F10** (or both sticks on the controller), or choose **RECOMP SETTINGS** in the game's OPTIONS, to change the port's settings. They are kept in `settings.toml` beside the exe.

Saves are kept in the `hdd` folder beside the exe.

## Features

### Display
- **Any resolution and aspect ratio**, with the game's own 16:9 mode.
- **Render resolution** - draw the game above or below the screen's resolution.
- **Display mode** - fullscreen, borderless window or window, on the monitor of your choice.
- **Full picture** - leaves off the border the game kept for old TVs.
- **60 FPS** - every step of the game's logic is shown (DISPLAY > FRAME RATE); the game keeps its speed. 30 is the console's.
- **FPS counter**.

### Graphics
- **SMAA** antialiasing.
- **Anisotropic filtering** up to 16x.
- **Shadow resolution** up to 8192x8192, and soft shadow filtering.
- **Texture detail** - sharper textures in the distance.
- **Async shaders** - shaders and pipelines are compiled in the background instead of stalling the game, and kept in the `shadercache` folder so each is made only once. Set `async_shaders = false` in `settings.toml` to wait for them instead.

### Controls
- Supports Xbox, PlayStation and Switch controllers, with hot plugging and rumble.
- **Keyboard** - every action can be rebound in RECOMP SETTINGS > CONTROLS. The menus use the arrows, Enter and Esc.
- **Button prompts** follow the device used last: while you play with the keyboard the game's A/B/X/Y prompts show its keys.

Default keys:

| Action | Keys |
| --- | --- |
| Steer | A / D, Left / Right |
| Accelerate / Brake | W / S, Up / Down |
| Gear up / down | E / Q |
| Reverse / Sidebrake | Space |
| Action | F |
| Change camera / Rear camera | C / R |
| Zoom camera / Map scale | Z / M |
| Look around | I J K L |
| Pause | Esc |

### General
- **Skip intros**.
- **Pause on focus loss** - the game and its sound stop while the window is in the background.

### Miscellaneous
- **Boot Splash** - Place a `splash.png` (or `.bmp`) beside the exe to display a splash screen at launch.

## System Requirements

### Minimum
- **OS**
  - Windows 10 64-bit
- **GPU** with support for Direct3D 12
- [Microsoft Visual C++ Redistributable 2015-2022 (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe)

## Building

### 1. Clone the Repository
Clone this repository using [Git](https://git-scm.com/).

### 2. Add the Required Game Files
Place your extracted game data files in the `assets` folder. `default.xbe` must be there, with the game's folders beside it.

### 3. Install Dependencies
Install Visual Studio 2022 with **Desktop development with C++**, **C++ Clang tools for Windows** and **C++ CMake tools for Windows** (`build.bat` offers to add the last two).

### 4. Build the Project
Run `build.bat`. The first build recompiles the game and takes a while. The exe is written to `out\build\release`. `release.bat` packs it with an empty `assets` folder into `DoubleSteal.zip` (needs [7-Zip](https://www.7-zip.org/)).

## Credits

- [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp), the recompiler and runtime this is built on.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) for the overridable function layout and instruction hooks.
- [Zydis](https://github.com/zyantific/zydis) for decoding x86 instructions, [toml++](https://github.com/marzer/tomlplusplus) for the configs, [Dear ImGui](https://github.com/ocornut/imgui) for the settings overlay and [SDL](https://github.com/libsdl-org/SDL) for the window, input and controllers.
