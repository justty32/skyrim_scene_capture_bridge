#include "SceneExporter.h"
#include "SceneExporter.Internal.h"

#include "Captures.h"
#include "Palette.h"

#include "log.h"

#include <cctype>

namespace SceneExporter {

    // One capturedItems[] enchantment block — shared by the Captures registry and
    // the palette's minted items (they expand through the SAME ModForge path:
    // ExpandCapturedItems -> WeaponSpec/ArmorSpec with a referenced-or-minted ENCH).
    static nlohmann::json EnchantJson(const std::string& target, const std::string& enchBase,
        std::uint16_t amount, const std::vector<Captures::Effect>& effects) {
        nlohmann::json ench;
        ench["target"] = target;                       // weapon | armor
        if (!enchBase.empty()) ench["base"] = enchBase;  // durable ENCH -> referenced
        if (amount) ench["amount"] = amount;
        if (!effects.empty()) {                        // runtime ENCH -> minted from MGEFs
            auto a = nlohmann::json::array();
            for (const auto& ef : effects)
                a.push_back({{"magicEffect", ef.magicEffect}, {"magnitude", ef.magnitude},
                    {"area", ef.area}, {"duration", ef.duration}});
            ench["effects"] = std::move(a);
        }
        return ench;
    }

    // capturedItems[] rows for objects placed with `sc pl ed1` — the MINT half of
    // the mint+reference path. AppendPlacements already pointed each such
    // placement's `base` at MintedEditorIdOf(row); this emits the record that
    // editorId names, INTO THE SAME FILE (a scene export otherwise carries no
    // capturedItems — the 2026-07-12 scope split sends the Captures registry to its
    // own file). It has to be the same file: an in-file dependency that lands in a
    // different json would be an unresolvable base, and build would drop the
    // placement.
    //
    // Must run AFTER AppendPlacements, and it emits ONLY the rows whose placement
    // was actually emitted — never a record nothing points at.
    void AppendMintedItems(nlohmann::json& scene, const PlacementCounters& counters) {
        if (counters.mintedEmitted.empty()) return;
        auto arr = nlohmann::json::array();
        for (const auto* pi : counters.mintedEmitted) {
            nlohmann::json c;
            c["editorId"] = Palette::MintedEditorIdOf(*pi);  // what the placement's `base` names
            c["kind"] = pi->extra.kind;                      // weapon | armor
            c["name"] = pi->name;                            // the instance's display name
            if (!pi->baseId.empty()) c["base"] = pi->baseId; // physical template to clone
            c["enchantment"] = EnchantJson(pi->extra.kind, pi->extra.enchBase,
                pi->extra.enchAmount, pi->extra.effects);
            arr.push_back(std::move(c));
        }
        SKSE::log::info("AppendMintedItems: {} minted item(s) for `sc pl ed1` placements",
            arr.size());
        scene["capturedItems"] = std::move(arr);
    }

