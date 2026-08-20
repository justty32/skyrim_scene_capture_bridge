#include "Palette.h"

#include "Aim.h"
#include "Markers.h"
#include "Modes.h"
#include "Physics.h"
#include "Preview.h"
#include "SceneExporter.h"
#include "log.h"

#include <fstream>

namespace {
    constexpr float kRadToDeg = 57.2957795f;

    std::vector<Palette::Slot> g_slots;
    std::size_t g_selected = 0;
    // The last Clear()'s slots, held so the panel can offer an undo. Session-
    // lived on purpose: it is a safety net for a misclick, not a second store.
    std::vector<Palette::Slot> g_cleared;

    bool AddsMaster(const std::string& id) {
        static const char* kBase[] = {
            "Skyrim.esm", "Update.esm", "Dawnguard.esm",
            "HearthFires.esm", "Dragonborn.esm",
        };
        for (const auto* b : kBase)
            if (id.starts_with(b)) return false;
        return true;
    }

    std::filesystem::path StorePath() {
        auto dir = SKSE::log::log_directory();
        return dir ? (*dir / "scene-capture-palette.json") : std::filesystem::path{};
    }

    // "<plugin>:0xHEX" -> live form. The stored id is already the LOCAL id
    // (ESL 12-bit / full 24-bit — ResolveDurableId's output), which is exactly
    // what TESDataHandler::LookupForm composes against the current load order.
    RE::TESForm* ResolveForm(const std::string& id) {
        const auto colon = id.rfind(':');
        if (colon == std::string::npos) return nullptr;
        const std::string plugin = id.substr(0, colon);
        std::uint32_t local = 0;
        try {
            local = static_cast<std::uint32_t>(std::stoul(id.substr(colon + 1), nullptr, 16));
        } catch (...) { return nullptr; }
        auto* dh = RE::TESDataHandler::GetSingleton();
        return dh ? dh->LookupForm(local, plugin) : nullptr;
    }

    RE::TESBoundObject* ResolveBase(const std::string& id) {
        RE::TESForm* form = ResolveForm(id);
        return form ? form->As<RE::TESBoundObject>() : nullptr;
    }

    // The enchantment ACTUALLY ON THIS INSTANCE (`sc pk ed1`). Exactly the split
    // Captures::CaptureEnchant makes: a player-applied enchant is ExtraEnchantment
    // on the ref (the base stays a plain iron sword); a looted pre-enchanted item
    // carries formEnchanting on its base. Prefer the instance's.
    //
    // ⚠️ `ench` (the live pointer) is cached ONLY for a DURABLE enchantment. A
    // player-crafted ENCH is a runtime form that lives in the SAVEGAME — caching
    // its pointer on a slot that gets written to disk and reloaded in another
    // playthrough would be a dangling pointer. For those the slot keeps the MGEF
    // effects instead, and the export MINTS a fresh ENCH from them (which is the
    // only thing that could ever land in an esp anyway).
    Palette::Extra ReadExtra(RE::TESObjectREFR* ref, RE::TESBoundObject* base) {
        Palette::Extra x;
        const bool isWeapon = base && base->GetFormType() == RE::FormType::Weapon;
        const bool isArmor = base && base->GetFormType() == RE::FormType::Armor;
        if (!isWeapon && !isArmor) return x;  // only weapon/armor carry an ExtraEnchantment

        RE::EnchantmentItem* ench = nullptr;
        std::uint16_t charge = 0;
        if (auto* xe = ref->extraList.GetByType<RE::ExtraEnchantment>(); xe && xe->enchantment) {
            ench = xe->enchantment;
            charge = xe->charge;
        } else if (auto* ef = base->As<RE::TESEnchantableForm>()) {
            ench = ef->formEnchanting;
            charge = ef->amountofEnchantment;
        }
        if (!ench) return x;

        x.present = true;
        x.kind = isWeapon ? "weapon" : "armor";
        x.enchAmount = charge;
        if (auto id = SceneExporter::ResolveDurableId(ench)) {
            x.enchBase = *id;   // authored ENCH — reference it, no minting needed
            x.ench = ench;      // durable => safe to cache the live pointer
        } else {
            // Runtime ENCH (player-crafted): unnameable in an esp. Keep the MGEF
            // effects so ModForge can mint an equivalent ENCH from them.
            for (const auto* eff : ench->effects) {
                if (!eff || !eff->baseEffect) continue;
                auto mid = SceneExporter::ResolveDurableId(eff->baseEffect);
                if (!mid) continue;
                Captures::Effect e;
                e.magicEffect = *mid;
                e.magnitude = eff->effectItem.magnitude;
                e.area = static_cast<std::int32_t>(eff->effectItem.area);
                e.duration = static_cast<std::int32_t>(eff->effectItem.duration);
                x.effects.push_back(std::move(e));
            }
            if (x.effects.empty()) {  // nothing nameable survived — no honest mint possible
                x.present = false;
                SKSE::log::warn("Palette: instance enchant has no resolvable MGEF — extra data dropped");
            }
        }
        return x;
    }

