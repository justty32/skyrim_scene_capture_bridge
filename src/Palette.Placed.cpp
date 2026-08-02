// Palette.Placed.cpp — the registry of refs WE PLACED. It is the export's
// definition of "yours". Split from Palette.cpp (300-line convention).
//
// 🔴 IT DID NOT USED TO BE THAT (2026-07-14, in-game). The exporter used to say
// "a dynamic ref (no durable id) IS a player placement" and emit it — a rule that
// is true of everything the player spawns and ALSO true of everything THE ENGINE
// spawns at runtime. A single exterior export proved it: 10 placements, of which
// the user had placed exactly ONE. The other nine were six copies of
// `DoNotPlaceSmallCritterLandingMarkerHelper` (the marker butterflies land on —
// the name is not subtle) and three fish from the Fishing CC. Fish and critters
// are PlaceAtMe'd by the engine and are byte-for-byte the same KIND of ref as
// yours. The heuristic could not tell, and never could have.
//
// So ownership is now RECORDED, not inferred (user-decided 2026-07-14 — the same
// "明示優於推導" the eraser's adopt button already stands on): every `sc pl` /
// ghost-commit registers a row here, and the exporter emits a dynamic ref ONLY if
// it has one. A fish cannot get a row, so a fish cannot ship.
//
// A row ALSO carries whatever extra the export must say about that placement:
//
//   noHavokSettle  (`sc pl py0`) -> the exported REFR gets the DontHavokSettle
//                  record flag (0x20000000). This is the ONLY half that ships:
//                  the in-session SetMotionType freeze dies with the savegame.
//   extra          (`sc pl ed1`) -> the slot carried an instance enchantment, so
//                  the export MINTS a capturedItems[] record for it and points
//                  the placement's `base` at that record's editorId.
//
// IDENTITY is the ObjectRefHandle, with a (base + position) fallback — a dynamic
// FormID is not reliably remapped across a full restart (the lesson Markers and
// Referrer both learned). The fallback runs lazily inside PlacedInfoFor, which
// the exporter calls while it is already walking the cell's refs, so it costs no
// extra sweep and needs no kPostLoadGame hook.

#include "Palette.h"

#include "Markers.h"
#include "Preview.h"
#include "SceneExporter.h"
#include "log.h"

#include <algorithm>
#include <cctype>

namespace {
    std::vector<Palette::PlacedInfo> g_placed;
    std::uint32_t g_nextSeq = 1;

    // Same tolerance Markers/Referrer use to re-pair an orphan: our objects do
    // not wander once placed (frozen or settled), so 16 units is generous.
    constexpr float kReacquireDist2 = 16.f * 16.f;
}

namespace Palette {

    std::vector<PlacedInfo>& Placed() { return g_placed; }

    void RegisterPlaced(RE::TESObjectREFR* ref, const Slot& slot, bool noHavokSettle) {
        if (!ref) return;
        PlacedInfo p;
        p.seq = g_nextSeq++;
        p.handle = ref->GetHandle();
        p.name = slot.name;
        p.baseId = slot.baseId;
        p.position = ref->GetPosition();
        p.noHavokSettle = noHavokSettle;
        p.extra = slot.extra;   // .present = false unless `sc pl ed1` carried it
        SKSE::log::info("Palette: placed ref #{} registered ('{}'{}{})", p.seq, p.name,
            p.noHavokSettle ? ", noHavokSettle" : "",
            p.extra.present ? ", extra -> " + MintedEditorIdOf(p) : "");
        g_placed.push_back(std::move(p));
    }

