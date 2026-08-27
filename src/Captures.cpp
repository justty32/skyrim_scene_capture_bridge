#include "Captures.h"

#include "Aim.h"
#include "Markers.h"
#include "Preview.h"
#include "SceneExporter.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {
    constexpr float kRadToDeg = 57.2957795f;

    std::vector<Captures::Entry> g_entries;
    std::uint32_t g_nextSeq = 1;

    // A MagicItem's effect list — shared by ENCH (weapon/armour enchant), ALCH
    // (potion) and INGR (ingredient). A runtime-only MGEF can't be named in an
    // esp, so it is dropped from the list (rare; kept quiet — the entry still
    // carries the effects that DO resolve).
    std::vector<Captures::Effect> ReadEffects(const RE::MagicItem* magic) {
        std::vector<Captures::Effect> out;
        if (!magic) return out;
        for (const auto* eff : magic->effects) {
            if (!eff || !eff->baseEffect) continue;
            auto id = SceneExporter::ResolveDurableId(eff->baseEffect);
            if (!id) continue;
            Captures::Effect e;
            e.magicEffect = *id;
            e.magnitude = eff->effectItem.magnitude;
            e.area = static_cast<std::int32_t>(eff->effectItem.area);
            e.duration = static_cast<std::int32_t>(eff->effectItem.duration);
            out.push_back(std::move(e));
        }
        return out;
    }

    // The enchantment ACTUALLY on this instance: a player-applied enchant lives
    // on the ref's ExtraEnchantment (base stays the vanilla weapon); a
    // pre-enchanted base carries formEnchanting. Prefer the instance's.
    void CaptureEnchant(Captures::Entry& e, RE::TESObjectREFR* ref, RE::TESEnchantableForm* form) {
        RE::EnchantmentItem* ench = nullptr;
        std::uint16_t charge = 0;
        if (ref) {
            if (auto* x = ref->extraList.GetByType<RE::ExtraEnchantment>(); x && x->enchantment) {
                ench = x->enchantment;
                charge = x->charge;
            }
        }
        if (!ench && form) {
            ench = form->formEnchanting;
            charge = form->amountofEnchantment;
        }
        if (!ench) return;
        e.effects = ReadEffects(ench);
        e.enchantAmount = charge;
        if (auto id = SceneExporter::ResolveDurableId(ench)) e.enchantBase = *id;
    }

    void ReadNpcAppearance(Captures::NpcData& n, RE::Actor* actor, RE::TESNPC* npc) {
        if (auto* race = npc->GetRace()) {
            if (auto id = SceneExporter::ResolveDurableId(race)) n.race = *id;
        }
        n.female = npc->IsFemale();
        n.unique = npc->IsUnique();
        n.essential = npc->IsEssential();
        n.dead = actor->IsDead();
        n.protectedActor = actor->IsProtected();
        n.weight = npc->weight;
        n.height = npc->height;
        n.bodyR = npc->bodyTintColor.red;
        n.bodyG = npc->bodyTintColor.green;
        n.bodyB = npc->bodyTintColor.blue;

        if (auto* hrd = npc->headRelatedData) {
            if (auto* hc = hrd->hairColor) {
                if (auto id = SceneExporter::ResolveDurableId(hc)) n.hairColor = *id;
                n.hairR = hc->color.red;
                n.hairG = hc->color.green;
                n.hairB = hc->color.blue;
            }
            if (auto* ft = hrd->faceDetails) {
                if (auto id = SceneExporter::ResolveDurableId(ft)) n.faceTexture = *id;
            }
        }
        if (auto* outfit = npc->defaultOutfit) {
            if (auto id = SceneExporter::ResolveDurableId(outfit)) n.defaultOutfit = *id;
        }

        if (npc->headParts && npc->numHeadParts > 0) {
            for (std::int8_t i = 0; i < npc->numHeadParts; ++i) {
                auto* hp = npc->headParts[i];
                if (!hp) continue;
                if (auto id = SceneExporter::ResolveDurableId(hp)) n.headParts.push_back(*id);
            }
        }
        if (npc->tintLayers) {
            for (auto* layer : *npc->tintLayers) {
                if (!layer) continue;
                Captures::TintLayer t;
                t.index = layer->tintIndex;
                t.preset = layer->preset;
                t.value = layer->interpolationValue;
                t.r = layer->tintColor.red;
                t.g = layer->tintColor.green;
                t.b = layer->tintColor.blue;
                t.a = layer->tintColor.alpha;
                n.tints.push_back(t);
            }
        }
        if (npc->faceData) {
            for (int i = 0; i < RE::TESNPC::FaceData::Morphs::kUnk; ++i) n.morphs.push_back(npc->faceData->morphs[i]);
            for (std::int32_t p : npc->faceData->parts) n.parts.push_back(p);
        }
    }

    void ReadNpcPerks(Captures::NpcData& n, RE::Actor* actor, RE::TESNPC* npc) {
        // Perks. An ordinary NPC carries them on its base's BGSPerkRankArray; the PLAYER's
        // base array is EMPTY — every perk the player ever took lives in PlayerCharacter's
        // runtime `addedPerks`. Same durable id + rank either way.
        //
        // ⚠ DO NOT "tidy" THIS BACK INTO `actor->As<RE::PlayerCharacter>()`. That cast is
        // HARDWIRED to return nullptr for EVERY actor — the player included — on every compiler.
        // `TESForm::As<T>()` is not a dynamic_cast: it is a `switch (GetFormType())` (CommonLibSSE
        // FormTraits.h) whose every case does
        //     if constexpr (std::is_convertible_v<const Concrete*, const T*>)
        // i.e. it recovers the concrete class of the form's FORM_TYPE and will only convert
        // UPWARD to a base of it. The player's ref has form type kCharacter, same as any NPC, and
        // that case maps to `Character` — there IS no PlayerCharacter case, because PlayerCharacter
        // has no form type of its own. So As<PlayerCharacter> asks for Character* -> PlayerCharacter*,
        // a DOWNcast, `is_convertible` is false, the case breaks and you silently get nullptr.
        // (As<RE::Actor> on line 64 works precisely because Actor IS a base of Character.)
        //
        // Cost of the bug, IN-GAME 2026-07-12: capture landed on the Player TESNPC
        // (Skyrim.esm:0x000007), yet `isPlayer` stayed false and the perk read fell into the `else`
        // branch (base BGSPerkRankArray), so every perk the player actually took was invisible.
        // Identity for the player is a SINGLETON POINTER COMPARE — what Aim/Editor/UI already use.
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* pc = (actor == player) ? player : nullptr;
        // Player identity flag — also catches a `sc capc` that happens to land on the player,
        // not just `sc capp`. Consumed by ModForge only as a warning trigger (no voiceType
        // fallback — 2026-07-12 user-decided).
        n.isPlayer = pc != nullptr;

        // PERKS — capture BOTH arrays, never one-or-the-other (2026-07-13 user-decided: full
        // fidelity at the bridge, ModForge decides downstream what a clone should keep).
        //   base BGSPerkRankArray — every actor has it. On the PLAYER it holds the vanilla Player
        //     record's plumbing perks (AllowShoutingPerk / VampireFeed / AlchemySkillBoosts / …).
        //   PlayerCharacter::addedPerks — the player ONLY, and the perks actually spent in the
        //     skill trees live nowhere else (an if/else here silently dropped one set or the other).
        // Same perk in both arrays -> keep the higher rank.
        std::unordered_map<std::string, std::size_t> perkAt;  // durable id -> index into n.perks
        auto addPerk = [&](RE::BGSPerk* perk, std::int32_t rank) {
            if (!perk) return;
            auto id = SceneExporter::ResolveDurableId(perk);
            if (!id) return;
            auto [it, fresh] = perkAt.emplace(*id, n.perks.size());
            if (fresh) {
                n.perks.push_back({*id, rank});
            } else if (rank > n.perks[it->second].rank) {
                n.perks[it->second].rank = rank;
            }
        };
        if (npc->perks && npc->perkCount > 0) {
            for (std::uint32_t i = 0; i < npc->perkCount; ++i)
                addPerk(npc->perks[i].perk, npc->perks[i].currentRank);
        }
        if (pc) {
            for (auto* pr : pc->GetPlayerRuntimeData().addedPerks)
                if (pr) addPerk(pr->perk, pr->currentRank);
        }
    }

    void ReadNpcStatsAndEffects(Captures::NpcData& n, RE::Actor* actor) {
        // EXPLICIT stats (all actors, not just the player). Capturing them lets ModForge write
        // DNAM straight and skip autoCalcStats — which only ESTIMATES H/M/S from class+level.
        // Skills are AV 6..23 in engine order = Mutagen's Skill enum order (OneHanded..
        // Enchanting), so the array index IS the mapping.
        //
        // PERMANENT, not base. An actor value is `base + modifiers[permanent|temporary|damage]`
        // (see Actor::avStorage). The three reads mean different things:
        //   GetActorValue()          base + permanent + temporary - damage → includes potions,
        //                            worn-enchantment fortifies and current damage. NOT wanted:
        //                            a clone must not inherit the buffs that happened to be up.
        //   GetBaseActorValue()      base ONLY. For an NPC that IS the character sheet (the
        //                            engine autocalcs stats into base at load). For the PLAYER
        //                            it is the CHARGEN value — level-up gains and skill
        //                            advancement are applied as PERMANENT modifiers, so base
        //                            stays at the race's starting table forever.
        //   GetPermanentActorValue() base + permanent → the character sheet WITHOUT buffs.
        //                            Correct for both: an NPC with no permanent modifiers reads
        //                            identical to base (Ancano still exports lvl 15 / 167-143-50),
        //                            while a levelled player finally exports his real numbers.
        if (auto* avo = actor->AsActorValueOwner()) {
            n.health = avo->GetPermanentActorValue(RE::ActorValue::kHealth);
            n.magicka = avo->GetPermanentActorValue(RE::ActorValue::kMagicka);
            n.stamina = avo->GetPermanentActorValue(RE::ActorValue::kStamina);
            for (int av = static_cast<int>(RE::ActorValue::kOneHanded);
                 av <= static_cast<int>(RE::ActorValue::kEnchanting); ++av) {
                const float v = avo->GetPermanentActorValue(static_cast<RE::ActorValue>(av));
                n.skills.push_back(static_cast<std::int32_t>(std::lround(v)));
            }
        }

        // Current buffs — live active-effect snapshot (source spell + base MGEF).
        if (auto* mt = actor->GetMagicTarget()) {
            if (auto* list = mt->GetActiveEffectList()) {
                for (auto* ae : *list) {
                    if (!ae) continue;
                    auto* mgef = ae->GetBaseObject();
                    if (!mgef) continue;
                    Captures::ActiveEffect a;
                    if (auto id = SceneExporter::ResolveDurableId(mgef)) a.magicEffect = *id;
                    else continue;  // runtime MGEF — can't name it
                    if (ae->spell) {
                        if (auto id = SceneExporter::ResolveDurableId(ae->spell)) a.source = *id;
                    }
                    a.magnitude = ae->magnitude;
                    a.duration = ae->duration;
                    a.elapsed = ae->elapsedSeconds;
                    n.activeEffects.push_back(std::move(a));
                }
            }
        }
    }

    void ReadNpcBehavior(Captures::NpcData& n, RE::Actor* actor, RE::TESNPC* npc) {
        // Class + effective level: what ModForge needs to autoCalc believable
        // stats (a class-less NPC_ with autoCalc computes ~0 HP).
        if (auto* cls = npc->npcClass) {
            if (auto id = SceneExporter::ResolveDurableId(cls)) n.npcClass = *id;
        }
        n.level = actor->GetLevel();
        // Combat behaviour + voice + castable spells. combatStyle decides whether the AI
        // even considers magic; voiceType keeps the clone from being mute; spells = the base
        // record's spell list + the actor's runtime-added ones (deduped).
        if (auto* cs = npc->combatStyle) {
            if (auto id = SceneExporter::ResolveDurableId(cs)) n.combatStyle = *id;
        }
        if (auto* vt = npc->voiceType) {
            if (auto id = SceneExporter::ResolveDurableId(vt)) n.voiceType = *id;
        }
        auto addSpell = [&](RE::SpellItem* sp) {
            if (!sp) return;
            if (auto id = SceneExporter::ResolveDurableId(sp)) {
                if (std::find(n.spells.begin(), n.spells.end(), *id) == n.spells.end())
                    n.spells.push_back(*id);
            }
        };
        if (auto* se = npc->actorEffects; se && se->spells)
            for (std::uint32_t k = 0; k < se->numSpells; ++k) addSpell(se->spells[k]);
        for (auto* sp : actor->GetActorRuntimeData().addedSpells) addSpell(sp);
    }

    void ReadNpcInventoryAndPlacement(Captures::NpcData& n, RE::Actor* actor,
                                      RE::TESObjectREFR* ref) {
        // Equipped: worn armour from the inventory + whatever the hands hold.
        // This dresses the clone even when defaultOutfit is a runtime shell
        // (PROTEUS template records are empty on disk).
        // Full carry sweep — every durable inventory entry becomes a row (worn armour is
        // flagged for the outfit route downstream; a held weapon IS an inventory entry, so no
        // separate hand scan). The row also harvests the INSTANCE enchantment (ExtraEnchantment)
        // so a player-crafted staff/armour keeps its magic: durable ENCH → referenced, runtime
        // ENCH → its MGEF effects (ModForge mints a fresh ENCH from them).
        for (auto& [obj, data] : actor->GetInventory()) {
            auto& [cnt, entry] = data;
            if (cnt <= 0 || !obj) continue;
            auto id = SceneExporter::ResolveDurableId(obj);
            if (!id) continue;
            Captures::NpcData::InvItem row;
            row.item = *id;
            row.count = cnt;
            row.worn = entry && entry->IsWorn() && obj->IsArmor();
            row.armorTarget = obj->IsArmor();
            if (entry && entry->extraLists) {
                for (auto* xl : *entry->extraLists) {
                    if (!xl) continue;
                    auto* xe = xl->GetByType<RE::ExtraEnchantment>();
                    if (!xe || !xe->enchantment) continue;
                    if (auto eid = SceneExporter::ResolveDurableId(xe->enchantment)) row.enchBase = *eid;
                    else row.enchEffects = ReadEffects(xe->enchantment);
                    row.enchAmount = xe->charge;
                    if (const char* dn = entry->GetDisplayName(); dn && *dn) row.name = dn;
                    break;
                }
            }
            n.inventory.push_back(std::move(row));
        }

        n.position = ref->GetPosition();
        const RE::NiPoint3& ang = ref->data.angle;
        n.angleDeg = {ang.x * kRadToDeg, ang.y * kRadToDeg, ang.z * kRadToDeg};
        const auto anchor = SceneExporter::AnchorOf(ref);
        n.cellOrWs = anchor.id;
        n.isInterior = anchor.interior;
    }

    // Read a captured actor's TESNPC appearance/identity into the entry. Unique
    // check is the caller's — this just harvests. (See header caveat: whether the
    // TESNPC reflects a live-override tool like PROTEUS is IN-GAME TBD.)
    bool ReadNpc(Captures::Entry& e, RE::TESObjectREFR* ref) {
        auto* actor = ref->As<RE::Actor>();
        auto* npc = actor ? actor->GetActorBase() : nullptr;
        if (!npc) return false;
        auto& n = e.npc;

        ReadNpcAppearance(n, actor, npc);
        ReadNpcPerks(n, actor, npc);
        ReadNpcStatsAndEffects(n, actor);
        ReadNpcBehavior(n, actor, npc);
        ReadNpcInventoryAndPlacement(n, actor, ref);
        return true;
    }

    Captures::Result CaptureRef(RE::NiPointer<RE::TESObjectREFR> ref, const char* how,
                                const std::string& label = "") {
        if (!ref) {
            SKSE::log::info("Captures: {} has no target", how);
            return Captures::Result::kNothing;
        }
        if (Markers::IsProxy(ref.get())) {
            SKSE::log::info("Captures: {} target is a marker gem — nothing to capture", how);
            return Captures::Result::kMarkerProxy;
        }
        if (Preview::IsGhost(ref.get())) {
            SKSE::log::info("Captures: {} target is the preview ghost — it carries no identity", how);
            return Captures::Result::kNothing;
        }
        // NPC capture (increment ②): harvest the actor's appearance/identity.
        // Unique NPCs are captured too (user-decided) — the `unique` flag rides
        // along for ModForge to act on.
        if (ref->GetFormType() == RE::FormType::ActorCharacter) {
            Captures::Entry e;
            const char* dn = ref->GetDisplayFullName();
            e.name = (dn && *dn) ? dn : "";
            e.label = label;
            if (auto* b = ref->GetBaseObject()) {
                if (auto id = SceneExporter::ResolveDurableId(b)) e.base = *id;
            }
            e.kind = Captures::Kind::kNpc;
            if (!ReadNpc(e, ref.get())) {
                SKSE::log::info("Captures: {} npc has no actor base — nothing captured", how);
                return Captures::Result::kNothing;
            }
            e.seq = g_nextSeq++;
            SKSE::log::info("Captures: captured NPC '{}'{} ({}) race={} {}{}{} — lvl {} "
                "H/M/S {:.0f}/{:.0f}/{:.0f}, {} skill(s), {} headpart(s), {} tint(s), "
                "{} perk(s), {} buff(s), {} item(s), face morphs {}", e.name,
                e.label.empty() ? "" : std::format(" [label '{}']", e.label),
                e.base.empty() ? "runtime base" : e.base,
                e.npc.race.empty() ? "?" : e.npc.race, e.npc.female ? "female" : "male",
                e.npc.unique ? " UNIQUE" : "", e.npc.isPlayer ? " PLAYER" : "", e.npc.level,
                e.npc.health, e.npc.magicka, e.npc.stamina, e.npc.skills.size(),
                e.npc.headParts.size(), e.npc.tints.size(),
                e.npc.perks.size(), e.npc.activeEffects.size(), e.npc.inventory.size(),
                e.npc.morphs.empty() ? "none" : "captured");
            g_entries.push_back(std::move(e));
            return Captures::Result::kCaptured;
        }
        auto* base = ref->GetBaseObject();
        if (!base) {
            SKSE::log::info("Captures: {} target has no base", how);
            return Captures::Result::kNothing;
        }

        Captures::Entry e;
        const char* dn = ref->GetDisplayFullName();
        e.name = (dn && *dn) ? dn : "";
        e.label = label;
        if (auto id = SceneExporter::ResolveDurableId(base)) e.base = *id;

        switch (base->GetFormType()) {
        case RE::FormType::Weapon:
            e.kind = Captures::Kind::kWeapon;
            CaptureEnchant(e, ref.get(), static_cast<RE::TESObjectWEAP*>(base));
            break;
        case RE::FormType::Armor:
            e.kind = Captures::Kind::kArmor;
            CaptureEnchant(e, ref.get(), static_cast<RE::TESObjectARMO*>(base));
            break;
        case RE::FormType::AlchemyItem:
            e.kind = Captures::Kind::kPotion;
            e.effects = ReadEffects(static_cast<RE::AlchemyItem*>(base));
            break;
        case RE::FormType::Ingredient:
            e.kind = Captures::Kind::kIngredient;
            e.effects = ReadEffects(static_cast<RE::IngredientItem*>(base));
            break;
        default:
            SKSE::log::info("Captures: {} target '{}' is not a capturable item "
                "(weapon/armour/potion/ingredient)", how, e.name);
            return Captures::Result::kNotItem;
        }

        // Nothing to mint: a plain (unenchanted) weapon/armour or an empty potion
        // has no definition worth an authored record — ModForge can reference the
        // base directly. Reject loudly so the player knows why nothing landed.
        if (e.effects.empty()) {
            SKSE::log::info("Captures: '{}' has no enchantment / effects to capture", e.name);
            return Captures::Result::kNotItem;
        }

        e.seq = g_nextSeq++;
        SKSE::log::info("Captures: captured '{}' [{}] ({}) — {} effect(s){}", e.name,
            Captures::KindName(e.kind), e.base.empty() ? "runtime base" : e.base,
            e.effects.size(), e.enchantBase.empty() ? "" : " (+authored ENCH)");
        g_entries.push_back(std::move(e));
        return Captures::Result::kCaptured;
    }
}

