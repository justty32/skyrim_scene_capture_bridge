#pragma once

// CoSave internals — shared ONLY between the CoSave translation units
// (`CoSave.cpp` and the per-record `CoSave.*.cpp` files). Nothing outside the
// co-save layer may include this: the public contract is `CoSave.h`, which is
// one function (`Register()`).
//
// `CoSave.cpp` had grown to 823 lines holding eight independent record
// families' worth of hand-rolled binary serialisation. The families were
// already self-contained — each is a Save/Load pair reading its OWN record
// version — so the file was cut along those seams (2026-08-27).
//
// 🔴 THE RULE THAT MAKES THIS SAFE: a Save/Load pair is a wire format. The two
// halves must be edited together and in the same byte order, and any change to
// what Save writes needs the matching record's kVer* bumped in `CoSave.cpp`
// (that constant is the ONLY thing that tells an older save's bytes from a
// newer one's — Load branches on it). Splitting the file does not relax this;
// it only puts each pair where you can see both halves at once.

namespace CoSave::detail {

    // ---- primitives (defined in CoSave.cpp) --------------------------------
    //
    // Length-prefixed string: a uint16 count then the bytes, so a record stays
    // readable without a separate string table. Anything longer than 0xFFFF is
    // truncated on write — no field here is meant to hold that much.
    void WriteStr(const SKSE::SerializationInterface* si, const std::string& s);
    [[nodiscard]] std::string ReadStr(const SKSE::SerializationInterface* si);

    // Handles are stored as the raw FormID and re-resolved through SKSE's
    // load-order remap on the way back in; an unresolvable id yields an empty
    // handle, which every Load* treats as "this row lost its object".
    [[nodiscard]] std::uint32_t FormIdOf(const RE::ObjectRefHandle& h);
    [[nodiscard]] RE::ObjectRefHandle ResolveHandle(const SKSE::SerializationInterface* si,
        std::uint32_t oldId);

    // "<plugin>:0xLOCALID" -> live form, for the one pointer we DO cache across
    // a load: a DURABLE enchantment on a palette-placed ref.
    [[nodiscard]] RE::EnchantmentItem* ResolveEnchant(const std::string& id);

    void WriteVec3(const SKSE::SerializationInterface* si, const RE::NiPoint3& v);
    void ReadVec3(const SKSE::SerializationInterface* si, RE::NiPoint3& v);

    // ---- per-record save/load ----------------------------------------------
    // Each pair owns one record tag. `version` is the version STORED IN THE
    // SAVE, not the current one: Load* must keep reading every older layout it
    // has ever written, which is why the branches accumulate instead of moving.

    // CoSave.Settings.cpp — 'SETT': current mode, per-mode binds/aim/physics,
    // editor step sizes, marker display flag.
    void SaveSettings(const SKSE::SerializationInterface* si);
    void LoadSettings(const SKSE::SerializationInterface* si, std::uint32_t version);

    // CoSave.Registries.cpp — the five editor registries.
    void SaveMarkers(const SKSE::SerializationInterface* si);      // 'MKRS'
    void LoadMarkers(const SKSE::SerializationInterface* si, std::uint32_t version);
    void SaveEraser(const SKSE::SerializationInterface* si);       // 'ERSR'
    void LoadEraser(const SKSE::SerializationInterface* si, std::uint32_t version);
    void SaveOverrides(const SKSE::SerializationInterface* si);    // 'OVRD'
    void LoadOverrides(const SKSE::SerializationInterface* si, std::uint32_t version);
    void SaveReferrer(const SKSE::SerializationInterface* si);     // 'RFRR'
    void LoadReferrer(const SKSE::SerializationInterface* si, std::uint32_t version);
    void SavePlaced(const SKSE::SerializationInterface* si);       // 'PLEX'
    void LoadPlaced(const SKSE::SerializationInterface* si, std::uint32_t version);

    // CoSave.Captures.cpp — 'SCCP', by far the widest layout history (v1..v10).
    void SaveCaptures(const SKSE::SerializationInterface* si);
    void LoadCaptures(const SKSE::SerializationInterface* si, std::uint32_t version);

}  // namespace CoSave::detail