    const PlacedInfo* PlacedInfoFor(RE::TESObjectREFR* ref) {
        if (!ref) return nullptr;
        const auto h = ref->GetHandle();
        for (auto& p : g_placed)
            if (p.handle == h) return &p;

        // Handle miss. Either this ref is not ours, or the row came back from a
        // co-save whose dynamic FormID did not survive a full restart. Only OUR
        // refs (no durable id) can be a candidate — re-bind by base + position.
        if (SceneExporter::ResolveDurableId(ref)) return nullptr;
        auto base = SceneExporter::ResolveDurableId(ref->GetBaseObject());
        if (!base) return nullptr;
        const auto pos = ref->GetPosition();
        for (auto& p : g_placed) {
            if (p.handle.get()) continue;          // alive but bound to a different ref
            if (p.baseId != *base) continue;
            const auto d = pos - p.position;
            if (d.x * d.x + d.y * d.y + d.z * d.z > kReacquireDist2) continue;
            p.handle = ref->GetHandle();           // re-acquired
            SKSE::log::info("Palette: re-acquired placed ref #{} ('{}') after a restart",
                p.seq, p.name);
            return &p;
        }
        return nullptr;
    }

    std::string MintedEditorIdOf(const PlacedInfo& p) {
        // The slot name is free-form ("Ebony Sword of Fire"); an EditorID is not.
        // Fold everything an EditorID can't hold to '_' and suffix the seq, so two
        // slots that sanitise alike still get distinct records. The seq rides the
        // co-save, so the id is stable across exports — a rebuild keeps pointing
        // at the same minted record. (Same shape as Referrer::EditorIdOf.)
        std::string out = "MFPal_";
        for (const char ch : p.name) {
            const auto uc = static_cast<unsigned char>(ch);
            out.push_back(std::isalnum(uc) ? ch : '_');
        }
        return out + "_" + std::to_string(p.seq);
    }

    std::size_t AdoptDynamicInCell() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        RE::TESObjectCELL* cell = player ? player->GetParentCell() : nullptr;
        if (!cell) return 0;

        std::size_t adopted = 0;
        cell->ForEachReference([&](RE::TESObjectREFR* ref) -> RE::BSContainer::ForEachResult {
            if (!ref || ref->IsDeleted() || ref->IsDisabled() || ref->IsPlayerRef())
                return RE::BSContainer::ForEachResult::kContinue;
            // Only dynamic refs are candidates (an authored one exports as itself),
            // and only ones with a nameable base (a runtime-only base could never
            // build). Editor chrome and actors are never content.
            if (SceneExporter::ResolveDurableId(ref)) return RE::BSContainer::ForEachResult::kContinue;
            if (ref->GetFormType() == RE::FormType::ActorCharacter) return RE::BSContainer::ForEachResult::kContinue;
            if (Markers::IsProxy(ref) || Preview::IsGhost(ref)) return RE::BSContainer::ForEachResult::kContinue;
            auto base = SceneExporter::ResolveDurableId(ref->GetBaseObject());
            if (!base) return RE::BSContainer::ForEachResult::kContinue;
            if (PlacedInfoFor(ref)) return RE::BSContainer::ForEachResult::kContinue;  // already ours

            Slot s;
            s.baseId = *base;
            const char* dn = ref->GetDisplayFullName();
            s.name = (dn && *dn) ? dn : *base;
            RegisterPlaced(ref, s, false);
            ++adopted;
            return RE::BSContainer::ForEachResult::kContinue;
        });
        SKSE::log::info("Palette: adopted {} dynamic ref(s) in this cell — they now export "
            "as placements (check the list: a fish adopted is a fish shipped)", adopted);
        return adopted;
    }

    void DropAllPlaced() { g_placed.clear(); }

    void OnPlacedRegistryRestored() {
        std::uint32_t hi = 0;
        std::size_t orphans = 0;
        for (const auto& p : g_placed) {
            hi = std::max(hi, p.seq);
            if (!p.handle.get()) ++orphans;
        }
        g_nextSeq = hi + 1;
        SKSE::log::info("Palette: placed-ref registry restored — {} row(s){}", g_placed.size(),
            orphans ? std::format(", {} awaiting re-acquire (base+position, at export)", orphans)
                    : std::string{});
    }

}  // namespace Palette
