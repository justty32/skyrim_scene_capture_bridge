#include "Catalog.h"

#include "SceneExporter.h"  // ResolveDurableId — the same key the exporter writes
#include "log.h"

#include <algorithm>
#include <cctype>

namespace {
    std::vector<Catalog::Entry> g_entries;
    std::vector<std::string> g_plugins;
    std::vector<RE::FormType> g_types;
    bool g_built = false;

    // The form types a scene can PLACE. Deliberately no actors: a cell export
    // carries none (the 2026-07-12 scope reversal — SceneExporter.cpp), NPCs go
    // through markers / `sc cap`. Order is the order the type filter shows.
    constexpr RE::FormType kTypes[] = {
        RE::FormType::Static,
        RE::FormType::MovableStatic,
        RE::FormType::StaticCollection,
        RE::FormType::Tree,
        RE::FormType::Flora,
        RE::FormType::Furniture,
        RE::FormType::Activator,
        RE::FormType::TalkingActivator,
        RE::FormType::Door,
        RE::FormType::Container,
        RE::FormType::Light,
        RE::FormType::Misc,
        RE::FormType::Weapon,
        RE::FormType::Armor,
        RE::FormType::Ammo,
        RE::FormType::Book,
        RE::FormType::AlchemyItem,
        RE::FormType::Ingredient,
        RE::FormType::SoulGem,
        RE::FormType::KeyMaster,
        RE::FormType::Scroll,
    };

    std::string Lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    // Whitespace-separated terms, all of which must appear. Lower-cased once by
    // the caller; the haystack is stored lower-cased at build time.
    std::vector<std::string> Terms(const std::string& query) {
        std::vector<std::string> out;
        std::size_t i = 0;
        while (i < query.size()) {
            while (i < query.size() && std::isspace(static_cast<unsigned char>(query[i]))) ++i;
            const auto start = i;
            while (i < query.size() && !std::isspace(static_cast<unsigned char>(query[i]))) ++i;
            if (i > start) out.push_back(query.substr(start, i - start));
        }
        return out;
    }
}

namespace Catalog {

    bool Built() { return g_built; }

    void EnsureBuilt() {
        if (g_built) return;
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) return;  // too early — try again on the next render
        g_built = true;

        std::size_t noModel = 0, noId = 0;
        for (const auto type : kTypes) {
            auto& arr = dh->GetFormArray(type);
            if (arr.empty()) continue;
            const auto before = g_entries.size();
            for (auto* form : arr) {
                auto* base = form ? form->As<RE::TESBoundObject>() : nullptr;
                if (!base) continue;

                // A base with no durable id is runtime-only: placing copies of it
                // would export placements that can never build. Same refusal the
                // eyedropper makes (Palette::PickRef).
                auto id = SceneExporter::ResolveDurableId(base);
                if (!id) { ++noId; continue; }

                // A base with no model would place an INVISIBLE object (no error,
                // nothing to see, nothing to preview) — the classic wrong-nif trap.
                // Nothing to browse here, so it is not in the browser.
                const auto* model = base->As<RE::TESModel>();
                const char* path = model ? model->GetModel() : nullptr;
                if (!path || !*path) { ++noModel; continue; }

                Entry e;
                e.base = base;
                e.type = type;
                e.id = std::move(*id);
                if (const auto colon = e.id.rfind(':'); colon != std::string::npos)
                    e.plugin = e.id.substr(0, colon);
                if (const auto* full = base->As<RE::TESFullName>()) {
                    if (const char* n = full->GetFullName(); n && *n) e.name = n;
                }
                e.model = path;
                e.search = Lower(e.name + "|" + e.model + "|" + e.id);
                g_entries.push_back(std::move(e));
            }
            if (g_entries.size() > before) g_types.push_back(type);
        }

        // Sorted by model path: it clusters a folder's worth of related objects
        // together (all of Landscape\Mountains\ in a row), which is exactly how
        // the CK's tree reads and what makes scrolling worth anything.
        std::sort(g_entries.begin(), g_entries.end(),
            [](const Entry& a, const Entry& b) { return a.search < b.search; });

        for (const auto& e : g_entries)
            if (std::find(g_plugins.begin(), g_plugins.end(), e.plugin) == g_plugins.end())
                g_plugins.push_back(e.plugin);
        std::sort(g_plugins.begin(), g_plugins.end());

        SKSE::log::info("Catalog: {} placeable base(s) from {} plugin(s), {} type(s) "
            "(skipped {} model-less, {} runtime-only)",
            g_entries.size(), g_plugins.size(), g_types.size(), noModel, noId);
    }

    const std::vector<Entry>& All() { return g_entries; }
    const std::vector<std::string>& Plugins() { return g_plugins; }
    const std::vector<RE::FormType>& Types() { return g_types; }

    const char* TypeName(RE::FormType t) {
        switch (t) {
        case RE::FormType::Static:           return "static";
        case RE::FormType::MovableStatic:    return "movable static";
        case RE::FormType::StaticCollection: return "static collection";
        case RE::FormType::Tree:             return "tree";
        case RE::FormType::Flora:            return "flora";
        case RE::FormType::Furniture:        return "furniture";
        case RE::FormType::Activator:        return "activator";
        case RE::FormType::TalkingActivator: return "talking activator";
        case RE::FormType::Door:             return "door";
        case RE::FormType::Container:        return "container";
        case RE::FormType::Light:            return "light";
        case RE::FormType::Misc:             return "misc";
        case RE::FormType::Weapon:           return "weapon";
        case RE::FormType::Armor:            return "armor";
        case RE::FormType::Ammo:             return "ammo";
        case RE::FormType::Book:             return "book";
        case RE::FormType::AlchemyItem:      return "potion";
        case RE::FormType::Ingredient:       return "ingredient";
        case RE::FormType::SoulGem:          return "soul gem";
        case RE::FormType::KeyMaster:        return "key";
        case RE::FormType::Scroll:           return "scroll";
        default:                             return "all types";
        }
    }

    std::vector<std::size_t> Filter(const std::string& query, RE::FormType type,
        const std::string& plugin) {
        const auto terms = Terms(Lower(query));
        std::vector<std::size_t> out;
        for (std::size_t i = 0; i < g_entries.size(); ++i) {
            const auto& e = g_entries[i];
            if (type != RE::FormType::None && e.type != type) continue;
            if (!plugin.empty() && e.plugin != plugin) continue;
            bool all = true;
            for (const auto& t : terms) {
                if (e.search.find(t) == std::string::npos) { all = false; break; }
            }
            if (all) out.push_back(i);
        }
        return out;
    }

}  // namespace Catalog