    // Captured DEFINITIONS — content with no durable base to reference, so
    // ModForge mints fresh authored records. Items (enchant/effects) go to
    // capturedItems[]; actors (appearance/identity) to capturedNpcs[].
    //
    // These are NOT part of a scene export any more (user-decided 2026-07-12):
    // the eyedropped definitions are a library, global and cell-independent,
    // and they get their own button + their own file so a cell export stays a
    // description of one place. Both keys are ModSpec members either way, so
    // whichever file carries them, `build` consumes them the same.
    void AppendCaptures(nlohmann::json& scene) {
        if (const auto& caps = Captures::All(); !caps.empty()) {
            auto effJson = [](const std::vector<Captures::Effect>& effs) {
                auto a = nlohmann::json::array();
                for (const auto& ef : effs)
                    a.push_back({{"magicEffect", ef.magicEffect}, {"magnitude", ef.magnitude},
                        {"area", ef.area}, {"duration", ef.duration}});
                return a;
            };
            // A capture's LABEL (`sc capp Hero` / `sc capc Sword`) becomes the record's
            // editorId: "MFCap_<label>", with everything an EditorID can't hold folded to
            // '_'. ModForge's "explicit editorId wins" rule then makes the label the stable
            // identity of the generated record (re-capture the same hero → same editorId →
            // the same record, not a second one).
            auto editorIdOf = [](const std::string& label) {
                std::string out = "MFCap_";
                for (const char ch : label) {
                    const auto uc = static_cast<unsigned char>(ch);
                    out.push_back(std::isalnum(uc) ? ch : '_');
                }
                return out;
            };
            auto items = nlohmann::json::array();
            auto npcs = nlohmann::json::array();
            for (const auto& e : caps) {
                if (e.kind == Captures::Kind::kNpc) {
                    const auto& n = e.npc;
                    nlohmann::json c;
                    c["name"] = e.name;
                    if (!e.label.empty()) c["editorId"] = editorIdOf(e.label);
                    if (!e.note.empty()) c["note"] = e.note;   // panel-written brief for the agent
                    if (!e.base.empty()) c["base"] = e.base;   // origin NPC_ if durable
                    if (!n.race.empty()) c["race"] = n.race;
                    c["female"] = n.female;
                    if (n.unique) c["unique"] = true;
                    // This entry IS the player (sc capp, or a sc capc that landed on the player).
                    // Advisory identity flag only — ModForge does NOT fall back a voiceType for it
                    // (2026-07-12 user-decided "as-captured, no fallback"); it just lets the consumer
                    // warn instead of silently shipping a mute clone when the player's base has none.
                    if (n.isPlayer) c["isPlayer"] = true;
                    if (n.essential) c["essential"] = true;
                    if (n.protectedActor) c["protected"] = true;
                    if (n.dead) c["dead"] = true;
                    c["weight"] = n.weight;
                    c["height"] = n.height;
                    c["bodyTint"] = {{"r", n.bodyR}, {"g", n.bodyG}, {"b", n.bodyB}};
                    if (!n.hairColor.empty())
                        c["hairColor"] = {{"id", n.hairColor}, {"r", n.hairR}, {"g", n.hairG}, {"b", n.hairB}};
                    if (!n.faceTexture.empty()) c["faceTexture"] = n.faceTexture;
                    if (!n.defaultOutfit.empty()) c["defaultOutfit"] = n.defaultOutfit;
                    if (!n.headParts.empty()) c["headParts"] = n.headParts;
                    if (!n.tints.empty()) {
                        auto tj = nlohmann::json::array();
                        for (const auto& t : n.tints)
                            tj.push_back({{"index", t.index}, {"preset", t.preset}, {"value", t.value},
                                {"color", {{"r", t.r}, {"g", t.g}, {"b", t.b}, {"a", t.a}}}});
                        c["tintLayers"] = std::move(tj);
                    }
                    if (!n.morphs.empty()) c["faceMorphs"] = n.morphs;
                    if (!n.parts.empty()) c["faceParts"] = n.parts;
                    if (!n.npcClass.empty()) c["class"] = n.npcClass;
                    if (n.level > 0) c["level"] = n.level;
                    // EXPLICIT stats (DNAM): the base actor values the engine really runs on.
                    // Present → ModForge writes them straight and leaves autoCalcStats OFF
                    // (class+level only ESTIMATE these; a cloned actor reports 50/50/50).
                    if (n.health > 0.f) c["health"] = n.health;
                    if (n.magicka > 0.f) c["magicka"] = n.magicka;
                    if (n.stamina > 0.f) c["stamina"] = n.stamina;
                    if (!n.skills.empty()) c["skills"] = n.skills;  // 18, in Skill-enum order
                    if (!n.combatStyle.empty()) c["combatStyle"] = n.combatStyle;
                    if (!n.voiceType.empty()) c["voiceType"] = n.voiceType;
                    if (!n.spells.empty()) c["spells"] = n.spells;
                    if (!n.inventory.empty()) {
                        auto inv = nlohmann::json::array();
                        for (const auto& it : n.inventory) {
                            nlohmann::json o{{"item", it.item}, {"count", it.count}};
                            if (it.worn) o["worn"] = true;
                            if (!it.enchBase.empty() || !it.enchEffects.empty()) {
                                if (!it.name.empty()) o["name"] = it.name;
                                nlohmann::json ench;
                                ench["target"] = it.armorTarget ? "armor" : "weapon";
                                if (!it.enchBase.empty()) ench["base"] = it.enchBase;
                                if (it.enchAmount) ench["amount"] = it.enchAmount;
                                if (!it.enchEffects.empty()) ench["effects"] = effJson(it.enchEffects);
                                o["enchantment"] = std::move(ench);
                            }
                            inv.push_back(std::move(o));
                        }
                        c["inventory"] = std::move(inv);
                    }
                    if (!n.perks.empty()) {
                        auto pj = nlohmann::json::array();
                        for (const auto& p : n.perks)
                            pj.push_back({{"perk", p.perk}, {"rank", p.rank}});
                        c["perks"] = std::move(pj);
                    }
                    if (!n.activeEffects.empty()) {
                        auto aj = nlohmann::json::array();
                        for (const auto& a : n.activeEffects) {
                            nlohmann::json o{{"magicEffect", a.magicEffect}, {"magnitude", a.magnitude},
                                {"duration", a.duration}, {"elapsed", a.elapsed}};
                            if (!a.source.empty()) o["source"] = a.source;
                            aj.push_back(std::move(o));
                        }
                        c["activeEffects"] = std::move(aj);   // runtime buff snapshot
                    }
                    c["position"] = Vec3(n.position);
                    c["rotation"] = Vec3(n.angleDeg);   // already degrees
                    if (!n.cellOrWs.empty()) c[n.isInterior ? "cell" : "worldspace"] = n.cellOrWs;
                    npcs.push_back(std::move(c));
                    continue;
                }
                nlohmann::json c;
                c["name"] = e.name;
                c["kind"] = Captures::KindName(e.kind);
                if (!e.label.empty()) c["editorId"] = editorIdOf(e.label);
                if (!e.note.empty()) c["note"] = e.note;  // panel-written brief for the agent
                if (!e.base.empty()) c["base"] = e.base;  // physical template source
                if (e.kind == Captures::Kind::kWeapon || e.kind == Captures::Kind::kArmor) {
                    nlohmann::json ench;
                    ench["target"] = (e.kind == Captures::Kind::kWeapon) ? "weapon" : "armor";
                    if (!e.enchantBase.empty()) ench["base"] = e.enchantBase;
                    if (e.enchantAmount) ench["amount"] = e.enchantAmount;
                    ench["effects"] = effJson(e.effects);
                    c["enchantment"] = std::move(ench);
                } else {
                    c["effects"] = effJson(e.effects);
                }
                items.push_back(std::move(c));
            }
            if (!items.empty()) scene["capturedItems"] = std::move(items);
            if (!npcs.empty()) scene["capturedNpcs"] = std::move(npcs);
        }
    }

}  // namespace SceneExporter
