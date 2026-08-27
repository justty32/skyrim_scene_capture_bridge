#include "SceneExporter.h"
#include "SceneExporter.Internal.h"

#include "Eraser.h"
#include "Markers.h"
#include "Overrides.h"
#include "Referrer.h"

#include "log.h"

#include <cctype>
#include <ctime>
#include <fstream>

namespace SceneExporter {

    namespace {
        Stats g_last;
        CaptureStats g_lastCaptures;
    }

    const Stats& LastExport() { return g_last; }
    const CaptureStats& LastCapturesExport() { return g_lastCaptures; }

    std::optional<std::string> ResolveDurableId(const RE::TESForm* form) {
        if (!form) {
            return std::nullopt;
        }
        // GetFile(0) = the file that first DEFINES this form (origin master),
        // not the last override — that is the reference ModForge needs as a
        // master. Runtime-only forms (PlaceAtMe dynamic refs) have no file.
        const RE::TESFile* file = form->GetFile(0);
        if (!file) {
            return std::nullopt;
        }

        const std::uint32_t rawId = form->GetFormID();
        // Light plugins (ESL/ESPFE) are 0xFExxxYYY -> 12-bit local id; full
        // plugins are 0xXXyyyyyy -> 24-bit. Verified offline against
        // ccBGSSSE037-Curios.esl, whose local ids top out at 0x88E (all < 0x1000),
        // and Skyrim.esm's 0x01605E (24-bit). Both round-trip through the
        // "{}:0x{:06X}" form ModForge expects.
        const std::uint32_t localId =
            file->IsLight() ? (rawId & 0x00000FFFu) : (rawId & 0x00FFFFFFu);

        return std::format("{}:0x{:06X}", file->fileName, localId);
    }

    Anchor AnchorOf(RE::TESObjectREFR* ref) {
        Anchor a;
        if (!ref) ref = RE::PlayerCharacter::GetSingleton();
        RE::TESObjectCELL* cell = ref ? ref->GetParentCell() : nullptr;
        if (!cell) return a;
        if (cell->IsInteriorCell()) {
            a.interior = true;
            if (auto id = ResolveDurableId(cell)) a.id = *id;
        } else if (auto* ws = cell->GetRuntimeData().worldSpace) {
            if (auto id = ResolveDurableId(ws)) a.id = *id;
        }
        return a;
    }

    static void RecordStats(const nlohmann::json& scene, const PlacementCounters& c,
        const std::string& cellLabel) {
        g_last.valid = true;
        g_last.placements = scene["placements"].size();
        g_last.actorsExcluded = c.actorsExcluded;
        g_last.preexisting = c.preexisting;
        g_last.skipped = c.skipped;
        g_last.cell = cellLabel;
        g_last.markers = c.markerProxies;
        g_last.removals = Eraser::All().size();
        g_last.overrides = Overrides::All().size();
        g_last.references = scene.contains("references") ? scene["references"].size() : 0;
        g_last.referencesSkipped = Referrer::All().size() - g_last.references;
        g_last.noHavokSettle = c.noHavokSettle;
        g_last.mintedItems = c.mintedEmitted.size();
        SKSE::log::info(
            "Export[{}]: {} placements, {} actors excluded (NPCs are ModForge's "
            "job), {} pre-existing, {} skipped (dynamic bases), {} marker "
            "proxies excluded, {} preview ghosts excluded, {} dynamic refs not ours "
            "(engine-spawned: fish/critters — adopt them in the panel if you want them), "
            "{} annotations, {} removals, "
            "{} overrides, {} references, {} noHavokSettle (sc pl py0), {} minted items (sc pl ed1)",
            cellLabel, scene["placements"].size(), c.actorsExcluded,
            c.preexisting, c.skipped, c.markerProxies, c.previewGhosts, c.notOurs, Markers::All().size(),
            Eraser::All().size(), Overrides::All().size(), g_last.references,
            c.noHavokSettle, c.mintedEmitted.size());
    }

    nlohmann::json ExportCell(RE::TESObjectCELL* cell) {
        // scene.json IS a ModSpec (see workflows/plans/ingame-scene-export.md):
        // `build scene.json out.esp` deserializes it straight into ModSpec, and
        // ModForge's reader SILENTLY IGNORES unknown keys, so every key must be
        // a real ModSpec member.
        nlohmann::json scene;
        scene["placements"] = nlohmann::json::array();
        if (!cell) {
            SKSE::log::warn("ExportCell: null cell, nothing to export");
            return scene;
        }
        PlacementCounters c;
        AppendPlacements(cell, scene, c);   // must precede AppendReferences (in-file targets)
        AppendRegistries(scene);
        AppendReferences(scene, c);   // in-file targets: needs AppendPlacements first
        AppendMintedItems(scene, c);  // in-file minted bases: likewise
        std::string label;
        if (cell->IsInteriorCell()) {
            if (auto id = ResolveDurableId(cell)) label = *id;
        } else if (auto* ws = cell->GetRuntimeData().worldSpace) {
            if (auto wid = ResolveDurableId(ws)) label = *wid;
        }
        RecordStats(scene, c, label);
        return scene;
    }

