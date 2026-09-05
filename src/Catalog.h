#pragma once

// Catalog — every placeable base object in the load order, as a searchable
// index. This is the Creation Kit's Object Window, in-game (user 2026-07-14).
//
// WHY: until now the ONLY way to get a mountain into the palette was to find
// one already standing in the world and eyedrop it (`sc pk`). The catalogue is
// the other half — pick from everything the load order actually has, whether or
// not an instance of it happens to be nearby.
//
// 🔴 THERE ARE NO EDITOR IDs IN RUNTIME RECORDS. TESForm::GetFormEditorID() is
// `{ return ""; }` for STAT/ACTI/FURN/... — Skyrim SE keeps EDIDs in memory only
// for a handful of types (CELL and WRLD among them, which is why SceneExporter
// can print those). So the search index is built from what IS in memory:
//
//   * the MODEL PATH (`TESModel`, or ARMO's `TESBipedModelForm::worldModels`) —
//     and it is a BETTER key than an EDID anyway: searching "mountain" hits
//     Landscape\Mountains\*.
//   * the display name (statics have none — that is normal, not a bug).
//   * the plugin + FormID (the durable id, which is also the export key).
//
// Real EDITOR IDs are optionally enriched from ModForge's scene-catalog.json
// (Mutagen reads them off the plugins). Runtime forms remain the placeable source
// of truth; file-only records are never admitted to this catalogue.

#include <cstddef>
#include <string>
#include <vector>

namespace Catalog {

    struct Entry {
        RE::TESBoundObject* base = nullptr;
        RE::FormType type = RE::FormType::None;
        std::string id;      // durable "<plugin>:0xLOCAL" — the export key
        std::string plugin;  // display + the plugin filter
        std::string editorId; // offline catalog metadata; empty when no catalog/match
        std::string name;    // display name; EMPTY for most statics (they have none)
        std::string model;   // "Landscape\Mountains\MountainCliff01.nif"
        std::string search;  // lowercased editorId+name+model+id — Filter() haystack
    };

    // Built once, lazily (first Browser render): a full form-array sweep is not
    // something to pay for at startup when most sessions never open the page.
    void EnsureBuilt();
    // Called at kDataLoaded. Missing/unreadable metadata is non-fatal: the
    // runtime-only catalogue remains fully usable.
    void LoadOffline();
    [[nodiscard]] bool Built();
    [[nodiscard]] const std::string& OfflineStatus();
    [[nodiscard]] std::size_t OfflineMatches();

    [[nodiscard]] const std::vector<Entry>& All();
    [[nodiscard]] const std::vector<std::string>& Plugins();  // filter combo, alphabetical order
    [[nodiscard]] const std::vector<RE::FormType>& Types();   // only the types actually present
    [[nodiscard]] const char* TypeName(RE::FormType t);

    // Indices into All() matching every active filter. The query is matched
    // AND-wise over whitespace-separated terms ("mountain snow" = both), so a
    // search narrows the way a person expects. Empty query = the whole slice.
    //
    // Callers cache the result and re-run this ONLY when a filter changes — it
    // is a linear scan of tens of thousands of entries, not a per-frame job.
    [[nodiscard]] std::vector<std::size_t> Filter(const std::string& query,
        RE::FormType type,            // FormType::None = every type
        const std::string& plugin);   // "" = every plugin

}  // namespace Catalog
