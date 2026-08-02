#include "CoSave.h"

#include "Captures.h"
#include "Editor.h"
#include "Eraser.h"
#include "Markers.h"
#include "Modes.h"
#include "Overrides.h"
#include "Palette.h"
#include "Preview.h"
#include "Referrer.h"
#include "log.h"

#include <algorithm>

namespace {
    constexpr std::uint32_t kUID = 'SCBR';
    constexpr std::uint32_t kSett = 'SETT';
    constexpr std::uint32_t kMkrs = 'MKRS';
    constexpr std::uint32_t kErsr = 'ERSR';
    constexpr std::uint32_t kOvrd = 'OVRD';
    constexpr std::uint32_t kCaps = 'SCCP';
    constexpr std::uint32_t kRfrr = 'RFRR';  // referrer registry (references[])
    constexpr std::uint32_t kPlex = 'PLEX';  // palette placed-ref riders (noHavokSettle / extra data)

    // Per-record versions (an older save's record is read with its own layout).
    // v7's binds still round-trip (that fix stays); what changed 2026-07-12 is WHO
    // WINS: SceneCaptureBridge.ini beats a stored bind for any mode it names, and
    // the co-save value only fills the gaps (Modes::ApplyCoSaveBind). Same bytes,
    // no version bump.
    constexpr std::uint32_t kVerSett = 8;  // v2 adds editor step sizes; v3 adds aim/axis; v4 adds capture aim; v5 adds referrer aim; v6 adds per-mode physics (place/edit) + extra data (pick/place); v7 binds actually applied on load (were write-only before), + capture/referrer binds (missing since P5); v8 place-mode ghost preview (sc pl gh0/gh1)
    constexpr std::uint32_t kVerMkrs = 2;  // v2: full angle (3f) + scale, was angleZ only
    constexpr std::uint32_t kVerErsr = 3;  // v2 adds name + position for panel rows; v3 adds label + note
    constexpr std::uint32_t kVerOvrd = 2;  // v2 adds label + note
    constexpr std::uint32_t kVerCaps = 10;  // v2 kNpc; v3 flags/perks/buffs; v4 class/level/equipped; v5 armor/weapons; v6 inventory; v7 rows+instance-ench; v8 label + explicit H/M/S + 18 skills; v9 isPlayer flag; v10 note
    constexpr std::uint32_t kVerRfrr = 1;
    constexpr std::uint32_t kVerPlex = 1;

    // ---- primitives -------------------------------------------------------

    void WriteStr(const SKSE::SerializationInterface* si, const std::string& s) {
        const auto len = static_cast<std::uint16_t>(std::min<std::size_t>(s.size(), 0xFFFF));
        si->WriteRecordData(len);
        if (len) si->WriteRecordData(s.data(), len);
    }

    std::string ReadStr(const SKSE::SerializationInterface* si) {
        std::uint16_t len = 0;
        if (!si->ReadRecordData(len) || !len) return {};
        std::string s(len, '\0');
        si->ReadRecordData(s.data(), len);
        return s;
    }

    std::uint32_t FormIdOf(const RE::ObjectRefHandle& h) {
        auto ref = h.get();
        return ref ? ref->GetFormID() : 0;
    }

    RE::ObjectRefHandle ResolveHandle(const SKSE::SerializationInterface* si,
                                      std::uint32_t oldId) {
        RE::FormID newId = 0;
        if (!oldId || !si->ResolveFormID(oldId, newId)) return {};
        auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(newId);
        return ref ? ref->GetHandle() : RE::ObjectRefHandle{};
    }

    // "<plugin>:0xLOCALID" -> live form, for the one pointer we DO cache across a
    // load: a DURABLE enchantment on a palette-placed ref (a runtime, player-crafted
    // ENCH is savegame-bound and is never cached — Palette.cpp ReadExtra explains).
    RE::EnchantmentItem* ResolveEnchant(const std::string& id) {
        const auto colon = id.rfind(':');
        if (colon == std::string::npos) return nullptr;
        std::uint32_t local = 0;
        try {
            local = static_cast<std::uint32_t>(std::stoul(id.substr(colon + 1), nullptr, 16));
        } catch (...) { return nullptr; }
        auto* dh = RE::TESDataHandler::GetSingleton();
        RE::TESForm* f = dh ? dh->LookupForm(local, id.substr(0, colon)) : nullptr;
        return f ? f->As<RE::EnchantmentItem>() : nullptr;
    }

    void WriteVec3(const SKSE::SerializationInterface* si, const RE::NiPoint3& v) {
        si->WriteRecordData(v.x); si->WriteRecordData(v.y); si->WriteRecordData(v.z);
    }
    void ReadVec3(const SKSE::SerializationInterface* si, RE::NiPoint3& v) {
        si->ReadRecordData(v.x); si->ReadRecordData(v.y); si->ReadRecordData(v.z);
    }

    // ---- per-record save/load ---------------------------------------------

