#include "Overrides.h"

#include "log.h"

#include <algorithm>

namespace {
    std::vector<Overrides::Entry> g_entries;

    bool AddsMaster(const std::string& plugin) {
        static const char* kBase[] = {
            "Skyrim.esm", "Update.esm", "Dawnguard.esm",
            "HearthFires.esm", "Dragonborn.esm",
        };
        for (const auto* b : kBase)
            if (plugin == b) return false;
        return true;
    }

    std::string PluginOf(const std::string& id) {
        const auto colon = id.find(':');
        return colon == std::string::npos ? id : id.substr(0, colon);
    }

    auto Find(const std::string& id) {
        return std::find_if(g_entries.begin(), g_entries.end(),
            [&](const Overrides::Entry& e) { return e.id == id; });
    }
}

namespace Overrides {

    void Register(const std::string& id, RE::TESObjectREFR* ref,
                  const RE::NiPoint3& origPos, const RE::NiPoint3& origAngle,
                  float origScale) {
        if (!ref) return;
        if (auto it = Find(id); it != g_entries.end()) {
            // Re-edit: refresh the committed transform, keep the baseline.
            it->pos = ref->GetPosition();
            it->angle = ref->data.angle;
            it->scale = ref->GetScale();
            it->handle = ref->GetHandle();
            SKSE::log::info("Overrides: {} updated (baseline kept)", id);
            return;
        }
        Entry e;
        e.id = id;
        e.plugin = PluginOf(id);
        e.addsMaster = AddsMaster(e.plugin);
        e.isActor = ref->GetFormType() == RE::FormType::ActorCharacter;
        e.handle = ref->GetHandle();
        e.origPos = origPos;
        e.origAngle = origAngle;
        e.origScale = origScale;
        e.pos = ref->GetPosition();
        e.angle = ref->data.angle;
        e.scale = ref->GetScale();
        const char* dn = ref->GetDisplayFullName();
        e.name = (dn && *dn) ? dn : e.id;
        SKSE::log::info("Overrides: registered {}{}", e.id,
            e.addsMaster ? " (adds a master!)" : "");
        g_entries.push_back(std::move(e));
    }

    std::vector<Entry>& All() { return g_entries; }

    bool Contains(const std::string& id) { return Find(id) != g_entries.end(); }

    void SetLabel(const std::string& id, const std::string& label) {
        if (auto it = Find(id); it != g_entries.end()) it->label = label;
    }

    void SetNote(const std::string& id, const std::string& note) {
        if (auto it = Find(id); it != g_entries.end()) it->note = note;
    }

    bool Revert(std::size_t index) {
        if (index >= g_entries.size()) return false;
        Entry e = std::move(g_entries[index]);
        g_entries.erase(g_entries.begin() + static_cast<std::ptrdiff_t>(index));
        if (auto ref = e.handle.get()) {
            ref->SetPosition(e.origPos);
            ref->SetAngle(e.origAngle);
            if (!e.isActor) ref->SetScale(e.origScale);
            ref->Update3DPosition(true);
            SKSE::log::info("Overrides: {} reverted to baseline", e.id);
        } else {
            SKSE::log::info("Overrides: {} unregistered (ref not loaded — not moved back)", e.id);
        }
        return true;
    }

    void Clear() {
        while (!g_entries.empty()) Revert(g_entries.size() - 1);
    }

    void DropAll() { g_entries.clear(); }

}  // namespace Overrides