namespace Captures {

    Result CaptureCrosshair() { return CaptureRef(Aim::CrosshairRef(), "crosshair"); }
    Result CaptureByRay() { return CaptureRef(Aim::RayRef(), "ray"); }

    Result CaptureConsoleRef(const std::string& label) {
        return CaptureRef(RE::Console::GetSelectedRef(), "console", label);
    }

    // The player is just another actor to the capture path — but only HALF of him lives where
    // an NPC's does, and the split is worth knowing:
    //   base TESNPC (Skyrim.esm:0x000007)  APPEARANCE. Chargen/RaceMenu write race, head parts,
    //       tints, morphs, hair colour, weight straight onto the player's TESNPC, and the save
    //       carries that modified record. (Proof: the on-disk record has weight 100; a captured
    //       player reads his chargen weight instead.) ReadNpc harvests it unchanged — which is
    //       why no PROTEUS clone is needed as an intermediary.
    //   RUNTIME actor                      PROGRESSION. Perks are in PlayerCharacter's
    //       addedPerks (the base's perk array is empty), and level / H-M-S / the 18 skills are
    //       actor values whose growth is stored as PERMANENT MODIFIERS — the TESNPC's own
    //       numbers never leave the chargen starting table. Hence GetPermanentActorValue in
    //       ReadNpc, never the base value.
    Result CapturePlayer(const std::string& label) {
        auto* pc = RE::PlayerCharacter::GetSingleton();
        if (!pc) {
            SKSE::log::info("Captures: no player singleton");
            return Result::kNothing;
        }
        return CaptureRef(RE::NiPointer<RE::TESObjectREFR>(pc), "player", label);
    }

