#include "SceneExporter.h"
#include "SceneExporter.Internal.h"

#include "Markers.h"
#include "Overrides.h"
#include "Eraser.h"
#include "Palette.h"
#include "Preview.h"
#include "Referrer.h"

#include "log.h"

namespace {
    // InitiallyDisabled record flag (bit 0x800) — a ref authored to spawn
    // disabled. We export this so ModForge round-trips the enable state.
    constexpr std::uint32_t kInitiallyDisabled = 0x00000800u;
}

namespace SceneExporter {

    // Sweep ONE cell's placed refs (the vanilla diff) and append the
    // player-added ones to scene["placements"]. No registry/global segments —
    // those are emitted once by AppendRegistries so export-all doesn't repeat
    // them per cell.
    void AppendPlacements(RE::TESObjectCELL* cell, nlohmann::json& scene,
        PlacementCounters& counters) {
        if (!cell) return;
        const bool isInterior = cell->IsInteriorCell();

        // Cell / worldspace attribution (§契約 coordinate contract):
        //  - interior: `cell` = the cell's durable id, positions are cell-local.
        //  - exterior: `worldspace` = the worldspace's durable id, positions
        //    are world-space (ModForge finds the right sub-cell to override).
        //
        // These live on EACH PlacementSpec, not at the top level — ModSpec has
        // no top-level `cell`/`worldspace`, and a placement carrying neither is
        // dropped with "cell '' not found in spec — skipped"
        // (Generator.Build.Placements.cs:48).
        std::string cellId;
        std::string worldspaceId;
        if (isInterior) {
            if (auto id = ResolveDurableId(cell)) {
                cellId = *id;
            }
        } else if (auto* ws = cell->GetRuntimeData().worldSpace) {
            if (auto id = ResolveDurableId(ws)) {
                worldspaceId = *id;
            }
        }
        if (cellId.empty() && worldspaceId.empty()) {
            SKSE::log::warn(
                "AppendPlacements: cell/worldspace unresolved — placements here "
                "would be dropped by build; skipping this cell");
            return;
        }

        // ForEachReference hands the callback a POINTER, not a reference.
        cell->ForEachReference([&](RE::TESObjectREFR* refPtr) -> RE::BSContainer::ForEachResult {
            if (!refPtr) {
                return RE::BSContainer::ForEachResult::kContinue;
            }
            RE::TESObjectREFR& ref = *refPtr;

            RE::TESBoundObject* base = ref.GetBaseObject();
            if (!base || ref.IsDeleted()) {
                return RE::BSContainer::ForEachResult::kContinue;
            }
            // Skip the player and refs whose base cannot be durably referenced.
            if (ref.IsPlayerRef()) {
                return RE::BSContainer::ForEachResult::kContinue;
            }
            // Marker proxies are editor chrome, not content — without this they
            // are dynamic refs and the vanilla diff would export them as
            // player-placed objects.
            if (Markers::IsProxy(refPtr)) {
                ++counters.markerProxies;
                return RE::BSContainer::ForEachResult::kContinue;
            }
            // The browser's preview ghost is the same story: a dynamic ref that
            // is NOT content. IsGhost recognises it by the sentinel on the ref
            // itself, so even a ghost orphaned in some old savegame — one this
            // session never spawned and knows nothing about — cannot ship.
            if (Preview::IsGhost(refPtr)) {
                ++counters.previewGhosts;
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // The vanilla diff. A cell sweep sees EVERY reference in it, so
            // exporting all of them would make ModForge re-place the whole
            // vanilla room on top of itself (Bannered Mare: 662 refs, every
            // chair doubled). Authored refs resolve to durable ids and are not
            // new placements: the registries route erased refs to `removals[]`
            // and moved/scaled refs to `overrides[]`; untouched refs are skipped.
            // A dynamic id alone does not prove player ownership — the ownership
            // gate below emits only refs recorded by Palette::PlacedInfoFor.
            if (auto refId = ResolveDurableId(&ref)) {
                // A ref marked by the eraser is not "pre-existing kept as-is" —
                // it exports through removals[], counted separately. Same for a
                // ref moved through the editor: it exports through overrides[].
                if (Eraser::MarkedIds().contains(*refId)) ++counters.removalsPending;
                else if (Overrides::Contains(*refId)) ++counters.overridesPending;
                else ++counters.preexisting;
                return RE::BSContainer::ForEachResult::kContinue;
            }
            // A disabled dynamic ref is one of our own placements the player
            // erased — true deletion semantics: it leaves no trace.
            if (ref.IsDisabled()) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // SCOPE REVERSAL (user-decided 2026-07-12): a cell export is pure
            // scene/object content. Actors used to land in `placements[]` with
            // kind:"npc"; they no longer do — ModForge places NPCs itself,
            // against the markers (annotations[]). An actor you actually want
            // rebuilt goes through `sc cap` -> the separate captures export,
            // which carries its identity, not just a base + transform.
            if (ref.GetFormType() == RE::FormType::ActorCharacter) {
                ++counters.actorsExcluded;
                return RE::BSContainer::ForEachResult::kContinue;
            }

            auto baseId = ResolveDurableId(base);
            if (!baseId) {
                ++counters.skipped;  // dynamic / runtime-only base — not esp-referenceable
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // 🔴 OWNERSHIP GATE (2026-07-14, in-game). Everything above this line
            // establishes "this ref is dynamic" — and the exporter used to treat
            // that AS "the player placed it". It is not. THE ENGINE PlaceAtMe's
            // things too: an exterior export emitted ten placements of which the
            // user had placed exactly one — the rest were six copies of
            // `DoNotPlaceSmallCritterLandingMarkerHelper` (what butterflies land
            // on) and three Fishing-CC fish. No property of the ref distinguishes
            // them; the fish is as dynamic as your chair.
            //
            // So ownership is RECORDED, not inferred: we emit a dynamic ref only
            // if OUR registry says we placed it (`sc pl`, the browser's commit, or
            // an explicit panel adopt). A fish cannot get a row, so a fish cannot
            // ship. Anything else in the cell is somebody else's — count it and
            // walk on.
            const auto* pi = Palette::PlacedInfoFor(refPtr);
            if (!pi) {
                ++counters.notOurs;
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // Authored transform (data.location/angle), not live physics pose.
            // Angle is radians in-engine; contract wants degrees.
            const RE::NiPoint3& pos = ref.data.location;
            const RE::NiPoint3& ang = ref.data.angle;
            nlohmann::json entry;
            entry["base"] = *baseId;

            {
                // py0 — physics off. The in-session SetMotionType freeze dies with the
                // savegame; THIS is the half that ships. `noHavokSettle` becomes the
                // REFR's DontHavokSettle record flag (0x20000000), which tells the
                // engine to skip the load-time havok settle pass — the pass that
                // launches a hand-placed cup across the room. Vanilla Skyrim.esm uses
                // it on 3791 refs (clutter AND statics), so it is NOT type-gated here.
                if (pi->noHavokSettle) {
                    entry["noHavokSettle"] = true;
                    ++counters.noHavokSettle;
                }
                // ed1 — the slot carried instance extra data (a player-applied
                // enchantment). The durable base is a PLAIN iron sword: pointing the
                // placement at it would ship the un-enchanted item. Instead point
                // `base` at a MINTED capturedItems[] record (emitted by
                // AppendMintedItems into THIS file) whose template IS that base and
                // whose enchantment is the captured one. Mint + reference — the same
                // in-file dependency the referrer uses for `references[]`.
                if (pi->extra.present) {
                    entry["base"] = Palette::MintedEditorIdOf(*pi);
                    counters.mintedEmitted.push_back(pi);
                }
            }

            // (B) IN-FILE DEPENDENCY — the referrer's core trick. This dynamic ref
            // has NO durable FormID, so a `references[]` row cannot name it by id
            // (the id is not portable and means nothing after the build). Give the
            // placement a STABLE editorId instead and let references[].ref point at
            // THAT — a dependency INSIDE the file. ModForge then owns the object and
            // forces it persistent, which is exactly what an alias/package anchor
            // needs. The reference row itself is emitted by AppendReferences, only
            // for the targets recorded here.
            if (const auto* rr = Referrer::InFileEntryFor(refPtr)) {
                entry["editorId"] = Referrer::EditorIdOf(*rr);
                counters.inFileRefsEmitted.insert(rr->seq);
            }
            if (!cellId.empty()) {
                entry["cell"] = cellId;
            } else {
                entry["worldspace"] = worldspaceId;
            }
            entry["position"] = Vec3(pos);
            entry["rotation"] = nlohmann::json{
                {"x", ang.x * kRadToDeg},
                {"y", ang.y * kRadToDeg},
                {"z", ang.z * kRadToDeg},
            };

            // Carry scale + the InitiallyDisabled state so ModForge reproduces
            // both. (Actors never get here — see the scope check above.)
            entry["scale"] = ref.GetScale();
            if ((ref.GetFormFlags() & kInitiallyDisabled) != 0) {
                entry["initiallyDisabled"] = true;
            }
            scene["placements"].push_back(std::move(entry));
            return RE::BSContainer::ForEachResult::kContinue;
        });
    }

}  // namespace SceneExporter
