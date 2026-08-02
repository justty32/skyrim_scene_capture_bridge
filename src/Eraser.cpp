#include "Eraser.h"

#include "Aim.h"
#include "Markers.h"
#include "Preview.h"
#include "SceneExporter.h"
#include "log.h"

namespace {
    std::vector<Eraser::Entry> g_entries;
    std::unordered_set<std::string> g_ids;

    bool AddsMaster(const std::string& plugin) {
        // The 5 base-game masters every load order has. CC .esl content is
        // deliberately NOT in this set: erasing a CC ref really does add that
        // esl as a master of the patch.
        static const std::unordered_set<std::string> kBase = {
            "Skyrim.esm", "Update.esm", "Dawnguard.esm",
            "HearthFires.esm", "Dragonborn.esm",
        };
        return !kBase.contains(plugin);
    }

    std::string PluginOf(const std::string& id) {
        const auto colon = id.find(':');
        return colon == std::string::npos ? id : id.substr(0, colon);
    }
}

namespace Eraser {

    static MarkResult MarkRef(RE::NiPointer<RE::TESObjectREFR> ref, const char* how) {
        if (!ref) {
            SKSE::log::info("Eraser: {} has no target", how);
            return MarkResult::kNone;
        }
        // The preview ghost is not in the world in any sense that matters —
        // erasing a picture of a decision is not an erasure. Clear it instead.
        if (Preview::IsGhost(ref.get())) {
            SKSE::log::info("Eraser: target is the preview ghost — nothing to erase");
            return MarkResult::kNone;
        }
        // A marker proxy is editor chrome — route to the marker system so it
        // vanishes from the registry too, instead of half-erasing it here.
        if (Markers::IsProxy(ref.get())) {
            for (const auto& m : Markers::All())
                if (m.proxy == ref->GetHandle()) {
                    Markers::Remove(m.seq);
                    SKSE::log::info("Eraser: crosshair was a marker proxy — removed marker instead");
                    return MarkResult::kMarkerProxy;
                }
            ref->Disable();  // orphan proxy (not in registry): just hide it
            return MarkResult::kMarkerProxy;
        }

        if (auto id = SceneExporter::ResolveDurableId(ref.get())) {
            if (g_ids.contains(*id)) return MarkResult::kDuplicate;
            const char* dn = ref->GetDisplayFullName();
            const RE::NiPoint3 pos = ref->GetPosition();  // capture before Disable
            ref->Disable();  // the visual feedback: it vanishes right now
            Entry e;
            e.id = *id;
            e.plugin = PluginOf(*id);
            e.cellOrWs = SceneExporter::AnchorOf(ref.get()).id;
            e.name = (dn && *dn) ? dn : "(unnamed)";
            e.position = pos;
            e.handle = ref->GetHandle();
            e.addsMaster = AddsMaster(e.plugin);
            SKSE::log::info("Eraser: marked {} for removal ({}){}", e.id, how,
                e.addsMaster ? " (adds a master!)" : "");
            g_ids.insert(e.id);
            g_entries.push_back(std::move(e));
            return MarkResult::kMarked;
        }

        // Dynamic ref = something the player placed this session. True
        // deletion semantics: disable, drop from every registry, no trace.
        ref->Disable();
        SKSE::log::info("Eraser: own dynamic ref erased (no trace)");
        return MarkResult::kOwnDeleted;
    }

    MarkResult MarkCrosshair() { return MarkRef(Aim::CrosshairRef(), "crosshair"); }
    MarkResult MarkByRay() { return MarkRef(Aim::RayRef(), "ray"); }

    MarkResult MarkConsoleRef() {
        auto ref = RE::Console::GetSelectedRef();
        if (!ref) return MarkResult::kNone;
        // "Objects only for now" (user-decided): actors have their own lifecycle
        // (disable/kill) and don't belong in removals[].
        if (ref->GetFormType() == RE::FormType::ActorCharacter) {
            SKSE::log::info("Eraser: console ref is an actor — objects only for now");
            return MarkResult::kNone;
        }
        return MarkRef(ref, "console");
    }

    std::vector<Entry>& All() { return g_entries; }
    const std::unordered_set<std::string>& MarkedIds() { return g_ids; }

    void SetLabel(const std::string& id, const std::string& label) {
        for (auto& e : g_entries)
            if (e.id == id) { e.label = label; return; }
    }

    void SetNote(const std::string& id, const std::string& note) {
        for (auto& e : g_entries)
            if (e.id == id) { e.note = note; return; }
    }

    // Shared tail of every undo path: re-enable the ref (if loaded) and drop
    // the entry + its id. `it` must be valid; invalidated on return.
    static bool ReenableErase(std::vector<Entry>::iterator it) {
        g_ids.erase(it->id);
        if (auto ref = it->handle.get()) {
            ref->Enable(false);
            SKSE::log::info("Eraser: undo — {} re-enabled", it->id);
        } else {
            SKSE::log::info("Eraser: undo — {} unmarked (ref not loaded)", it->id);
        }
        g_entries.erase(it);
        return true;
    }

    bool Undo() {
        if (g_entries.empty()) return false;
        return ReenableErase(std::prev(g_entries.end()));
    }

    bool UndoEntry(const std::string& id) {
        for (auto it = g_entries.begin(); it != g_entries.end(); ++it)
            if (it->id == id) return ReenableErase(it);
        return false;
    }

    bool UndoInCell(const std::string& cellId) {
        // Last mark made in this cell (newest-first, matching the panel order).
        for (auto it = g_entries.end(); it != g_entries.begin();) {
            --it;
            if (it->cellOrWs == cellId) return ReenableErase(it);
        }
        return false;
    }

    void Clear() {
        while (!g_entries.empty()) Undo();
    }

    void DropAll() {
        g_entries.clear();
        g_ids.clear();
    }

    void OnRegistryRestored() {
        g_ids.clear();
        for (const auto& e : g_entries) g_ids.insert(e.id);
        SKSE::log::info("Eraser: registry restored from co-save — {} mark(s)",
            g_entries.size());
    }

}  // namespace Eraser
