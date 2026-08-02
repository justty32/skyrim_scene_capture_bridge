#pragma once

// Referrer — NAME an EXISTING placed ref (Idea #24 pointer/referrer primitive,
// user-decided 2026-07-11; ModForge consumer side landed 2026-07-12).
//
// The third sibling of the Eraser (erase existing) and Overrides (move
// existing): the referrer NAMES an existing ref and touches NOTHING —— no new
// record, no transform change, no Disable(). It records the target's identity
// plus a free-form LABEL, and Export writes them into the spec's top-level
// `references[]`, where ModForge registers the label as a resolvable name that
// ANY ref field of the spec can point at (a package's sandbox `location`, a
// quest alias `forced:`, `linkedRefs`, `enableParent`, an objective target, a
// script Form property).
//
// TWO TARGET CLASSES — the load-bearing distinction (plan backlog 🔑):
//
//   (B) IN-FILE — the target is a ref WE placed in-game (`sc pl`): a dynamic
//       0xFF...... ref with NO durable FormID. Writing that FormID into the
//       spec would be meaningless (it is not portable and does not survive the
//       build). Instead the exporter emits that ref into `placements[]` WITH A
//       STABLE editorId (EditorIdOf below) and `references[].ref` points at
//       that editorId — a file-internal dependency. ModForge owns the object,
//       so it forces it persistent (0x400 + the cell's Persistent group), which
//       is exactly what "an alias/package can target this ref" needs. THE CLEAN
//       PATH. Identity here is the HANDLE (there is no durable id to key on).
//
//   (A) EXTERNAL — a vanilla / other-mod authored ref (a chair in an inn). We
//       record its durable "<plugin>:0xLOCALID" + base + transform. ModForge
//       warns when the master says it is a TEMPORARY ref (a poor
//       specific-reference target) and offers the `anchor` escape hatch.
//       ⚠️ The DLL NEVER fills `anchor` — that choice belongs to ModForge / the
//       authoring agent (user-decided). We just report what is out there.
//
// LABELS ARE A GLOBAL NAME SPACE in ModForge (the label IS the resolvable id),
// so the registry REFUSES a duplicate label — validate would otherwise fail the
// whole build downstream.
//
// Marker proxies are refused outright: they are editor chrome, excluded from
// `placements[]` by design, so an in-file reference to one could never resolve.
// They already carry a label + note and export as `annotations[]`.

#include <cstdint>
#include <string>
#include <vector>

namespace Referrer {

    struct Entry {
        std::uint32_t seq = 0;      // stable identity of the row (co-save); feeds the editorId
        std::string label;          // the NAME the rest of the spec points at — unique, case preserved
        std::string note;           // free-form brief for the agent -> references[].note
        std::string id;             // (A) durable "<plugin>:0xLOCALID"; EMPTY => (B) in-file target
        std::string base;           // durable base id (advisory; ModForge's anchor:"replace" needs it)
        std::string name;           // display name at mark time — panel row info
        RE::NiPoint3 position;      // at mark time; export prefers the live pose when the ref is loaded
        RE::NiPoint3 angleDeg;      // degrees (contract), not the engine's radians
        float scale = 1.f;
        std::string cellOrWs;       // durable anchor of the containing cell/worldspace
        bool isInterior = false;
        bool isActor = false;       // actors: no scale in the export (XSCL is dead on ACHR)
        RE::ObjectRefHandle handle; // (B): THE identity. (A): convenience only (id is the payload)
    };

    // What a mark attempt did — the console/panel word things by this.
    enum class Result {
        kNone,         // nothing aimed at / no console selection
        kMarked,
        kDuplicate,    // that exact ref is already referred to (label reported back)
        kLabelTaken,   // another entry already owns this label (global name space!)
        kMarkerProxy,  // editor chrome — never a reference target
        kOwnActor,     // an actor WE spawned: cell export excludes actors, so no placement to point at
    };

    // `label` may be empty -> auto "ref-<seq>". Case is PRESERVED (the console
    // passes the RAW param, never the lower-cased one — the label becomes a
    // name in the spec).
    Result MarkCrosshair(const std::string& label);
    Result MarkByRay(const std::string& label);
    Result MarkConsoleRef(const std::string& label);  // `sc refc [XXX]` — aim-free

    [[nodiscard]] std::vector<Entry>& All();
    [[nodiscard]] Entry* FindBySeq(std::uint32_t seq);

    // The label of the entry that already owns `label` (skipping `exceptSeq`),
    // or 0 when it is free. Panel + mark path both gate on this.
    [[nodiscard]] std::uint32_t LabelOwner(const std::string& label, std::uint32_t exceptSeq = 0);

    // Rename in place. False = refused (empty or duplicate label) — the caller
    // shows why; the entry keeps its old label.
    bool Rename(std::uint32_t seq, const std::string& label);
    void SetNote(std::uint32_t seq, const std::string& note);
    void Remove(std::uint32_t seq);  // drops the row only — the world is untouched (that is the point)
    void Clear();

    // The in-file (B) target's stable editorId in the exported spec:
    // "MFRef_<sanitised label>_<seq>". The seq keeps it unique even when two
    // labels sanitise to the same thing, and it is stable across exports
    // because the seq rides the co-save.
    [[nodiscard]] std::string EditorIdOf(const Entry& e);

    // Is `ref` the in-file target of a referrer? The exporter calls this while
    // sweeping placements: a hit means the emitted placement must carry that
    // entry's editorId, and the reference row can be emitted.
    [[nodiscard]] const Entry* InFileEntryFor(RE::TESObjectREFR* ref);

    // A dynamic ref's FormID is not reliably remapped across a full restart, so
    // an in-file entry can come back from the co-save with a dead handle. The
    // object DOES survive in the savegame — re-find it in the player's cell by
    // (base + position), the same rescue Markers::AdoptOrphans does. Returns
    // how many were re-bound. Entries that stay orphaned are KEPT (the panel
    // says so; the export skips them with a warning rather than lying).
    std::size_t ReacquireOrphans();

    // Co-save plumbing (CoSave.cpp). DropAll clears the registry only.
    void DropAll();
    void OnRegistryRestored();

}  // namespace Referrer
