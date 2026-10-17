// Recomp Settings: a page drawn like the game's own OPTIONS screen (F10, or both sticks pressed on pad 1), and the
// frame rate counter. Every change is saved to settings.toml at once.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include <imgui.h>

#include "app/app.h"
#include "core/input.h"
#include "core/log.h"

namespace debug {
void Pad(int port, input::Pad& pad);
}
namespace shadows {
extern uint32_t g_resolution;
extern bool g_filter;
}

namespace ui {

bool g_fps_counter = false;
bool g_skip_intros = false;
uint32_t g_frame_rate = 30;  // 30 the console's, 60 each step of the game's logic shown

namespace {

using Clock = std::chrono::steady_clock;

// --- the page's look: the game's OPTIONS screen, measured on a 1440x810 frame -------------------------------------

constexpr float kW = 1440, kH = 810;
const ImU32 kGold = IM_COL32(184, 120, 0, 255);  // title, top lines
// the game's tints of its white lettering: rows not chosen, the chosen row's name, its value, all of it at a flash
const ImU32 kLabel = IM_COL32(97, 97, 97, 255);
const ImU32 kChosen = IM_COL32(42, 192, 5, 255);
const ImU32 kValueChosen = IM_COL32(186, 121, 0, 255);
const ImU32 kFlash = IM_COL32(186, 186, 186, 255);
const ImU32 kTitleTint = IM_COL32(184, 184, 184, 255);  // of the orange title lettering
const ImU32 kArrow = IM_COL32(184, 56, 0, 255);
const ImU32 kHelp = IM_COL32(200, 133, 0, 255);  // the game's help line at its letters' middle, 1080p capture
const ImU32 kLine = IM_COL32(150, 150, 150, 255);
const ImU32 kDiamond = IM_COL32(19, 19, 19, 255);

ImFont* g_heavy = nullptr;  // Arial Black, slanted when drawn: the menu lettering
ImFont* g_bold = nullptr;  // Arial Bold: the counter, the help line without g_help
// The game's help lines are sprites (Options.xpr) of Arial Bold rasterised small, capitals 15 texels high, with a
// one texel dark edge, drawn magnified (filtered) 2.99 x 2.14 screen pixels a texel at 1080p: Arial Bold at that size,
// drawn the same way, any line comes out as theirs
ImFont* g_help = nullptr;
constexpr float kHelpPx = 23.4f;                       // the font's size in texels
constexpr float kHelpTexelX = 1.68f, kHelpTexelY = 1.60f;  // frame units a texel, before the widescreen widening
constexpr float kFontPx = 64;

// --- the game's lettering ------------------------------------------------------------------------------------------
// The game's menu rows and page titles are sprites of whole words in data\screen\*.xpr (DXT textures): the rows
// white letters with a black edge, tinted grey or green as the game draws them, the titles orange. Each letter here
// is cut out of one of those words, along the slant of the lettering, into ImGui's font atlas (g_game, g_title);
// the cuts were measured on the US files, whose sizes are checked. Letters from the smaller words are scaled to the
// rows' height. Without the files (the first launch, before the game has installed them to Z:) the page falls back
// to Arial Black.

ImFont* g_game = nullptr;
ImFont* g_title = nullptr;
constexpr float kCell = 30;      // a sprite's height in texels, the fonts' size
constexpr float kTexel = 1.53f;  // a texel of a page's sprite in the 1440x810 frame, as the game scales its pages

struct Sheet {
    const char* file;
    uint32_t size;
    int texture;  // among the file's textures
};
const Sheet kSheets[] = {{"Options.xpr", 665600, 0},           {"MainMenu.xpr", 598016, 0}, {"MissionSelect.xpr", 770048, 0},
                         {"ControllerSetting.xpr", 792576, 1}, {"System.xpr", 415744, 0},   {"LoadGame.xpr", 870400, 2},
                         {"LoadGame.xpr", 870400, 1}};

// a letter: its sheet, the word's rows, its slanted bounds at the bottom row, where it starts and how far it moves
// the pen (both from the bright middle of its strokes), and its scale to the rows' height
struct Cut {
    char ch;
    uint8_t sheet;
    uint16_t y0, y1;
    float left, right, origin, advance, scale;
};
const Cut kRowCuts[] = {
    {'-', 3, 127, 144, 62.0f, 73.5f, 62.2f, 11.6f, 1.0000f},
    {'0', 4, 120, 135, -2.0f, 15.0f, -0.2f, 13.5f, 1.2667f},
    {'1', 4, 120, 135, 15.0f, 28.5f, 16.8f, 9.5f, 1.2667f},
    {'2', 4, 120, 135, 28.5f, 46.5f, 30.8f, 14.5f, 1.2667f},
    {'3', 4, 120, 135, 46.5f, 63.0f, 47.8f, 13.5f, 1.2667f},
    {'4', 4, 120, 135, 63.0f, 79.0f, 64.8f, 13.5f, 1.2667f},
    {'5', 4, 120, 135, 79.0f, 94.5f, 79.8f, 13.5f, 1.2667f},
    {'6', 4, 120, 135, 94.5f, 110.0f, 95.8f, 12.5f, 1.2667f},
    {'7', 4, 120, 135, 110.0f, 126.0f, 111.8f, 12.5f, 1.2667f},
    {'8', 4, 120, 135, 126.0f, 142.5f, 127.8f, 13.5f, 1.2667f},
    {'9', 4, 120, 135, 142.5f, 160.5f, 143.8f, 13.5f, 1.2667f},
    {':', 0, 7, 23, 338.0f, 350.0f, 340.0f, 6.0f, 1.0000f},
    {'A', 0, 37, 56, 261.5f, 277.5f, 261.2f, 16.6f, 1.0000f},
    {'B', 1, 187, 206, 203.0f, 222.0f, 205.2f, 16.6f, 1.0000f},
    {'C', 0, 66, 85, 177.0f, 197.0f, 177.2f, 15.6f, 1.0000f},
    {'D', 0, 37, 56, 201.5f, 217.5f, 201.2f, 16.6f, 1.0000f},
    {'E', 0, 37, 56, 217.5f, 238.0f, 217.2f, 16.6f, 1.0000f},
    {'F', 0, 336, 355, 168.5f, 184.5f, 168.2f, 16.6f, 1.0000f},
    {'G', 0, 37, 56, 162.0f, 180.0f, 164.2f, 15.6f, 1.0000f},
    {'H', 0, 276, 295, 0.0f, 19.0f, 2.2f, 16.6f, 1.0000f},
    {'I', 0, 37, 56, 196.0f, 201.5f, 196.2f, 5.6f, 1.0000f},
    {'K', 5, 105, 120, 299.5f, 313.0f, 299.2f, 13.6f, 1.2667f},
    {'L', 0, 66, 85, 232.0f, 242.0f, 232.2f, 9.6f, 1.0000f},
    {'M', 0, 37, 56, 240.0f, 261.5f, 242.2f, 19.6f, 1.0000f},
    {'N', 0, 126, 145, 123.5f, 139.5f, 123.2f, 16.6f, 1.0000f},
    {'O', 0, 66, 85, 216.5f, 232.0f, 216.2f, 15.6f, 1.0000f},
    {'P', 0, 37, 56, 277.5f, 298.0f, 277.2f, 16.6f, 1.0000f},
    {'R', 0, 186, 205, 35.0f, 53.0f, 37.2f, 15.6f, 1.0000f},
    {'S', 0, 66, 85, 155.5f, 171.5f, 155.2f, 16.6f, 1.0000f},
    {'T', 0, 186, 205, 100.5f, 118.0f, 100.2f, 13.6f, 1.0000f},
    {'U', 0, 37, 56, 180.0f, 196.0f, 180.2f, 15.6f, 1.0000f},
    {'V', 0, 66, 85, 200.0f, 216.5f, 202.2f, 14.6f, 1.0000f},
    {'W', 2, 163, 178, 28.0f, 52.0f, 30.2f, 21.6f, 1.2667f},
    {'X', 0, 246, 265, 139.5f, 155.0f, 139.2f, 15.6f, 1.0000f},
    {'Y', 1, 37, 56, 125.0f, 146.0f, 125.2f, 16.6f, 1.0000f},
};
const Cut kTitleCuts[] = {
    {'A', 6, 215, 236, 82.5f, 105.0f, 82.6f, 22.8f, 1.0f},
    {'C', 3, 425, 446, -2.0f, 21.0f, -0.4f, 21.8f, 1.0f},
    {'D', 6, 215, 236, 154.0f, 179.0f, 158.6f, 20.8f, 1.0f},
    {'E', 3, 425, 446, 167.0f, 186.5f, 166.6f, 19.8f, 1.0f},
    {'G', 3, 425, 446, 329.0f, 354.0f, 329.6f, 21.8f, 1.0f},
    {'H', 6, 245, 265, 162.5f, 184.5f, 162.6f, 21.8f, 1.0f},
    {'I', 3, 425, 446, 296.5f, 306.0f, 296.6f, 8.8f, 1.0f},
    {'L', 3, 425, 446, 129.5f, 148.5f, 129.6f, 18.8f, 1.0f},
    {'M', 1, 5, 26, 57.0f, 83.5f, 58.6f, 24.8f, 1.0f},
    {'N', 3, 425, 446, 43.5f, 65.5f, 43.6f, 21.8f, 1.0f},
    {'O', 3, 425, 446, 21.0f, 43.5f, 20.6f, 22.8f, 1.0f},
    {'P', 0, 5, 26, 22.5f, 42.0f, 22.6f, 19.8f, 1.0f},
    {'R', 3, 425, 446, 86.5f, 107.5f, 86.6f, 21.8f, 1.0f},
    {'S', 3, 425, 446, 212.5f, 236.5f, 216.6f, 19.8f, 1.0f},
    {'T', 3, 425, 446, 65.5f, 86.5f, 65.6f, 20.8f, 1.0f},
    {'Y', 6, 245, 265, 113.0f, 135.0f, 112.6f, 19.8f, 1.0f},
};

// a lettering: its letters, their slant (x moves this much to the right a row up), their rows within the cell
struct Face {
    const Cut* cuts;
    size_t count;
    float slant, top, band, space;
};
const Face kRows = {kRowCuts, std::size(kRowCuts), 0.18f, 7, 19, 8.4f};
const Face kTitles = {kTitleCuts, std::size(kTitleCuts), 0.2f, 5, 21, 8.2f};

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;  // straight alpha
};

void Dxt(const uint8_t* src, size_t len, int w, int h, bool dxt5, Image& img) {
    img.w = w;
    img.h = h;
    img.rgba.assign(size_t(w) * h * 4, 0);
    size_t block = dxt5 ? 16 : 8;
    if (len < size_t(w / 4) * (h / 4) * block) return;
    auto rgb = [](uint16_t c, uint8_t* o) {
        o[0] = uint8_t(((c >> 11) & 31) * 255 / 31);
        o[1] = uint8_t(((c >> 5) & 63) * 255 / 63);
        o[2] = uint8_t((c & 31) * 255 / 31);
    };
    for (int by = 0; by < h / 4; by++)
        for (int bx = 0; bx < w / 4; bx++, src += block) {
            uint8_t alpha[16];
            std::fill(std::begin(alpha), std::end(alpha), uint8_t(255));
            if (dxt5) {
                uint8_t a[8] = {src[0], src[1]};
                for (int i = 0; i < 6; i++)
                    a[2 + i] = src[0] > src[1] ? uint8_t(((6 - i) * src[0] + (i + 1) * src[1]) / 7)
                               : i < 4         ? uint8_t(((4 - i) * src[0] + (i + 1) * src[1]) / 5)
                                               : uint8_t(i == 4 ? 0 : 255);
                uint64_t bits = 0;
                for (int i = 0; i < 6; i++) bits |= uint64_t(src[2 + i]) << (8 * i);
                for (int i = 0; i < 16; i++) alpha[i] = a[(bits >> (3 * i)) & 7];
            }
            const uint8_t* c = src + block - 8;
            uint16_t c0 = uint16_t(c[0] | c[1] << 8), c1 = uint16_t(c[2] | c[3] << 8);
            uint32_t idx = uint32_t(c[4] | c[5] << 8 | c[6] << 16 | uint32_t(c[7]) << 24);
            uint8_t p[4][3] = {};
            rgb(c0, p[0]);
            rgb(c1, p[1]);
            bool four = c0 > c1 || dxt5;
            for (int k = 0; k < 3; k++) {
                p[2][k] = uint8_t(four ? (2 * p[0][k] + p[1][k]) / 3 : (p[0][k] + p[1][k]) / 2);
                p[3][k] = uint8_t(four ? (p[0][k] + 2 * p[1][k]) / 3 : 0);
            }
            for (int i = 0; i < 16; i++) {
                int k = (idx >> (2 * i)) & 3;
                uint8_t* o = &img.rgba[((size_t(by) * 4 + i / 4) * w + bx * 4 + i % 4) * 4];
                o[0] = p[k][0];
                o[1] = p[k][1];
                o[2] = p[k][2];
                o[3] = !four && k == 3 ? 0 : alpha[i];
            }
        }
}

// an xpr's texture: its resource headers are found by their D3DCOMMON (a texture, one reference)
bool LoadSheet(const std::filesystem::path& dir, const Sheet& s, Image& img) {
    FILE* file = _wfopen((dir / s.file).c_str(), L"rb");
    if (!file) return false;
    std::vector<uint8_t> d(s.size + 1);
    size_t got = std::fread(d.data(), 1, d.size(), file);
    std::fclose(file);
    if (got != s.size) return false;
    auto u32 = [&](size_t o) { return uint32_t(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24); };
    uint32_t header = u32(8);
    if (header >= s.size) return false;
    int n = 0;
    for (size_t o = 12; o + 20 <= header; o += 4) {
        if (u32(o) != 0x00040001) continue;
        if (n++ != s.texture) continue;
        uint32_t data = u32(o + 4), format = u32(o + 12);
        uint32_t code = (format >> 8) & 0xFF;
        int w = 1 << ((format >> 20) & 15), h = 1 << ((format >> 24) & 15);
        if ((code != 0x0C && code != 0x0F) || size_t(header) + data >= s.size) return false;
        Dxt(&d[size_t(header) + data], s.size - header - data, w, h, code == 0x0F, img);
        return true;
    }
    return false;
}

// the letter as it goes into the atlas: its own pixels of the word, scaled
Image CutOut(const Image& sheet, const Cut& c, float slant) {
    int rows = c.y1 - c.y0, x0 = int(std::floor(c.left));
    float width = c.right - c.left + rows * slant + 2;
    Image img;
    img.w = int(std::ceil(width * c.scale));
    img.h = int(std::ceil(rows * c.scale));
    img.rgba.assign(size_t(img.w) * img.h * 4, 0);
    auto add = [&](int x, int y, float weight, float* acc) {  // premultiplied, nothing outside the letter
        if (x < 0 || x >= sheet.w || y < c.y0 || y >= c.y1) return;
        float slanted = x - (c.y1 - 1 - y) * slant;
        if (slanted < c.left || slanted >= c.right) return;
        const uint8_t* p = &sheet.rgba[(size_t(y) * sheet.w + x) * 4];
        float a = p[3] / 255.0f * weight;
        for (int k = 0; k < 3; k++) acc[k] += p[k] * a;
        acc[3] += a;
    };
    for (int ty = 0; ty < img.h; ty++)
        for (int tx = 0; tx < img.w; tx++) {
            float sx = x0 + (tx + 0.5f) / c.scale - 0.5f, sy = c.y0 + (ty + 0.5f) / c.scale - 0.5f;
            int ix = int(std::floor(sx)), iy = int(std::floor(sy));
            float fx = sx - ix, fy = sy - iy;
            float acc[4] = {};
            add(ix, iy, (1 - fx) * (1 - fy), acc);
            add(ix + 1, iy, fx * (1 - fy), acc);
            add(ix, iy + 1, (1 - fx) * fy, acc);
            add(ix + 1, iy + 1, fx * fy, acc);
            if (acc[3] <= 0) continue;
            uint8_t* o = &img.rgba[(size_t(ty) * img.w + tx) * 4];
            for (int k = 0; k < 3; k++) o[k] = uint8_t(std::clamp(acc[k] / acc[3], 0.0f, 255.0f));
            o[3] = uint8_t(std::clamp(acc[3] * 255, 0.0f, 255.0f));
        }
    return img;
}

// The A OK and B BACK under a page: the game's own sprites (Options.xpr's first texture), a texel of margin round
// each, and where the OPTIONS page shows them (measured at 1080p): the left of their pixels in the page's units,
// their bottom in the frame's
struct Sprite {
    int x0, y0, x1, y1;  // in the sheet, the margin included
    float page_x;        // the left of the pixels in the page's 640 units
};
constexpr Sprite kButtons[] = {{2, 422, 60, 448, 24.8f}, {2, 452, 86, 478, 96.5f}};
constexpr int kButtonSplit = 28;          // the sheet's column between a button and its word
constexpr float kSpriteTexel = 1.46f;     // frame units a texel, before the widescreen widening
constexpr float kButtonsBottom = 719.7f;  // in the frame
int g_button_rects[2] = {-1, -1};
// the page's background (its Bg element): a dark see-through frame, bars and row strips over the whole 640x480 page
constexpr int kBgX = 384, kBgY = 0, kBgW = 640, kBgH = 480;
int g_bg_rect = -1;

// before the atlas is built: the letters as glyphs of host (an Arial Black, for anything else); after: their pixels
struct Placed {
    int rect;
    Image img;
};
std::vector<Placed> g_placed;

// the two faces over hosts (Arial Blacks of the cell's size, for anything else), all or nothing
void AddLettering(ImGuiIO& io, ImFont* rows, ImFont* titles) {
    int instance = app::Instance();
    std::filesystem::path dir = app::ExeDir() / (instance > 1 ? "hdd" + std::to_string(instance) : std::string("hdd")) /
                                "Z" / "data" / "screen";
    std::vector<Image> sheets(std::size(kSheets));
    for (size_t i = 0; i < std::size(kSheets); i++)
        if (!LoadSheet(dir, kSheets[i], sheets[i])) return;
    for (auto [face, host] : {std::pair{&kRows, rows}, std::pair{&kTitles, titles}}) {
        for (const Cut* c = face->cuts; c != face->cuts + face->count; c++) {
            Image img = CutOut(sheets[c->sheet], *c, face->slant);
            float top = face->top + (face->band - (c->y1 - c->y0) * c->scale) / 2;
            int rect = io.Fonts->AddCustomRectFontGlyph(host, ImWchar(c->ch), img.w, img.h, c->advance * c->scale,
                                                        {(std::floor(c->left) - c->origin) * c->scale, top});
            g_placed.push_back({rect, std::move(img)});
        }
        Image space;
        space.w = space.h = 1;
        space.rgba.assign(4, 0);
        g_placed.push_back({io.Fonts->AddCustomRectFontGlyph(host, ' ', 1, 1, face->space), std::move(space)});
    }
    g_game = rows;
    g_title = titles;
    for (int i = 0; i < 2; i++) {
        const Sprite& b = kButtons[i];
        Image img;
        img.w = b.x1 - b.x0;
        img.h = b.y1 - b.y0;
        img.rgba.resize(size_t(img.w) * img.h * 4);
        for (int y = 0; y < img.h; y++)
            std::memcpy(&img.rgba[size_t(y) * img.w * 4], &sheets[0].rgba[(size_t(b.y0 + y) * sheets[0].w + b.x0) * 4],
                        size_t(img.w) * 4);
        g_button_rects[i] = io.Fonts->AddCustomRectRegular(img.w, img.h);
        g_placed.push_back({g_button_rects[i], std::move(img)});
    }
    Image bg;
    bg.w = kBgW;
    bg.h = kBgH;
    bg.rgba.resize(size_t(kBgW) * kBgH * 4);
    for (int y = 0; y < kBgH; y++)
        std::memcpy(&bg.rgba[size_t(y) * kBgW * 4], &sheets[0].rgba[(size_t(kBgY + y) * sheets[0].w + kBgX) * 4],
                    size_t(kBgW) * 4);
    g_bg_rect = io.Fonts->AddCustomRectRegular(kBgW, kBgH);
    g_placed.push_back({g_bg_rect, std::move(bg)});
}

void PlaceLettering(ImGuiIO& io) {
    if (g_placed.empty()) return;
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    for (const Placed& p : g_placed) {
        const ImFontAtlasCustomRect* r = io.Fonts->GetCustomRectByIndex(p.rect);
        for (int y = 0; y < p.img.h; y++)
            std::memcpy(pixels + ((size_t(r->Y) + y) * w + r->X) * 4, &p.img.rgba[size_t(y) * p.img.w * 4],
                        size_t(p.img.w) * 4);
    }
    g_placed.clear();
}

struct Frame {
    ImVec2 o;
    float s;
    ImVec2 P(float x, float y) const { return {o.x + x * s, o.y + y * s}; }
};

Frame Fit() {
    ImVec2 d = ImGui::GetIO().DisplaySize;
    float s = std::min(d.x / kW, d.y / kH);
    Frame f{{(d.x - kW * s) / 2, (d.y - kH * s) / 2}, s};
    // the game's frame with its overscan border left off (app::ShownRect): the same part enlarged
    float uv[4];
    const uint32_t* a = app::kGame.safe_area;
    app::ShownRect(a[4], a[5], uv);
    if (uv[2] < 1) {
        float k = 1 / uv[2];
        f.o = {f.o.x - uv[0] * kW * s * k, f.o.y - uv[1] * kH * s * k};
        f.s = s * k;
    }
    return f;
}

enum Align { kStart, kCentre, kEnd };

ImU32 Fade(ImU32 c, float a) { return (c & 0x00FFFFFF) | (ImU32(((c >> 24) & 0xFF) * a) << 24); }

// the game draws its 4:3 pages widened on a 16:9 screen, its lettering with them
float Widen() { return app::Widescreen() ? 4.0f / 3 : 1.0f; }

// text at (x, y) = its left, centre or right edge and its vertical middle; slanted like the game's lettering
void Text(ImDrawList* dl, const Frame& f, ImFont* font, float px, float x, float y, ImU32 colour, const std::string& s,
          Align align = kStart, bool slant = true, float widen = 1) {
    if (!font) font = ImGui::GetFont();
    float size = px * f.s;
    ImVec2 ext = font->CalcTextSizeA(size, FLT_MAX, 0, s.c_str());
    ext.x *= widen;
    ImVec2 at = f.P(x, y);
    at.y -= ext.y / 2;
    if (align == kCentre) at.x -= ext.x / 2;
    if (align == kEnd) at.x -= ext.x;
    int first = dl->VtxBuffer.Size;
    // a dark edge under the letters, as the game's have
    dl->AddText(font, size, {at.x + 2 * f.s, at.y + 2 * f.s}, IM_COL32(0, 0, 0, 200), s.c_str());
    dl->AddText(font, size, at, colour, s.c_str());
    float base = at.y + ext.y;
    for (int i = first; i < dl->VtxBuffer.Size; i++) {
        ImVec2& p = dl->VtxBuffer[i].pos;
        p.x = at.x + (p.x - at.x) * widen + (slant ? (base - p.y) * 0.22f : 0);
    }
}

double Frames(Clock::time_point since);
float Widen();

// The game's timelines (the page's description: STEAL_PAGE_DUMP) move an element from one place to another over
// some frames, eased as a cube (measured on the OPTIONS title, frame by frame), and fade it from nothing to its
// colour, alpha and colour both, so its light grows as the square of the time
float Left(double frames, double n) {  // what is left of the way
    double t = 1 - std::clamp(frames / n, 0.0, 1.0);
    return float(t * t * t);
}
float FadeIn(double frames, double n) {
    double t = std::clamp(frames / n, 0.0, 1.0);
    return float(t * t);
}
float PageUnit() { return 1.53f * Widen(); }  // frame units of the page's 640 across

// A new help line comes in as the game's (the OPTIONS page's timelines): over 15 frames it slides 100 of the
// page's units from the left, eased as the game eases its moves (Left), and brightens from nothing. A line goes
// again once it was not drawn for a while (its row left).
float HelpIn(const std::string& s, float& slide) {
    static std::string last;
    static Clock::time_point since, drawn;
    auto now = Clock::now();
    if (s != last || now - drawn > std::chrono::milliseconds(100)) last = s, since = now;
    drawn = now;
    double t = Frames(since);
    slide = -100 * PageUnit() * Left(t, 15);
    return float(std::min(1.0, t / 15));
}

// the help line: the game's Arial Bold in orange with a black edge all round
void Help(ImDrawList* dl, const Frame& f, const std::string& s, float alpha = 1) {
    float slide;
    alpha *= HelpIn(s, slide);
    ImFont* font = g_help ? g_help : g_bold ? g_bold : ImGui::GetFont();
    float sy = kHelpTexelY * f.s, sx = kHelpTexelX * Widen() * f.s;
    float size = kHelpPx * sy, widen = sx / sy;
    ImVec2 ext = font->CalcTextSizeA(size, FLT_MAX, 0, s.c_str());
    // a line longer than the sprites' 512 texels is squeezed into them, as the game's longest
    constexpr float kHelpTexels = 500;
    widen = std::min(widen, kHelpTexels * sx / std::max(ext.x, 1.0f));
    ImVec2 at = f.P(720 + slide, 655);
    at = {at.x - ext.x * widen / 2, at.y - ext.y / 2};
    int first = dl->VtxBuffer.Size;
    // the edge a texel around (x offsets before the widening)
    const ImVec2 edge[] = {{-sy, 0}, {sy, 0}, {0, -sy}, {0, sy}, {-sy, -sy}, {sy, -sy}, {-sy, sy}, {sy, sy}};
    for (ImVec2 o : edge) dl->AddText(font, size, {at.x + o.x, at.y + o.y}, Fade(IM_COL32(0, 0, 0, 255), alpha), s.c_str());
    dl->AddText(font, size, at, Fade(kHelp, alpha), s.c_str());
    for (int i = first; i < dl->VtxBuffer.Size; i++) dl->VtxBuffer[i].pos.x = at.x + (dl->VtxBuffer[i].pos.x - at.x) * widen;
}

// a row's words in the game's own lettering at (x, y) = its left, centre or right edge and the middle of its letters
// (Arial Black slanted without it); returns its width in the frame's units
// fit: the widest it may be in the frame's units (0: any), squeezed across past it
float Lettering(ImDrawList* dl, const Frame& f, float x, float y, ImU32 colour, const std::string& s, Align align = kStart,
                float scale = 1, float fit = 0) {
    if (!g_game) {
        float w = (g_heavy ? g_heavy : ImGui::GetFont())->CalcTextSizeA(38 * scale * f.s, FLT_MAX, 0, s.c_str()).x / f.s;
        if (dl) Text(dl, f, g_heavy, 38 * scale, x, y, colour, s, align);
        return w;
    }
    float size = kCell * kTexel * scale * f.s, widen = Widen();
    float raw = g_game->CalcTextSizeA(size, FLT_MAX, 0, s.c_str()).x;
    if (fit > 0 && raw * widen > fit * f.s) widen = fit * f.s / raw;
    float w = raw * widen;
    if (!dl) return w / f.s;
    ImVec2 at = f.P(x, y);
    at.y -= (kRows.top + kRows.band / 2) * kTexel * scale * f.s;
    if (align == kCentre) at.x -= w / 2;
    if (align == kEnd) at.x -= w;
    int first = dl->VtxBuffer.Size;
    dl->AddText(g_game, size, at, colour, s.c_str());
    for (int i = first; i < dl->VtxBuffer.Size; i++) dl->VtxBuffer[i].pos.x = at.x + (dl->VtxBuffer[i].pos.x - at.x) * widen;
    return w / f.s;
}

// a page's title, the game's orange lettering at its left, (x, y) its left edge and middle
void Title(ImDrawList* dl, const Frame& f, float x, float y, const std::string& s, float alpha = 1) {
    if (!g_title) {
        Text(dl, f, g_heavy, 54, x, y, Fade(kGold, alpha), s, kStart, true, Widen());
        return;
    }
    float size = kCell * kTexel * f.s, widen = Widen();
    ImVec2 at = f.P(x, y);
    at.y -= (kTitles.top + kTitles.band / 2) * kTexel * f.s;
    int first = dl->VtxBuffer.Size;
    dl->AddText(g_title, size, at, Fade(kTitleTint, alpha), s.c_str());
    for (int i = first; i < dl->VtxBuffer.Size; i++) dl->VtxBuffer[i].pos.x = at.x + (dl->VtxBuffer[i].pos.x - at.x) * widen;
}

void Polyline(ImDrawList* dl, const Frame& f, std::initializer_list<ImVec2> pts, ImU32 colour, float thick) {
    std::vector<ImVec2> p;
    for (ImVec2 v : pts) p.push_back(f.P(v.x, v.y));
    dl->AddPolyline(p.data(), int(p.size()), colour, 0, thick * f.s);
}

// the game's orange pointer, a triangle facing left (dir -1) or right (+1)
void Pointer(ImDrawList* dl, const Frame& f, float x, float y, int dir, float alpha = 1) {
    float w = 13 * dir, h = 10;
    dl->AddTriangleFilled(f.P(x - w, y - h), f.P(x + w, y), f.P(x - w, y + h), Fade(kArrow, alpha));
}

// a list's arrow above (dir -1) or below (1) its rows while there is more of it that way
void Arrow(ImDrawList* dl, const Frame& f, float x, float y, int dir, float alpha = 1) {
    float w = 13, h = 10 * dir;
    dl->AddTriangleFilled(f.P(x - w, y - h), f.P(x + w, y - h), f.P(x, y + h), Fade(kArrow, alpha));
}

// --- the game's animations, measured on its OPTIONS page (30 frames a second) --------------------------------------

double Frames(Clock::time_point since) { return std::chrono::duration<double>(Clock::now() - since).count() * 30; }

// A page coming up (its first timeline): row i (from 0) comes in over 5 (i + 1) frames, from the left and from
// nothing to grey; a row of name, colon and value opens out, the name moved 50 of the page's units, the colon 100,
// the value 150; a row of one word moves 100. The title moves 80 over 20 frames and brightens over 12; the A and B
// buttons move 65 and 205 over 15.
double RowFrames(int row) { return 5.0 * (row + 1); }

// the chosen row flashes white every 22 frames, from its choosing, and fades back over 14
float Flash(double frames) { return std::max(0.0f, 0.95f - float(std::floor(std::fmod(frames, 22))) * 0.068f); }

// at each flash the arrows beside the chosen row jump toward it and spring back
float Spring(double frames) { return 20 * std::pow(0.5f, float(std::floor(std::fmod(frames, 22)))); }

ImU32 Mix(ImU32 a, ImU32 b, float t) {
    ImU32 out = a & 0xFF000000;
    for (int k = 0; k < 24; k += 8) {
        float ca = float((a >> k) & 0xFF), cb = float((b >> k) & 0xFF);
        out |= ImU32(ca + (cb - ca) * t + 0.5f) << k;
    }
    return out;
}

Clock::time_point g_shown_at = Clock::now();   // the page or section came up
Clock::time_point g_chosen_at = Clock::now();  // the chosen row was chosen

// the game's OPTIONS page was up within the last few frames
std::atomic<int64_t> g_options_seen{0};
bool OptionsUp() {
    int64_t now = Clock::now().time_since_epoch().count();
    return now - g_options_seen.load() < std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(150)).count();
}

