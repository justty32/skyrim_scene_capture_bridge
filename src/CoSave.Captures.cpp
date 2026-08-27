#include "CoSave.h"
#include "CoSave.Internal.h"

#include "Captures.h"
#include "log.h"

namespace CoSave::detail {

    void SaveNpcPayload(const SKSE::SerializationInterface* si, const Captures::NpcData& n) {
        WriteStr(si, n.race);
        si->WriteRecordData(static_cast<std::uint8_t>(n.female ? 1 : 0));
        si->WriteRecordData(n.weight);
        si->WriteRecordData(n.height);
        si->WriteRecordData(n.bodyR); si->WriteRecordData(n.bodyG); si->WriteRecordData(n.bodyB);
        WriteStr(si, n.hairColor);
        si->WriteRecordData(n.hairR); si->WriteRecordData(n.hairG); si->WriteRecordData(n.hairB);
        WriteStr(si, n.faceTexture);
        WriteStr(si, n.defaultOutfit);
        si->WriteRecordData(static_cast<std::uint32_t>(n.headParts.size()));
        for (const auto& hp : n.headParts) WriteStr(si, hp);
        si->WriteRecordData(static_cast<std::uint32_t>(n.tints.size()));
        for (const auto& t : n.tints) {
            si->WriteRecordData(t.index); si->WriteRecordData(t.preset); si->WriteRecordData(t.value);
            si->WriteRecordData(t.r); si->WriteRecordData(t.g); si->WriteRecordData(t.b); si->WriteRecordData(t.a);
        }
        si->WriteRecordData(static_cast<std::uint32_t>(n.morphs.size()));
        for (float m : n.morphs) si->WriteRecordData(m);
        si->WriteRecordData(static_cast<std::uint32_t>(n.parts.size()));
        for (std::int32_t p : n.parts) si->WriteRecordData(p);
        WriteVec3(si, n.position);
        WriteVec3(si, n.angleDeg);
        WriteStr(si, n.cellOrWs);
        si->WriteRecordData(static_cast<std::uint8_t>(n.isInterior ? 1 : 0));
        // v3 appendix: flags + perks + active-effect snapshot.
        si->WriteRecordData(static_cast<std::uint8_t>(n.unique ? 1 : 0));
        si->WriteRecordData(static_cast<std::uint8_t>(n.dead ? 1 : 0));
        si->WriteRecordData(static_cast<std::uint8_t>(n.essential ? 1 : 0));
        si->WriteRecordData(static_cast<std::uint8_t>(n.protectedActor ? 1 : 0));
        si->WriteRecordData(static_cast<std::uint32_t>(n.perks.size()));
        for (const auto& p : n.perks) { WriteStr(si, p.perk); si->WriteRecordData(p.rank); }
        si->WriteRecordData(static_cast<std::uint32_t>(n.activeEffects.size()));
        for (const auto& a : n.activeEffects) {
            WriteStr(si, a.source);
            WriteStr(si, a.magicEffect);
            si->WriteRecordData(a.magnitude);
            si->WriteRecordData(a.duration);
            si->WriteRecordData(a.elapsed);
        }
        // v4 appendix: class + level; v7 adds combatStyle/voiceType/spells + full carry rows.
        WriteStr(si, n.npcClass);
        si->WriteRecordData(n.level);
        WriteStr(si, n.combatStyle);
        WriteStr(si, n.voiceType);
        si->WriteRecordData(static_cast<std::uint32_t>(n.spells.size()));
        for (const auto& sp : n.spells) WriteStr(si, sp);
        si->WriteRecordData(static_cast<std::uint32_t>(n.inventory.size()));
        for (const auto& it : n.inventory) {
            WriteStr(si, it.item);
            si->WriteRecordData(it.count);
            si->WriteRecordData(static_cast<std::uint8_t>(it.worn ? 1 : 0));
            si->WriteRecordData(static_cast<std::uint8_t>(it.armorTarget ? 1 : 0));
            WriteStr(si, it.name);
            WriteStr(si, it.enchBase);
            si->WriteRecordData(it.enchAmount);
            si->WriteRecordData(static_cast<std::uint32_t>(it.enchEffects.size()));
            for (const auto& ef : it.enchEffects) {
                WriteStr(si, ef.magicEffect);
                si->WriteRecordData(ef.magnitude);
                si->WriteRecordData(ef.area);
                si->WriteRecordData(ef.duration);
            }
        }
        // v8 appendix: explicit base stats (DNAM) — H/M/S + the 18 skills (AV 6..23).
        si->WriteRecordData(n.health);
        si->WriteRecordData(n.magicka);
        si->WriteRecordData(n.stamina);
        si->WriteRecordData(static_cast<std::uint32_t>(n.skills.size()));
        for (std::int32_t s : n.skills) si->WriteRecordData(s);
        // v9 appendix: player identity flag (advisory — ModForge's "no voiceType fallback" warning).
        si->WriteRecordData(static_cast<std::uint8_t>(n.isPlayer ? 1 : 0));
    }

