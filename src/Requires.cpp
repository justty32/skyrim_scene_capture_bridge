#include "Requires.h"

#include "SceneExporter.h"
#include "log.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>

namespace {

    // ---------------------------------------------------------------------------
    //  Master classification — MUST stay in step with Generator.Dependencies.cs
    //  (VanillaMasters + CcMaster). If those five ever change, change them here.
    // ---------------------------------------------------------------------------

    bool IEquals(std::string_view a, std::string_view b) {
        return a.size() == b.size() &&
            std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                return std::tolower(static_cast<unsigned char>(x)) ==
                       std::tolower(static_cast<unsigned char>(y));
            });
    }

    // The C# `LooksExternalRef`, verbatim: a colon at index > 0 and a plugin
    // extension. Deliberately NOT stricter — matching the other side's judgement
    // matters more than being clever, so the two reports can be diffed.
    bool LooksExternalRef(const std::string& s, std::string& master) {
        const auto colon = s.find(':');
        if (colon == std::string::npos || colon == 0) return false;
        std::string_view m{s.data(), colon};
        while (!m.empty() && std::isspace(static_cast<unsigned char>(m.back()))) m.remove_suffix(1);
        if (m.size() < 4) return false;
        const auto ext = m.substr(m.size() - 4);
        if (!IEquals(ext, ".esm") && !IEquals(ext, ".esp") && !IEquals(ext, ".esl")) return false;
        master.assign(m);
        return true;
    }

    // ---------------------------------------------------------------------------
    //  Field classification.
    //
    //  The trap this exists to avoid: a spec field can NAME a form without the
    //  form ever becoming a FormLink in the built esp. `capturedNpcs[].
    //  activeEffects[]` is a runtime buff snapshot ModForge never reads — listing
    //  it as a requirement would be a LIE, because deleting it does not drop the
    //  master. It is not the only one; the full set below was read out of the C#
    //  consumers (Generator.CapturedNpcs.cs / Generator.CapturedItems.cs /
    //  Generator.Build.*.cs), not guessed.
    // ---------------------------------------------------------------------------

    enum class Kind {
        kHard,         // becomes a direct FormLink -> a real master. Missing mod = silent no-load.
        kTemplate,     // ModForge DEEP-COPIES this record. The clone keeps OUR FormKey, so the
                       // form itself is not linked — but the copy drags the source's own links
                       // and its meshes/textures. You need the mod; "master" is not guaranteed.
        kConditional,  // masters ONLY if something else in the spec consumes it (references[]).
        kIgnore,       // named but never consumed. Reporting it would be a lie.
    };

    // What the enclosing object tells us about a ref inside it. Two of ModForge's
    // rules are value-dependent, not path-dependent, so the walker carries them down.
    struct Ctx {
        bool npcHasWornArmor = false;  // -> a minted OTFT REPLACES defaultOutfit (it is dropped)
        bool itemIsIngredient = false; // -> IngredientSpec has no Template: `base` is dead data
        bool invRowEnchanted = false;  // -> the row mints a template clone, not a CNTO link
    };

    Kind Classify(std::string_view shape, const Ctx& ctx) {
        // --- named but NEVER consumed (see Spec.CapturedNpcs.cs / Spec.Annotations.cs) ---
        // capturedNpcs[].base: ModForge always MINTS a fresh NPC_, never overrides the origin.
        if (shape == "captures.capturedNpcs[].base") return Kind::kIgnore;
        // The famous one — plus `.source`, the SECOND ref on the same object.
        if (shape == "captures.capturedNpcs[].activeEffects[].magicEffect" ||
            shape == "captures.capturedNpcs[].activeEffects[].source") return Kind::kIgnore;
        // annotations[] are inert by design: "no records, ever".
        if (shape == "scene.annotations[].cell" || shape == "scene.annotations[].worldspace")
            return Kind::kIgnore;
        // references[] with the default anchor (the DLL deliberately never writes `anchor`)
        // reads NEITHER base NOR cell/worldspace — they are context for a human, nothing more.
        if (shape == "scene.references[].base" || shape == "scene.references[].cell" ||
            shape == "scene.references[].worldspace") return Kind::kIgnore;
        // An OUTFIT minted from worn armour replaces defaultOutfit outright — the captured
        // one is then silently dropped, so the mod that owns it is not pulled in by this line.
        if (shape == "captures.capturedNpcs[].defaultOutfit" && ctx.npcHasWornArmor)
            return Kind::kIgnore;
        // An ingredient's `base` has nowhere to go: IngredientSpec has no Template field.
        if (shape.ends_with("capturedItems[].base") && ctx.itemIsIngredient) return Kind::kIgnore;

        // --- deep-copy template clones: the mod is needed, but not necessarily as a master ---
        if (shape.ends_with("capturedItems[].base")) return Kind::kTemplate;
        if (shape == "captures.capturedNpcs[].inventory[].item" && ctx.invRowEnchanted)
            return Kind::kTemplate;

        // --- named-only: masters when, and only when, something points at the label ---
        if (shape == "scene.references[].ref") return Kind::kConditional;

        // Everything else that looks like an external ref really does link:
        // placements[].base/cell/worldspace, removals[] (bare-string form) and
        // removals[].ref (the annotated object form), overrides[].ref, and the whole
        // consumed half of a capture (race/class/combatStyle/voiceType/spells/perks/
        // headParts/faceTexture/hairColor.id/inventory/enchantments/cell/worldspace).
        // Defaulting to "it links" is the safe direction: a field we have not classified
        // yet gets REPORTED rather than silently swallowed.
        return Kind::kHard;
    }

    // Path -> shape: strip the array indices, so `capturedNpcs[0].spells[17]`
    // becomes `capturedNpcs[].spells[]` and one rule covers every row.
    std::string ShapeOf(std::string_view path) {
        std::string out;
        out.reserve(path.size());
        for (std::size_t i = 0; i < path.size(); ++i) {
            if (path[i] == '[') {
                out += "[]";
                while (i < path.size() && path[i] != ']') ++i;
                continue;
            }
            out.push_back(path[i]);
        }
        return out;
    }

    // ---------------------------------------------------------------------------
    //  The walk. Same job as the C# reflection walk over ModSpec — except the DLL
    //  walks the EXPORTED JSON, which is the same document `build` will read. So a
    //  path printed here (`capturedNpcs[0].spells[17]`) is literally the line in
    //  the file the user is about to hand over.
    // ---------------------------------------------------------------------------

    struct Hit {
        std::string source;   // "captures.capturedNpcs[0].spells[17] = PROTEUS.esp:0x08073D"
        Kind kind;
    };

    struct Collected {
        std::map<std::string, std::vector<Hit>> byMaster;   // ordered → stable output
        std::map<std::string, std::size_t> ignoredByField;  // shape -> count (the honesty ledger)
    };

    void Walk(const nlohmann::json& node, const std::string& path, Ctx ctx, Collected& scan) {
        if (node.is_string()) {
            const auto& s = node.get_ref<const std::string&>();
            std::string master;
            if (!LooksExternalRef(s, master)) return;
            const auto shape = ShapeOf(path);
            const auto kind = Classify(shape, ctx);
            if (kind == Kind::kIgnore) {
                ++scan.ignoredByField[shape];
                return;
            }
            scan.byMaster[master].push_back({std::format("{} = {}", path, s), kind});
            return;
        }
        if (node.is_object()) {
            // Value-dependent rules: read them off THIS object before descending.
            const auto shape = ShapeOf(path);
            if (shape == "captures.capturedNpcs[]") {
                ctx.npcHasWornArmor = false;
                if (auto inv = node.find("inventory"); inv != node.end() && inv->is_array()) {
                    for (const auto& row : *inv) {
                        if (row.is_object() && row.value("worn", false)) {
                            ctx.npcHasWornArmor = true;
                            break;
                        }
                    }
                }
            } else if (shape.ends_with("capturedItems[]")) {
                ctx.itemIsIngredient = (node.value("kind", std::string{}) == "ingredient");
            } else if (shape == "captures.capturedNpcs[].inventory[]") {
                ctx.invRowEnchanted = node.contains("enchantment");
            }
            for (const auto& el : node.items()) {
                Walk(el.value(), path.empty() ? el.key() : std::format("{}.{}", path, el.key()),
                    ctx, scan);
            }
            return;
        }
        if (node.is_array()) {
            std::size_t i = 0;
            for (const auto& item : node) {
                Walk(item, std::format("{}[{}]", path, i++), ctx, scan);
            }
        }
    }

    Requires::Stats g_last;

    const char* KindNote(Kind k) {
        switch (k) {
            case Kind::kTemplate:
                return "  [template clone — ModForge deep-copies the record: you need the mod's "
                       "ASSETS, and its sub-links usually master it too]";
            case Kind::kConditional:
                return "  [named only — becomes a master when something in the spec points at "
                       "this label]";
            default:
                return "";
        }
    }

}  // namespace