    // The file lists slots in PANEL order — newest (top of the list) first —
    // so a palette json reads like what the panel shows. The vector keeps the
    // opposite order (index 0 = oldest = bottom), hence the reverse walk here
    // and the reverse insert in Adopt().
    nlohmann::json ExtraJson(const Palette::Extra& x) {
        nlohmann::json e;
        e["kind"] = x.kind;
        if (!x.enchBase.empty()) e["enchBase"] = x.enchBase;
        if (x.enchAmount) e["enchAmount"] = x.enchAmount;
        if (!x.effects.empty()) {
            auto a = nlohmann::json::array();
            for (const auto& ef : x.effects)
                a.push_back({{"magicEffect", ef.magicEffect}, {"magnitude", ef.magnitude},
                    {"area", ef.area}, {"duration", ef.duration}});
            e["effects"] = std::move(a);
        }
        return e;
    }

    Palette::Extra ParseExtra(const nlohmann::json& e) {
        Palette::Extra x;
        x.kind = e.value("kind", "");
        x.enchBase = e.value("enchBase", "");
        x.enchAmount = e.value("enchAmount", std::uint16_t{0});
        if (auto a = e.find("effects"); a != e.end() && a->is_array()) {
            for (const auto& ef : *a) {
                Captures::Effect c;
                c.magicEffect = ef.value("magicEffect", "");
                if (c.magicEffect.empty()) continue;
                c.magnitude = ef.value("magnitude", 0.f);
                c.area = ef.value("area", 0);
                c.duration = ef.value("duration", 0);
                x.effects.push_back(std::move(c));
            }
        }
        // A slot with neither a durable ENCH nor any effect has nothing to mint.
        x.present = !x.kind.empty() && (!x.enchBase.empty() || !x.effects.empty());
        // Re-resolve the live pointer for a DURABLE ENCH only (see ReadExtra).
        if (x.present && !x.enchBase.empty()) {
            if (auto* f = ResolveForm(x.enchBase)) x.ench = f->As<RE::EnchantmentItem>();
        }
        return x;
    }

    nlohmann::json SlotsJson() {
        nlohmann::json j = nlohmann::json::array();
        for (auto it = g_slots.rbegin(); it != g_slots.rend(); ++it) {
            const auto& s = *it;
            nlohmann::json o{
                {"name", s.name}, {"base", s.baseId},
                {"angle", {{"x", s.angle.x}, {"y", s.angle.y}, {"z", s.angle.z}}},
                {"scale", s.scale}, {"isActor", s.isActor},
            };
            if (!s.note.empty()) o["note"] = s.note;  // omitted when unused — palettes stay terse
            if (s.extra.present) o["extra"] = ExtraJson(s.extra);  // `sc pk ed1` slots only
            j.push_back(std::move(o));
        }
        return j;
    }

    // Read a palette json into slots, in FILE order (= panel order, top first).
    // Bases are re-resolved against the current load order; a slot whose plugin
    // is gone stays listed but unavailable (base == nullptr).
    std::vector<Palette::Slot> ParseSlots(const std::filesystem::path& path) {
        std::vector<Palette::Slot> out;
        std::ifstream in(path);
        nlohmann::json j;
        try { in >> j; } catch (const std::exception& e) {
            SKSE::log::warn("Palette: {} unreadable ({})", path.string(), e.what());
            return out;
        }
        if (!j.is_array()) {
            SKSE::log::warn("Palette: {} is not a slot array", path.string());
            return out;
        }
        for (const auto& item : j) {
            Palette::Slot s;
            s.name = item.value("name", "");
            s.note = item.value("note", "");  // absent in every pre-note palette — fine
            s.baseId = item.value("base", "");
            if (s.baseId.empty()) continue;
            if (auto a = item.find("angle"); a != item.end())
                s.angle = {a->value("x", 0.f), a->value("y", 0.f), a->value("z", 0.f)};
            s.scale = item.value("scale", 1.f);
            s.isActor = item.value("isActor", false);
            if (auto x = item.find("extra"); x != item.end() && x->is_object())
                s.extra = ParseExtra(*x);   // absent in every pre-2026-07-12 palette — fine
            s.addsMaster = AddsMaster(s.baseId);
            s.base = ResolveBase(s.baseId);  // null when the plugin isn't loaded
            out.push_back(std::move(s));
        }
        return out;
    }

