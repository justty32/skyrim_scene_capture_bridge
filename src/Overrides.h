#pragma once

// Overrides — committed transform edits of AUTHORED placed refs (plan P4,
// contract decided 2026-07-11: top-level `overrides[]`, the sibling of
// `removals[]`). The numpad editor calls Register() on COMMIT of an authored
// ref; Export writes the list into the spec's `overrides[]`, which ModForge's
// BuildOverrides turns into a GetOrAddAsOverride transform re-stamp.
//
// State model matches the Eraser (user-decided: explicit over inferred): only
// refs moved THROUGH the editor are registered — no diff, because havok moves
// things on its own and a diff cannot tell a rolled cup from an edit. The
// registry lives in RAM; after a full game restart the moved pose survives in
// the savegame but is NOT re-registered — re-edit the ref (numpad 5, nudge,
// commit) to re-register. MVP-accepted limitation, documented in the README.
//
// BASELINE CAVEAT: `orig*` (the revert target) is the transform at FIRST
// selection this session — normally the authored pose, but for a havok object
// that already tumbled it is the tumbled pose. Good enough for revert-my-edit;
// it never leaks into the export (only the new transform does).

#include <cstdint>
#include <string>
#include <vector>

namespace Overrides {

    struct Entry {
        std::string id;          // "Skyrim.esm:0x0D1991" — durable ref id
        std::string name;        // display name, for the panel
        std::string plugin;
        bool addsMaster;         // not one of the 5 base-game masters
        bool isActor;            // actors: exporter emits no scale (XSCL is dead on ACHR)
        RE::ObjectRefHandle handle;
        RE::NiPoint3 origPos;    // first-registration baseline — revert target
        RE::NiPoint3 origAngle;  // radians
        float origScale = 1.f;
        RE::NiPoint3 pos;        // committed transform (radians); export prefers the
        RE::NiPoint3 angle;      // live pose when the handle still resolves
        float scale = 1.f;
        // Author's own naming of the row (panel-editable, rides the co-save).
        // `note` rides into the exported overrides[] entry — the reason a thing
        // was moved is exactly what the agent reading the scene json needs.
        std::string label;
        std::string note;
    };

    // Register (or refresh) a committed edit. The new transform is read off the
    // ref; orig* stick from the FIRST registration so revert always returns to
    // the pre-edit baseline, not to some intermediate commit.
    void Register(const std::string& id, RE::TESObjectREFR* ref,
                  const RE::NiPoint3& origPos, const RE::NiPoint3& origAngle,
                  float origScale);

    [[nodiscard]] std::vector<Entry>& All();
    [[nodiscard]] bool Contains(const std::string& id);

    // Panel row naming. Keyed by the durable id (the row's identity — the list
    // has no seq); a no-op when nothing owns that id. A re-edit of the same ref
    // keeps them, exactly as it keeps the baseline.
    void SetLabel(const std::string& id, const std::string& label);
    void SetNote(const std::string& id, const std::string& note);

    bool Revert(std::size_t index);  // restore the baseline transform + unregister
    void Clear();                    // revert everything

    // Co-save plumbing (CoSave.cpp): registry-only clear — no world touches,
    // unlike Clear(), which physically moves refs back.
    void DropAll();

}  // namespace Overrides