// the RECOMP SETTINGS row in the game's OPTIONS page (hooks.cpp moves the game's rows to make room)
std::atomic<int> g_row_y{0};
std::atomic<bool> g_row_chosen{false};
std::atomic<float> g_row_alpha{0};
std::atomic<int64_t> g_row_shown_at{0}, g_row_chosen_at{0};
std::atomic<int64_t> g_row_seen{0};  // Clock ticks: the OPTIONS page was last drawn, settled or leaving
std::atomic<bool> g_replay{false};   // the OPTIONS page to come in again (hooks.cpp runs its first timeline)
bool RowUp() {
    int64_t now = Clock::now().time_since_epoch().count();
    return now - g_row_seen.load() < std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(150)).count();
}  // Clock ticks: the OPTIONS page came up, the row was chosen

void Button(ImDrawList* dl, const Frame& f, float x, ImU32 colour, const char* letter, const char* name) {
    dl->AddCircleFilled(f.P(x, 704), 17 * f.s, colour);
    dl->AddCircle(f.P(x, 704), 17 * f.s, IM_COL32(255, 255, 255, 90), 0, 1.5f * f.s);
    Text(dl, f, g_heavy, 26, x, 704, IM_COL32(255, 255, 255, 230), letter, kCentre, false);
    Lettering(dl, f, x + 30, 704, IM_COL32(230, 230, 230, 255), name, kStart, 0.8f);
}

