#include "Referrer.h"

#include "Aim.h"
#include "Markers.h"
#include "Preview.h"
#include "SceneExporter.h"
#include "log.h"

#include <algorithm>
#include <cctype>

namespace {
    constexpr float kRadToDeg = 57.2957795f;

    std::vector<Referrer::Entry> g_entries;
    std::uint32_t g_nextSeq = 1;

    // Same tolerance Markers uses to re-pair an orphaned proxy: our objects do
    // not move once placed (havok-frozen or settled), so 16 units is generous.
    constexpr float kReacquireDist2 = 16.f * 16.f;
}

namespace Referrer {

    std::vector<Entry>& All() { return g_entries; }

    Entry* FindBySeq(std::uint32_t seq) {
        for (auto& e : g_entries)
            if (e.seq == seq) return &e;
        return nullptr;
    }

    std::uint32_t LabelOwner(const std::string& label, std::uint32_t exceptSeq) {
        for (const auto& e : g_entries)
            if (e.seq != exceptSeq && e.label == label) return e.seq;
        return 0;
    }

    std::string EditorIdOf(const Entry& e) {
        // The label is free-form ("sofia's chair"); an EditorID is not. Fold
        // everything an EditorID can't hold to '_' and suffix the seq, so two
        // labels that sanitise alike still get distinct ids.
        std::string out = "MFRef_";
        for (const char ch : e.label) {
            const auto uc = static_cast<unsigned char>(ch);
            out.push_back(std::isalnum(uc) ? ch : '_');
        }
        return out + "_" + std::to_string(e.seq);
    }

    const Entry* InFileEntryFor(RE::TESObjectREFR* ref) {
        if (!ref) return nullptr;
        const auto h = ref->GetHandle();
        for (const auto& e : g_entries)
            if (e.id.empty() && e.handle == h) return &e;  // id empty == in-file (B) target
        return nullptr;
    }

    static Result MarkRef(RE::NiPointer<RE::TESObjectREFR> ref, const char* how,
                          const std::string& label) {
        if (!ref) {
            SKSE::log::info("Referrer: {} has no target", how);
            return Result::kNone;
        }
        // Editor chrome. A marker proxy is excluded from placements[] by design,
        // so an in-file reference to one could never resolve — and a marker
        // already IS a labelled thing (it exports as annotations[]). Refuse.
        if (Preview::IsGhost(ref.get())) {
            SKSE::log::info("Referrer: target is the preview ghost — place it first, "
                            "then name the real thing");
            return Result::kNone;
        }
        if (Markers::IsProxy(ref.get())) {
            SKSE::log::info("Referrer: target is a marker gem — refused (markers export "
                            "as annotations[]; label the marker itself)");
            return Result::kMarkerProxy;
        }

        const bool isActor = ref->GetFormType() == RE::FormType::ActorCharacter;
        // The discriminator, same one the exporter's vanilla diff uses: an
        // authored ref resolves to a durable id (EXTERNAL, class A); a ref the
        // player spawned this session does not (OURS, class B — in-file).
        const auto durable = SceneExporter::ResolveDurableId(ref.get());

        if (!durable && isActor) {
            // Our own spawned actor: a cell export deliberately carries no
            // actors (2026-07-12 scope reversal), so there would be no
            // placements[] row for the reference to point at. An AUTHORED actor
            // (vanilla NPC) is fine — that one has a durable id and falls
            // through to the external path below.
            SKSE::log::info("Referrer: target is an actor WE spawned — refused (cell exports "
                            "carry no actors; use `sc cap` / place it from a marker)");
            return Result::kOwnActor;
        }

        // Already referred to? Identity is the durable id (A) or the handle (B).
        const auto h = ref->GetHandle();
        for (const auto& e : g_entries) {
            const bool same = durable ? (e.id == *durable) : (e.id.empty() && e.handle == h);
            if (same) {
                SKSE::log::info("Referrer: already referred to as '{}'", e.label);
                return Result::kDuplicate;
            }
        }

        const std::uint32_t seq = g_nextSeq;
        std::string lbl = label;
        if (lbl.empty()) {
            lbl = std::format("ref-{}", seq);  // renameable in the panel
        } else if (const auto owner = LabelOwner(lbl)) {
            // Labels are a GLOBAL name space in ModForge (the label is the
            // resolvable id) — a duplicate would fail validate on the whole
            // spec, so it is refused here, where the user can see why.
            SKSE::log::warn("Referrer: label '{}' is already used by #{} — refused", lbl, owner);
            return Result::kLabelTaken;
        }

        Entry e;
        e.seq = seq;
        e.label = std::move(lbl);
        e.name = [&] {
            const char* dn = ref->GetDisplayFullName();
            return (dn && *dn) ? std::string{dn} : std::string{"(unnamed)"};
        }();
        if (durable) e.id = *durable;                       // (A) external
        if (auto b = SceneExporter::ResolveDurableId(ref->GetBaseObject())) e.base = *b;
        e.position = ref->GetPosition();
        e.angleDeg = ref->data.angle * kRadToDeg;           // engine radians -> contract degrees
        e.scale = ref->GetScale();
        const auto a = SceneExporter::AnchorOf(ref.get());
        e.cellOrWs = a.id;
        e.isInterior = a.interior;
        e.isActor = isActor;
        e.handle = h;                                       // (B): THE identity

        SKSE::log::info("Referrer: #{} '{}' -> {} ({}){}", e.seq, e.label,
            e.id.empty() ? "IN-FILE (our own placement)" : e.id, how,
            e.base.empty() ? " [base unresolved]" : "");
        g_entries.push_back(std::move(e));
        ++g_nextSeq;
        return Result::kMarked;
    }

