#include "CoSave.h"
#include "CoSave.Internal.h"

#include "Editor.h"
#include "Markers.h"
#include "Modes.h"
#include "log.h"

namespace CoSave::detail {

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

}  // namespace CoSave::detail
