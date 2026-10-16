// Recomp Settings: a page drawn like the game's own OPTIONS screen (F1, or both sticks pressed on pad 1), and the
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
#include <string>
#include <vector>

#include <imgui.h>

#include "app/app.h"
#include "core/input.h"

namespace debug {
void Pad(int port, input::Pad& pad);
}
namespace shadows {
extern uint32_t g_resolution;
extern bool g_filter;
}

namespace ui {

bool g_fps_counter = false;
bool g_vibration = true;
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
const ImU32 kHelp = IM_COL32(184, 120, 0, 255);
const ImU32 kLine = IM_COL32(150, 150, 150, 255);
const ImU32 kDiamond = IM_COL32(19, 19, 19, 255);

ImFont* g_heavy = nullptr;  // Arial Black, slanted when drawn: the menu lettering
ImFont* g_bold = nullptr;  // Arial Bold: the help line, the counter
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

// the help line: the game's Arial Bold in orange with a black edge all round
void Help(ImDrawList* dl, const Frame& f, const std::string& s, float alpha = 1) {
    ImFont* font = g_bold ? g_bold : ImGui::GetFont();
    float size = 31 * f.s, widen = Widen();
    ImVec2 ext = font->CalcTextSizeA(size, FLT_MAX, 0, s.c_str());
    ImVec2 at = f.P(720, 655);
    at = {at.x - ext.x * widen / 2, at.y - ext.y / 2};
    int first = dl->VtxBuffer.Size;
    float e = 2 * f.s;
    const ImVec2 edge[] = {{-e, 0}, {e, 0}, {0, -e}, {0, e}, {-e, -e}, {e, -e}, {-e, e}, {e, e}};
    for (ImVec2 o : edge) dl->AddText(font, size, {at.x + o.x, at.y + o.y}, Fade(IM_COL32(0, 0, 0, 255), alpha), s.c_str());
    dl->AddText(font, size, at, Fade(kHelp, alpha), s.c_str());
    for (int i = first; i < dl->VtxBuffer.Size; i++) dl->VtxBuffer[i].pos.x = at.x + (dl->VtxBuffer[i].pos.x - at.x) * widen;
}

// a row's words in the game's own lettering at (x, y) = its left, centre or right edge and the middle of its letters
// (Arial Black slanted without it); returns its width in the frame's units
float Lettering(ImDrawList* dl, const Frame& f, float x, float y, ImU32 colour, const std::string& s, Align align = kStart,
                float scale = 1) {
    if (!g_game) {
        float w = (g_heavy ? g_heavy : ImGui::GetFont())->CalcTextSizeA(38 * scale * f.s, FLT_MAX, 0, s.c_str()).x / f.s;
        if (dl) Text(dl, f, g_heavy, 38 * scale, x, y, colour, s, align);
        return w;
    }
    float size = kCell * kTexel * scale * f.s, widen = Widen();
    float w = g_game->CalcTextSizeA(size, FLT_MAX, 0, s.c_str()).x * widen;
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
void Title(ImDrawList* dl, const Frame& f, float x, float y, const std::string& s) {
    if (!g_title) {
        Text(dl, f, g_heavy, 54, x, y, kGold, s, kStart, true, Widen());
        return;
    }
    float size = kCell * kTexel * f.s, widen = Widen();
    ImVec2 at = f.P(x, y);
    at.y -= (kTitles.top + kTitles.band / 2) * kTexel * f.s;
    int first = dl->VtxBuffer.Size;
    dl->AddText(g_title, size, at, kTitleTint, s.c_str());
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

// --- the game's animations, measured on its OPTIONS page (30 frames a second) --------------------------------------

double Frames(Clock::time_point since) { return std::chrono::duration<double>(Clock::now() - since).count() * 30; }

// a page's rows come in one after another from the top: the chosen one at once, row i (from 0) eased in over
// 9 + 5 (i - 1) frames
float RowIn(double frames, float row, bool chosen) {
    if (chosen || row <= 0) return 1;
    float t = float(frames) / (9 + 5 * (row - 1));
    return std::min(1.0f, t * t);
}

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
std::atomic<int64_t> g_row_shown_at{0}, g_row_chosen_at{0};  // Clock ticks: the OPTIONS page came up, the row was chosen

void Button(ImDrawList* dl, const Frame& f, float x, ImU32 colour, const char* letter, const char* name) {
    dl->AddCircleFilled(f.P(x, 704), 17 * f.s, colour);
    dl->AddCircle(f.P(x, 704), 17 * f.s, IM_COL32(255, 255, 255, 90), 0, 1.5f * f.s);
    Text(dl, f, g_heavy, 26, x, 704, IM_COL32(255, 255, 255, 230), letter, kCentre, false);
    Lettering(dl, f, x + 30, 704, IM_COL32(230, 230, 230, 255), name, kStart, 0.8f);
}

// opened from the game's OPTIONS page: drawn over that page's own background, frame and buttons (hooks.cpp hides
// its rows and title meanwhile)
std::atomic<bool> g_over_game{false};

void Chrome(ImDrawList* dl, const Frame& f, const std::string& title, const std::string& help) {
    if (g_over_game) {
        Title(dl, f, 152, 105, title);
        Help(dl, f, help);
        return;
    }
    ImVec2 d = ImGui::GetIO().DisplaySize;
    dl->AddRectFilled({0, 0}, d, IM_COL32(0, 0, 0, 255));
    // the diamond weave behind the rows
    for (int r = 0; r < 10; r++)
        for (int c = 0; c < 15; c++) {
            float cx = 245 + c * 68 + (r & 1) * 34, cy = 165 + r * 46;
            dl->AddQuadFilled(f.P(cx, cy - 22), f.P(cx + 30, cy), f.P(cx, cy + 22), f.P(cx - 30, cy), kDiamond);
        }
    Polyline(dl, f, {{66, 83}, {1228, 83}, {1276, 47}, {1372, 47}}, kGold, 3);
    Polyline(dl, f, {{66, 128}, {1250, 128}, {1298, 92}, {1372, 92}}, kGold, 3);
    Title(dl, f, 152, 105, title);
    Polyline(dl, f, {{66, 655}, {125, 655}, {155, 632}, {1285, 632}, {1315, 655}, {1372, 655}}, kLine, 2.5f);
    Help(dl, f, help);
    Polyline(dl, f, {{66, 678}, {1372, 678}}, kLine, 2.5f);
    Polyline(dl, f, {{66, 730}, {1372, 730}}, kLine, 2.5f);
    Button(dl, f, 167, IM_COL32(62, 166, 40, 255), "A", "OK");
    Button(dl, f, 309, IM_COL32(178, 34, 34, 255), "B", "BACK");
}

// --- what the page holds -------------------------------------------------------------------------------------------

struct Row {
    const char* name;
    const char* help;
    std::string (*value)();
    bool (*step)(int dir);  // -1 or +1; false when the value is already at that end
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
    {"VIBRATION", "The controller's rumble",
     [] { return std::string(g_vibration ? "ON" : "OFF"); },
     [](int dir) { return Flag(g_vibration, dir); }},
};

struct Section {
    const char* name;
    const char* help;
    const Row* rows;
    int count;
};
const Section kSections[] = {
    {"DISPLAY", "Monitor, display mode, resolution, frame rate counter", kDisplay, int(std::size(kDisplay))},
    {"GRAPHICS", "Antialiasing, texture filtering", kGraphics, int(std::size(kGraphics))},
    {"GENERAL", "Start up, background pause", kGeneral, int(std::size(kGeneral))},
};
constexpr int kSectionCount = int(std::size(kSections));

// all under UiLock: the window thread (keys), the input thread (pad), the render thread (drawing)
std::atomic<bool> g_open{false};
bool g_swallow = false;  // closed from the pad: the game sees nothing until every button is let go
int g_section = -1;  // -1: the list of sections
int g_row = 0;

// --- input ---------------------------------------------------------------------------------------------------------

enum Key { kUp, kDown, kLeft, kRight, kOk, kBack, kKeys };

void Open() {
    g_section = -1;
    g_row = 0;
    g_over_game = OptionsUp();
    g_open = true;
    g_shown_at = g_chosen_at = Clock::now();
}

void Close() {
    g_open = false;
    g_swallow = true;
    app::Save();
}

void Turn(Key k, int rows);

void Press(Key k) {
    int rows = g_section < 0 ? kSectionCount + 1 : kSections[g_section].count;
    int section = g_section, row = g_row;
    Turn(k, rows);
    if (g_section != section) g_shown_at = g_chosen_at = Clock::now();
    else if (g_row != row) g_chosen_at = Clock::now();
}

void Turn(Key k, int rows) {
    switch (k) {
        case kUp: g_row = (g_row + rows - 1) % rows; break;
        case kDown: g_row = (g_row + 1) % rows; break;
        case kLeft:
        case kRight:
            if (g_section >= 0 && kSections[g_section].rows[g_row].step(k == kRight ? 1 : -1)) app::Save();
            break;
        case kOk:
            if (g_section < 0) {
                if (g_row == kSectionCount) Close();
                else g_section = g_row, g_row = 0;
            } else {
                // A moves the value on, from the last back to the first
                const Row& r = kSections[g_section].rows[g_row];
                if (!r.step(1))
                    while (r.step(-1)) {
                    }
                app::Save();
            }
            break;
        case kBack:
            if (g_section < 0) Close();
            else g_row = g_section, g_section = -1;
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
        Chrome(dl, f, "RECOMP SETTINGS", g_row < kSectionCount ? kSections[g_row].help : "Back to the game");
        for (int i = 0; i <= kSectionCount; i++) {
            bool on = i == g_row;
            float in = RowIn(shown, float(i), on);
            const char* name = i < kSectionCount ? kSections[i].name : "EXIT";
            float y = 250 + i * 62;
            float w = Lettering(dl, f, 720, y, Fade(on ? Mix(kChosen, kFlash, flash) : kLabel, in), name, kCentre);
            if (on) {
                Pointer(dl, f, 720 - w / 2 - 36 + spring, y, 1);
                Pointer(dl, f, 720 + w / 2 + 44 - spring, y, -1);
            }
        }
        return;
    }
    const Section& sec = kSections[g_section];
    Chrome(dl, f, sec.name, sec.rows[g_row].help);
    // the rows a little smaller than the game's, for the longer names and values; around the page's middle
    constexpr float kScale = 0.9f, kPitch = 46;
    float top = 380 - (sec.count - 1) * kPitch / 2;
    for (int i = 0; i < sec.count; i++) {
        bool on = i == g_row;
        float in = RowIn(shown, float(i), on);
        float y = top + i * kPitch;
        // as the game's chosen row: the name green, the colon and value orange, all flashing
        ImU32 name = on ? Mix(kChosen, kFlash, flash) : kLabel, value = on ? Mix(kValueChosen, kFlash, flash) : kLabel;
        Lettering(dl, f, 640, y, Fade(name, in), sec.rows[i].name, kEnd, kScale);
        Lettering(dl, f, 674, y, Fade(value, in), ":", kCentre, kScale);
        float w = Lettering(dl, f, 730, y, Fade(value, in), sec.rows[i].value(), kStart, kScale);
        if (on) {
            Pointer(dl, f, 704 + spring, y, -1);
            Pointer(dl, f, 730 + w + 30 - spring, y, 1);
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
    a *= RowIn(Frames(at(g_row_shown_at)), 6.5f, false);
    ImU32 colour = on ? Mix(kChosen, kFlash, Flash(Frames(at(g_row_chosen_at)))) : kLabel;
    Lettering(dl, f, 720, y, Fade(colour, a), "RECOMP SETTINGS", kCentre);
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
    g_row_y = y;
    g_row_chosen = chosen;
    g_row_alpha = alpha;
}

void OpenPage() {
    std::lock_guard lock(app::UiLock());
    Open();
}

bool HideOptions() { return g_open && g_over_game; }

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
    if (g_open) {
        Page();
    } else if (OptionsUp()) {
        OptionsRowDraw();
    }
    if (g_fps_counter) Counter(fps);
}

// F1 opens and closes the page; with it up the arrows, Enter and Esc work it
bool Message(void*, uint32_t msg, uintptr_t wp, intptr_t, intptr_t*) {
    if (msg != WM_KEYDOWN) return false;
    std::lock_guard lock(app::UiLock());
    if (wp == VK_F1) {
        if (g_open) Close();
        else Open();
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

// the motors stay still with VIBRATION off
void Rumble(int, uint16_t& left, uint16_t& right) {
    if (!g_vibration) left = right = 0;
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
        if (g_open) Close();
        else Open();
    } else if (!g_open && y_pressed && OptionsUp()) {
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