namespace Requires {

    bool IsVanillaMaster(std::string_view master) {
        // The five every SE install has. Nothing else — CC is NOT in here on purpose.
        static constexpr std::string_view kVanilla[] = {
            "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm",
        };
        for (const auto v : kVanilla) {
            if (IEquals(master, v)) return true;
        }
        return false;
    }

    bool IsCreationClubMaster(std::string_view master) {
        // C# regex: ^(cc[A-Za-z]{3}SSE\d{3}|_ResourcePack), case-insensitive.
        // Owned per account, so a missing CC master is exactly as fatal as a missing
        // Nexus one — flagged separately only so the report can name the reason.
        if (master.size() >= 13 && IEquals(master.substr(0, 13), "_ResourcePack")) return true;
        if (master.size() < 11) return false;                       // "ccABCSSE001"
        if (!IEquals(master.substr(0, 2), "cc")) return false;
        for (std::size_t i = 2; i < 5; ++i)
            if (!std::isalpha(static_cast<unsigned char>(master[i]))) return false;
        if (!IEquals(master.substr(5, 3), "SSE")) return false;
        for (std::size_t i = 8; i < 11; ++i)
            if (!std::isdigit(static_cast<unsigned char>(master[i]))) return false;
        return true;
    }

    Report Analyze(const nlohmann::json& scene, const nlohmann::json& captures) {
        Report report;

        // The two documents an export would write, unified under the roots that name
        // the FILE each row lands in (`scene-export_*.json` / `captures_*.json`). The
        // C# side sees one merged ModSpec and so has no such prefix; ours says which
        // of the two files to go edit, which is strictly more actionable in-game.
        Collected scan;
        Walk(scene, "scene", Ctx{}, scan);
        Walk(captures, "captures", Ctx{}, scan);

        for (auto& [name, hits] : scan.byMaster) {
            Master m;
            m.name = name;
            m.vanilla = IsVanillaMaster(name);
            m.creationClub = IsCreationClubMaster(name);
            m.links = hits.size();
            m.sources.reserve(hits.size());
            for (const auto& h : hits) m.sources.push_back(h.source + KindNote(h.kind));
            report.masters.push_back(std::move(m));
        }
        for (const auto& [_, n] : scan.ignoredByField) report.fidelityOnly += n;

        // Non-vanilla first (they are the ones that can kill a load), most links first.
        std::ranges::stable_sort(report.masters, [](const Master& a, const Master& b) {
            if (a.vanilla != b.vanilla) return !a.vanilla;
            if (a.links != b.links) return a.links > b.links;
            return a.name < b.name;
        });
        for (const auto& m : report.masters) {
            if (m.vanilla) continue;
            ++report.external;
            if (m.creationClub) ++report.creationClub;
            report.links += m.links;
        }

        // The honesty ledger: say WHICH fields we deliberately did not count, so a
        // reader who knows a mod's spell is in there can see we saw it and why it is
        // not a dependency — rather than assume the scan is broken.
        for (const auto& [shape, n] : scan.ignoredByField) {
            SKSE::log::info("Requires: ignored {} ref(s) at {} — named but never consumed by "
                "build (no FormLink, so it is NOT a dependency)", n, shape);
        }
        return report;
    }