// opened from the game's OPTIONS page: drawn over that page's own background, frame and buttons (hooks.cpp hides
// its rows and title meanwhile)
std::atomic<bool> g_over_game{false};

void Chrome(ImDrawList* dl, const Frame& f, const std::string& title, const std::string& help, double shown) {
    float slide = -80 * PageUnit() * Left(shown, 20), in = FadeIn(shown, 12);
    if (g_over_game) {
        Title(dl, f, 152 + slide, 105, title, in);
        Help(dl, f, help);
        return;
    }
    ImVec2 d = ImGui::GetIO().DisplaySize;
    dl->AddRectFilled({0, 0}, d, IM_COL32(0, 0, 0, 255));
    if (g_bg_rect >= 0) {
        // the game's own background over black, on the page as the game places it
        ImFontAtlas* atlas = ImGui::GetIO().Fonts;
        ImVec2 uv0, uv1;
        atlas->CalcCustomRectUV(atlas->GetCustomRectByIndex(g_bg_rect), &uv0, &uv1);
        constexpr float kBgTop = 177 - 77 * 1.53f - 21.8f;  // its lines where the game's are at 1080p
        dl->AddImage(atlas->TexID, f.P(720 - 320 * PageUnit(), kBgTop), f.P(720 + 320 * PageUnit(), kBgTop + 480 * 1.53f),
                     uv0, uv1);
        Title(dl, f, 152 + slide, 105, title, in);
        Help(dl, f, help);
    } else {
    // the diamond weave behind the rows
    for (int r = 0; r < 10; r++)
        for (int c = 0; c < 15; c++) {
            float cx = 245 + c * 68 + (r & 1) * 34, cy = 165 + r * 46;
            dl->AddQuadFilled(f.P(cx, cy - 22), f.P(cx + 30, cy), f.P(cx, cy + 22), f.P(cx - 30, cy), kDiamond);
        }
    Polyline(dl, f, {{66, 83}, {1228, 83}, {1276, 47}, {1372, 47}}, kGold, 3);
    Polyline(dl, f, {{66, 128}, {1250, 128}, {1298, 92}, {1372, 92}}, kGold, 3);
    Title(dl, f, 152 + slide, 105, title, in);
    Polyline(dl, f, {{66, 655}, {125, 655}, {155, 632}, {1285, 632}, {1315, 655}, {1372, 655}}, kLine, 2.5f);
    Help(dl, f, help);
    Polyline(dl, f, {{66, 678}, {1372, 678}}, kLine, 2.5f);
    Polyline(dl, f, {{66, 730}, {1372, 730}}, kLine, 2.5f);
    }
    if (g_button_rects[0] >= 0) {
        ImFontAtlas* atlas = ImGui::GetIO().Fonts;
        float sy = kSpriteTexel, sx = kSpriteTexel * Widen();
        for (int i = 0; i < 2; i++) {
            const Sprite& b = kButtons[i];
            const ImFontAtlasCustomRect* r = atlas->GetCustomRectByIndex(g_button_rects[i]);
            ImVec2 uv0, uv1;
            atlas->CalcCustomRectUV(r, &uv0, &uv1);
            float left = 720 + (b.page_x - 320 - (i ? 205 : 65) * Left(shown, 15)) * sx - sx;
            float bottom = kButtonsBottom + sy, top = bottom - (b.y1 - b.y0) * sy;
            // the button, then its word, each as dim as the game shows them (the word dimmer, 1080p capture)
            float split = float(kButtonSplit - b.x0), w = float(b.x1 - b.x0);
            float uvx = uv0.x + (uv1.x - uv0.x) * split / w;
            float in = FadeIn(shown, 15);
            dl->AddImage(atlas->TexID, f.P(left, top), f.P(left + split * sx, bottom), uv0, {uvx, uv1.y},
                         Fade(IM_COL32(220, 220, 220, 255), in));
            dl->AddImage(atlas->TexID, f.P(left + split * sx, top), f.P(left + w * sx, bottom), {uvx, uv0.y}, uv1,
                         Fade(IM_COL32(191, 191, 191, 255), in));
        }
    } else {
        Button(dl, f, 167, IM_COL32(62, 166, 40, 255), "A", "OK");
        Button(dl, f, 309, IM_COL32(178, 34, 34, 255), "B", "BACK");
    }
}

