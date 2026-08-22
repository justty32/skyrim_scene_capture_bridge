#pragma once

// Aim — the two deliberate world-ray semantics used by the tools:
//   * the established player-facing ray for markers and explicit ref picking;
//   * the rendered-camera ray used only by ghost/direct placement.
//
// Ray selection is deliberately NOT an automatic fallback of the crosshair
// pick (user-decided 2026-07-11): the ray almost always hits SOME ref — walls
// and floors are refs too — so a silent fallback would make "nothing selected"
// impossible and numpad-5-on-empty would grab the wall behind. The crosshair
// keeps its exact old feel; the ray runs only behind explicit buttons/keys the
// user pressed ON PURPOSE, where grabbing a tree or a wall is the expectation.

namespace Aim {

    // Havok ray from eye level along the player's facing (range 4096).
    // Used by markers and explicit ref picking, not object placement.
    // Pitch sign verified in-game 2026-07-11.
    bool LookHit(RE::NiPoint3& out);

    // Placement-only ray from the NiCamera that rendered the world. This is
    // deliberately separate from LookHit/RayRef: marker and explicit ref-pick
    // controls retain their established player-facing semantics, while the
    // ghost and both placement paths agree with the centre of the actual view.
    // The collector ignores the player so a third-person camera ray can pass
    // through the avatar instead of stopping on their back.
    bool RenderedCameraHit(RE::NiPoint3& out);

    // The activatable ref under the crosshair (CrosshairPickData) — chairs,
    // NPCs, clutter. Null when the crosshair shows nothing. The classic feel.
    RE::NiPointer<RE::TESObjectREFR> CrosshairRef();

    // The ref owning the first collidable the look-ray hits — trees, rocks,
    // architecture, anything with physics. Null on no hit / LAND / the player.
    // Explicit entries only (panel buttons, dedicated key) — see header note.
    RE::NiPointer<RE::TESObjectREFR> RayRef();

}  // namespace Aim
