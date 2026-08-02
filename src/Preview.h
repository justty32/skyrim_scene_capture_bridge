#pragma once

// Preview — the GHOST: what `sc pl` is about to place, standing in the world at
// your aim point, at real scale, in real light.
//
// 🔴 THE INVARIANT (user's redesign, 2026-07-14). The ghost is not a browser
// feature that happens to place things; it IS place mode's cursor:
//
//     a ghost exists  ⟺  mode == place  ∧  `sc pl gh1`  ∧  something is selected
//
// The user put it best: "browser 本質上是 sc pl 的一個附屬品". So the Browser page
// does not own a preview of its own — clicking an entry there switches you INTO
// place mode and pins that entry as what place mode will place. Leave place mode
// and the ghost goes with it. There is exactly one ghost, it always means the same
// thing, and nothing can conflict with anything.
//
// WHAT IT IS MADE OF:
//   * SOURCE — either the selected palette slot (the default; change the selection
//     and the ghost swaps) or a catalogue entry the Browser pinned.
//   * POSE — position follows your aim (position is NOT numpad-editable: aiming IS
//     the positioning). Rotation and scale ARE numpad-editable, with the same keys
//     and the same hold-to-repeat feel as `sc ed ax` (Numpad.h owns both).
//   * AUTO-SCALE — a farmhouse previewed at arm's length fills the whole screen and
//     you can judge nothing. So a fresh ghost is scaled to take up about a ninth of
//     the screen (a third in each dimension) — never scaled UP, so small things keep
//     their real size. numpad 0 puts it back to 1.0.
//
// 🔴 A GHOST MUST NEVER REACH THE EXPORT. The exporter's discriminator is "dynamic
// ref = the player put it there", and a ghost is a dynamic ref. Two layers, and the
// second is the one that matters:
//   1. the live handle — cheap, exact, and gone the moment the session ends.
//   2. a SENTINEL on the ref itself (ExtraTextDisplayData, which the savegame
//      serializes along with every created ref). Quicksave with a ghost up, reload
//      tomorrow: our registry is empty, but IsGhost() still recognises it, because
//      the evidence rides the ref and not our memory. State you can reconstruct from
//      the world beats state you have to remember.

#include <cstddef>
#include <cstdint>
#include <string>

namespace Preview {

    // ---- sources -----------------------------------------------------------

    // Show the palette's slot `index` (the default source). No-op when the slot
    // is missing or its plugin is not loaded.
    bool ShowSlot(std::size_t index);

    // Browser: pin a catalogue entry as what place mode will place. The caller
    // (UI.Browser) also switches to place mode and turns the ghost on — see
    // ForceGhostOn.
    bool ShowBase(RE::TESBoundObject* base, const std::string& label);

    // Turn `sc pl gh1` on for the user (remembering that WE did), so the Browser's
    // preview works even for someone who had ghosts off. Clearing the ghost puts
    // the setting back — that is what makes it "temporary".
    void ForceGhostOn();

    void Clear();  // no-trace delete (disable + mark deleted), like the eraser's own-ref path

    [[nodiscard]] bool Active();
    [[nodiscard]] const std::string& Label();

    // The export gate + every picker's gate. True for our live ghost AND for an
    // orphan ghost left in a savegame by an earlier session (see the header note).
    [[nodiscard]] bool IsGhost(RE::TESObjectREFR* ref);

    // Per frame (the panel's HUD element, which renders with the panel CLOSED —
    // that is the point: you pick a thing, close the panel, and the ghost follows
    // your aim while you walk to the spot). Enforces the invariant above, keeps the
    // ghost on the palette's selection, and tracks your aim. Idempotent and cheap.
    void Update();

    // ---- numpad: rotate + scale (never position — aiming IS the positioning) ---
    // Same keys as `sc ed ax`: 4/6 yaw, 1/3 pitch, 7/9 roll, and each pair's MIDDLE
    // key (2/5/8) reverts its own axis. Plus +/- scale, numpad 0 = scale back to
    // 1.0 (undo the auto-scale), numpad . = clear the ghost.
    bool HandleKey(std::uint32_t code);                    // tap; true = consumed
    void HandleHold(std::uint32_t code, float heldSecs);   // hold-to-repeat

    // ---- panel -------------------------------------------------------------
    void SetFollow(bool on);   // off: the ghost stays put so you can walk around it
    [[nodiscard]] bool Follow();
    void SetYaw(float degrees);
    [[nodiscard]] float Yaw();
    void SetScale(float scale);
    [[nodiscard]] float Scale();

    // Ghost -> real placement, at the ghost's exact pose (NOT re-aimed: what you
    // see is what you get). The ghost stays up, so a row of trees is one key
    // pressed five times. Returns false when there is nothing to commit.
    bool Commit();

    // kPostLoadGame: delete any orphan ghost the loaded save is carrying.
    std::size_t SweepOrphans();

    void DropState();  // co-save revert: forget the handle; the world is being replaced

}  // namespace Preview
