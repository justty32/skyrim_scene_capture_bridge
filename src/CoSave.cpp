#include "CoSave.h"
#include "CoSave.Internal.h"

#include "Captures.h"
#include "Eraser.h"
#include "Markers.h"
#include "Modes.h"
#include "Overrides.h"
#include "Palette.h"
#include "Preview.h"
#include "Referrer.h"
#include "log.h"

#include <algorithm>

namespace CoSave::detail {

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

}  // namespace CoSave::detail

namespace {

    using namespace CoSave::detail;

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
