#pragma once

// CoSave — SKSE SerializationInterface plumbing (P5, user-decided: no ini,
// persistent state lives IN THE SAVEGAME).
//
// What rides along with every save:
//   'SETT'  current mode, per-mode keybinds, marker display (dp0/dp1)
//   'MKRS'  the marker registry (label/kind/NOTE/coords + proxy FormID)
//   'ERSR'  the eraser registry (durable ids + handles)
//   'OVRD'  the overrides registry (durable ids, baseline + committed pose)
//
// This upgrades the persistence story: registries used to die with the DLL
// (full game restart), leaving adopt as the recovery path. With the co-save
// they follow the save — adopt is demoted to a RESCUE tool for orphans the
// co-save can't know about (markers placed in another save, erasures from a
// different playthrough). Loading a save without our records simply resets to
// defaults (revert runs before every load).
//
// FormIDs are stored raw and re-resolved through ResolveFormID on load — SKSE
// remaps dynamic (0xFF) ids when the save's load order changed. Entries whose
// ref no longer resolves are dropped (markers) or kept with a dead handle
// (eraser/overrides — the durable id string is the real payload).

namespace CoSave {

    void Register();  // call once from SKSEPluginLoad

}  // namespace CoSave