// --- what the page holds -------------------------------------------------------------------------------------------

struct Row {
    const char* name;
    const char* help;
    std::string (*value)();
    bool (*step)(int dir);  // -1 or +1; false when the value is already at that end
    bool keys = false;  // a control's keys: OK waits for one
};

struct Display {
    std::string device;  // \.\DISPLAY1
    int number;          // as Windows numbers it
    app::Mode desktop;
};

// the connected monitors, by their numbers
std::vector<Display> Displays() {
    std::vector<Display> out;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR mon, HDC, LPRECT, LPARAM list) -> BOOL {
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (!GetMonitorInfoW(mon, &mi)) return TRUE;
        if (!EnumDisplaySettingsW(mi.szDevice, ENUM_REGISTRY_SETTINGS, &dm) || !dm.dmPelsWidth)
            EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm);
        Display d{std::string(), 0, {dm.dmPelsWidth, dm.dmPelsHeight}};
        for (const wchar_t* c = mi.szDevice; *c; c++) d.device += char(*c);
        size_t digits = d.device.find_last_not_of("0123456789") + 1;
        d.number = digits < d.device.size() ? std::atoi(d.device.c_str() + digits) : 0;
        reinterpret_cast<std::vector<Display>*>(list)->push_back(d);
        return TRUE;
    }, LPARAM(&out));
    std::sort(out.begin(), out.end(), [](const Display& a, const Display& b) { return a.number < b.number; });
    return out;
}

