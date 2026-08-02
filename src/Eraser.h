#pragma once

// Eraser — mark existing placed refs for removal (Idea #24 §E ③ / plan P2).
//
// Marking an AUTHORED ref (vanilla or any mod) disables it on the spot as the
// visual feedback and records its durable id; Export writes the list into the
// spec's `removals[]`, which ModForge's landed BuildRemovals turns into an
// InitiallyDisabled+buried override — the standard, reversible "disable vanilla
// clutter" patch. Marking one of OUR OWN dynamic refs is true deletion: it is
// disabled and leaves no trace anywhere (user-decided semantics).
//
// State model: the marked list rides in the co-save (durable ids re-resolve
// reliably), so the old cross-save "scan disabled refs + adopt" rescue was
// removed as redundant (2026-07-11). Undo re-enables (per-row / per-cell /
// most-recent); Clear re-enables everything.

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace Eraser {

    struct Entry {
        std::string id;      // "Skyrim.esm:0x0D1991" — durable ref id
        std::string plugin;  // "Skyrim.esm"
        bool addsMaster;     // not one of the 5 base-game masters (CC counts as adding)
        std::string cellOrWs;  // durable anchor at mark time — panel cell filter
        std::string name;    // display name at mark time — panel row info
        RE::NiPoint3 position;  // world coords at mark time — panel row info
        RE::ObjectRefHandle handle;
        // Author's own naming of the row (panel-editable, rides the co-save).
        // `label` renames the row; `note` says WHY it goes ("cleared for the
        // shelf") and rides into the exported removals[] entry, which is what
        // gets the reason in front of the agent reading the scene json.
        std::string label;
        std::string note;
    };

    // What the marking did — the panel and log word things by this.
    enum class MarkResult { kNone, kMarked, kOwnDeleted, kDuplicate, kMarkerProxy };

    MarkResult MarkConsoleRef();  // `sc delc` — the console's selected ref (objects only)
    MarkResult MarkCrosshair();  // F8 — the activatable crosshair target, old feel
    // Explicit physics-ray erase (panel button) for trees/non-activatable
    // statics. NOT a fallback of F8: the ray always hits some ref (walls),
    // so F8-on-empty must stay a no-op, not "erased the wall behind".
    MarkResult MarkByRay();

    [[nodiscard]] std::vector<Entry>& All();
    [[nodiscard]] const std::unordered_set<std::string>& MarkedIds();

    // Panel row naming. Keyed by the durable id (the row's identity — the list
    // has no seq); a no-op when nothing owns that id.
    void SetLabel(const std::string& id, const std::string& label);
    void SetNote(const std::string& id, const std::string& note);

    bool Undo();   // re-enable the most recent mark
    void Clear();  // re-enable everything
    bool UndoEntry(const std::string& id);       // per-row undo (panel button)
    bool UndoInCell(const std::string& cellId);  // undo the last mark in one cell

    // Co-save plumbing (CoSave.cpp). DropAll clears the REGISTRY ONLY — no
    // Enable() calls, unlike Clear(): on a save-load revert the incoming save
    // carries its own disable states and the old world must not be touched.
    void DropAll();
    void OnRegistryRestored();  // rebuild the id set after entries are loaded

}  // namespace Eraser