    void LoadNpcPayload(const SKSE::SerializationInterface* si, Captures::NpcData& n, std::uint32_t version) {
        std::uint8_t female = 0, interior = 0;
        std::uint32_t cnt = 0;
        n.race = ReadStr(si);
        si->ReadRecordData(female); n.female = female != 0;
        si->ReadRecordData(n.weight);
        si->ReadRecordData(n.height);
        si->ReadRecordData(n.bodyR); si->ReadRecordData(n.bodyG); si->ReadRecordData(n.bodyB);
        n.hairColor = ReadStr(si);
        si->ReadRecordData(n.hairR); si->ReadRecordData(n.hairG); si->ReadRecordData(n.hairB);
        n.faceTexture = ReadStr(si);
        n.defaultOutfit = ReadStr(si);
        si->ReadRecordData(cnt);
        for (std::uint32_t k = 0; k < cnt; ++k) n.headParts.push_back(ReadStr(si));
        si->ReadRecordData(cnt);
        for (std::uint32_t k = 0; k < cnt; ++k) {
            Captures::TintLayer t;
            si->ReadRecordData(t.index); si->ReadRecordData(t.preset); si->ReadRecordData(t.value);
            si->ReadRecordData(t.r); si->ReadRecordData(t.g); si->ReadRecordData(t.b); si->ReadRecordData(t.a);
            n.tints.push_back(t);
        }
        si->ReadRecordData(cnt);
        for (std::uint32_t k = 0; k < cnt; ++k) { float m = 0.f; si->ReadRecordData(m); n.morphs.push_back(m); }
        si->ReadRecordData(cnt);
        for (std::uint32_t k = 0; k < cnt; ++k) { std::int32_t p = 0; si->ReadRecordData(p); n.parts.push_back(p); }
        ReadVec3(si, n.position);
        ReadVec3(si, n.angleDeg);
        n.cellOrWs = ReadStr(si);
        si->ReadRecordData(interior); n.isInterior = interior != 0;
        if (version >= 3) {
            std::uint8_t uq = 0, dd = 0, es = 0, pr = 0;
            std::uint32_t cnt = 0;
            si->ReadRecordData(uq); n.unique = uq != 0;
            si->ReadRecordData(dd); n.dead = dd != 0;
            si->ReadRecordData(es); n.essential = es != 0;
            si->ReadRecordData(pr); n.protectedActor = pr != 0;
            si->ReadRecordData(cnt);
            for (std::uint32_t k = 0; k < cnt; ++k) {
                Captures::PerkEntry p;
                p.perk = ReadStr(si);
                si->ReadRecordData(p.rank);
                n.perks.push_back(std::move(p));
            }
            si->ReadRecordData(cnt);
            for (std::uint32_t k = 0; k < cnt; ++k) {
                Captures::ActiveEffect a;
                a.source = ReadStr(si);
                a.magicEffect = ReadStr(si);
                si->ReadRecordData(a.magnitude);
                si->ReadRecordData(a.duration);
                si->ReadRecordData(a.elapsed);
                n.activeEffects.push_back(std::move(a));
            }
        }
        if (version >= 4) {
            n.npcClass = ReadStr(si);
            si->ReadRecordData(n.level);
            std::uint32_t cnt = 0;
            if (version >= 7) {
                n.combatStyle = ReadStr(si);
                n.voiceType = ReadStr(si);
                si->ReadRecordData(cnt);
                for (std::uint32_t k = 0; k < cnt; ++k) n.spells.push_back(ReadStr(si));
            }
            si->ReadRecordData(cnt);
            // v4/v5/v6 legacy shapes all fold into v7 rows. v4: one mixed worn-armour-
            // dominated list; v5/v6: an armour id list first. (v7+ skips this block — its
            // first count belongs to the full-row list below.)
            if (version <= 6)
                for (std::uint32_t k = 0; k < cnt; ++k) {
                    Captures::NpcData::InvItem it;
                    it.item = ReadStr(si);
                    it.worn = true; it.armorTarget = true;
                    n.inventory.push_back(std::move(it));
                }
            if (version == 5) {  // weapons id list → plain rows (count 1)
                si->ReadRecordData(cnt);
                for (std::uint32_t k = 0; k < cnt; ++k) {
                    Captures::NpcData::InvItem it;
                    it.item = ReadStr(si);
                    n.inventory.push_back(std::move(it));
                }
            }
            if (version == 6) {  // {item,count} rows
                si->ReadRecordData(cnt);
                for (std::uint32_t k = 0; k < cnt; ++k) {
                    Captures::NpcData::InvItem it;
                    it.item = ReadStr(si);
                    si->ReadRecordData(it.count);
                    n.inventory.push_back(std::move(it));
                }
            }
            if (version >= 7) {  // full rows incl. instance enchantment (cnt read above)
                for (std::uint32_t k = 0; k < cnt; ++k) {
                    Captures::NpcData::InvItem it;
                    std::uint8_t worn = 0, at = 0;
                    it.item = ReadStr(si);
                    si->ReadRecordData(it.count);
                    si->ReadRecordData(worn); it.worn = worn != 0;
                    si->ReadRecordData(at); it.armorTarget = at != 0;
                    it.name = ReadStr(si);
                    it.enchBase = ReadStr(si);
                    si->ReadRecordData(it.enchAmount);
                    std::uint32_t ec = 0;
                    si->ReadRecordData(ec);
                    for (std::uint32_t x = 0; x < ec; ++x) {
                        Captures::Effect ef;
                        ef.magicEffect = ReadStr(si);
                        si->ReadRecordData(ef.magnitude);
                        si->ReadRecordData(ef.area);
                        si->ReadRecordData(ef.duration);
                        it.enchEffects.push_back(std::move(ef));
                    }
                    n.inventory.push_back(std::move(it));
                }
            }
        }
        if (version >= 8) {  // explicit base stats (DNAM): H/M/S + the 18 skills
            si->ReadRecordData(n.health);
            si->ReadRecordData(n.magicka);
            si->ReadRecordData(n.stamina);
            std::uint32_t sc = 0;
            si->ReadRecordData(sc);
            for (std::uint32_t k = 0; k < sc; ++k) {
                std::int32_t v = 0;
                si->ReadRecordData(v);
                n.skills.push_back(v);
            }
        }
        if (version >= 9) {  // player identity flag
            std::uint8_t pl = 0;
            si->ReadRecordData(pl);
            n.isPlayer = pl != 0;
        }
    }