    // Push parsed (file/panel-order) slots onto the vector so they land ON TOP
    // of whatever is already there, in the file's own order — the panel's
    // newest-first convention (same as a fresh pick).
    void Adopt(std::vector<Palette::Slot>& parsed) {
        for (auto it = parsed.rbegin(); it != parsed.rend(); ++it)
            g_slots.push_back(std::move(*it));
        g_selected = g_slots.empty() ? 0 : g_slots.size() - 1;
    }

    std::size_t Unavailable() {
        std::size_t n = 0;
        for (const auto& s : g_slots) if (!s.base) ++n;
        return n;
    }

    void Save() {
        const auto path = StorePath();
        if (path.empty()) return;
        std::ofstream out(path, std::ios::trunc);
        if (out) out << SlotsJson().dump(2);
    }

    // Refactored core: both pick entries land here. Rejections are loud —
    // a slot that can never build is worse than no slot.
    bool PickRef(RE::NiPointer<RE::TESObjectREFR> ref, const char* how) {
        if (!ref) {
            SKSE::log::info("Palette: {} has no target", how);
            return false;
        }
        if (Markers::IsProxy(ref.get())) {
            SKSE::log::info("Palette: {} target is a marker proxy — nothing to pick", how);
            return false;
        }
        if (Preview::IsGhost(ref.get())) {
            SKSE::log::info("Palette: {} target is the preview ghost — it isn't there", how);
            return false;
        }
        RE::TESBoundObject* base = ref->GetBaseObject();
        if (!base) return false;
        auto baseId = SceneExporter::ResolveDurableId(base);
        if (!baseId) {
            // A runtime-only base cannot be named in an esp — placing copies of
            // it would export placements that never build. Refuse loudly.
            SKSE::log::warn("Palette: target's base is runtime-only (no durable id) — not pickable");
            return false;
        }

        Palette::Slot s;
        s.baseId = *baseId;
        s.base = base;
        s.angle = ref->data.angle;      // captured pose, radians
        s.scale = ref->GetScale();
        s.isActor = ref->GetFormType() == RE::FormType::ActorCharacter;
        s.addsMaster = AddsMaster(*baseId);
        const char* dn = ref->GetDisplayFullName();
        s.name = (dn && *dn) ? dn : *baseId;
        // `sc pk ed1`: also take the INSTANCE's extra data. Off (the default) the
        // eyedropper behaves exactly as it always has — durable base only.
        if (Modes::ExtraData(Modes::Mode::kPick)) s.extra = ReadExtra(ref.get(), base);

        SKSE::log::info("Palette: picked '{}' ({}, {}) rotZ={:.1f} scale={:.2f}{}{}",
            s.name, s.baseId, how, s.angle.z * kRadToDeg, s.scale,
            s.addsMaster ? " (adds a master!)" : "",
            s.extra.present
                ? std::format(" +extra[{} ench {}]", s.extra.kind,
                      s.extra.enchBase.empty()
                          ? std::format("{} effect(s), minted", s.extra.effects.size())
                          : s.extra.enchBase)
                : "");
        g_slots.push_back(std::move(s));
        g_selected = g_slots.size() - 1;
        Save();
        return true;
    }
}

namespace Palette {

    bool PickCrosshair() { return PickRef(Aim::CrosshairRef(), "crosshair"); }
    bool PickByRay() { return PickRef(Aim::RayRef(), "ray"); }

    bool PickConsoleRef(const std::string& label) {
        if (!PickRef(RE::Console::GetSelectedRef(), "console")) return false;
        // The label names the fresh slot (the one PickRef just pushed on top).
        // Case is preserved: Console.cpp hands us the RAW param.
        if (!label.empty()) {
            g_slots.back().name = label;
            Save();
            SKSE::log::info("Palette: slot labelled '{}'", label);
        }
        return true;
    }

    bool PlaceSelected() {
        if (g_selected >= g_slots.size()) {
            SKSE::log::info("Palette: no slot selected — pick something first (`sc pk`)");
            return false;
        }
        return PlaceSlot(g_slots[g_selected]);
    }

