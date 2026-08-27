#pragma once

// SceneExporter internals — shared ONLY between the SceneExporter translation
// units (`SceneExporter.cpp` and the per-segment `SceneExporter.*.cpp` files).
// Nothing outside the exporter may include this: the public contract is
// `SceneExporter.h`, and this header exists purely so one 870-line file could
// be cut along the segment boundaries it already had (2026-08-27).
//
// Each `Append*` here owns exactly one ModSpec segment of the output json:
//   AppendPlacements   -> placements[]        (SceneExporter.Placements.cpp)
//   AppendRegistries   -> removals[] / overrides[] / annotations[]
//   AppendReferences   -> references[]        (both in SceneExporter.Registries.cpp)
//   AppendMintedItems  -> capturedItems[] for `sc pl ed1` placements
//   AppendCaptures     -> capturedItems[] + capturedNpcs[] from the registry
//                                              (both in SceneExporter.Captures.cpp)
//
// CALL ORDER IS A CONTRACT, not a style choice: AppendPlacements must run
// first, because AppendReferences and AppendMintedItems may only emit rows
// whose in-file target actually landed in placements[] — and that is recorded
// in PlacementCounters as the sweep runs.

#include <cstddef>
#include <unordered_set>
#include <vector>

namespace Palette {
    struct PlacedInfo;
}

namespace SceneExporter {

    // Degrees are the export contract; the engine stores radians.
    constexpr float kRadToDeg = 57.2957795f;

    // Emit {x,y,z} as a json object matching PlacementSpec.Position/Rotation.
    inline nlohmann::json Vec3(const RE::NiPoint3& v) {
        return nlohmann::json{{"x", v.x}, {"y", v.y}, {"z", v.z}};
    }

    // Running tallies while sweeping one or more cells for placements.
    struct PlacementCounters {
        std::size_t actorsExcluded = 0;  // player-placed actors, deliberately not emitted
        std::size_t preexisting = 0;
        std::size_t skipped = 0;
        std::size_t markerProxies = 0;
        std::size_t previewGhosts = 0;     // browser preview refs — never content
        std::size_t notOurs = 0;           // dynamic refs the ENGINE spawned (fish, critters…)
        std::size_t removalsPending = 0;   // in swept cells (log only)
        std::size_t overridesPending = 0;  // in swept cells (log only)
        // Referrer rows whose IN-FILE target actually made it into placements[]
        // this export — the only ones AppendReferences may emit (a `ref` pointing
        // at an editorId that is not in the file would be dropped by build).
        std::unordered_set<std::uint32_t> inFileRefsEmitted;
        // Palette placed-ref rows (`sc pl ed1`) whose placement was emitted with a
        // MINTED base — AppendMintedItems must emit exactly these as capturedItems[]
        // rows, or the placement's `base` would name an editorId that is not in the
        // file. Same in-file-dependency discipline as inFileRefsEmitted above.
        std::vector<const Palette::PlacedInfo*> mintedEmitted;
        std::size_t noHavokSettle = 0;   // placements exported with the flag (log/panel)
    };

    // ---- segment producers (definitions in the sibling SceneExporter.*.cpp) ----

    void AppendPlacements(RE::TESObjectCELL* cell, nlohmann::json& scene,
        PlacementCounters& counters);
    void AppendRegistries(nlohmann::json& scene);
    void AppendReferences(nlohmann::json& scene, const PlacementCounters& counters);
    void AppendMintedItems(nlohmann::json& scene, const PlacementCounters& counters);
    void AppendCaptures(nlohmann::json& scene);

}  // namespace SceneExporter