    void SaveItemPayload(const SKSE::SerializationInterface* si, const Captures::Entry& e) {
        WriteStr(si, e.enchantBase);
        si->WriteRecordData(e.enchantAmount);
        si->WriteRecordData(static_cast<std::uint32_t>(e.effects.size()));
        for (const auto& ef : e.effects) {
            WriteStr(si, ef.magicEffect);
            si->WriteRecordData(ef.magnitude);
            si->WriteRecordData(ef.area);
            si->WriteRecordData(ef.duration);
        }
    }

    void LoadItemPayload(const SKSE::SerializationInterface* si, Captures::Entry& e) {
        std::uint32_t nEff = 0;
        e.enchantBase = ReadStr(si);
        si->ReadRecordData(e.enchantAmount);
        si->ReadRecordData(nEff);
        for (std::uint32_t k = 0; k < nEff; ++k) {
            Captures::Effect ef;
            ef.magicEffect = ReadStr(si);
            si->ReadRecordData(ef.magnitude);
            si->ReadRecordData(ef.area);
            si->ReadRecordData(ef.duration);
            e.effects.push_back(std::move(ef));
        }
    }

    void SaveCaptures(const SKSE::SerializationInterface* si) {
        const auto& all = Captures::All();
        si->WriteRecordData(static_cast<std::uint32_t>(all.size()));
        for (const auto& e : all) {
            si->WriteRecordData(e.seq);
            si->WriteRecordData(static_cast<std::uint8_t>(e.kind));
            WriteStr(si, e.name);
            WriteStr(si, e.base);
            WriteStr(si, e.label);  // v8 — the player-typed identity label (case preserved)
            WriteStr(si, e.note);   // v10 — panel-written brief for the agent
            if (e.kind == Captures::Kind::kNpc) SaveNpcPayload(si, e.npc);
            else SaveItemPayload(si, e);
        }
    }

    void LoadCaptures(const SKSE::SerializationInterface* si, std::uint32_t version) {
        std::uint32_t count = 0;
        si->ReadRecordData(count);
        auto& all = Captures::All();
        for (std::uint32_t i = 0; i < count; ++i) {
            Captures::Entry e;
            std::uint8_t kind = 0;
            si->ReadRecordData(e.seq);
            si->ReadRecordData(kind);
            e.kind = static_cast<Captures::Kind>(kind);
            e.name = ReadStr(si);
            e.base = ReadStr(si);
            if (version >= 8) e.label = ReadStr(si);
            if (version >= 10) e.note = ReadStr(si);
            // v1 only ever held item kinds (kNpc didn't exist), so always item payload.
            if (version >= 2 && e.kind == Captures::Kind::kNpc) LoadNpcPayload(si, e.npc, version);
            else LoadItemPayload(si, e);
            all.push_back(std::move(e));
        }
    }

}  // namespace CoSave::detail
