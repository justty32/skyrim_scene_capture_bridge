#pragma once

// Requires — "what mods will the .esp I am about to build NEED?", answered
// IN-GAME, at export time.
//
// WHY THIS EXISTS
// Naming `PROTEUS.esp:0x08073D` anywhere in a spec makes PROTEUS.esp a MASTER of
// the built plugin, and Skyrim SILENTLY refuses to load a plugin whose masters
// are missing: no error, no log line, the records simply are not there. `sc capp`
// does that in BULK — a player clone drags in every mod that ever gave the player
// a spell, a perk, a piece of gear. Not filtering that is deliberate (full
// fidelity beats portability — the user's call), so this changes NOTHING about
// what gets exported. It only makes the dependency set VISIBLE.
//
// WHY IN THE DLL, when ModForge already prints this at build time
// (`src/ModForge.Core/Generator.Dependencies.cs` + `<plugin>.requires.txt`):
// TIMING. The C# report exists only AFTER a build — by then you have quit the
// game. This one exists while you are still standing in the room, so you can
// decide THERE AND THEN whether that one PROTEUS spell is worth making PROTEUS a
// hard requirement of your mod, and just re-capture without it if it is not.
// The two are deliberately format-aligned so their output can be diffed.
//
// SAME RULES AS THE C# SIDE (keep them in step):
//   * vanilla = Skyrim.esm / Update.esm / Dawnguard.esm / HearthFires.esm /
//     Dragonborn.esm — those five, nothing else.
//   * Creation Club (ccXxxSSE###* / _ResourcePack) is NOT vanilla: it is owned
//     per account, so a missing CC master kills the load exactly like a Nexus one.
//   * Only refs that really become an esp dependency are listed. A field that
//     merely NAMES a form without producing a FormLink (capturedNpcs[].
//     activeEffects[] — a runtime buff snapshot, no record) must NOT appear:
//     saying so would be a lie, because deleting it would not drop the master.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Requires {

    // One master the exported spec would make the built plugin depend on.
    struct Master {
        std::string name;                   // "PROTEUS.esp"
        bool vanilla = false;               // one of the five base-game masters
        bool creationClub = false;          // owned per account — not on every install
        std::size_t links = 0;              // how many exported refs point into it
        // Why: `captures.capturedNpcs[0].spells[17] = PROTEUS.esp:0x08073D`.
        // The path is the spec field, so it names the line to delete.
        std::vector<std::string> sources;
    };

    struct Report {
        std::vector<Master> masters;        // non-vanilla first, most links first
        std::size_t external = 0;           // non-vanilla masters (the ones that hurt)
        std::size_t creationClub = 0;       // of which, CC
        std::size_t links = 0;              // external links, total
        std::size_t fidelityOnly = 0;       // refs deliberately NOT counted (see .cpp)
    };

    // The analysis, PURE: the two documents an export would write, in; the report,
    // out. Split from Scan() on purpose — this half touches no game state, so it is
    // the half that can be exercised on a hand-made json off the machine (there is
    // no other way to test a claim about what `build` will do with a capture).
    [[nodiscard]] Report Analyze(const nlohmann::json& scene, const nlohmann::json& captures);

    // Scan everything the DLL currently holds that would land in an export:
    // placements[] / removals[] / overrides[] / references[] / annotations[] and
    // the whole Captures registry. Read-only — it writes nothing and changes no
    // registry (not even the panel's last-export stats).
    [[nodiscard]] Report Scan();

    // The report, as the plain text that goes in the file. Always non-empty: an
    // all-vanilla scene is a RESULT worth seeing here ("nothing to install"),
    // unlike the C# sidecar, which is only written when there is a problem.
    [[nodiscard]] std::string Text(const Report& report);

    // What the last "Export requires" press produced, for the panel.
    struct Stats {
        bool valid = false;
        std::size_t external = 0;
        std::size_t creationClub = 0;
        std::size_t links = 0;
        std::string path;
    };
    [[nodiscard]] const Stats& Last();

    // Scan + write `requires_<YYYYMMDD-HHMM>.txt` next to the exports, through the
    // same never-overwrite UniquePath the json exports use.
    void ExportToFile();

    // The vanilla/CC classification, shared with the UI so the panel and the file
    // can never disagree about what counts as a dependency.
    [[nodiscard]] bool IsVanillaMaster(std::string_view master);
    [[nodiscard]] bool IsCreationClubMaster(std::string_view master);

}  // namespace Requires
