#pragma once

// Modes — the P5 mode system (user-decided 2026-07-11).
//
// One mode is active at a time; each mode has its OWN action-key binding
// (duplicates allowed — with a single active mode the same key can serve every
// mode, which is the default: everything on F11). `sc <cmd>` in the console
// switches modes (Console.cpp); the panel Settings page switches too.
//
// There are NO classic direct hotkeys (user-decided: removed entirely, not
// toggled off). The whole input surface is: per-mode action key + the numpad
// keys INSIDE the editor's edit mode + numpad * ray-select. Export is a panel
// button only.
//
// KEYS ARE CONFIGURED IN AN .INI, NOT IN-GAME (user-decided 2026-07-12, after
// the second in-game rebind attempt failed). SceneCaptureBridge.ini in the SKSE
// folder holds one line per mode (KeyIni.cpp); the in-game key-capture flow is
// GONE — grabbing a key from a panel that does not pause the game, while the
// player's hand is still on WASD, never worked reliably. Nothing to arm, no
// input sink to fight.
//
// The current mode and the binds still ride the SAVEGAME (co-save, SETT v7) —
// but the INI WINS over a co-save bind for any mode the ini names (see
// ApplyCoSaveBind): the ini is the user's explicit configuration, the co-save is
// only this save's state.

#include <cstdint>
#include <string>

namespace Modes {

    enum class Mode : std::uint8_t {
        kOff = 0,
        kMarker,   // action: place a marker at the aimed point
        kDelete,   // action: erase the crosshair target
        kPick,     // action: eyedrop the crosshair target into the palette
        kPlace,    // action: place the selected palette slot at the aimed point
        kEdit,     // action: select the crosshair target into numpad edit mode
        kCapture,  // action: eyedrop the aimed item enchant/effects (or NPC) into capturedItems[]
        kReferrer, // action: NAME the aimed existing ref (no world change) -> references[]
        kTotal
    };

    [[nodiscard]] Mode Current();
    void Set(Mode m);  // DebugNotification + log

    [[nodiscard]] const char* Name(Mode m);  // "off" / "marker" / ...
    [[nodiscard]] const char* Cmd(Mode m);   // "off" / "mk" / "del" / "pk" / "pl" / "ed"

    // Per-mode action key (DIK scancode). kOff has no binding.
    [[nodiscard]] std::uint32_t Bind(Mode m);
    void SetBind(Mode m, std::uint32_t scancode);

    // ---- where a bind comes from (ini > co-save > F11) ---------------------
    //
    // SetIniBind: the value SceneCaptureBridge.ini gave this mode (KeyIni.cpp).
    // It is remembered, so it survives OnRevert (ResetDefaults re-applies it)
    // and beats whatever the loaded savegame carries.
    void SetIniBind(Mode m, std::uint32_t scancode);
    void ClearIniBinds();                       // KeyIni re-parse starts clean
    [[nodiscard]] bool BindFromIni(Mode m);     // panel: "(ini)" vs "(save)"

    // ApplyCoSaveBind: the co-save's stored bind (CoSave.cpp SETT v7). Applied
    // ONLY when the ini did not name this mode — the ini is the explicit
    // configuration, the save is just this playthrough's state. Also revalidated
    // against IsBindable: a save written by the old (removed) in-game rebind UI
    // can carry a reserved scancode such as W, and that bug must not outlive it.
    void ApplyCoSaveBind(Mode m, std::uint32_t scancode);

    // Per-mode aim source: false = the interaction crosshair (classic feel),
    // true = a physics ray (trees / non-activatable statics). Toggled by
    // `sc del er0/er1`, `sc pk ...`, `sc ed ...`, `sc cap ...`, `sc ref ...`.
    // Only delete/pick/edit/capture/referrer read it (marker/place are
    // inherently aimed). Persists in the co-save.
    [[nodiscard]] bool UseRay(Mode m);
    void SetUseRay(Mode m, bool useRay);

