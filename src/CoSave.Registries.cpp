#include "CoSave.h"
#include "CoSave.Internal.h"

#include "Captures.h"
#include "Eraser.h"
#include "Markers.h"
#include "Overrides.h"
#include "Palette.h"
#include "Referrer.h"
#include "log.h"

namespace CoSave::detail {

    void SaveMarkers(const SKSE::SerializationInterface* si) {
        const auto& all = Markers::All();
        si->WriteRecordData(static_cast<std::uint32_t>(all.size()));
        for (const auto& e : all) {
            si->WriteRecordData(e.seq);
            WriteStr(si, e.label);
            WriteStr(si, e.kind);
            WriteStr(si, e.note);
            WriteVec3(si, e.position);
            WriteVec3(si, e.angleDeg);   // v2 (was a single angleZ float)
            si->WriteRecordData(e.scale);  // v2
            WriteStr(si, e.cellOrWs);
            si->WriteRecordData(static_cast<std::uint8_t>(e.isInterior ? 1 : 0));
            si->WriteRecordData(FormIdOf(e.proxy));
        }
    }

    void LoadMarkers(const SKSE::SerializationInterface* si, std::uint32_t version) {
        std::uint32_t count = 0;
        si->ReadRecordData(count);
        auto& all = Markers::All();
        std::size_t dropped = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            Markers::Entry e;
            std::uint8_t interior = 0;
            std::uint32_t proxyId = 0;
            si->ReadRecordData(e.seq);
            e.label = ReadStr(si);
            e.kind = ReadStr(si);
            e.note = ReadStr(si);
            ReadVec3(si, e.position);
            if (version >= 2) {
                ReadVec3(si, e.angleDeg);
                si->ReadRecordData(e.scale);
            } else {
                float angleZ = 0.f;  // v1 stored only the Z angle
                si->ReadRecordData(angleZ);
                e.angleDeg = {0.f, 0.f, angleZ};
            }
            e.cellOrWs = ReadStr(si);
            si->ReadRecordData(interior);
            si->ReadRecordData(proxyId);
            e.isInterior = interior != 0;
            e.proxy = ResolveHandle(si, proxyId);
            if (!e.proxy.get()) {
                // Proxy FormID didn't resolve (dynamic refs aren't reliably
                // remapped across a full restart). The gem still exists in the
                // save — hand the note/kind to Markers so the load-time adopt
                // scan can merge them back by position, instead of losing them.
                ++dropped;
                Markers::AddPendingOrphan(e.position, e.label, e.kind, e.note);
                continue;
            }
            all.push_back(std::move(e));
        }
        if (dropped)
            SKSE::log::info("CoSave: dropped {} marker(s) with unresolvable proxies", dropped);
    }

    void SaveEraser(const SKSE::SerializationInterface* si) {
        const auto& all = Eraser::All();
        si->WriteRecordData(static_cast<std::uint32_t>(all.size()));
        for (const auto& e : all) {
            WriteStr(si, e.id);
            WriteStr(si, e.plugin);
            si->WriteRecordData(static_cast<std::uint8_t>(e.addsMaster ? 1 : 0));
            WriteStr(si, e.cellOrWs);
            WriteStr(si, e.name);       // v2
            WriteVec3(si, e.position);  // v2
            si->WriteRecordData(FormIdOf(e.handle));
            WriteStr(si, e.label);      // v3
            WriteStr(si, e.note);       // v3
        }
    }

    void LoadEraser(const SKSE::SerializationInterface* si, std::uint32_t version) {
        std::uint32_t count = 0;
        si->ReadRecordData(count);
        auto& all = Eraser::All();
        for (std::uint32_t i = 0; i < count; ++i) {
            Eraser::Entry e;
            std::uint8_t adds = 0;
            std::uint32_t formId = 0;
            e.id = ReadStr(si);
            e.plugin = ReadStr(si);
            si->ReadRecordData(adds);
            e.cellOrWs = ReadStr(si);
            if (version >= 2) {
                e.name = ReadStr(si);
                ReadVec3(si, e.position);
            }
            si->ReadRecordData(formId);
            if (version >= 3) {  // a pre-v3 save simply has no naming — empty, as before
                e.label = ReadStr(si);
                e.note = ReadStr(si);
            }
            e.addsMaster = adds != 0;
            // A dead handle is fine: the durable id is what exports; undo on a
            // not-loaded ref just unmarks (Eraser already words it that way).
            e.handle = ResolveHandle(si, formId);
            all.push_back(std::move(e));
        }
    }

    void SaveOverrides(const SKSE::SerializationInterface* si) {
        const auto& all = Overrides::All();
        si->WriteRecordData(static_cast<std::uint32_t>(all.size()));
        for (const auto& e : all) {
            WriteStr(si, e.id);
            WriteStr(si, e.name);
            WriteStr(si, e.plugin);
            si->WriteRecordData(static_cast<std::uint8_t>(e.addsMaster ? 1 : 0));
            si->WriteRecordData(static_cast<std::uint8_t>(e.isActor ? 1 : 0));
            si->WriteRecordData(FormIdOf(e.handle));
            WriteVec3(si, e.origPos); WriteVec3(si, e.origAngle);
            si->WriteRecordData(e.origScale);
            WriteVec3(si, e.pos); WriteVec3(si, e.angle);
            si->WriteRecordData(e.scale);
            WriteStr(si, e.label);  // v2
            WriteStr(si, e.note);   // v2
        }
    }

    void LoadOverrides(const SKSE::SerializationInterface* si, std::uint32_t version) {
        std::uint32_t count = 0;
        si->ReadRecordData(count);
        auto& all = Overrides::All();
        for (std::uint32_t i = 0; i < count; ++i) {
            Overrides::Entry e;
            std::uint8_t adds = 0, actor = 0;
            std::uint32_t formId = 0;
            e.id = ReadStr(si);
            e.name = ReadStr(si);
            e.plugin = ReadStr(si);
            si->ReadRecordData(adds);
            si->ReadRecordData(actor);
            si->ReadRecordData(formId);
            ReadVec3(si, e.origPos); ReadVec3(si, e.origAngle);
            si->ReadRecordData(e.origScale);
            ReadVec3(si, e.pos); ReadVec3(si, e.angle);
            si->ReadRecordData(e.scale);
            if (version >= 2) {  // a pre-v2 save simply has no naming — empty, as before
                e.label = ReadStr(si);
                e.note = ReadStr(si);
            }
            e.addsMaster = adds != 0;
            e.isActor = actor != 0;
            e.handle = ResolveHandle(si, formId);  // dead handle kept — id is the payload
            all.push_back(std::move(e));
        }
    }

    // Referrer registry: identity + label of refs the player NAMED. Two flavours in
    // one list — an EXTERNAL row's payload is its durable `id` (the handle is a
    // convenience), an IN-FILE row's payload is the HANDLE (our own dynamic ref has
    // no durable id at all). A dynamic FormID isn't reliably remapped across a full
    // restart, so an in-file row can come back with a dead handle: it is KEPT (the
    // base + position let Referrer::ReacquireOrphans re-find the object in the
    // savegame, exactly like the marker gems' adopt scan) rather than dropped.
    void SaveReferrer(const SKSE::SerializationInterface* si) {
        const auto& all = Referrer::All();
        si->WriteRecordData(static_cast<std::uint32_t>(all.size()));
        for (const auto& e : all) {
            si->WriteRecordData(e.seq);
            WriteStr(si, e.label);
            WriteStr(si, e.note);
            WriteStr(si, e.id);     // empty => in-file target (our own placement)
            WriteStr(si, e.base);
            WriteStr(si, e.name);
            WriteVec3(si, e.position);
            WriteVec3(si, e.angleDeg);
            si->WriteRecordData(e.scale);
            WriteStr(si, e.cellOrWs);
            si->WriteRecordData(static_cast<std::uint8_t>(e.isInterior ? 1 : 0));
            si->WriteRecordData(static_cast<std::uint8_t>(e.isActor ? 1 : 0));
            si->WriteRecordData(FormIdOf(e.handle));
        }
    }

    void LoadReferrer(const SKSE::SerializationInterface* si, std::uint32_t) {
        std::uint32_t count = 0;
        si->ReadRecordData(count);
        auto& all = Referrer::All();
        for (std::uint32_t i = 0; i < count; ++i) {
            Referrer::Entry e;
            std::uint8_t interior = 0, actor = 0;
            std::uint32_t formId = 0;
            si->ReadRecordData(e.seq);
            e.label = ReadStr(si);
            e.note = ReadStr(si);
            e.id = ReadStr(si);
            e.base = ReadStr(si);
            e.name = ReadStr(si);
            ReadVec3(si, e.position);
            ReadVec3(si, e.angleDeg);
            si->ReadRecordData(e.scale);
            e.cellOrWs = ReadStr(si);
            si->ReadRecordData(interior);
            si->ReadRecordData(actor);
            si->ReadRecordData(formId);
            e.isInterior = interior != 0;
            e.isActor = actor != 0;
            e.handle = ResolveHandle(si, formId);  // dead handle kept — see the note above
            all.push_back(std::move(e));
        }
    }

    // Palette placed-ref riders: the rows for objects we placed with `sc pl py0`
    // (-> noHavokSettle in the export) and/or `sc pl ed1` (-> a minted enchanted
    // item the placement's `base` points at). The palette SLOTS themselves are NOT
    // here — they are disk-persisted (scene-capture-palette.json) and savegame-
    // agnostic by design. These rows are the opposite: they name refs inside ONE
    // savegame, so they ride the co-save like Eraser/Overrides/Referrer.
    //
    // A dynamic FormID is not reliably remapped across a full restart, so a row can
    // come back with a dead handle. It is KEPT: base + position let PlacedInfoFor
    // re-bind it lazily while the exporter walks the cell (same rescue as Referrer,
    // no extra sweep).
    void SavePlaced(const SKSE::SerializationInterface* si) {
        const auto& all = Palette::Placed();
        si->WriteRecordData(static_cast<std::uint32_t>(all.size()));
        for (const auto& p : all) {
            si->WriteRecordData(p.seq);
            WriteStr(si, p.name);
            WriteStr(si, p.baseId);
            WriteVec3(si, p.position);
            si->WriteRecordData(static_cast<std::uint8_t>(p.noHavokSettle ? 1 : 0));
            si->WriteRecordData(static_cast<std::uint8_t>(p.extra.present ? 1 : 0));
            WriteStr(si, p.extra.kind);
            WriteStr(si, p.extra.enchBase);
            si->WriteRecordData(p.extra.enchAmount);
            si->WriteRecordData(static_cast<std::uint32_t>(p.extra.effects.size()));
            for (const auto& ef : p.extra.effects) {
                WriteStr(si, ef.magicEffect);
                si->WriteRecordData(ef.magnitude);
                si->WriteRecordData(ef.area);
                si->WriteRecordData(ef.duration);
            }
            si->WriteRecordData(FormIdOf(p.handle));
        }
    }

    void LoadPlaced(const SKSE::SerializationInterface* si, std::uint32_t) {
        std::uint32_t count = 0;
        si->ReadRecordData(count);
        auto& all = Palette::Placed();
        for (std::uint32_t i = 0; i < count; ++i) {
            Palette::PlacedInfo p;
            std::uint8_t noSettle = 0, hasExtra = 0;
            std::uint32_t formId = 0, nEff = 0;
            si->ReadRecordData(p.seq);
            p.name = ReadStr(si);
            p.baseId = ReadStr(si);
            ReadVec3(si, p.position);
            si->ReadRecordData(noSettle);
            si->ReadRecordData(hasExtra);
            p.extra.kind = ReadStr(si);
            p.extra.enchBase = ReadStr(si);
            si->ReadRecordData(p.extra.enchAmount);
            si->ReadRecordData(nEff);
            for (std::uint32_t k = 0; k < nEff; ++k) {
                Captures::Effect ef;
                ef.magicEffect = ReadStr(si);
                si->ReadRecordData(ef.magnitude);
                si->ReadRecordData(ef.area);
                si->ReadRecordData(ef.duration);
                p.extra.effects.push_back(std::move(ef));
            }
            si->ReadRecordData(formId);
            p.noHavokSettle = noSettle != 0;
            p.extra.present = hasExtra != 0;
            // The live ENCH pointer is intentionally NOT serialised — only a DURABLE
            // enchantment may be cached, and that one re-resolves from enchBase.
            if (p.extra.present && !p.extra.enchBase.empty())
                p.extra.ench = ResolveEnchant(p.extra.enchBase);
            p.handle = ResolveHandle(si, formId);  // dead handle kept — see the note above
            all.push_back(std::move(p));
        }
    }

}  // namespace CoSave::detail
