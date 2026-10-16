// The sun's shadow map: a 512x512 depth target the game draws with a colour surface that has no memory (data 0) and
// samples as a shadow. Both are made at the render scale like every target unless SHADOW RESOLUTION picks their size:
// the depth target is only bound with a colour target of its size.
#include <memory>

#include "gpu/ext.h"

namespace shadows {

uint32_t g_resolution = 0;  // texels a side, 0 at the render scale

namespace {

constexpr uint32_t kSide = 512;  // the console's

class Extension : public gpu::Extension {
public:
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