    Result MarkCrosshair(const std::string& label) {
        return MarkRef(Aim::CrosshairRef(), "crosshair", label);
    }

    Result MarkByRay(const std::string& label) {
        return MarkRef(Aim::RayRef(), "ray", label);
    }

    Result MarkConsoleRef(const std::string& label) {
        auto ref = RE::Console::GetSelectedRef();
        if (!ref) {
            SKSE::log::info("Referrer: no console ref selected");
            return Result::kNone;
        }
        return MarkRef(ref, "console", label);
    }

    bool Rename(std::uint32_t seq, const std::string& label) {
        auto* e = FindBySeq(seq);
        if (!e || label.empty()) return false;
        if (label == e->label) return true;
        if (const auto owner = LabelOwner(label, seq)) {
            SKSE::log::warn("Referrer: rename #{} -> '{}' refused — #{} already owns that label",
                seq, label, owner);
            return false;
        }
        SKSE::log::info("Referrer: #{} renamed '{}' -> '{}'", seq, e->label, label);
        e->label = label;
        return true;
    }

    void SetNote(std::uint32_t seq, const std::string& note) {
        if (auto* e = FindBySeq(seq)) e->note = note;
    }

    void Remove(std::uint32_t seq) {
        // Registry-only: the referrer never touched the world, so dropping a row
        // has nothing to undo.
        std::erase_if(g_entries, [seq](const Entry& e) { return e.seq == seq; });
    }

    void Clear() { g_entries.clear(); }

    std::size_t ReacquireOrphans() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        RE::TESObjectCELL* cell = player ? player->GetParentCell() : nullptr;
        if (!cell) return 0;

        std::size_t found = 0;
        for (auto& e : g_entries) {
            if (!e.id.empty() || e.handle.get()) continue;  // external, or still alive
            cell->ForEachReference([&](RE::TESObjectREFR* ref) -> RE::BSContainer::ForEachResult {
                if (!ref || ref->IsDeleted() || ref->IsDisabled()) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                // Only OUR refs (no durable id) can be an in-file target, and the
                // base + position must match the row we are trying to re-bind.
                if (SceneExporter::ResolveDurableId(ref)) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                auto b = SceneExporter::ResolveDurableId(ref->GetBaseObject());
                if (!b || *b != e.base) return RE::BSContainer::ForEachResult::kContinue;
                const auto d = ref->GetPosition() - e.position;
                if (d.x * d.x + d.y * d.y + d.z * d.z > kReacquireDist2) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                e.handle = ref->GetHandle();
                ++found;
                SKSE::log::info("Referrer: re-acquired in-file target of '{}'", e.label);
                return RE::BSContainer::ForEachResult::kStop;
            });
        }
        return found;
    }

    void DropAll() { g_entries.clear(); }

    void OnRegistryRestored() {
        std::uint32_t maxSeq = 0;
        std::size_t orphans = 0;
        for (const auto& e : g_entries) {
            maxSeq = std::max(maxSeq, e.seq);
            if (e.id.empty() && !e.handle.get()) ++orphans;
        }
        g_nextSeq = maxSeq + 1;
        SKSE::log::info("Referrer: registry restored from co-save — {} reference(s){}",
            g_entries.size(),
            orphans ? std::format(", {} in-file target(s) awaiting re-acquire", orphans)
                    : std::string{});
    }

}  // namespace Referrer
