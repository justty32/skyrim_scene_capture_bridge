#pragma once

// Palette — the eyedropper (Idea #24 §E ①, plan P2 新增).
//
// Pick: capture the crosshair target's BASE + current rotation + scale into a
// named slot (the open palette — grab anything from any mod, no design-time
// catalogue). Place: spawn the selected slot's base at the aimed point and
// re-apply the captured pose. The spawned thing is an ordinary dynamic ref, so
// the vanilla diff exports it into `placements[]` unchanged — contract zero.
//
// PERSISTENT and save-agnostic (user request 2026-07-11): slots hold durable
// base ids, nothing savegame-bound, so the whole palette serializes to
// scene-capture-palette.json next to the export and loads back on startup —
// pick in one playthrough, place in another. A slot whose base no longer
// resolves (plugin removed from the load order) stays listed but unavailable.

#include "Captures.h"  // Captures::Effect — the enchant effect shape is shared

#include <cstdint>
#include <string>
#include <vector>

namespace Palette {

    // INSTANCE extra data (`sc pk ed1`, 2026-07-12). A player-applied enchant
    // lives on the REF (ExtraEnchantment), not on the base — so the plain
    // eyedropper, which only ever took GetBaseObject(), silently dropped it: you
    // picked a legendary flaming sword and got a plain iron one. With `sc pk ed1`
    // the slot records the instance's enchantment too, and a `sc pl ed1`
    // placement exports through the capturedItems[] MINT+REFERENCE path.
    struct Extra {
        bool present = false;
        std::string kind;                        // "weapon" | "armor" — capturedItems[].kind
        std::string enchBase;                    // durable ENCH id; "" => mint from `effects`
        std::uint16_t enchAmount = 0;            // charge pool
        std::vector<Captures::Effect> effects;   // MGEF list when the ENCH is runtime-only
        RE::EnchantmentItem* ench = nullptr;     // session pointer, DURABLE ENCH only (see
                                                 // Palette.cpp: a runtime ENCH is savegame-bound
                                                 // and must never be cached on a disk-persisted slot)
    };

    struct Slot {
        std::string name;
        // Free-form brief, panel-editable. Unlike every other registry's note
        // this one is DISK state, not co-save state: it goes into the palette
        // json beside the slot, so it follows `save to file` and outlives the
        // playthrough, exactly like the slot it describes.
        std::string note;
        std::string baseId;               // durable "<plugin>:0x…" (display + master warning)
        RE::TESBoundObject* base = nullptr;  // session pointer; null = unavailable
        RE::NiPoint3 angle;               // captured pose, radians
        float scale = 1.f;
        bool isActor = false;
        bool addsMaster = false;          // base not from the 5 base-game masters
        Extra extra;                      // instance data (`sc pk ed1`); .present = false otherwise
    };

    // A ref WE placed (`sc pl`) whose export needs more than "base + transform".
    // Registered by PlaceSelected, persisted in the co-save ('PLEX'), consumed by
    // SceneExporter::AppendPlacements. Two independent riders:
    //
    //   noHavokSettle  placed under `sc pl py0` — the exported REFR gets the
    //                  DontHavokSettle record flag so the SHIPPED mod's object
    //                  survives the engine's load-time havok settle pass.
    //   extra          placed under `sc pl ed1` with a slot that carried instance
    //                  data — the export MINTS a capturedItems[] record and points
    //                  this placement's `base` at its editorId (MintedEditorIdOf).
    struct PlacedInfo {
        std::uint32_t seq = 0;
        RE::ObjectRefHandle handle;   // identity within the session
        std::string name;             // slot name at place time (minted record's Name)
        std::string baseId;           // durable base — the minted item's physical template,
                                      // and (with position) the re-acquire key after a restart
        RE::NiPoint3 position;
        bool noHavokSettle = false;
        Extra extra;                  // .present = false when the slot had none
    };

    bool PickCrosshair();   // the activatable crosshair target, old feel
    // Explicit physics-ray pick (panel button) for trees/non-activatable
    // statics. NOT a fallback — same no-silent-fallback rule as the
    // editor/eraser: the ray always hits some ref (walls/floors).
    bool PickByRay();
    // `sc pkc [XXX]` — the console-selected ref (aim-free, like delc/capc/refc):
    // click the thing in the console, then pick it. A non-empty label RENAMES the
    // new slot on the spot. Case is PRESERVED (the label rides the RAW console
    // param, never the lower-cased one — same trap `sc capp` hit).
    bool PickConsoleRef(const std::string& label = "");
    bool PlaceSelected();   // spawn selected slot at the aimed point (feet fallback)

    // THE place path — `sc pl` and the Browser's ghost-commit both end here, so
    // physics (`py0`/`py1`), extra data (`ed0`/`ed1`), the placed-ref registry
    // and therefore the whole export contract can only ever behave one way.
    // posOverride: place exactly THERE (the browser's ghost is already standing
    // where the player wants it — re-aiming would move it out from under them);
    // null = the aim point, feet fallback, the historic behaviour.
    bool PlaceSlot(const Slot& s, const RE::NiPoint3* posOverride = nullptr);