// the chosen monitor's modes, smallest first
std::vector<app::Mode>& Modes() {
    static std::string of = "?";
    static std::vector<app::Mode> modes;
    if (of != app::Get().monitor) {
        of = app::Get().monitor;
        modes.clear();
        std::wstring device(of.begin(), of.end());
        std::vector<app::Mode>& m = modes;
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        for (DWORD i = 0; EnumDisplaySettingsW(device.empty() ? nullptr : device.c_str(), i, &dm); i++) {
            if (dm.dmBitsPerPel != 32 || dm.dmPelsWidth < 640 || dm.dmPelsHeight < 480) continue;
            if (std::none_of(m.begin(), m.end(),
                             [&](const app::Mode& o) { return o.width == dm.dmPelsWidth && o.height == dm.dmPelsHeight; }))
                m.push_back({dm.dmPelsWidth, dm.dmPelsHeight});
        }
        std::sort(m.begin(), m.end(), [](const app::Mode& a, const app::Mode& b) {
            return a.width != b.width ? a.width < b.width : a.height < b.height;
        });
    }
    return modes;
}

template <class T, size_t N>
bool Cycle(T& v, const T (&list)[N], int dir) {
    int i = int(std::find(std::begin(list), std::end(list), v) - std::begin(list));
    if (i == int(N)) i = 0;
    int j = std::clamp(i + dir, 0, int(N) - 1);
    if (list[j] == v) return false;
    v = list[j];
    return true;
}

bool Flag(bool& v, int dir) {
    bool want = dir > 0;
    if (v == want) return false;
    v = want;
    return true;
}

std::string Size(uint32_t w, uint32_t h) { return std::to_string(w) + "X" + std::to_string(h); }

Clock::time_point g_apply_at{};  // a display change goes out once the choice rests
void ApplySoon() { g_apply_at = Clock::now() + std::chrono::milliseconds(700); }

const Row kDisplay[] = {
    {"MONITOR", "The monitor the game is shown on",
     [] {
         auto ds = Displays();
         for (const Display& d : ds)
             if (d.device == app::Get().monitor) return std::to_string(d.number) + " " + Size(d.desktop.width, d.desktop.height);
         return std::string("PRIMARY");
     },
     [](int dir) {
         auto ds = Displays();
         // PRIMARY first, then each monitor by number
         int i = 0;
         for (size_t k = 0; k < ds.size(); k++)
             if (ds[k].device == app::Get().monitor) i = int(k) + 1;
         int j = std::clamp(i + dir, 0, int(ds.size()));
         if (j == i) return false;
         app::Get().monitor = j ? ds[j - 1].device : std::string();
         app::Get().width = app::Get().height = 0;  // that monitor's desktop size
         ApplySoon();
         return true;
     }},
    {"DISPLAY MODE", "Fullscreen, a borderless window over the desktop or a window",
     [] {
         static const char* k[] = {"FULLSCREEN", "BORDERLESS", "WINDOWED"};
         return std::string(k[uint32_t(app::Get().display_mode)]);
     },
     [](int dir) {
         static const app::DisplayMode k[] = {app::DisplayMode::Fullscreen, app::DisplayMode::Borderless,
                                              app::DisplayMode::Windowed};
         bool moved = Cycle(app::Get().display_mode, k, dir);
         if (moved) ApplySoon();
         return moved;
     }},
    {"RESOLUTION", "The size of the picture on the screen",
     [] {
         const app::Settings& s = app::Get();
         return s.width ? Size(s.width, s.height) : "DESKTOP " + Size(app::Desktop().width, app::Desktop().height);
     },
     [](int dir) {
         app::Settings& s = app::Get();
         auto& m = Modes();
         // DESKTOP first, then the monitor's modes from the smallest
         int i = 0;
         for (size_t k = 0; k < m.size(); k++)
             if (m[k].width == s.width && m[k].height == s.height) i = int(k) + 1;
         int j = std::clamp(i + dir, 0, int(m.size()));
         if (j == i) return false;
         s.width = j ? m[j - 1].width : 0;
         s.height = j ? m[j - 1].height : 0;
         ApplySoon();
         return true;
     }},
    {"RENDER RESOLUTION", "The size the game is drawn at, against the screen's",
     [] {
         uint32_t p = app::Get().render_percent;
         app::Mode o = app::Output();
         return std::to_string(p) + "% " + Size(o.width * p / 100, o.height * p / 100);
     },
     [](int dir) {
         static const uint32_t k[] = {25, 50, 75, 100, 125, 150, 175, 200};
         return Cycle(app::Get().render_percent, k, dir);
     }},
    {"VERTICAL SYNC", "Waits for the screen's refresh: no tearing",
     [] { return std::string(app::Get().vsync ? "ON" : "OFF"); },
     [](int dir) { return Flag(app::Get().vsync, dir); }},
    {"FULL PICTURE", "Fills the screen, leaving off the border kept for old TVs",
     [] { return std::string(app::Get().full_picture ? "ON" : "OFF"); },
     [](int dir) { return Flag(app::Get().full_picture, dir); }},
    {"FRAME RATE", "60 shows every step of the game's logic; a slower PC keeps the game's speed",
     [] { return std::to_string(g_frame_rate) + (g_frame_rate < 60 ? " (ORIGINAL)" : ""); },
     [](int dir) {
         static const uint32_t k[] = {30, 60};
         return Cycle(g_frame_rate, k, dir);
     }},
    {"FPS COUNTER", "Shows the frame rate in the top left corner",
     [] { return std::string(g_fps_counter ? "ON" : "OFF"); },
     [](int dir) { return Flag(g_fps_counter, dir); }},
};

