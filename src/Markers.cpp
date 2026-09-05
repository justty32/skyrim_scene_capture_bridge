#include "Markers.h"

#include "Aim.h"
#include "Physics.h"
#include "SceneExporter.h"
#include "log.h"

#include <algorithm>
#include <cmath>

namespace {
    constexpr float kRadToDeg = 57.2957795f;

    std::vector<Markers::Entry> g_entries;
    std::uint32_t g_nextSeq = 1;
    bool g_display = true;  // sc mk dp0/dp1 — persists via co-save

    // Marker data that came back from the co-save but whose proxy FormID no
    // longer resolves (dynamic refs are not reliably remapped across a full
    // restart). The physical gem DID persist in the savegame, so an adopt scan
    // re-finds it; we hold the note/kind/label here and merge them back by a
    // position match, so a restart no longer silently drops the note.
    struct PendingOrphan {
        RE::NiPoint3 position;
        std::string label, kind, note;
    };
    std::vector<PendingOrphan> g_pending;

    // Freeze the gem's clutter havok so it can't be kicked or fall. The deferred
    // retry (the 3D isn't loaded yet right after PlaceObjectAtMe) now lives in
    // Physics.h — Palette's place path needed the same thing.
    using Physics::FreezeDeferred;

    // The visible proxy base. Preferred: the tooling esp's MarkerACTI — model
    // is Weapons\Iron\IronDagger.nif (verified via houseCARL against WEAP
    // 01397E). A dagger over the soul gem because its tip VISUALISES the
    // marker's orientation (the user records + edits full rotation now). It has
    // weapon clutter havok so it would fall — PlaceAt freezes it (SetMotionType
    // kKeyframed). Fallback: vanilla SummonTargetFXActivator 0x0007CD55 (no
    // collision -> no E, hotkeys only) so the plugin still works without the esp.
    RE::TESBoundObject* ProxyBase() {
        static RE::TESBoundObject* base = nullptr;
        if (base) return base;

        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) return nullptr;

        base = dh->LookupForm<RE::TESObjectACTI>(0x800, "SceneCaptureTools.esp");
        if (base) {
            SKSE::log::info("Markers: proxy base = SceneCaptureTools.esp MarkerACTI");
        } else {
            base = dh->LookupForm<RE::TESObjectACTI>(0x07CD55, "Skyrim.esm");
            SKSE::log::info(
                "Markers: tooling esp absent — proxy base = vanilla "
                "SummonTargetFXActivator ({})", base ? "ok" : "MISSING");
            }
        return base;
    }
}

namespace Markers {

    static bool PlaceAt(const RE::NiPoint3& pos, const char* how) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* base = ProxyBase();
        if (!player || !base) {
            SKSE::log::error("Markers: no player or no proxy base — marker not placed");
            return false;
        }
        // Durable anchor of the player's current cell/worldspace.
        const auto a = SceneExporter::AnchorOf(nullptr);
        std::string anchor = a.id;
        bool interior = a.interior;

        RE::NiPointer<RE::TESObjectREFR> proxy = player->PlaceObjectAtMe(base, false);
        if (!proxy) {
            SKSE::log::error("Markers: PlaceObjectAtMe failed");
            return false;
        }
        proxy->SetPosition(pos);
        // Orient the dagger along the player's facing so its tip points somewhere
        // meaningful from the start (edit mode can re-aim it).
        const RE::NiPoint3 ang{0.f, 0.f, player->GetAngleZ()};
        proxy->SetAngle(ang);
        // The weapon model has clutter havok — freeze it or it falls / gets kicked.
        // The 3D is not loaded yet here, so freeze on the task queue once it is
        // (the EXPORTED pose is fixed right now regardless).
        FreezeDeferred(proxy->GetHandle(), 60);

        Entry e;
        e.seq = g_nextSeq++;
        e.label = std::format("marker-{}", e.seq);
        e.position = pos;                               // fixed now, not the proxy's live pose
        e.angleDeg = ang * kRadToDeg;                   // engine radians -> contract degrees
        e.scale = proxy->GetScale();
        e.cellOrWs = std::move(anchor);
        e.isInterior = interior;
        e.proxy = proxy->GetHandle();