    // Adopt a slot built elsewhere — the Browser's "add to palette" (a catalogue
    // entry, no world instance involved). Same contract as a pick: it lands on
    // top, becomes the selection, and is persisted on the spot. This is how a
    // browse becomes a kit you keep: the catalogue is EVERYTHING the load order
    // has, the palette is the handful you actually work with.
    void AddSlot(const Slot& s);

    void Load();  // kDataLoaded: read scene-capture-palette.json, re-resolve bases
                  // (writes happen automatically on pick/rename/remove)

    // FILE ORDER == PANEL ORDER (2026-07-12): every palette json lists the
    // TOP-most slot first (newest first), so a file reads like what you see.
    // The internal vector keeps the opposite convention (index 0 = oldest =
    // bottom of the panel) — the file I/O is where the two meet.

    // Panel "load from file (append)": read another palette json (by filename,
    // resolved next to scene-capture-palette.json) and APPEND its slots ON TOP
    // of the existing ones, keeping the file's own order. Returns how many were
    // added; the merged set is then saved to the default store.
    std::size_t LoadFromFile(const std::string& filename);

    // Panel "replace from file": same read, but the file REPLACES the whole
    // palette (existing slots are dropped). Refuses to wipe on a missing /
    // unreadable / slot-less file — the palette is only cleared once the new
    // slots are in hand. Returns how many slots the palette now holds.
    std::size_t ReplaceFromFile(const std::string& filename);

    // Panel "save to file": write the current slots to a named json (same
    // folder) — export a curated palette to share or reload later.
    bool SaveToFile(const std::string& filename);

    // Panel "clear all slots": drop every slot and persist the empty palette.
    //
    // This is the ONE destructive palette action with nothing behind it: unlike
    // an erasure or an override (savegame state, revertable), the slots are a
    // DISK store that outlives every save — clearing it throws away work from
    // other playthroughs. So it is guarded twice: the panel makes you click a
    // confirmation, and Clear() keeps the dropped slots in memory so UndoClear()
    // can put them back (this session; the store is rewritten either way).
    void Clear();
    bool UndoClear();                                   // false = nothing to undo
    [[nodiscard]] std::size_t ClearedCount();           // slots the undo would restore

    [[nodiscard]] std::vector<Slot>& All();
    [[nodiscard]] std::size_t SelectedIndex();
    void Select(std::size_t index);
    void Rename(std::size_t index, const std::string& name);
    // Persisted on the spot, like Rename — an empty note is a legitimate value
    // (it clears one), so unlike a name it is not guarded against being blank.
    void SetNote(std::size_t index, const std::string& note);
    void Remove(std::size_t index);

    // ---- placed-ref registry (Palette.Placed.cpp) --------------------------
    // Unlike the SLOTS (which are disk-persisted and savegame-agnostic — they
    // hold nothing but durable ids), these rows are bound to refs in ONE
    // savegame, so they ride the co-save like Eraser/Overrides/Referrer do.

    [[nodiscard]] std::vector<PlacedInfo>& Placed();

    // Register a ref we just placed. Called for EVERY placement (2026-07-14): a
    // row is no longer an optional rider, it is the export's PROOF OF OWNERSHIP.
    // Without one, a dynamic ref is not emitted — because "dynamic" alone never
    // meant "the player put it there" (the engine PlaceAtMe's fish and critters
    // too, and an exterior export shipped nine of them; see Palette.Placed.cpp).
    void RegisterPlaced(RE::TESObjectREFR* ref, const Slot& slot, bool noHavokSettle);

    // Panel: adopt every unregistered dynamic ref in the player's cell — the
    // explicit way back in for things WE did not place but you do want exported
    // (a console `placeatme`, or placements made before ownership was recorded).
    // Deliberately a button and not a heuristic: it will happily adopt a fish if
    // a fish is standing there, and that is the point — you said so.
    std::size_t AdoptDynamicInCell();

    // The row for this ref, or null. Matches on the handle; falling back to
    // (base + position) so a row whose dynamic FormID did not survive a full
    // restart is still re-found — the same rescue Markers/Referrer do, but done
    // lazily at export time (the exporter is already walking the cell's refs, so
    // it costs nothing and needs no kPostLoadGame hook).
    [[nodiscard]] const PlacedInfo* PlacedInfoFor(RE::TESObjectREFR* ref);

    // The minted capturedItems[] record's editorId for an extra-carrying row:
    // "MFPal_<sanitised name>_<seq>". The seq rides the co-save, so it is stable
    // across exports — a rebuild keeps pointing at the same record.
    [[nodiscard]] std::string MintedEditorIdOf(const PlacedInfo& p);

    void DropAllPlaced();             // co-save revert — registry only, world untouched
    void OnPlacedRegistryRestored();  // reseed the seq counter

}  // namespace Palette
