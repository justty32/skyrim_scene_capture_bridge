#pragma once

// Aim — the shared look-ray: where is the player pointing, in world units.
// Used by Markers (place marker there), Palette (place object there), and as
// an EXPLICIT ray-selection entry for Editor/Eraser/Palette.
//
// Ray selection is deliberately NOT an automatic fallback of the crosshair
// pick (user-decided 2026-07-11): the ray almost always hits SOME ref — walls
// and floors are refs too — so a silent fallback would make "nothing selected"
// impossible and numpad-5-on-empty would grab the wall behind. The crosshair
// keeps its exact old feel; the ray runs only behind explicit buttons/keys the
// user pressed ON PURPOSE, where grabbing a tree or a wall is the expectation.

namespace Aim {

    // Havok ray from eye level along the player's facing (range 4096).
    // Pitch sign verified in-game 2026-07-11.
    bool LookHit(RE::NiPoint3& out);

    // The activatable ref under the crosshair (CrosshairPickData) — chairs,
    // NPCs, clutter. Null when the crosshair shows nothing. The classic feel.
    RE::NiPointer<RE::TESObjectREFR> CrosshairRef();

    // The ref owning the first collidable the look-ray hits — trees, rocks,
    // architecture, anything with physics. Null on no hit / LAND / the player.
    // Explicit entries only (panel buttons, dedicated key) — see header note.
    RE::NiPointer<RE::TESObjectREFR> RayRef();

}  // namespace Aim
