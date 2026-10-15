// Hooks for default.xbe (Double S.T.E.A.L. - The Second Clash, XDK 5849).
#include "kernel/kernel.h"
#include "xdk/hle.h"

// XMVDecoder_Play(decoder, flags, rect), __stdcall: blocks until the movie ends. The SDK has no XMV
// decoder (Bink only), so the intro and attract movies are skipped as if they had finished.
X86_FUNC(sub_00177B8E) {
    (void)base;
    kernel::Return(ctx, 0, 3);
}

