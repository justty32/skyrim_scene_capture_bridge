#include "Numpad.h"

#include <algorithm>
#include <unordered_map>

namespace {
    // Held for less than this = still just a tap (one step, from the key going
    // down). Without the dead zone every normal press would drift a little.
    constexpr float kRepeatDelay = 0.35f;
    // A frame this long means the game was paused / loading / hitching: the
    // engine's held counter kept running but no one was watching.
    constexpr float kMaxFrame = 0.25f;

    // Steps per second while held, ramping up.
    float RateOf(float heldPastDelay) {
        constexpr float kSlow = 8.f, kFast = 40.f, kRampSecs = 1.5f;
        const float t = std::min(heldPastDelay / kRampSecs, 1.f);
        return kSlow + (kFast - kSlow) * t;
    }

    // Where each held key's counter was last frame — the difference IS the frame
    // delta, so we never have to ask the engine for one.
    std::unordered_map<std::uint32_t, float> g_heldAt;
}

namespace Numpad {

    void OnTap(std::uint32_t code) { g_heldAt[code] = 0.f; }

    float StepsFor(std::uint32_t code, float heldSecs) {
        float& last = g_heldAt[code];
        if (heldSecs < last) last = 0.f;  // a new press — the engine's clock restarted
        if (heldSecs < kRepeatDelay) {    // still a tap; the tap path already moved it
            last = heldSecs;
            return 0.f;
        }
        // Measure the frame from the END of the dead zone the first time we cross
        // it, or the delay itself would be applied as displacement in one lump.
        const float from = std::max(last, kRepeatDelay);
        const float dt = heldSecs - from;
        last = heldSecs;
        if (dt <= 0.f || dt > kMaxFrame) return 0.f;
        return dt * RateOf(heldSecs - kRepeatDelay);
    }

}  // namespace Numpad
