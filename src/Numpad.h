#pragma once

// Numpad — the numpad's scancodes and its long-press FEEL, in one place.
//
// Extracted from Editor.cpp 2026-07-14, when the browser's preview ghost needed
// the same keys and the same hold-to-repeat behaviour (rotate/scale it before you
// place it). Two consumers, one owner: a second copy of the repeat clock would
// look identical for a week and then drift — and "tap and hold can never drift
// apart" is the property the editor's numpad was built around in the first place.
//
// The STEP SIZES are NOT here: they are the Editor's settings (Editor::YawStep()
// / ScaleStep(), panel-editable, co-saved), and the ghost reads those same ones —
// so "how far one tap turns a thing" has one owner too.

#include <cstdint>

namespace Numpad {

    // DIK scancodes. NOT from the verified F-key block — if a key does nothing
    // in-game, edit mode logs the actual code of every unmapped press, so one
    // session of tapping reveals a wrong constant.
    constexpr std::uint32_t kSelect = 0x4C;     // numpad 5
    constexpr std::uint32_t kSelectRay = 0x37;  // numpad *
    constexpr std::uint32_t kCommit = 0x52;     // numpad 0
    constexpr std::uint32_t kCancel = 0x53;     // numpad . (Del)
    constexpr std::uint32_t kFwd = 0x48;        // numpad 8
    constexpr std::uint32_t kBack = 0x50;       // numpad 2
    constexpr std::uint32_t kLeft = 0x4B;       // numpad 4
    constexpr std::uint32_t kRight = 0x4D;      // numpad 6
    constexpr std::uint32_t kYawNeg = 0x47;     // numpad 7
    constexpr std::uint32_t kYawPos = 0x49;     // numpad 9
    constexpr std::uint32_t kDown = 0x4F;       // numpad 1
    constexpr std::uint32_t kUp = 0x51;         // numpad 3
    constexpr std::uint32_t kScaleUp = 0x4E;    // numpad +
    constexpr std::uint32_t kScaleDn = 0x4A;    // numpad -

    // True for the numpad block (+ * / Enter) — used to log unmapped presses
    // without drowning the log in WASD.
    [[nodiscard]] constexpr bool IsNumpad(std::uint32_t code) {
        return (code >= 0x47 && code <= 0x53) || code == kSelectRay ||
               code == 0xB5 || code == 0x9C;
    }

    // ---- hold-to-repeat ----------------------------------------------------
    //
    // A tap is ONE step. A held key keeps going, ramping up: slow at first so you
    // can place a thing precisely, fast after a moment so you can shove it across
    // the room. Both go through the SAME step function at the call site — the tap
    // passes 1.0, the hold passes a fraction per frame.

    // Call on the tap (key-down): restarts the repeat clock, so the dead zone is
    // measured from THIS press and not from whatever came before.
    void OnTap(std::uint32_t code);

    // Steps to apply for this frame of a held key. 0 = do nothing:
    //   * still inside the dead zone (the tap already did its one step), or
    //   * the frame was absurdly long — the game paused, loaded or hitched, and
    //     the engine's held-counter kept running while nobody was watching.
    //     Applying that gap as one lump would teleport the object across the room.
    [[nodiscard]] float StepsFor(std::uint32_t code, float heldSecs);

}  // namespace Numpad
