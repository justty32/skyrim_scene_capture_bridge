#pragma once

// Markers — the unified marker system (Idea #24 P1, the MVP editing modality).
//
// A marker is a named world coordinate: the player drops one (hotkey/spell),
// renames it in the panel, and Export writes every marker into the spec's
// `annotations[]` — advisory anchors an AI agent reads to author the real spec
// sections ("place a goat at marker 'goat'"). The visible in-world proxy ref
// is EDITOR CHROME, never content: ExportCell must exclude it from
// `placements[]` via IsProxy().
//
// Registry is session memory (same model as the eraser list). The proxy's
// display name is set to the label — display names persist in the SAVEGAME,
// so a future adopt-scan can recover markers WITH labels after a reload.

#include <cstdint>
#include <string>
#include <vector>

namespace Markers {

    struct Entry {
        std::uint32_t seq = 0;       // placement order; ordered kinds (navmesh) rely on it
        std::string label;
        std::string kind = "note";   // note | navmesh | mapMarker | vfx | tag | ...
        std::string note;            // free-form brief for the agent -> annotations[].note.
                                     // Registry-only: NOT recoverable by adopt (the savegame
                                     // stores just the display name = label).
        RE::NiPoint3 position;       // fixed at placement time (not the proxy's live pose)
        RE::NiPoint3 angleDeg;       // full orientation, degrees (the dagger tip points along it)
        float scale = 1.f;           // proxy scale (edit mode can change it)
        std::string cellOrWs;        // durable id of the containing cell/worldspace
        bool isInterior = false;
        RE::ObjectRefHandle proxy;
    };

    // Place a marker at the player's feet (the navmesh-vision primitive:
    // "record where I stand"). Returns false when no proxy base resolves.
    bool PlaceAtPlayer();

    // Place a marker where the player is LOOKING: havok ray from eye level
    // along the facing direction (range 4096). Falls back to the feet when
    // nothing is hit — one key serves both, no extra scancode risk.
    bool PlaceAimed();

    // Adopt proxies that exist in the player's cell but not in the registry —
    // markers from a previous session survive in the SAVEGAME (dynamic refs +
    // display names persist), while this registry lives in the DLL. Label is
    // recovered from the proxy's display name. Returns how many were adopted.
    std::size_t AdoptOrphans();

    [[nodiscard]] std::vector<Entry>& All();

    // True when `ref` is one of our proxies — by registry handle, or by base
    // (catches orphaned proxies from an earlier session after a reload).
    [[nodiscard]] bool IsProxy(RE::TESObjectREFR* ref);

    void Rename(std::uint32_t seq, const std::string& label);
    void SetKind(std::uint32_t seq, const std::string& kind);
    void SetNote(std::uint32_t seq, const std::string& note);
    // Update a marker's exported pose (edit mode commits a moved proxy here):
    // full position + orientation (degrees) + scale.
    void SetTransform(std::uint32_t seq, const RE::NiPoint3& position,
        const RE::NiPoint3& angleDeg, float scale);
    void Remove(std::uint32_t seq);  // destroys the proxy too — no trace

    // Lookup for the E-interaction path: activating a proxy opens its editor
    // window. SeqOf returns 0 when the ref is an orphaned proxy not in the
    // registry — AdoptOne then claims exactly that ref (label from its saved
    // display name) so the window can still open on it.
    [[nodiscard]] Entry* FindBySeq(std::uint32_t seq);
    [[nodiscard]] std::uint32_t SeqOf(RE::TESObjectREFR* ref);
    std::uint32_t AdoptOne(RE::TESObjectREFR* ref);  // returns the new seq, 0 on refusal

    // After a game load, proxies from the pre-load session are gone (dynamic
    // refs live in the save, our registry lives in the DLL). Drop entries
    // whose proxy no longer resolves so the panel doesn't list ghosts.
    void PruneDeadProxies();

    // `sc mk dp0` / `dp1`: hide/show every gem. Visual only — positions were
    // fixed at placement and the exporter excludes proxies before it ever
    // looks at the disabled flag, so nothing downstream changes. Hidden gems
    // have no collision, so E-interaction needs dp1 first.
    void SetProxiesVisible(bool visible);
    [[nodiscard]] bool ProxiesVisible();

    // A co-save marker whose proxy FormID no longer resolves: stash its
    // label/kind/note so an adopt scan (auto-run on load) can merge them back
    // onto the re-found gem by a position match. ClearPending resets the stash
    // at revert time, before the new save's markers are read.
    void AddPendingOrphan(const RE::NiPoint3& position, const std::string& label,
        const std::string& kind, const std::string& note);
    void ClearPending();

    // CoSave hands the registry back after a load: re-sync the seq counter,
    // re-freeze the gems' clutter havok, re-apply the display state.
    void OnRegistryRestored();

}  // namespace Markers