const Row kGraphics[] = {
    {"SMAA", "Smooths jagged edges over the whole picture",
     [] { return std::string(app::Get().smaa ? "ON" : "OFF"); },
     [](int dir) { return Flag(app::Get().smaa, dir); }},
    {"ANISOTROPIC FILTERING", "Sharper road and ground textures far away",
     [] {
         uint32_t a = app::Get().aniso;
         return a ? std::to_string(a) + "X" : std::string("OFF");
     },
     [](int dir) {
         static const uint32_t k[] = {0, 2, 4, 8, 16};
         return Cycle(app::Get().aniso, k, dir);
     }},
    {"SHADOW RESOLUTION", "Sharper shadows; AUTO follows the render resolution",
     [] {
         uint32_t r = shadows::g_resolution;
         return r ? std::to_string(r) + "X" + std::to_string(r) : std::string("AUTO");
     },
     [](int dir) {
         static const uint32_t k[] = {0, 512, 1024, 2048, 4096, 8192};
         return Cycle(shadows::g_resolution, k, dir);
     }},
    {"SHADOW FILTERING", "Soft shadow edges instead of hard ones",
     [] { return std::string(shadows::g_filter ? "ON" : "OFF"); },
     [](int dir) { return Flag(shadows::g_filter, dir); }},
    {"TEXTURE DETAIL", "Sharper textures far away; SHARPER can shimmer on fine patterns",
     [] {
         float b = app::Get().lod_bias;
         return std::string(b >= 0 ? "NORMAL" : b > -0.75f ? "SHARP" : "SHARPER");
     },
     [](int dir) {
         static const float k[] = {0.0f, -0.5f, -1.0f};
         return Cycle(app::Get().lod_bias, k, dir);
     }},
};

const Row kGeneral[] = {
    {"SKIP INTROS", "Leaves out the logo movie at start, from the next launch",
     [] { return std::string(g_skip_intros ? "ON" : "OFF"); },
     [](int dir) { return Flag(g_skip_intros, dir); }},
    {"PAUSE ON FOCUS LOSS", "The game and its sound stop while the window is in the background",
     [] { return std::string(app::Get().pause_on_focus_loss ? "ON" : "OFF"); },
     [](int dir) { return Flag(app::Get().pause_on_focus_loss, dir); }},
};

// KEYBOARD: whether the keyboard works controller 1, a row for each of the controller's controls with its keys, and
// the keys back as they were. On a control's row OK waits for a key and sets it, right waits for a second one and
// left takes the second off; Esc (or B) leaves the waiting.
// The game's menu sounds (its sound player's ids), played by the game's thread (hooks.cpp takes them each frame)
constexpr uint32_t kSoundCursor = 0x2C03, kSoundOk = 0x2001, kSoundBack = 0x2002, kSoundValue = 0x2003;
std::mutex g_sound_lock;
std::vector<uint32_t> g_sounds;
void Play(uint32_t id) {
    std::lock_guard lock(g_sound_lock);
    LOGI("ui sound %04X queued (%zu waiting)", id, g_sounds.size());
    if (g_sounds.size() < 8) g_sounds.push_back(id);
}

int g_capture = -1;       // the control waiting for a key
int g_capture_slot = 0;   // its first key or its second
Clock::time_point g_capture_at;

template <int C>
std::string Keys() {
    if (g_capture == C) return "PRESS A KEY";
    const auto& k = input::Keys()[C];
    std::string first = input::KeyName(k[0]), second = input::KeyName(k[1]);
    if (first.empty()) first.swap(second);
    if (first.empty()) return "NONE";
    return second.empty() ? first : first + ", " + second;
}
template <int C>
bool KeyStep(int dir) {
    if (dir < 0) {  // the second key off
        auto& k = input::Keys()[C];
        if (!k[1]) return false;
        k[1] = 0;
        return true;
    }
    g_capture = C;
    g_capture_slot = dir > 1 ? 0 : 1;  // OK (2) the first, right (1) the second
    g_capture_at = Clock::now();
    return false;
}
#define KEY_ROW(name, c) {name, "OK sets its key, right adds a second, left takes the second off", Keys<c>, KeyStep<c>, true}

const Row kKeyboard[] = {
    {"KEYBOARD", "The keyboard works controller 1 while the game window has the focus",
     [] { return std::string(app::Get().keyboard ? "ON" : "OFF"); },
     [](int dir) { return Flag(app::Get().keyboard, dir); }},
    KEY_ROW("LEFT STICK LEFT", 0),
    KEY_ROW("LEFT STICK RIGHT", 1),
    KEY_ROW("LEFT STICK UP", 2),
    KEY_ROW("LEFT STICK DOWN", 3),
    KEY_ROW("RIGHT TRIGGER", 4),
    KEY_ROW("LEFT TRIGGER", 5),
    KEY_ROW("A", 6),
    KEY_ROW("B", 7),
    KEY_ROW("X", 8),
    KEY_ROW("Y", 9),
    KEY_ROW("BLACK", 10),
    KEY_ROW("WHITE", 11),
    KEY_ROW("START", 12),
    KEY_ROW("BACK", 13),
    KEY_ROW("D-PAD UP", 14),
    KEY_ROW("D-PAD DOWN", 15),
    KEY_ROW("D-PAD LEFT", 16),
    KEY_ROW("D-PAD RIGHT", 17),
    KEY_ROW("RIGHT STICK UP", 18),
    KEY_ROW("RIGHT STICK DOWN", 19),
    KEY_ROW("RIGHT STICK LEFT", 20),
    KEY_ROW("RIGHT STICK RIGHT", 21),
    KEY_ROW("LEFT THUMB", 22),
    KEY_ROW("RIGHT THUMB", 23),
    {"RESET KEYS", "All the keys back as they were at first", [] { return std::string(); },
     [](int dir) {
         if (dir <= 0 || input::Keys() == input::DefaultKeys()) return false;
         input::Keys() = input::DefaultKeys();
         return true;
     }},
};
#undef KEY_ROW

struct Section {
    const char* name;
    const char* help;
    const Row* rows;
    int count;
};
const Section kSections[] = {
    {"DISPLAY", "Monitor, display mode, resolution, frame rate counter", kDisplay, int(std::size(kDisplay))},
    {"GRAPHICS", "Antialiasing, texture filtering", kGraphics, int(std::size(kGraphics))},
    {"CONTROLS", "The keys that work controller 1", kKeyboard, int(std::size(kKeyboard))},
    {"GENERAL", "Start up, background pause", kGeneral, int(std::size(kGeneral))},
};
constexpr int kSectionCount = int(std::size(kSections));

// all under UiLock: the window thread (keys), the input thread (pad), the render thread (drawing)
std::atomic<bool> g_open{false};
bool g_swallow = false;  // closed from the pad: the game sees nothing until every button is let go
int g_section = -1;  // -1: the list of sections
int g_row = 0;
int g_first = 0;  // a long list's first row shown

// --- input ---------------------------------------------------------------------------------------------------------

enum Key { kUp, kDown, kLeft, kRight, kOk, kBack, kKeys };

// The page comes and goes as the game's pages switch: what was up darkens to black over 8 frames, the screen stays
// black 3, and what comes next brightens over 8 (its title sliding in, its rows coming in from the top)
constexpr double kDarken = 8, kBlack = 5, kBrighten = 8;
enum class Veil { kNone, kOpening, kClosing };
Veil g_veil = Veil::kNone;
Clock::time_point g_veil_at;