    void SaveSettings(const SKSE::SerializationInterface* si) {
        si->WriteRecordData(static_cast<std::uint8_t>(Modes::Current()));
        si->WriteRecordData(static_cast<std::uint8_t>(Markers::ProxiesVisible() ? 1 : 0));
        for (auto m : {Modes::Mode::kMarker, Modes::Mode::kDelete, Modes::Mode::kPick,
                 Modes::Mode::kPlace, Modes::Mode::kEdit})
            si->WriteRecordData(Modes::Bind(m));
        si->WriteRecordData(Editor::MoveStep());   // v2
        si->WriteRecordData(Editor::YawStep());    // v2
        si->WriteRecordData(Editor::ScaleStep());  // v2
        for (auto m : {Modes::Mode::kDelete, Modes::Mode::kPick, Modes::Mode::kEdit})
            si->WriteRecordData(static_cast<std::uint8_t>(Modes::UseRay(m) ? 1 : 0));  // v3
        si->WriteRecordData(static_cast<std::uint8_t>(Editor::RotateMode() ? 1 : 0));  // v3
        si->WriteRecordData(static_cast<std::uint8_t>(Modes::UseRay(Modes::Mode::kCapture) ? 1 : 0));  // v4
        si->WriteRecordData(static_cast<std::uint8_t>(Modes::UseRay(Modes::Mode::kReferrer) ? 1 : 0));  // v5
        // v6: the per-mode physics + extra-data switches. Written in a fixed order
        // so LoadSettings can read them back positionally (same discipline as v3).
        si->WriteRecordData(static_cast<std::uint8_t>(Modes::Physics(Modes::Mode::kPlace) ? 1 : 0));
        si->WriteRecordData(static_cast<std::uint8_t>(Modes::Physics(Modes::Mode::kEdit) ? 1 : 0));
        si->WriteRecordData(static_cast<std::uint8_t>(Modes::ExtraData(Modes::Mode::kPick) ? 1 : 0));
        si->WriteRecordData(static_cast<std::uint8_t>(Modes::ExtraData(Modes::Mode::kPlace) ? 1 : 0));
        // v7 (rebind rework): the two action-key binds P5 never wired into the
        // co-save (kCapture/kReferrer stayed un-persisted since they were added).
        si->WriteRecordData(Modes::Bind(Modes::Mode::kCapture));
        si->WriteRecordData(Modes::Bind(Modes::Mode::kReferrer));
        // v8: place mode's ghost preview (`sc pl gh0/gh1`).
        si->WriteRecordData(static_cast<std::uint8_t>(Modes::Ghost(Modes::Mode::kPlace) ? 1 : 0));
    }

    void LoadSettings(const SKSE::SerializationInterface* si, std::uint32_t version) {
        std::uint8_t mode = 0, display = 1;
        si->ReadRecordData(mode);
        si->ReadRecordData(display);
        for (auto m : {Modes::Mode::kMarker, Modes::Mode::kDelete, Modes::Mode::kPick,
                 Modes::Mode::kPlace, Modes::Mode::kEdit}) {
            std::uint32_t bind = 0;
            si->ReadRecordData(bind);
            // The binds DO round-trip (v7 fixed them being written-but-discarded)
            // — but the ini outranks them, and a reserved scancode left behind by
            // the removed in-game rebind is refused. Both rules live in
            // Modes::ApplyCoSaveBind; this is just the wire format.
            Modes::ApplyCoSaveBind(m, bind);
        }
        if (version >= 2) {
            float mv = 0.f, yaw = 0.f, sc = 0.f;
            si->ReadRecordData(mv);
            si->ReadRecordData(yaw);
            si->ReadRecordData(sc);
            Editor::SetMoveStep(mv);
            Editor::SetYawStep(yaw);
            Editor::SetScaleStep(sc);
        }
        if (version >= 3) {
            for (auto m : {Modes::Mode::kDelete, Modes::Mode::kPick, Modes::Mode::kEdit}) {
                std::uint8_t ray = 0;
                si->ReadRecordData(ray);
                Modes::SetUseRay(m, ray != 0);
            }
            std::uint8_t rot = 0;
            si->ReadRecordData(rot);
            Editor::SetRotateMode(rot != 0);
        }
        if (version >= 4) {
            std::uint8_t ray = 0;
            si->ReadRecordData(ray);
            Modes::SetUseRay(Modes::Mode::kCapture, ray != 0);
        }
        if (version >= 5) {
            std::uint8_t ray = 0;
            si->ReadRecordData(ray);
            Modes::SetUseRay(Modes::Mode::kReferrer, ray != 0);
        }
        if (version >= 6) {
            // Per-mode physics + extra data. A pre-v6 save simply doesn't reach
            // here, so it keeps the defaults OnRevert already installed (place =
            // py1, edit = py0, extra data off) — old saves behave exactly as before.
            std::uint8_t b = 0;
            si->ReadRecordData(b); Modes::SetPhysics(Modes::Mode::kPlace, b != 0);
            si->ReadRecordData(b); Modes::SetPhysics(Modes::Mode::kEdit, b != 0);
            si->ReadRecordData(b); Modes::SetExtraData(Modes::Mode::kPick, b != 0);
            si->ReadRecordData(b); Modes::SetExtraData(Modes::Mode::kPlace, b != 0);
        }
        if (version >= 7) {
            // kCapture/kReferrer binds — first persisted at v7 (see SaveSettings).
            for (auto m : {Modes::Mode::kCapture, Modes::Mode::kReferrer}) {
                std::uint32_t bind = 0;
                si->ReadRecordData(bind);
                Modes::ApplyCoSaveBind(m, bind);
            }
        }
        if (version >= 8) {  // place-mode ghost preview (`sc pl gh0/gh1`)
            std::uint8_t gh = 1;
            si->ReadRecordData(gh);
            Modes::SetGhost(Modes::Mode::kPlace, gh != 0);
        }
        if (mode < static_cast<std::uint8_t>(Modes::Mode::kTotal))
            Modes::Set(static_cast<Modes::Mode>(mode));
        // Registry is still empty here (MKRS is read after SETT — write
        // order): this just records the flag; OnRegistryRestored applies it.
        Markers::SetProxiesVisible(display != 0);
    }

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