    Report Scan() {
        // ScanAll/ScanCaptures = the exports, minus the writing and minus the stats
        // side-effect. What we analyse IS what the export buttons would hand to `build`.
        return Analyze(SceneExporter::ScanAll(), SceneExporter::ScanCaptures());
    }

    std::string Text(const Report& report) {
        std::string out;
        out += "# install requirements — scanned IN-GAME by SceneCaptureBridge\n";
        out += "#\n";
        out += "# Every master listed below must be INSTALLED AND ENABLED by whoever plays the\n";
        out += "# plugin you build from this export, or Skyrim SILENTLY refuses to load it: no\n";
        out += "# error, no log line, the records simply are not there in-game.\n";
        out += "#\n";
        out += "# This is the same analysis `modforge build` prints (and writes to\n";
        out += "# <plugin>.requires.txt) — brought forward to EXPORT time, so you can still do\n";
        out += "# something about it: you are standing in the room, and dropping one mod-sourced\n";
        out += "# spell is a re-capture away. Nothing is filtered on your behalf; full fidelity\n";
        out += "# is the point. This file only makes the cost visible.\n";
        out += "#\n";
        out += "# Each line under a master is the SPEC FIELD that pulls it in — i.e. the line to\n";
        out += "# delete (and re-export) if you decide the mod is not worth the dependency.\n";
        out += "# Paths are the fields of the files the export buttons write:\n";
        out += "#   scene.*    -> scene-export_<where>_<stamp>.json\n";
        out += "#   captures.* -> captures_<stamp>.json\n";
        out += "#\n";
        out += "# Two qualifiers may follow a line:\n";
        out += "#   [template clone] build DEEP-COPIES that record instead of linking it, so the\n";
        out += "#                    form itself is not a master link — but the copy drags the\n";
        out += "#                    source's own links along and uses its meshes/textures. In\n";
        out += "#                    practice you still need the mod.\n";
        out += "#   [named only]     a references[] label. It masters the mod only once something\n";
        out += "#                    in the spec actually points at that label (a package target,\n";
        out += "#                    an alias, a script property). Named and unused = free.\n";
        out += "#\n";

        std::string vanilla;
        for (const auto& m : report.masters) {
            if (!m.vanilla) continue;
            if (!vanilla.empty()) vanilla += ", ";
            vanilla += m.name;
        }
        out += std::format("# vanilla (in every install, no action needed): {}\n\n",
            vanilla.empty() ? "none" : vanilla);

        if (report.external == 0) {
            out += "requires NOTHING beyond the base game.\n";
            out += "Everything this export names comes from the five base-game masters, so the\n";
            out += "plugin will load for anybody.\n";
        } else {
            out += std::format("requires {} non-vanilla master(s), {} link(s) total:\n",
                report.external, report.links);
            for (const auto& m : report.masters) {
                if (m.vanilla) continue;
                out += std::format("\n{}  ({} link(s)){}\n", m.name, m.links,
                    m.creationClub
                        ? "  [Creation Club — owned per account, NOT on every install]"
                        : "");
                for (const auto& s : m.sources) out += std::format("    {}\n", s);
            }
        }

        if (report.fidelityOnly) {
            out += std::format(
                "\n---\n{} more ref(s) name a plugin but are NOT dependencies, and are deliberately\n"
                "left out above: build records them for fidelity and never links them (a runtime\n"
                "buff snapshot, an origin NPC we mint instead of override, an inert marker anchor).\n"
                "Deleting one would not drop a master, so listing it would be a lie. The SKSE log\n"
                "names every one of them, field by field.\n", report.fidelityOnly);
        }
        return out;
    }

    const Stats& Last() { return g_last; }

    void ExportToFile() {
        const auto report = Scan();
        auto dir = SKSE::log::log_directory();
        if (!dir) {
            SKSE::log::error("Requires::ExportToFile: no log_directory");
            return;
        }
        // Same never-overwrite discipline as every other export (…-2.txt, …-3.txt).
        const auto path = SceneExporter::UniquePath(
            *dir, std::format("requires_{}", SceneExporter::TimeStamp()), ".txt");
        try {
            std::ofstream file(path, std::ios::trunc);
            if (!file) {
                SKSE::log::error("Requires::ExportToFile: cannot open {}", path.string());
                return;
            }
            file << Text(report);
        } catch (const std::exception& e) {
            SKSE::log::error("Requires::ExportToFile: {}", e.what());
            return;
        }

        g_last.valid = true;
        g_last.external = report.external;
        g_last.creationClub = report.creationClub;
        g_last.links = report.links;
        g_last.path = path.string();
        SKSE::log::info("Requires: {} non-vanilla master(s), {} link(s), {} fidelity-only ref(s) "
            "ignored -> {}", report.external, report.links, report.fidelityOnly, path.string());
    }

}  // namespace Requires