Clock::time_point After(double frames) {
    return Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(frames / 30));
}

// the page is drawn: not while what was up before it darkens
bool PageUp() { return g_open && !(g_veil == Veil::kOpening && Frames(g_veil_at) < kDarken + kBlack); }

void Open() {
    g_section = -1;
    g_row = 0;
    g_capture = -1;
    g_over_game = OptionsUp();
    g_open = true;
    g_veil = Veil::kOpening;
    g_veil_at = Clock::now();
    g_shown_at = g_chosen_at = After(kDarken + kBlack);
}

// the page goes once darkened (Draw)
void Close() {
    if (g_veil == Veil::kClosing) return;
    g_capture = -1;
    g_veil = Veil::kClosing;
    g_veil_at = Clock::now();
    g_swallow = true;
    app::Save();
}

// the black over everything: rising, held, falling as the veil goes on; closes the page once it is black
float VeilDarkness() {
    if (g_veil == Veil::kNone) return 0;
    double t = Frames(g_veil_at);
    if (t < kDarken) return float(t / kDarken);
    if (t < kDarken + kBlack) return 1;
    if (g_veil == Veil::kClosing && g_open) {
        g_open = false;
        // back on the OPTIONS page: it comes in again as the game brings a page in, the new row with it
        if (g_over_game) {
            g_replay = true;
            g_row_shown_at = Clock::now().time_since_epoch().count();
        }
    }
    t -= kDarken + kBlack;
    if (t < kBrighten) return float(1 - t / kBrighten);
    g_veil = Veil::kNone;
    return 0;
}

void Turn(Key k, int rows);

void Press(Key k) {
    if (g_veil != Veil::kNone) return;  // not while the page comes or goes
    if (g_capture >= 0) {  // waiting for a key: B leaves it
        if (k == kBack) {
            g_capture = -1;
            Play(kSoundBack);
        }
        return;
    }
    int rows = g_section < 0 ? kSectionCount + 1 : kSections[g_section].count;
    int section = g_section, row = g_row;
    Turn(k, rows);
    if (g_section != section) g_shown_at = g_chosen_at = Clock::now();
    else if (g_row != row) g_chosen_at = Clock::now();
}

// with the game's sounds: the cursor moving, a value changing, OK, back
void Turn(Key k, int rows) {
    switch (k) {
        case kUp:
            g_row = (g_row + rows - 1) % rows;
            Play(kSoundCursor);
            break;
        case kDown:
            g_row = (g_row + 1) % rows;
            Play(kSoundCursor);
            break;
        case kLeft:
        case kRight:
            if (g_section >= 0 && kSections[g_section].rows[g_row].step(k == kRight ? 1 : -1)) {
                app::Save();
                Play(kSoundValue);
            } else if (g_capture >= 0) {
                Play(kSoundOk);  // waiting for a second key
            }
            break;
        case kOk:
            if (g_section < 0) {
                if (g_row == kSectionCount) {
                    Play(kSoundBack);
                    Close();
                } else {
                    g_section = g_row, g_row = 0;
                    Play(kSoundOk);
                }
            } else if (kSections[g_section].rows[g_row].keys) {
                kSections[g_section].rows[g_row].step(2);  // waits for its first key
                Play(kSoundOk);
            } else {
                // A moves the value on, from the last back to the first
                const Row& r = kSections[g_section].rows[g_row];
                if (!r.step(1))
                    while (r.step(-1)) {
                    }
                app::Save();
                Play(kSoundValue);
            }
            break;
        case kBack:
            Play(kSoundBack);
            if (g_section < 0) Close();
            else g_row = g_section, g_section = -1, g_first = 0;
            break;
        default: break;
    }
}

// --- drawing -------------------------------------------------------------------------------------------------------

void Page() {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    Frame f = Fit();
    double shown = Frames(g_shown_at), chosen = Frames(g_chosen_at);
    float flash = Flash(chosen), spring = Spring(chosen);
    if (g_section < 0) {
        Chrome(dl, f, "RECOMP SETTINGS", g_row < kSectionCount ? kSections[g_row].help : "Back to the game", shown);
        for (int i = 0; i <= kSectionCount; i++) {
            bool on = i == g_row;
            float in = FadeIn(shown, RowFrames(i)), x = 720 - 100 * PageUnit() * Left(shown, RowFrames(i));
            const char* name = i < kSectionCount ? kSections[i].name : "EXIT";
            float y = 250 + i * 62;
            float w = Lettering(dl, f, x, y, Fade(on ? Mix(kChosen, kFlash, flash) : kLabel, in), name, kCentre);
            if (on) {
                Pointer(dl, f, x - w / 2 - 36 + spring, y, 1, in);
                Pointer(dl, f, x + w / 2 + 44 - spring, y, -1, in);
            }
        }
        return;
    }
    const Section& sec = kSections[g_section];
    Chrome(dl, f, sec.name, sec.rows[g_row].help, shown);
    // the rows a little smaller than the game's, for the longer names and values; around the page's middle. A longer
    // list shows kShown rows at a time, moved along with the cursor, arrows above and below while there is more
    constexpr float kScale = 0.9f, kPitch = 46;
    constexpr int kShown = 10;
    int shown_rows = std::min(sec.count, kShown);
    g_first = std::clamp(g_first, std::max(0, g_row - kShown + 1), std::min(g_row, sec.count - shown_rows));
    float top = 380 - (shown_rows - 1) * kPitch / 2;
    float more = FadeIn(shown, RowFrames(0));
    if (g_first > 0) Arrow(dl, f, 720, top - kPitch * 0.75f, -1, more);
    if (g_first + shown_rows < sec.count) Arrow(dl, f, 720, top + (shown_rows - 0.25f) * kPitch, 1, more);
    for (int i = g_first; i < g_first + shown_rows; i++) {
        bool on = i == g_row;
        int place = i - g_first;
        float in = FadeIn(shown, RowFrames(place)), out = PageUnit() * Left(shown, RowFrames(place));
        float y = top + place * kPitch;
        // a key awaited: its row's value blinks
        if (on && g_capture >= 0) in *= 0.55f + 0.45f * float(std::cos(Frames(g_capture_at) * 0.4));
        // as the game's chosen row: the name green, the colon and value orange, all flashing
        ImU32 name = on ? Mix(kChosen, kFlash, flash) : kLabel, value = on ? Mix(kValueChosen, kFlash, flash) : kLabel;
        float colon = 674 - 100 * out, at = 730 - 150 * out;
        // inside the diamond weave (x 215 to 1261): names from 230, values to 1200 with the arrow after
        Lettering(dl, f, 640 - 50 * out, y, Fade(name, in), sec.rows[i].name, kEnd, kScale, 640 - 230);
        Lettering(dl, f, colon, y, Fade(value, in), ":", kCentre, kScale);
        float w = Lettering(dl, f, at, y, Fade(value, in), sec.rows[i].value(), kStart, kScale, 1200 - 730);
        if (on) {
            Pointer(dl, f, at - 26 + spring, y, -1, in);
            Pointer(dl, f, at + w + 30 - spring, y, 1, in);
        }
    }
}

// the row as the game's rows are drawn: CONTROLLER SETTING's lettering, green when chosen, then its help line.
// The page's units to the 1440x810 frame: GUIDE MAP's y 77 sits at 177, 1.53 a unit. It comes in between
// CONTROLLER SETTING (the game's row 6) and RESET TO DEFAULT (7).
void OptionsRowDraw() {
    float a = g_row_alpha;
    if (a <= 0) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    Frame f = Fit();
    float y = 177 + (g_row_y - 77) * 1.53f;
    bool on = g_row_chosen;
    auto at = [](const std::atomic<int64_t>& t) { return Clock::time_point(Clock::duration(t.load())); };
    // between CONTROLLER SETTING (35 frames) and RESET TO DEFAULT (40), from 100 units to the left and from nothing
    // (the game's rows brighten as the square of the time, measured); going, hooks.cpp darkens it with the page
    double shown = Frames(at(g_row_shown_at));
    a *= FadeIn(shown, 37.5);
    float x = 720 - 100 * PageUnit() * Left(shown, 37.5);
    ImU32 colour = on ? Mix(kChosen, kFlash, Flash(Frames(at(g_row_chosen_at)))) : kLabel;
    Lettering(dl, f, x, y, Fade(colour, a), "RECOMP SETTINGS", kCentre);
    if (on) Help(dl, f, "Display, graphics and other settings of this port", a);
}

