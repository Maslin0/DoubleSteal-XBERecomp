// Hooks for default.xbe (Double S.T.E.A.L. - The Second Clash, XDK 5849).
#include <cstdlib>

#include "kernel/kernel.h"
#include "xdk/hle.h"

X86_EXTERN_FUNC(__imp__sub_00177B8E);

// XMVDecoder_Play(decoder, flags, rect), __stdcall: the library's own player decodes the movie (LOGO1.XMV, the
// publisher logo) onto the overlay the SDK presents. STEAL_SKIP_MOVIES=1 skips it as if it had finished.
X86_FUNC(sub_00177B8E) {
    if (std::getenv("STEAL_SKIP_MOVIES")) {
        kernel::Return(ctx, 0, 3);
        return;
    }
    __imp__sub_00177B8E(ctx, base);
}