    // ---- SKSE callbacks ----------------------------------------------------

    void OnSave(SKSE::SerializationInterface* si) {
        if (si->OpenRecord(kSett, kVerSett)) SaveSettings(si);
        if (si->OpenRecord(kMkrs, kVerMkrs)) SaveMarkers(si);
        if (si->OpenRecord(kErsr, kVerErsr)) SaveEraser(si);
        if (si->OpenRecord(kOvrd, kVerOvrd)) SaveOverrides(si);
        if (si->OpenRecord(kCaps, kVerCaps)) SaveCaptures(si);
        if (si->OpenRecord(kRfrr, kVerRfrr)) SaveReferrer(si);
        if (si->OpenRecord(kPlex, kVerPlex)) SavePlaced(si);
        SKSE::log::info("CoSave: saved {} marker(s), {} erasure(s), {} override(s), "
            "{} capture(s), {} reference(s), {} placed-ref rider(s)",
            Markers::All().size(), Eraser::All().size(), Overrides::All().size(),
            Captures::All().size(), Referrer::All().size(), Palette::Placed().size());
    }

    void OnLoad(SKSE::SerializationInterface* si) {
        std::uint32_t type = 0, version = 0, length = 0;
        while (si->GetNextRecordInfo(type, version, length)) {
            switch (type) {
            case kSett: LoadSettings(si, version); break;
            case kMkrs: LoadMarkers(si, version); break;
            case kErsr: LoadEraser(si, version); break;
            case kOvrd: LoadOverrides(si, version); break;
            case kCaps: LoadCaptures(si, version); break;
            case kRfrr: LoadReferrer(si, version); break;
            case kPlex: LoadPlaced(si, version); break;
            default:
                SKSE::log::warn("CoSave: unknown record 0x{:X} — skipped", type);
                break;
            }
        }
        Markers::OnRegistryRestored();   // seq counter + freeze + display state
        Eraser::OnRegistryRestored();    // rebuild the marked-id set
        Captures::OnRegistryRestored();  // reseed the capture seq counter
        Referrer::OnRegistryRestored();  // reseed the seq counter; report dead in-file handles
        Palette::OnPlacedRegistryRestored();  // reseed the seq counter; report dead handles
        SKSE::log::info("CoSave: loaded {} marker(s), {} erasure(s), {} override(s), "
            "{} capture(s), {} reference(s), {} placed-ref rider(s)",
            Markers::All().size(), Eraser::All().size(), Overrides::All().size(),
            Captures::All().size(), Referrer::All().size(), Palette::Placed().size());
    }

    // Runs before every load AND on new game: wipe registries (no world
    // touches — the incoming save owns the world state) and reset settings so
    // a save without our records starts from defaults.
    void OnRevert(SKSE::SerializationInterface*) {
        Markers::All().clear();
        Markers::ClearPending();  // stale orphan notes from the previous load
        Eraser::DropAll();
        Overrides::DropAll();
        Captures::DropAll();
        Referrer::DropAll();
        Palette::DropAllPlaced();  // registry only — the palette SLOTS live on disk, untouched
        // The ghost belonged to the world we are leaving. Forget the handle; the
        // incoming save's own ghost (if it has one) is swept on kPostLoadGame.
        Preview::DropState();
        Modes::ResetDefaults();    // incl. place = py1, edit = py0, extra data off
        Markers::SetProxiesVisible(true);  // registry is empty: flag only
    }
}

namespace CoSave {

    void Register() {
        auto* si = SKSE::GetSerializationInterface();
        if (!si) {
            SKSE::log::error("CoSave: no serialization interface — state will not persist");
            return;
        }
        si->SetUniqueID(kUID);
        si->SetSaveCallback(OnSave);
        si->SetLoadCallback(OnLoad);
        si->SetRevertCallback(OnRevert);
        SKSE::log::info("CoSave: registered (UID 'SCBR')");
    }

}  // namespace CoSave
