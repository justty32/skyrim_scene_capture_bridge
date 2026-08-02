#pragma once

// KeyIni — the action keys live in a FILE, not in the game (user-decided
// 2026-07-12, after the second in-game rebind attempt failed in-game).
//
// WHY AN INI AND NOT A PANEL BUTTON. Capturing a keypress from the panel means
// reading the raw input stream while the game is still running (SKSE Menu
// Framework does not pause it) — so the "key the player pressed" is competing
// with the keys the player's hand is ALREADY on: the movement keys they were
// walking with a moment before they reached for the mouse. Two attempts failed
// in-game (P5 2026-07-11: bound W; the reserved-key + press-and-release rework
// 2026-07-12: still failed for the user). A file has none of that surface: no
// armed state, no input sink, no timing, no held keys. You type F4, you save,
// you get F4.
//
// FILE: <Documents>/My Games/Skyrim Special Edition/SKSE/SceneCaptureBridge.ini
// — the same folder as the palette store and every export, i.e. the folder the
// user (and the agent) already opens. Deliberately NOT Data/SKSE/Plugins/: that
// one lives inside the MO2 mod folder, where reinstalling the mod zip silently
// reverts it (a trap this project has been bitten by before).
//
// Auto-created with defaults + a full key-name table in comments when missing,
// so the feature is discoverable without reading any documentation.

#include <cstdint>
#include <string>

namespace KeyIni {

    // Read the ini and apply its binds (Modes::SetIniBind). Writes the commented
    // default file first if none exists. Called at kDataLoaded, and again by the
    // panel's "reload keys from ini" button — re-parsing is a pure re-apply, so
    // it is safe at any time.
    // Returns how many mode lines were applied.
    std::size_t Load();

    [[nodiscard]] std::string PathString();  // for the panel ("edit THIS file")

    // Last Load()'s outcome, for the Settings page: how many binds came from the
    // ini, whether the file existed, and the first parse complaint (if any).
    struct Status {
        bool loaded = false;      // the file was read (it exists / was created)
        std::size_t applied = 0;  // mode lines accepted
        std::string problem;      // "" when clean; else the first rejected line
    };
    [[nodiscard]] const Status& Last();

}  // namespace KeyIni