    bool PlaceSlot(const Slot& s, const RE::NiPoint3* posOverride) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;
        if (!s.base) {
            SKSE::log::warn("Palette: '{}' ({}) is unavailable — its plugin is "
                "not in the load order", s.name, s.baseId);
            return false;
        }

        RE::NiPoint3 pos;
        bool aimed = true;
        if (posOverride) {
            pos = *posOverride;
        } else if (!(aimed = Aim::LookHit(pos))) {
            pos = player->GetPosition();
        }

        RE::NiPointer<RE::TESObjectREFR> placed = player->PlaceObjectAtMe(s.base, false);
        if (!placed) {
            SKSE::log::error("Palette: PlaceObjectAtMe failed for {}", s.baseId);
            return false;
        }
        placed->SetPosition(pos);
        placed->SetAngle(s.angle);   // re-apply the captured pose
        if (!s.isActor && s.scale != 1.f) placed->SetScale(s.scale);

        // `sc pl py0` — physics OFF. TWO layers, and both are needed:
        //   (a) HERE, this session: freeze the havok so the object does not slide,
        //       topple or get punted the moment you walk into it (deferred — the
        //       3D isn't loaded yet on the frame PlaceObjectAtMe returns).
        //   (b) IN THE ESP: the placement is registered below and exports with
        //       `noHavokSettle`, which becomes the REFR's DontHavokSettle record
        //       flag. Without (b) the shipped mod's object gets havok-settled at
        //       cell load and the arrangement you built is gone.
        // The RUNTIME freeze is type-gated (keyframing a STAT is meaningless), the
        // ESP FLAG is NOT — vanilla puts DontHavokSettle on STAT refs too.
        const bool physicsOff = !Modes::Physics(Modes::Mode::kPlace);
        if (physicsOff && Physics::HavokMovable(s.base)) Physics::FreezeDeferred(placed->GetHandle());

        // `sc pl ed1` — carry the slot's instance extra data into the export.
        const bool carryExtra = Modes::ExtraData(Modes::Mode::kPlace) && s.extra.present;
        if (carryExtra && s.extra.ench) {
            // Durable ENCH: give the WORLD object the enchantment too, so the thing
            // you just placed really is the flaming sword when you pick it up. A
            // RUNTIME (player-crafted) ENCH is deliberately not applied — its form
            // is savegame-bound and the slot never caches it (see ReadExtra); the
            // export still mints it, which is what actually ships.
            if (!placed->extraList.HasType<RE::ExtraEnchantment>()) {
                placed->extraList.Add(new RE::ExtraEnchantment(s.extra.ench, s.extra.enchAmount, false));
            }
        }
        // EVERY placement gets a row (2026-07-14). It used to be only the ones with
        // a rider (py0 / ed1), because the exporter could "tell" a placement by its
        // dynamic FormID — it could not: the engine PlaceAtMe's fish and critters
        // with the same kind of FormID, and they shipped. The row IS the ownership.
        RegisterPlaced(placed.get(), s, physicsOff);