        // Label doubles as the proxy's display name — display names persist in
        // the savegame, which is what AdoptOrphans() recovers labels from.
        proxy->SetDisplayName(e.label.c_str(), true);
        if (!g_display) proxy->Disable();  // dp0 active: new gems follow it

        SKSE::log::info("Markers: placed #{} '{}' ({}) at ({:.1f}, {:.1f}, {:.1f}) in {}",
            e.seq, e.label, how, pos.x, pos.y, pos.z,
            e.cellOrWs.empty() ? "(unresolved)" : e.cellOrWs);
        g_entries.push_back(std::move(e));
        return true;
    }

    bool PlaceAtPlayer() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        return player && PlaceAt(player->GetPosition(), "feet");
    }

    bool PlaceAimed() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;
        RE::NiPoint3 hit;
        if (Aim::LookHit(hit)) return PlaceAt(hit, "aimed");
        SKSE::log::info("Markers: ray hit nothing within range — falling back to feet");
        return PlaceAt(player->GetPosition(), "feet-fallback");
    }

    std::uint32_t AdoptOne(RE::TESObjectREFR* ref) {
        if (!ref || ref->IsDeleted() || ref->IsDisabled()) return 0;
        if (ref->GetBaseObject() != ProxyBase() || !ProxyBase()) return 0;
        if (const auto seq = SeqOf(ref)) return seq;  // already ours

        Entry e;
        e.seq = g_nextSeq++;
        const char* dn = ref->GetDisplayFullName();
        e.label = (dn && *dn) ? dn : std::format("marker-{}", e.seq);
        // note is NOT recoverable: only the display name (= label) lives in
        // the savegame. Documented in the README persistence table.
        e.position = ref->GetPosition();
        e.angleDeg = ref->data.angle * kRadToDeg;
        e.scale = ref->GetScale();
        const auto a = SceneExporter::AnchorOf(ref);
        e.cellOrWs = a.id;
        e.isInterior = a.interior;
        e.proxy = ref->GetHandle();
        // Merge back a co-save record whose proxy FormID didn't resolve: match
        // by position (gems are static) so the note/kind survive a restart.
        for (auto it = g_pending.begin(); it != g_pending.end(); ++it) {
            const auto d = it->position - e.position;
            if (d.x * d.x + d.y * d.y + d.z * d.z <= 16.f * 16.f) {
                if (!it->label.empty()) e.label = it->label;
                e.kind = it->kind;
                e.note = it->note;
                if (auto proxy = ref->GetHandle().get())
                    proxy->SetDisplayName(e.label.c_str(), true);
                g_pending.erase(it);
                break;
            }
        }
        // Re-created from the save with its nif's clutter havok — freeze again.
        FreezeDeferred(ref->GetHandle(), 60);
        SKSE::log::info("Markers: adopted '{}' at ({:.1f}, {:.1f}, {:.1f})",
            e.label, e.position.x, e.position.y, e.position.z);
        const auto seq = e.seq;
        g_entries.push_back(std::move(e));
        return seq;
    }

    std::size_t AdoptOrphans() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        RE::TESObjectCELL* cell = player ? player->GetParentCell() : nullptr;
        if (!cell || !ProxyBase()) return 0;

        std::size_t adopted = 0;
        cell->ForEachReference([&](RE::TESObjectREFR* ref) -> RE::BSContainer::ForEachResult {
            if (ref && ref->GetBaseObject() == ProxyBase() && !SeqOf(ref))
                if (AdoptOne(ref)) ++adopted;
            return RE::BSContainer::ForEachResult::kContinue;
        });
        return adopted;
    }

    std::vector<Entry>& All() { return g_entries; }

    bool IsProxy(RE::TESObjectREFR* ref) {
        if (!ref) return false;
        // Base check first: also catches orphaned proxies from a previous
        // session that are not in this session's registry.
        if (ref->GetBaseObject() == ProxyBase() && ProxyBase()) return true;
        const auto h = ref->GetHandle();
        for (const auto& e : g_entries)
            if (e.proxy == h) return true;
        return false;
    }

    Entry* FindBySeq(std::uint32_t seq) {
        for (auto& e : g_entries)
            if (e.seq == seq) return &e;
        return nullptr;
    }

    std::uint32_t SeqOf(RE::TESObjectREFR* ref) {
        if (!ref) return 0;
        const auto h = ref->GetHandle();
        for (const auto& e : g_entries)
            if (e.proxy == h) return e.seq;
        return 0;
    }

    void Rename(std::uint32_t seq, const std::string& label) {
        if (auto* e = FindBySeq(seq)) {
            e->label = label;
            if (auto proxy = e->proxy.get())
                proxy->SetDisplayName(label.c_str(), true);
        }
    }

    void SetKind(std::uint32_t seq, const std::string& kind) {
        if (auto* e = FindBySeq(seq))
            e->kind = kind.empty() ? "note" : kind;
    }

    void SetNote(std::uint32_t seq, const std::string& note) {
        if (auto* e = FindBySeq(seq))
            e->note = note;
    }

    void SetTransform(std::uint32_t seq, const RE::NiPoint3& position,
        const RE::NiPoint3& angleDeg, float scale) {
        if (auto* e = FindBySeq(seq)) {
            e->position = position;    // the exported pose follows the proxy
            e->angleDeg = angleDeg;
            e->scale = scale;
            SKSE::log::info("Markers: #{} moved to ({:.1f}, {:.1f}, {:.1f}) "
                "rot({:.0f},{:.0f},{:.0f}) scale {:.2f}", seq,
                position.x, position.y, position.z,
                angleDeg.x, angleDeg.y, angleDeg.z, scale);
        }
    }

    void Remove(std::uint32_t seq) {
        for (auto it = g_entries.begin(); it != g_entries.end(); ++it) {
            if (it->seq != seq) continue;
            if (auto proxy = it->proxy.get()) {
                proxy->Disable();
                proxy->SetDelete(true);
            }
            g_entries.erase(it);   // no trace: the true-deletion semantics
            return;
        }
    }

    void PruneDeadProxies() {
        std::size_t before = g_entries.size();
        std::erase_if(g_entries, [](const Entry& e) { return !e.proxy.get(); });
        if (before != g_entries.size())
            SKSE::log::info("Markers: pruned {} marker(s) whose proxy died with the old save",
                before - g_entries.size());
        // Survivors were re-created from the save with live clutter havok —
        // re-freeze so the gems don't drop after every load.
        for (auto& e : g_entries)
            FreezeDeferred(e.proxy, 60);
    }

    void SetProxiesVisible(bool visible) {
        g_display = visible;
        std::size_t touched = 0;
        for (auto& e : g_entries) {
            if (auto proxy = e.proxy.get()) {
                if (visible) proxy->Enable(false);
                else         proxy->Disable();
                ++touched;
            }
        }
        SKSE::log::info("Markers: {} {} gem(s)", visible ? "showed" : "hid", touched);
        if (visible) {
            // Enable re-spawns the 3D with live clutter havok — re-freeze.
            for (auto& e : g_entries)
                FreezeDeferred(e.proxy, 60);
        }
    }

    bool ProxiesVisible() { return g_display; }

    void AddPendingOrphan(const RE::NiPoint3& position, const std::string& label,
        const std::string& kind, const std::string& note) {
        g_pending.push_back({position, label, kind, note});
    }

    void ClearPending() { g_pending.clear(); }

    void OnRegistryRestored() {
        std::uint32_t maxSeq = 0;
        for (const auto& e : g_entries) maxSeq = std::max(maxSeq, e.seq);
        g_nextSeq = maxSeq + 1;
        PruneDeadProxies();  // drops unresolvable, re-freezes the rest
        if (!g_display) {
            for (auto& e : g_entries)
                if (auto proxy = e.proxy.get())
                    proxy->Disable();
        }
        SKSE::log::info("Markers: registry restored from co-save — {} marker(s)",
            g_entries.size());
    }

}  // namespace Markers
