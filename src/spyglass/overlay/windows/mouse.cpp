#ifdef _WIN32

#include "spyglass/overlay/windows/mouse.h"

#include <cstdint>

#include "bedrock/input/mouse.h"
#include "spyglass/detail.h"
#include "spyglass/hook.h"
#include "spyglass/overlay/windows/overlay.h"
#include "spyglass/signature.h"

namespace {

void *g_feed = nullptr;

}  // namespace

void MouseDevice::feed(const char actionButtonId, const std::int8_t buttonData, const short x, const short y,
                       const short dx, const short dy, const bool forceMotionlessPointer)
{
    if (spyglass::Overlay::instance().owns_mouse()) {
        return;
    }
    SPYGLASS_CALL_ORIGINAL(&MouseDevice::feed, g_feed, this, actionButtonId, buttonData, x, y, dx, dy,
                           forceMotionlessPointer);
}

namespace spyglass {

void install_mouse_hook()
{
    install_hook("MouseDevice::feed", kMouseFeed, detail::fp_cast(&MouseDevice::feed), &g_feed);
}

}  // namespace spyglass

#endif