        SKSE::log::info("Palette: placed '{}' ref={:08X} ({}, scale {:.2f}) at ({:.1f}, {:.1f}, {:.1f}){}{}",
            s.name, placed->GetFormID(),
            posOverride ? "ghost"
                        : Modes::Ghost(Modes::Mode::kPlace)
                            ? (aimed ? "aimed, no ghost up" : "feet, no ghost up")
                            : (aimed ? "aimed, ghosts OFF (gh0) — slot's own size" : "feet, gh0"),
            s.scale, pos.x, pos.y, pos.z,
            physicsOff ? " [physics OFF -> noHavokSettle]" : "",
            carryExtra ? " [extra data -> minted item]" : "");
        return true;   // a plain dynamic ref — the vanilla diff exports it
    }

    void AddSlot(const Slot& s) {
        if (s.baseId.empty()) return;
        Slot copy = s;
        copy.addsMaster = AddsMaster(copy.baseId);   // never trust the caller for this
        if (copy.name.empty()) copy.name = copy.baseId;
        SKSE::log::info("Palette: added '{}' ({}) from the catalogue{}", copy.name, copy.baseId,
            copy.addsMaster ? " (adds a master!)" : "");
        g_slots.push_back(std::move(copy));
        g_selected = g_slots.size() - 1;
        Save();
    }

    void Load() {
        const auto path = StorePath();
        if (path.empty() || !std::filesystem::exists(path)) return;
        auto parsed = ParseSlots(path);
        g_slots.clear();
        Adopt(parsed);
        const auto unavailable = Unavailable();
        SKSE::log::info("Palette: loaded {} slot(s) from disk{}", g_slots.size(),
            unavailable ? std::format(" ({} unavailable)", unavailable) : "");
    }

    std::size_t LoadFromFile(const std::string& filename) {
        auto dir = SKSE::log::log_directory();
        if (!dir || filename.empty()) return 0;
        const auto path = *dir / filename;
        if (!std::filesystem::exists(path)) {
            SKSE::log::warn("Palette: load-from-file '{}' not found", path.string());
            return 0;
        }
        auto parsed = ParseSlots(path);
        const std::size_t added = parsed.size();
        if (!added) {
            SKSE::log::warn("Palette: '{}' has no usable slot", filename);
            return 0;
        }
        Adopt(parsed);   // appended ON TOP, in the file's order
        Save();          // fold the import into the persistent store
        SKSE::log::info("Palette: appended {} slot(s) from '{}' on top ({} total)",
            added, filename, g_slots.size());
        return added;
    }

    std::size_t ReplaceFromFile(const std::string& filename) {
        auto dir = SKSE::log::log_directory();
        if (!dir || filename.empty()) return 0;
        const auto path = *dir / filename;
        if (!std::filesystem::exists(path)) {
            SKSE::log::warn("Palette: replace-from-file '{}' not found — palette untouched",
                path.string());
            return 0;
        }
        auto parsed = ParseSlots(path);
        if (parsed.empty()) {
            // Never wipe on a bad read: an unreadable or slot-less file would
            // silently destroy the whole (disk-persisted) palette.
            SKSE::log::warn("Palette: '{}' has no usable slot — palette untouched", filename);
            return 0;
        }
        const std::size_t dropped = g_slots.size();
        g_slots.clear();
        Adopt(parsed);
        Save();
        SKSE::log::info("Palette: replaced {} slot(s) with {} from '{}'",
            dropped, g_slots.size(), filename);
        return g_slots.size();
    }

    void Clear() {
        if (g_slots.empty()) return;
        g_cleared = g_slots;   // the undo buffer — copy, the slots are about to go
        const auto n = g_slots.size();
        g_slots.clear();
        g_selected = 0;
        Save();                // the empty palette IS the new truth on disk
        SKSE::log::info("Palette: cleared {} slot(s) (undo available this session)", n);
    }

    bool UndoClear() {
        if (g_cleared.empty()) return false;
        // Restore ON TOP of whatever has been picked since (a pick after a clear
        // is not something the undo should throw away in turn).
        for (auto& s : g_cleared) g_slots.push_back(std::move(s));
        g_cleared.clear();
        g_selected = g_slots.empty() ? 0 : g_slots.size() - 1;
        Save();
        SKSE::log::info("Palette: undo clear — {} slot(s) restored", g_slots.size());
        return true;
    }

    std::size_t ClearedCount() { return g_cleared.size(); }

    bool SaveToFile(const std::string& filename) {
        auto dir = SKSE::log::log_directory();
        if (!dir || filename.empty()) return false;
        std::ofstream out(*dir / filename, std::ios::trunc);
        if (!out) {
            SKSE::log::warn("Palette: cannot write '{}'", filename);
            return false;
        }
        out << SlotsJson().dump(2);
        SKSE::log::info("Palette: saved {} slot(s) to '{}'", g_slots.size(), filename);
        return true;
    }

    std::vector<Slot>& All() { return g_slots; }
    std::size_t SelectedIndex() { return g_selected; }
    void Select(std::size_t index) { if (index < g_slots.size()) g_selected = index; }

    void Rename(std::size_t index, const std::string& name) {
        if (index < g_slots.size() && !name.empty()) {
            g_slots[index].name = name;
            Save();
        }
    }

    void SetNote(std::size_t index, const std::string& note) {
        if (index >= g_slots.size() || g_slots[index].note == note) return;
        g_slots[index].note = note;
        Save();  // the note is disk state — it is only real once it is written
    }

    void Remove(std::size_t index) {
        if (index >= g_slots.size()) return;
        g_slots.erase(g_slots.begin() + static_cast<std::ptrdiff_t>(index));
        if (g_selected >= g_slots.size() && g_selected > 0) g_selected = g_slots.size() - 1;
        Save();
    }

}  // namespace Palette