    std::vector<Entry>& All() { return g_entries; }

    void SetLabel(std::uint32_t seq, const std::string& label) {
        for (auto& e : g_entries)
            if (e.seq == seq) { e.label = label; return; }
    }

    void SetNote(std::uint32_t seq, const std::string& note) {
        for (auto& e : g_entries)
            if (e.seq == seq) { e.note = note; return; }
    }

    const char* KindName(Kind k) {
        switch (k) {
        case Kind::kWeapon: return "weapon";
        case Kind::kArmor: return "armor";
        case Kind::kPotion: return "potion";
        case Kind::kIngredient: return "ingredient";
        case Kind::kNpc: return "npc";
        default: return "item";
        }
    }

    bool Undo() {
        if (g_entries.empty()) return false;
        g_entries.pop_back();
        return true;
    }

    bool UndoEntry(std::uint32_t seq) {
        auto it = std::find_if(g_entries.begin(), g_entries.end(),
            [seq](const Entry& e) { return e.seq == seq; });
        if (it == g_entries.end()) return false;
        g_entries.erase(it);
        return true;
    }

    void Clear() { g_entries.clear(); }

    void DropAll() { g_entries.clear(); }

    void OnRegistryRestored() {
        // Reseed the counter past the highest loaded seq so new captures don't
        // collide with restored ones (same pattern as Markers).
        std::uint32_t hi = 0;
        for (const auto& e : g_entries) hi = std::max(hi, e.seq);
        g_nextSeq = hi + 1;
    }

}  // namespace Captures
