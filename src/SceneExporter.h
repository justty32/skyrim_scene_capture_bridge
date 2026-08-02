#pragma once

// SceneExporter — the "collection bridge" core (Idea #24, component ③).
//
// Walks a target cell's placed references, reads each ref's base + world
// transform + enable state, resolves every runtime FormID back to a durable
// "<plugin>:0xLOCALID" string, and serialises the result into scene.json —
// the contract consumed by ModForge (`dotnet run -- build scene.json`).
//
// Contract authority: workflows/specs/ingame-scene-export-design.md §契約.
// This module owns only the OUTPUT shape; ModForge owns the generation.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace SceneExporter {

    // Resolve a runtime form to a durable, load-order-independent id of the
    // form "<plugin>:0xLOCALID" (e.g. "Skyrim.esm:0x0001A26F").
    //
    // Runtime FormIDs embed the load-order index in the high byte(s), which
    // shifts between launches — so it MUST be stripped before export. The
    // owning file comes from the form's defining plugin; the local id is the
    // plugin-relative portion (masked per full/light plugin width).
    //
    // Returns std::nullopt for forms with no owning file (e.g. dynamically
    // created runtime-only forms), which cannot be referenced from an esp.
    [[nodiscard]] std::optional<std::string> ResolveDurableId(const RE::TESForm* form);

    // Durable anchor of the cell containing `ref` (the player when null):
    // interior -> the cell's durable id, exterior -> the worldspace's.
    // Shared by Markers (marker attribution), Eraser (per-entry cell for the
    // panel's this-cell-only filter) and the UI header line.
    struct Anchor {
        std::string id;        // empty when unresolvable
        bool interior = false;
    };
    [[nodiscard]] Anchor AnchorOf(RE::TESObjectREFR* ref);

    // What the last ExportCell saw. The log line is not enough once a UI wants
    // to show the same numbers — and the pre-existing count is the one that
    // tells you the vanilla diff is working.
    struct Stats {
        bool valid = false;           // false until the first export
        std::size_t placements = 0;   // player-placed refs emitted
        std::size_t actorsExcluded = 0;  // player-placed actors NOT emitted (scope reversal)
        std::size_t preexisting = 0;  // authored refs skipped (the vanilla diff)
        std::size_t skipped = 0;      // dynamic bases, not esp-referenceable
        std::size_t markers = 0;      // marker proxies excluded (editor chrome, not content)
        std::size_t removals = 0;     // erased authored refs exported via removals[]
        std::size_t overrides = 0;    // moved authored refs exported via overrides[]
        std::size_t references = 0;   // named existing refs exported via references[]
        std::size_t referencesSkipped = 0;  // referrers whose in-file target wasn't in this export
        // `sc pl py0` placements: exported with `noHavokSettle` -> the REFR's
        // DontHavokSettle flag, so the built esp's object stays where you put it.
        std::size_t noHavokSettle = 0;
        // `sc pl ed1` placements: their `base` names a MINTED capturedItems[] record
        // (an enchanted clone) emitted into this same file — an in-file dependency.
        std::size_t mintedItems = 0;
        std::string cell;             // durable id of the exported cell/worldspace
        std::string path;             // where the last WriteSceneFile went
    };
    [[nodiscard]] const Stats& LastExport();

    // What the last CAPTURES export wrote. Captures live in their own file now
    // (own button, own timestamped name) — a scene export never carries them.
    struct CaptureStats {
        bool valid = false;
        std::size_t items = 0;  // capturedItems[]
        std::size_t npcs = 0;   // capturedNpcs[]
        std::string path;
    };
    [[nodiscard]] const CaptureStats& LastCapturesExport();

    // Build a scene.json object for one cell: iterates placed refs, emits the
    // `placements[]` segment plus each placement's `cell`/`worldspace`
    // attribution. Semantic-marker / role / removal segments (§B/§D/§E) are
    // layered in by the in-game editor UI, not by a raw cell sweep — this is
    // the M4 "spike" surface (walk cell → placements → scene.json → ModForge).
    //
    // SCOPE (user-decided 2026-07-12): objects only — ACTORS ARE NOT EXPORTED.
    // A cell export is pure scene/object content plus the marker annotations;
    // NPCs are ModForge's job, placed against those markers. Captured actors
    // ride the separate captures export instead.
    [[nodiscard]] nlohmann::json ExportCell(RE::TESObjectCELL* cell);

    // Convenience: export the cell the player is currently in.
    [[nodiscard]] nlohmann::json ExportPlayerCell();

    // Export placements from EVERY loaded cell (interior = one; exterior = the
    // streamed grid) plus the global registries once. Placements in cells that
    // have unloaded can't be recovered — logged, never silently dropped.
    [[nodiscard]] nlohmann::json ExportAll();

    // Build a captures-only object: `capturedItems[]` + `capturedNpcs[]` from
    // the Captures registry, nothing else. Still a legal ModSpec (both are
    // ModSpec members), so `build captures_*.json out.esp` works on its own.
    [[nodiscard]] nlohmann::json ExportCaptures();

    // Same json as ExportAll()/ExportCaptures(), but the panel's "last export"
    // stats are left EXACTLY as they were. A requires scan (Requires::Scan) reads
    // what an export WOULD write; it is not an export, and it must not make the
    // Export page claim a file it never wrote.
    [[nodiscard]] nlohmann::json ScanAll();
    [[nodiscard]] nlohmann::json ScanCaptures();

    // Serialise a scene.json object to disk (pretty-printed, 2-space indent).
    // Returns true on success. Default target: SKSE/Plugins/SceneCaptureBridge/.
    bool WriteSceneFile(const nlohmann::json& scene, const std::filesystem::path& path);

    // Full one-shot: export the player's cell and write it next to the log dir
    // as `scene-export_<where>_<YYYYMMDD-HHMM>.json`. Every export gets its own
    // file — a fixed name meant successive exports silently ate each other.
    void ExportPlayerCellToFile();

    // Same, but ExportAll() — every loaded cell (`<where>` = "all").
    void ExportAllToFile();

    // Captures only → `captures_<YYYYMMDD-HHMM>.json`, kept apart from the
    // scene export (user-decided 2026-07-12): the eyedropped definitions are a
    // library of their own, not scene content of the cell you happen to be in.
    void ExportCapturesToFile();

    // ---- file-naming primitives, shared by every writer (also Requires) ----

    // YYYYMMDD-HHMM, local time — the player's clock is what they match the file
    // against. Every export file name carries it.
    [[nodiscard]] std::string TimeStamp();

    // `dir/<stem><ext>`, or `<stem>-2<ext>`, `-3`… when that exists. Exports
    // NEVER overwrite an earlier one — a fixed name meant two exports in a row
    // silently ate each other, and that is the bug this exists to prevent.
    [[nodiscard]] std::filesystem::path UniquePath(const std::filesystem::path& dir,
        const std::string& stem, std::string_view ext = ".json");

}  // namespace SceneExporter
