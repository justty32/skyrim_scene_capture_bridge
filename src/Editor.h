#pragma once

// Editor — the numpad transform mode (Idea #24 細摳②「修改」, plan P2).
//
// ENTRY (P5): the edit mode's action key (Modes.cpp -> EnterSelect, crosshair)
// or numpad * (explicit ray-select for trees/non-activatable statics). The
// numpad then nudges the selection; numpad 0 commits, numpad . (Del) cancels
// and restores the original transform. Numpad 5 is no longer an entry key —
// in-mode it RESETS the target to its pre-edit pose without leaving edit mode.
//
// Two kinds of target (contract decided 2026-07-11):
//   * OUR OWN dynamic refs — contract-zero: their live pose is what the
//     exporter already emits into `placements[]`.
//   * AUTHORED refs (vanilla or any mod) — commit registers the ref in the
//     Overrides registry, which exports as the top-level `overrides[]`
//     (explicit registration, never diff-inferred — havok noise).
//
// Key map (one deviation from the user spec, marked for review): 8/2 =
// forward/back (player-relative), 4/6 = left/right, 7/9 = yaw, +/- = scale,
// **1/3 = height down/up** — the spec gave 1379 to rotation and left no
// height axis; furniture placement needs Z far more than a second rotation
// axis. Remap is one constant per key.

#include <cstdint>

namespace Editor {

    [[nodiscard]] bool Active();

    // Feed a keyboard scancode (the key going DOWN). Returns true when consumed
    // — the caller's own hotkeys must not fire while edit mode is live. One
    // press = exactly one step, as it always did.
    bool HandleKey(std::uint32_t scancode);

    // Feed a key that is being HELD (heldSecs = the engine's running count).
    // After a short dead zone the nudge keys start moving the target
    // continuously, so "push it into place" stops being a drumming exercise.
    //
    // ONLY the nudge keys (move / rotate / scale) repeat. commit, cancel,
    // select and the per-axis reverts stay strictly single-shot — a commit
    // firing every frame while your finger rests on numpad 0 would be a
    // catastrophe, and that asymmetry is the whole reason this is a separate
    // entry point rather than "call HandleKey again".
    void HandleHold(std::uint32_t scancode, float heldSecs);

    // Explicit physics-ray selection (panel button; numpad * is the key
    // equivalent) — for trees and non-activatable statics the crosshair never
    // sees. Deliberately NOT a fallback of the crosshair: the ray always hits
    // some ref (walls/floors), so a fallback would make "no target" impossible.
    bool SelectByRay();

    // The edit mode's action-key entry (Modes.cpp): select the crosshair
    // target and enter numpad edit mode. No-op while already editing.
    bool EnterSelect();

    void Cancel();  // restore the original transform and leave edit mode

    // For the panel: current target + live transform, or a hint when idle.
    struct Status {
        bool active = false;
        const char* name = "";
        RE::NiPoint3 pos;
        float yawDeg = 0.f;
        float scale = 1.f;
    };
    [[nodiscard]] Status Current();

    // Per-tap edit step sizes (Settings page; persisted in the co-save SETT
    // v2). Setters ignore non-positive values.
    [[nodiscard]] float MoveStep();
    [[nodiscard]] float YawStep();
    [[nodiscard]] float ScaleStep();
    void SetMoveStep(float v);
    void SetYawStep(float v);
    void SetScaleStep(float v);

    // Pure-rotation sub-mode (`sc ed ax` toggles). On: numpad drives rotation
    // (4/6 yaw, 1/3 pitch, 7/9 roll) and each pair's middle key REVERTS only
    // its own axis to the pre-edit value (2 = pitch, 5 = yaw, 8 = roll —
    // revert, not zero: the object may have been rotated already). Off: numpad
    // moves, and 5 restores the whole pre-edit pose. Persisted in the co-save.
    [[nodiscard]] bool RotateMode();
    void SetRotateMode(bool on);
    bool ToggleRotateMode();  // returns the new state

}  // namespace Editor
