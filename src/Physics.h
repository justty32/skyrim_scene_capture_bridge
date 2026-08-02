#pragma once

// Physics — the havok freeze/release primitives, shared by every module that
// puts an object somewhere and needs it to STAY there (Markers' gems, Palette's
// placements, the Editor's live nudging).
//
// Extracted 2026-07-12 for the per-mode physics switches (`sc pl py0/py1`,
// `sc ed py0/py1`): Markers and Editor each had their own copy of this, and the
// place path needed a third. Behaviour is unchanged — the code is the same code.
//
// TWO LAYERS, don't confuse them:
//   * THIS FILE = the RUNTIME freeze (SetMotionType). Live-session only: it is
//     actor/ref state, it does not survive into an esp, and it is gated on
//     HavokMovable because keyframing a STAT/FURN is meaningless and restoring
//     one to kDynamic would knock the level's walls loose.
//   * THE ESP  = `PlacementSpec.noHavokSettle` -> the REFR record header flag
//     DontHavokSettle (0x20000000), written by ModForge. That is what makes a
//     placement stay put in the SHIPPED mod (it tells the engine to skip the
//     load-time havok settle pass — the thing that launches a hand-placed cup
//     across the room). Vanilla Skyrim.esm sets it on 3791 refs, so it is the
//     canonical mechanism, and it is NOT type-gated there (247 of them are
//     STATs) — neither is ours.

#include <cstdint>

namespace Physics {

    // Is this base one of the naturally havok-DYNAMIC clutter types (cups,
    // books, weapons on tables)? Only these get the runtime freeze: a STAT/FURN
    // has no dynamic rigid body to freeze, and pushing one to kDynamic on
    // release would drop the world's walls.
    [[nodiscard]] bool HavokMovable(RE::TESBoundObject* base);

    // Freeze an object's havok so it cannot fall or be kicked. The catch: right
    // after PlaceObjectAtMe the 3D (hence the rigid body) is not loaded yet, so
    // an immediate SetMotionType silently no-ops (log: "Target does not have
    // 3D"). Retry on the SKSE task queue until Get3D() is live — one frame is
    // usually enough; the retry cap stops a never-loading ref from spinning.
    void FreezeDeferred(RE::ObjectRefHandle handle, int retries = 60);

    // Hand the object back to havok (it will settle). Returns false when there
    // was nothing to release.
    bool Release(RE::TESObjectREFR* ref);

}  // namespace Physics