void Counter(double fps) {
    char text[32];
    std::snprintf(text, sizeof(text), "FPS: %.1f", fps);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float px = std::max(16.0f, ImGui::GetIO().DisplaySize.y / 38);
    ImFont* font = g_bold ? g_bold : ImGui::GetFont();
    // top, between the game's TIME LIMIT and its minimap, clear of its HUD
    ImVec2 size = font->CalcTextSizeA(px, FLT_MAX, 0, text);
    ImVec2 at{ImGui::GetIO().DisplaySize.x * 0.79f - size.x, px * 0.5f};
    dl->AddText(font, px, {at.x + 1.5f, at.y + 1.5f}, IM_COL32(0, 0, 0, 220), text);
    dl->AddText(font, px, at, IM_COL32(255, 255, 255, 255), text);
}

}  // namespace

// the window is up and the DX12 side has not built ImGui's font texture yet: the menu fonts from Windows
void OptionsRow(int y, bool chosen, float alpha) {
    int64_t now = Clock::now().time_since_epoch().count();
    static int64_t last = 0;
    if (now - last > std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(150)).count()) g_row_shown_at = now;
    last = now;
    if (chosen && !g_row_chosen) g_row_chosen_at = now;
    g_row_seen = now;
    g_row_y = y;
    g_row_chosen = chosen;
    g_row_alpha = alpha;
}

void OpenPage() {
    std::lock_guard lock(app::UiLock());
    Open();
}

bool HideOptions() { return PageUp() && g_over_game; }

bool TakeOptionsReplay() { return g_replay.exchange(false); }

bool TakeSound(uint32_t& id) {
    std::lock_guard lock(g_sound_lock);
    if (g_sounds.empty()) return false;
    id = g_sounds.front();
    g_sounds.erase(g_sounds.begin());
    return true;
}

void OptionsShown() { g_options_seen = Clock::now().time_since_epoch().count(); }

void Window(void*) {
    std::lock_guard lock(app::UiLock());
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontDefault();
    wchar_t dir[MAX_PATH];
    GetWindowsDirectoryW(dir, MAX_PATH);
    auto load = [&](const wchar_t* name, float px = kFontPx) -> ImFont* {
        char path[MAX_PATH * 2];
        std::snprintf(path, sizeof(path), "%ls\\Fonts\\%ls", dir, name);
        return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES ? io.Fonts->AddFontFromFileTTF(path, px) : nullptr;
    };
    g_heavy = load(L"ariblk.ttf");
    g_bold = load(L"arialbd.ttf");
    {
        // rasterised at the sprites' size, no finer: magnified it softens as theirs
        char path[MAX_PATH * 2];
        std::snprintf(path, sizeof(path), "%ls\\Fonts\\arialbd.ttf", dir);
        ImFontConfig c;
        c.OversampleH = c.OversampleV = 1;
        c.PixelSnapH = true;
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) g_help = io.Fonts->AddFontFromFileTTF(path, kHelpPx, &c);
    }
    // the game's lettering, over an Arial Black of a row's size for what it has not
    ImFont* rows = load(L"ariblk.ttf", kCell);
    ImFont* titles = load(L"ariblk.ttf", kCell);
    if (rows && titles) {
        AddLettering(io, rows, titles);
        PlaceLettering(io);
    }
}

void Draw(double fps) {
    if (g_apply_at != Clock::time_point{} && Clock::now() >= g_apply_at) {
        g_apply_at = {};
        app::ApplyDisplay();
    }
    float dark = VeilDarkness();
    if (PageUp()) {
        Page();
    } else if (RowUp()) {  // also while the page darkens away, as the game's rows
        OptionsRowDraw();
    }
    if (dark > 0) {
        ImGui::GetForegroundDrawList()->AddRectFilled({0, 0}, ImGui::GetIO().DisplaySize,
                                                      IM_COL32(0, 0, 0, int(dark * 255 + 0.5f)));
    }
    if (g_fps_counter) Counter(fps);
}

// F10 opens and closes the page (Windows sends it as a system key); with it up the arrows, Enter and Esc work it
bool Message(void*, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t*) {
    if (g_capture >= 0 && (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN)) {
        std::lock_guard lock(app::UiLock());
        if (g_capture < 0) return true;
        if (lp & (1 << 30)) return true;  // held from before (the OK that started the waiting)
        uint16_t vk = uint16_t(wp);
        bool extended = (lp >> 24) & 1;
        if (vk == VK_SHIFT) vk = uint16_t(MapVirtualKeyW(UINT((lp >> 16) & 0xFF), MAPVK_VSC_TO_VK_EX));
        if (vk == VK_CONTROL) vk = extended ? VK_RCONTROL : VK_LCONTROL;
        if (vk == VK_MENU) vk = extended ? VK_RMENU : VK_LMENU;
        if (vk != VK_ESCAPE && vk != VK_F10) {
            auto& k = input::Keys()[size_t(g_capture)];
            k[size_t(g_capture_slot)] = vk;
            if (k[0] == k[1]) k[1] = 0;
            if (!k[0]) std::swap(k[0], k[1]);
            app::Save();
            Play(kSoundValue);
        } else {
            Play(kSoundBack);
        }
        g_capture = -1;
        return true;
    }
    if (g_capture >= 0 && (msg == WM_KEYUP || msg == WM_SYSKEYUP)) return true;
    if (wp == VK_F10 && (msg == WM_SYSKEYUP || msg == WM_KEYUP)) return true;  // not the window menu
    if (msg != WM_KEYDOWN && !(msg == WM_SYSKEYDOWN && wp == VK_F10)) return false;
    std::lock_guard lock(app::UiLock());
    if (wp == VK_F10) {
        if (g_open) {
            Play(kSoundBack);
            Close();
        } else {
            Play(kSoundOk);
            Open();
        }
        return true;
    }
    if (!g_open) return false;
    switch (wp) {
        case VK_UP: Press(kUp); return true;
        case VK_DOWN: Press(kDown); return true;
        case VK_LEFT: Press(kLeft); return true;
        case VK_RIGHT: Press(kRight); return true;
        case VK_RETURN:
        case VK_SPACE: Press(kOk); return true;
        case VK_ESCAPE:
        case VK_BACK: Press(kBack); return true;
    }
    return false;
}

// both sticks pressed open and close it; while it is up pad 1 works it and the game gets nothing
void Pad(int port, input::Pad& pad) {
    debug::Pad(port, pad);
    if (port != 0) return;
    static uint16_t last_buttons = 0;
    constexpr uint16_t kBothSticks = 0xC0;
    bool sticks = (pad.buttons & kBothSticks) == kBothSticks && (last_buttons & kBothSticks) != kBothSticks;
    last_buttons = pad.buttons;
    std::lock_guard lock(app::UiLock());
    bool now[kKeys] = {
        (pad.buttons & 0x01) || pad.ly > 16000,  (pad.buttons & 0x02) || pad.ly < -16000,
        (pad.buttons & 0x04) || pad.lx < -16000, (pad.buttons & 0x08) || pad.lx > 16000,
        pad.analog[0] > 30 || (pad.buttons & 0x10),  // A or START
        pad.analog[1] > 30 || (pad.buttons & 0x20),  // B or BACK
    };
    if (pad.keyboard) std::fill(std::begin(now), std::end(now), false);  // keys work the page through Message
    static bool held[kKeys] = {};
    static Clock::time_point repeat[kKeys];
    bool y = pad.analog[3] > 30;
    static bool y_held = false;
    bool y_pressed = y && !y_held;
    y_held = y;
    if (sticks) {
        Play(g_open ? kSoundBack : kSoundOk);
        if (g_open) Close();
        else Open();
    } else if (!g_open && y_pressed && OptionsUp()) {
        Play(kSoundOk);
        Open();
        g_swallow = true;
    } else if (g_open) {
        auto t = Clock::now();
        for (int k = 0; k < kKeys; k++) {
            if (now[k] && !held[k]) {
                Press(Key(k));
                repeat[k] = t + std::chrono::milliseconds(400);
            } else if (now[k] && k <= kRight && t >= repeat[k]) {
                Press(Key(k));
                repeat[k] = t + std::chrono::milliseconds(110);
            }
        }
    }
    std::copy(std::begin(now), std::end(now), std::begin(held));
    if (g_swallow) {
        bool any = pad.buttons || pad.lx > 16000 || pad.lx < -16000 || pad.ly > 16000 || pad.ly < -16000;
        for (uint8_t a : pad.analog) any |= a > 30;
        g_swallow = any;
    }
    if (!g_open && !g_swallow) return;
    bool connected = pad.connected;
    pad = input::Pad{};
    pad.connected = connected;
}

}  // namespace ui
