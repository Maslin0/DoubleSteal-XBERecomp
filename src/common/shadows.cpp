// The sun's shadow map: a 512x512 depth target the game draws with a colour surface that has no memory (data 0) and
// samples as a shadow. Both are made at the render scale like every target unless SHADOW RESOLUTION picks their size:
// the depth target is only bound with a colour target of its size.
#include <memory>
#include <string>

#include "gpu/ext.h"

namespace shadows {

uint32_t g_resolution = 0;  // texels a side, 0 at the render scale
bool g_filter = false;       // soft edges: the map compared around each point on a disc

namespace {

constexpr uint32_t kSide = 512;  // the console's
constexpr float kFilterTexels = 1.25f;  // the disc's radius in the console's texels, whatever the map's size

// the console's one compare (four, bilinear), or with the filter twelve of them over a disc of ext.x in uv
class Hooks : public gpu::ShaderHooks {
public:
    std::string Shadow() const override {
        return R"(float4 shadow(Texture2D<float> t, SamplerComparisonState s, float2 uv, float z, int fmt, float2 px, float2 size) {
    float mx = fmt == 2 ? 65535.0 : 16777215.0;
    float d = clamp(z, 0, mx) / mx;
    if (ext.x <= 0) return t.SampleCmpLevelZero(s, uv, d).xxxx;
    static const float2 disc[12] = {
        float2(-0.326, -0.406), float2(-0.840, -0.074), float2(-0.696, 0.457), float2(-0.203, 0.621),
        float2(0.962, -0.195), float2(0.473, -0.480), float2(0.519, 0.767), float2(0.185, -0.893),
        float2(0.507, 0.064), float2(0.896, 0.412), float2(-0.322, -0.933), float2(-0.792, -0.598)};
    float sum = 0;
    [unroll] for (int i = 0; i < 12; i++) sum += t.SampleCmpLevelZero(s, uv + disc[i] * ext.x, d);
    return (sum / 12).xxxx;
}
)";
    }
};
const Hooks g_hooks;

class Extension : public gpu::Extension {
public:
    const gpu::ShaderHooks* Shaders() const override { return &g_hooks; }
    void Constants(float (*)[4], float* ext, const gpu::Target*, uint32_t) override {
        ext[0] = g_filter ? kFilterTexels / kSide : 0.0f;
    }
    bool Settings() override {
        uint32_t want = g_resolution;
        if (want == applied_) return false;
        applied_ = want;
        return true;  // targets made again at the new size
    }
    void MakeTarget(const gpu::Surface& s, bool depth, uint32_t gw, uint32_t gh, float& scale, DXGI_FORMAT&) override {
        if (applied_ && gw == kSide && gh == kSide && (depth || !s.data)) scale = float(applied_) / kSide;
    }

private:
    uint32_t applied_ = 0;
};

const bool g_installed = (gpu::SetExtension(std::make_unique<Extension>()), true);

}  // namespace
}  // namespace shadows