    // Per-mode PHYSICS switch — `sc pl py0/py1` and `sc ed py0/py1`. The stored
    // value is "physics is KEPT", so it reads straight off the command: py1 =
    // true, py0 = false. Defaults differ per mode (that is the whole point):
    //
    //   kPlace  DEFAULT py1 (physics kept) — a placed object behaves normally.
    //           `sc pl py0` = physics OFF: the object is havok-frozen the moment
    //           it is placed AND the export carries `noHavokSettle` so the
    //           SHIPPED esp keeps it put (the engine's load-time havok settle
    //           pass is what launches a hand-placed cup across the room).
    //   kEdit   DEFAULT py0 (physics off while you drive it) — the existing P3
    //           freeze-on-select behaviour. `sc ed py1` leaves havok running.
    //
    // Only place/edit read this. Persists in the co-save (SETT v6).
    [[nodiscard]] bool Physics(Mode m);
    void SetPhysics(Mode m, bool keepPhysics);

    // Per-mode EXTRA-DATA switch — `sc pk ed0/ed1` and `sc pl ed0/ed1`. False
    // (the default) = the durable BASE only, the historic behaviour. True:
    //
    //   kPick   the eyedropper also records the INSTANCE's extra data (a
    //           player-applied ExtraEnchantment lives on the ref, not the base),
    //           so the palette slot carries it.
    //   kPlace  a slot placed with it on is exported through the MINT+REFERENCE
    //           path: the scene file gets a `capturedItems[]` row for the
    //           enchanted item and the placement's `base` points at that row's
    //           editorId (a file-internal dependency, same trick as the
    //           referrer's in-file `references[]`). With it off the same slot
    //           places/exports as the plain unenchanted base.
    //
    // Only pick/place read this. Persists in the co-save (SETT v6).
    [[nodiscard]] bool ExtraData(Mode m);
    void SetExtraData(Mode m, bool on);

    // GHOST PREVIEW — `sc pl gh0/gh1`, kPlace only, DEFAULT ON (SETT v8).
    //
    // On, place mode shows you what it is about to place: the selected palette slot
    // (or the entry the Browser pinned) stands at your aim point, and the action key
    // drops a real copy of exactly that. Off, place mode is what it always was —
    // the selected slot appears at the aim point when you press the key, unseen
    // until then. See Preview.h for the invariant this flag is half of.
    [[nodiscard]] bool Ghost(Mode m);
    void SetGhost(Mode m, bool on);

    // Feed a key-down. Returns true when it matched the current mode's binding
    // and ran the mode's action (debounced).
    bool HandleKey(std::uint32_t scancode);

    // False for keys that must never become an action-key binding: Esc, the
    // console key, Tab/Enter (ImGui/console chrome) and the movement keys
    // (WASD/Space/Shift/Ctrl). Now that binding happens in an .ini this is a
    // VALIDATOR, not a capture filter: it rejects a self-inflicted foot-gun in
    // the ini (and any reserved bind left in an old savegame by the removed
    // in-game rebind UI, which is exactly how the bug used to persist).
    [[nodiscard]] bool IsBindable(std::uint32_t scancode);

    // off + every binding back to F11, THEN the ini's binds re-applied (they
    // outlive a savegame revert — they are configuration, not save state).
    void ResetDefaults();

    // Short scancode label ("F11", "numpad 5", "0x2A") and its inverse — the
    // ini writes/reads these names, so a player never types a raw scancode.
    //
    // 🔴 The "0x2A" form comes out of ONE shared static buffer, so the returned
    // pointer is only valid until the NEXT KeyName() call. One call per
    // expression; copy to a std::string if you need two.
    [[nodiscard]] const char* KeyName(std::uint32_t scancode);
    // "F11" / "numpad 5" / "0x57" / "87" -> scancode; 0 when unrecognised.
    // Case- and space-insensitive ("NumPad5" == "numpad 5").
    [[nodiscard]] std::uint32_t KeyCode(const std::string& name);

}  // namespace Modes