    nlohmann::json ExportAll() {
        // Sweep every LOADED cell for placements (interior = just this one;
        // exterior = the whole streamed grid), then the global registries once.
        // Objects placed in cells that have since unloaded can't be recovered —
        // logged so "export all" never silently under-reports.
        nlohmann::json scene;
        scene["placements"] = nlohmann::json::array();
        PlacementCounters c;
        std::size_t cells = 0;
        if (auto* tes = RE::TES::GetSingleton()) {
            tes->ForEachCell([&](RE::TESObjectCELL* cell) {
                if (cell && cell->IsAttached()) { AppendPlacements(cell, scene, c); ++cells; }
            });
        }
        AppendRegistries(scene);
        AppendReferences(scene, c);   // in-file targets: needs AppendPlacements first
        AppendMintedItems(scene, c);  // in-file minted bases: likewise
        RecordStats(scene, c, std::format("ALL/{} loaded cells", cells));
        SKSE::log::info("ExportAll: swept {} loaded cell(s) — placements in "
            "unloaded cells are not captured (visit them, or export per-cell)", cells);
        return scene;
    }

    nlohmann::json ExportPlayerCell() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        RE::TESObjectCELL* cell = player ? player->GetParentCell() : nullptr;
        return ExportCell(cell);
    }

    nlohmann::json ExportCaptures() {
        nlohmann::json scene;
        AppendCaptures(scene);
        std::size_t items = 0, npcs = 0;
        if (scene.contains("capturedItems")) items = scene["capturedItems"].size();
        if (scene.contains("capturedNpcs")) npcs = scene["capturedNpcs"].size();
        g_lastCaptures.valid = true;
        g_lastCaptures.items = items;
        g_lastCaptures.npcs = npcs;
        SKSE::log::info("ExportCaptures: {} captured item(s), {} captured npc(s)", items, npcs);
        return scene;
    }

    // A SCAN is a read of what an export would write — the requires report needs
    // exactly the same json (that is the whole point: it reports on the file you
    // are about to hand to `build`), but it writes no file, so the panel's
    // "Wrote <path>" line must keep describing the last REAL export.
    nlohmann::json ScanAll() {
        const Stats saved = g_last;
        auto scene = ExportAll();
        g_last = saved;
        return scene;
    }

    nlohmann::json ScanCaptures() {
        const CaptureStats saved = g_lastCaptures;
        auto scene = ExportCaptures();
        g_lastCaptures = saved;
        return scene;
    }

    bool WriteSceneFile(const nlohmann::json& scene, const std::filesystem::path& path) {
        try {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            std::ofstream out(path, std::ios::trunc);
            if (!out) {
                SKSE::log::error("WriteSceneFile: cannot open {}", path.string());
                return false;
            }
            out << scene.dump(2);
            SKSE::log::info("WriteSceneFile: wrote {}", path.string());
            return true;
        } catch (const std::exception& e) {
            SKSE::log::error("WriteSceneFile: {}", e.what());
            return false;
        }
    }

    namespace {
        // Every export used to land on a fixed `scene-export.json`, so two
        // exports in a row ate each other. A file name now says WHERE and WHEN
        // it came from — a cell EditorID (or worldspace + grid) plus a stamp.

        // Filename-safe: letters, digits, '-', '_' and '.' survive; anything
        // else (spaces, ':' from a durable id, path separators) becomes '_'.
        std::string SanitizeName(std::string_view in) {
            std::string out;
            out.reserve(in.size());
            for (const char ch : in) {
                const auto uc = static_cast<unsigned char>(ch);
                if (std::isalnum(uc) || ch == '-' || ch == '_' || ch == '.') out.push_back(ch);
                else if (!out.empty() && out.back() != '_') out.push_back('_');
            }
            while (!out.empty() && out.back() == '_') out.pop_back();
            if (out.size() > 48) out.resize(48);  // keep the whole name shell-friendly
            return out;
        }

        // Where an export came from, for the file name: interior -> the cell's
        // EditorID; exterior -> worldspace EditorID + the cell grid (an exterior
        // "cell" is one 4096-unit tile, so the grid is the only thing that
        // distinguishes two exports in the same worldspace).
        std::string SceneTag(RE::TESObjectCELL* cell) {
            if (!cell) return "unknown";
            if (cell->IsInteriorCell()) {
                const char* ed = cell->GetFormEditorID();
                if (ed && *ed) return SanitizeName(ed);
                const char* fn = cell->GetFullName();
                if (fn && *fn) return SanitizeName(fn);
                if (auto id = ResolveDurableId(cell)) return SanitizeName(*id);
                return "interior";
            }
            std::string ws = "exterior";
            if (auto* w = cell->GetRuntimeData().worldSpace) {
                const char* ed = w->GetFormEditorID();
                if (!ed || !*ed) ed = w->GetFullName();
                if (ed && *ed) ws = SanitizeName(ed);
            }
            if (auto* xy = cell->GetCoordinates()) {
                return std::format("{}_x{}y{}", ws, xy->cellX, xy->cellY);
            }
            return ws;
        }
    }

    // YYYYMMDD-HHMM, local time (the player's clock is what they'll match the
    // file against).
    std::string TimeStamp() {
        const std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_s(&tm, &t);
        return std::format("{:04}{:02}{:02}-{:02}{:02}", tm.tm_year + 1900, tm.tm_mon + 1,
            tm.tm_mday, tm.tm_hour, tm.tm_min);
    }

    // Same minute, same cell, two exports — still two files, never a silent
    // overwrite (the whole point of the rename). `ext` so the requires report
    // (a .txt) gets the identical never-clobber guarantee.
    std::filesystem::path UniquePath(const std::filesystem::path& dir,
        const std::string& stem, std::string_view ext) {
        std::error_code ec;
        auto path = dir / (stem + std::string(ext));
        if (!std::filesystem::exists(path, ec)) return path;

        // Numbered suffixes are the readable form, and they are what the old
        // loop produced — but that loop tested `exists(path) && n < 100` and so
        // ran out of numbers SILENTLY, returning `<stem>-99<ext>` whether or not
        // that name was free. Every caller opens what we return with
        // std::ios::trunc, so the 100th export of one cell inside one minute ATE
        // the 99th — the exact bug this function exists to prevent (and the
        // minute-resolution stamp in `stem` is what makes a run of collisions
        // possible at all: a scripted `sc export` loop shares one stem).
        //
        // Every `return` below has just been checked to name a file that does
        // not exist, so no path this function hands out can clobber an earlier
        // export.
        for (int n = 2; n < 100; ++n) {
            path = dir / std::format("{}-{}{}", stem, n, ext);
            if (!std::filesystem::exists(path, ec)) return path;
        }

        // Out of numbers. Fall back to a suffix `stem` cannot already carry:
        // whole seconds (the stamp only goes to the minute) plus a per-session
        // serial, so two calls in the same second still differ.
        static std::uint32_t serial = 0;
        for (int n = 0; n < 1000; ++n) {
            path = dir / std::format("{}-{}-{}{}", stem,
                static_cast<std::int64_t>(std::time(nullptr)), ++serial, ext);
            if (!std::filesystem::exists(path, ec)) return path;
        }

        // ~1100 taken names for a single stem: something is badly wrong. Fail
        // CLOSED. An empty path makes the caller's ofstream fail to open, which
        // it already logs and returns from — this export is lost, but every
        // earlier one survives, and that is the trade this function is for.
        SKSE::log::error("UniquePath: no free name for '{}{}' in {} — refusing to "
            "write rather than overwrite an earlier export", stem, ext, dir.string());
        return {};
    }

    void ExportPlayerCellToFile() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        RE::TESObjectCELL* cell = player ? player->GetParentCell() : nullptr;
        auto scene = ExportCell(cell);
        auto dir = SKSE::log::log_directory();
        if (!dir) {
            SKSE::log::error("ExportPlayerCellToFile: no log_directory");
            return;
        }
        const auto out = UniquePath(*dir,
            std::format("scene-export_{}_{}", SceneTag(cell), TimeStamp()));
        if (WriteSceneFile(scene, out)) {
            g_last.path = out.string();
        }
    }

    void ExportAllToFile() {
        auto scene = ExportAll();
        auto dir = SKSE::log::log_directory();
        if (!dir) {
            SKSE::log::error("ExportAllToFile: no log_directory");
            return;
        }
        // "all" is a sweep of every loaded cell, so no single cell names it —
        // anchor the name on where the player was standing when they pressed it.
        auto* player = RE::PlayerCharacter::GetSingleton();
        RE::TESObjectCELL* here = player ? player->GetParentCell() : nullptr;
        const auto out = UniquePath(*dir,
            std::format("scene-export_all-{}_{}", SceneTag(here), TimeStamp()));
        if (WriteSceneFile(scene, out)) {
            g_last.path = out.string();
        }
    }

    void ExportCapturesToFile() {
        auto scene = ExportCaptures();
        auto dir = SKSE::log::log_directory();
        if (!dir) {
            SKSE::log::error("ExportCapturesToFile: no log_directory");
            return;
        }
        const auto out = UniquePath(*dir, std::format("captures_{}", TimeStamp()));
        if (WriteSceneFile(scene, out)) {
            g_lastCaptures.path = out.string();
        }
    }

}  // namespace SceneExporter
