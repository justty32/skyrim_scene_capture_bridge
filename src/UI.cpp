#include "UI.h"

#include "Captures.h"
#include "Editor.h"
#include "Eraser.h"
#include "Markers.h"
#include "Overrides.h"
#include "Palette.h"
#include "Preview.h"
#include "Requires.h"
#include "SceneExporter.h"
#include "UI.Fields.h"
#include "log.h"

#include "SKSEMenuFramework.h"

#include <string>

namespace {
    // Bound-field slot ids (UI.Fields): unique per field per page, and they
    // double as the ImGui label. Rows keyed by seq (captures) or by a hash of
    // the durable id (eraser/overrides) — NOT by list position, which shifts
    // under an undo/revert. The palette is the exception: its slots carry no
    // identity at all, so it keys by index and calls ForgetEdits() on a reshape.
    constexpr const char* kErLabel = "##er.label";
    constexpr const char* kErNote = "##er.note";
    constexpr const char* kCapLabel = "##cap.label";
    constexpr const char* kCapNote = "##cap.note";
    constexpr const char* kOvLabel = "##ov.label";
    constexpr const char* kOvNote = "##ov.note";
    constexpr const char* kSlotName = "##pal.name";
    constexpr const char* kSlotNote = "##pal.note";

    // Resolve the cell the player is standing in, for display. Cheap enough to
    // do per frame: it is a couple of pointer hops plus a formatted string.
    std::string CurrentCellLabel() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        RE::TESObjectCELL* cell = player ? player->GetParentCell() : nullptr;
        if (!cell) {
            return "(no cell)";
        }
        if (cell->IsInteriorCell()) {
            if (auto id = SceneExporter::ResolveDurableId(cell)) {
                return "interior " + *id;
            }
            return "interior (unresolvable)";
        }
        if (auto* ws = cell->GetRuntimeData().worldSpace) {
            if (auto id = SceneExporter::ResolveDurableId(ws)) {
                return "exterior " + *id;
            }
        }
        return "exterior (unresolvable)";
    }
}

void UI::Register() {
    if (!SKSEMenuFramework::IsInstalled()) {
        SKSE::log::info("SKSE Menu Framework not present — hotkey only, no panel");
        return;
    }
    SKSEMenuFramework::SetSection("Scene Capture Bridge");
    SKSEMenuFramework::AddSectionItem("Export", Export::Render);
    SKSEMenuFramework::AddSectionItem("Markers", MarkersPage::Render);
    SKSEMenuFramework::AddSectionItem("Eraser", EraserPage::Render);
    SKSEMenuFramework::AddSectionItem("Browser", BrowserPage::Render);
    SKSEMenuFramework::AddSectionItem("Palette", PalettePage::Render);
    SKSEMenuFramework::AddSectionItem("Captures", CapturesPage::Render);
    SKSEMenuFramework::AddSectionItem("References", ReferencesPage::Render);
    SKSEMenuFramework::AddSectionItem("Editor", EditorPage::Render);
    SKSEMenuFramework::AddSectionItem("Settings", SettingsPage::Render);
    MarkerEditor::Init();  // the standalone E-interaction window

    // The per-frame hook the browser's ghost needs. A HUD element renders even
    // with the PANEL CLOSED — which is the whole point: you pick an object, close
    // the panel, and the ghost follows your aim while you walk to the spot. (The
    // Browser page calls Update() too, for the frames the panel IS open; the call
    // is idempotent, so having both costs nothing and neither can be the only one.)
    SKSEMenuFramework::AddHudElement([]() { Preview::Update(); });

    SKSE::log::info("SKSE Menu Framework panel registered");
}

void __stdcall UI::Export::Render() {
    UI::ModeLine();
    const auto label = CurrentCellLabel();
    ImGuiMCP::Text("Cell: %s", label.c_str());

    // Human-readable location + the player's world coords, so the exported
    // anchor can be sanity-checked against where you're actually standing.
    if (auto* player = RE::PlayerCharacter::GetSingleton()) {
        RE::TESObjectCELL* cell = player->GetParentCell();
        if (cell && !cell->IsInteriorCell()) {
            if (auto* ws = cell->GetRuntimeData().worldSpace) {
                const char* wn = ws->GetFullName();
                if (!wn || !*wn) wn = ws->GetFormEditorID();
                ImGuiMCP::Text("World: %s", (wn && *wn) ? wn : "(unnamed)");
            }
        } else if (cell) {
            const char* cn = cell->GetFullName();
            if (cn && *cn) ImGuiMCP::Text("Cell name: %s", cn);
        }
        const auto p = player->GetPosition();
        ImGuiMCP::Text("Player XYZ: (%.1f, %.1f, %.1f)", p.x, p.y, p.z);
    }
    ImGuiMCP::Separator();

    // Export deliberately has no hotkey (user-decided): this button is it.
    // Each press writes its OWN file — scene-export_<where>_<YYYYMMDD-HHMM>.json
    // — so two exports in a row no longer overwrite one another.
    if (ImGuiMCP::Button("Export player cell")) {
        SceneExporter::ExportPlayerCellToFile();
    }
    ImGuiMCP::SameLine();
    // Every loaded cell + all registries. Placements in unloaded cells can't be
    // captured (registries — markers/erasures/overrides — are always global).
    if (ImGuiMCP::Button("Export all (loaded cells)")) {
        SceneExporter::ExportAllToFile();
    }
    ImGuiMCP::TextWrapped("Scene exports carry objects + markers only — NPCs are "
        "not swept (place them from the markers in ModForge). Captured "
        "definitions have their own button below.");

    ImGuiMCP::Separator();

    // Captures are a cell-independent library, not scene content of whatever
    // cell you happen to stand in — own button, own captures_<stamp>.json.
    if (ImGuiMCP::Button("Export captures")) {
        SceneExporter::ExportCapturesToFile();
    }
    ImGuiMCP::SameLine();
    ImGuiMCP::Text("(%zu in registry -> capturedItems[]/capturedNpcs[])",
        ::Captures::All().size());
    if (const auto& cs = SceneExporter::LastCapturesExport(); cs.valid) {
        ImGuiMCP::BulletText("last: %zu item(s), %zu npc(s)", cs.items, cs.npcs);
        if (!cs.path.empty()) ImGuiMCP::TextWrapped("Wrote %s", cs.path.c_str());
    }

    ImGuiMCP::Separator();

    // WHICH MODS WILL THE BUILT ESP NEED? Answered here, NOW — not after a build,
    // when you have already quit. A mod-sourced spell/perk/item makes that mod a
    // MASTER, and Skyrim silently refuses to load a plugin whose masters are
    // missing. Scanning is read-only: it writes a .txt and changes nothing.
    constexpr ImGuiMCP::ImVec4 kWarnCol{1.f, 0.55f, 0.25f, 1.f};
    if (ImGuiMCP::Button("Export requires")) {
        ::Requires::ExportToFile();
    }
    ImGuiMCP::SameLine();
    ImGuiMCP::Text("(what mods the built esp will REQUIRE)");
    if (const auto& r = ::Requires::Last(); r.valid) {
        if (r.external == 0) {
            ImGuiMCP::BulletText("vanilla only — the plugin will load for anybody");
        } else {
            ImGuiMCP::TextColored(kWarnCol,
                "  %zu non-vanilla master(s), %zu link(s)%s — anyone missing them gets NO "
                "plugin (Skyrim drops it silently)", r.external, r.links,
                r.creationClub ? " (incl. Creation Club)" : "");
        }
        if (!r.path.empty()) ImGuiMCP::TextWrapped("Wrote %s", r.path.c_str());
    } else {
        ImGuiMCP::TextWrapped("Scans placements, removals, overrides, references, markers and "
            "the whole capture registry; writes requires_<stamp>.txt. Same rules as `modforge "
            "build`'s <plugin>.requires.txt, so the two can be compared.");
    }

    ImGuiMCP::Separator();

    const auto& s = SceneExporter::LastExport();
    if (!s.valid) {
        ImGuiMCP::TextWrapped("No scene export yet this session.");
        return;
    }

    ImGuiMCP::Text("Last scene export — %s", s.cell.c_str());
    // Added / modified / removed each count independently (user-requested).
    ImGuiMCP::BulletText("%zu added (placements[])", s.placements);
    ImGuiMCP::BulletText("%zu modified (overrides[])", s.overrides);
    ImGuiMCP::BulletText("%zu removed (removals[])", s.removals);
    // Named existing refs. A referrer whose in-file target wasn't in this export
    // (its cell wasn't swept / the object is gone) is NOT written — say so here,
    // otherwise the count silently disagrees with the References page.
    ImGuiMCP::BulletText("%zu named (references[])", s.references);
    if (s.referencesSkipped) {
        ImGuiMCP::BulletText("%zu reference(s) skipped — their own-placement target "
            "wasn't in this export (see the log)", s.referencesSkipped);
    }
    // The number that proves the vanilla diff: authored refs are recognised and
    // skipped, so `build` does not re-place the whole room on top of itself.
    ImGuiMCP::BulletText("%zu pre-existing (skipped)", s.preexisting);
    if (s.actorsExcluded) {
        ImGuiMCP::BulletText("%zu actor(s) excluded (NPCs go via markers/captures)",
            s.actorsExcluded);
    }
    if (s.skipped) {
        // A dynamic base cannot be named in an esp, so its ref cannot be
        // exported at all — worth surfacing rather than silently dropping.
        ImGuiMCP::BulletText("%zu skipped (dynamic base, not exportable)", s.skipped);
    }
    if (!s.path.empty()) {
        ImGuiMCP::TextWrapped("Wrote %s", s.path.c_str());
    }
}

// MarkersPage + MarkerEditor live in UI.Markers.cpp (300-line convention).

void __stdcall UI::EraserPage::Render() {
    UI::ModeLine();
    constexpr ImGuiMCP::ImVec4 kWarn{1.f, 0.55f, 0.25f, 1.f};
    static bool thisCellOnly = false;

    auto& marked = ::Eraser::All();
    const std::string here = SceneExporter::AnchorOf(nullptr).id;

    ImGuiMCP::Text("%zu marked for removal. In delete mode (sc del) the action "
                   "key erases the crosshair target.", marked.size());
    // With "this cell only" on, undo pops the last mark made in THIS cell.
    if (ImGuiMCP::Button("undo")) {
        if (thisCellOnly) ::Eraser::UndoInCell(here); else ::Eraser::Undo();
    }
    ImGuiMCP::SameLine();
    if (ImGuiMCP::Button("clear (re-enable all)")) { ::Eraser::Clear(); }
    ImGuiMCP::SameLine();
    ImGuiMCP::Checkbox("this cell only", &thisCellOnly);
    ImGuiMCP::Separator();

    // Each row: [undo] [label] [note] [apply], then what was marked. Newest
    // first, matching undo order. The note says WHY it goes ("cleared for the
    // shelf") and rides into the exported removals[] entry.
    std::string undoId;
    for (auto it = marked.rbegin(); it != marked.rend(); ++it) {
        const auto& e = *it;
        if (thisCellOnly && e.cellOrWs != here) continue;
        ImGuiMCP::PushID(e.id.c_str());
        // Rows are identified by their durable id, not by position in the list:
        // an undo two rows up must not drag every field down with it.
        const auto row = UI::RowKey(e.id);
        std::string edit;

        if (ImGuiMCP::Button("undo")) undoId = e.id;
        ImGuiMCP::SameLine();
        if (UI::BoundText(kErLabel, row, e.label, 64, 150.f, edit)) ::Eraser::SetLabel(e.id, edit);
        ImGuiMCP::SameLine();
        if (UI::BoundText(kErNote, row, e.note, 256, 220.f, edit)) ::Eraser::SetNote(e.id, edit);
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("apply")) {
            ::Eraser::SetLabel(e.id, UI::Shown(kErLabel, row));
            ::Eraser::SetNote(e.id, UI::Shown(kErNote, row));
        }

        const auto& col = e.addsMaster ? kWarn : ImGuiMCP::ImVec4{1.f, 1.f, 1.f, 1.f};
        ImGuiMCP::TextColored(col, "  %s  %s  (%.0f, %.0f, %.0f)%s",
            e.id.c_str(), e.name.empty() ? "" : e.name.c_str(),
            e.position.x, e.position.y, e.position.z,
            e.addsMaster ? "  -- adds a master" : "");
        ImGuiMCP::PopID();
    }
    if (!undoId.empty()) ::Eraser::UndoEntry(undoId);
}

void __stdcall UI::CapturesPage::Render() {
    UI::ModeLine();
    auto& caps = ::Captures::All();

    ImGuiMCP::TextWrapped("%zu captured definition(s). Point at an enchanted "
        "weapon/armour, potion or ingredient (-> capturedItems[]) or an NPC "
        "(-> capturedNpcs[], unique NPCs included) and capture it for ModForge to "
        "rebuild. (`sc cap` mode, action key; `sc cap er0/er1` crosshair/ray)",
        caps.size());
    if (ImGuiMCP::Button("clear")) { ::Captures::Clear(); }
    ImGuiMCP::SameLine();
    // Own file (captures_<stamp>.json), separate from any scene export — same
    // button as the Export page's, put here because this is where you are.
    if (ImGuiMCP::Button("export captures")) { SceneExporter::ExportCapturesToFile(); }
    if (const auto& cs = SceneExporter::LastCapturesExport(); cs.valid && !cs.path.empty()) {
        ImGuiMCP::TextWrapped("Wrote %s", cs.path.c_str());
    }
    ImGuiMCP::Separator();

    // Each row: [undo] [label] [note] [apply], then what was captured. Newest
    // first, matching undo order. The label was console-only (`sc capp <label>`)
    // until now — it becomes the captured record's editorId, so being able to
    // fix a typo without re-capturing the NPC is the point.
    std::uint32_t undoSeq = 0;
    bool doUndo = false;
    for (auto it = caps.rbegin(); it != caps.rend(); ++it) {
        const auto& e = *it;
        ImGuiMCP::PushID(std::to_string(e.seq).c_str());
        std::string edit;

        if (ImGuiMCP::Button("undo")) { undoSeq = e.seq; doUndo = true; }
        ImGuiMCP::SameLine();
        if (UI::BoundText(kCapLabel, e.seq, e.label, 64, 150.f, edit))
            ::Captures::SetLabel(e.seq, edit);
        ImGuiMCP::SameLine();
        if (UI::BoundText(kCapNote, e.seq, e.note, 256, 220.f, edit))
            ::Captures::SetNote(e.seq, edit);
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("apply")) {
            ::Captures::SetLabel(e.seq, UI::Shown(kCapLabel, e.seq));
            ::Captures::SetNote(e.seq, UI::Shown(kCapNote, e.seq));
        }

        if (e.kind == ::Captures::Kind::kNpc) {
            const auto& n = e.npc;
            ImGuiMCP::Text("  %s  [npc%s%s]  %s %s, %zu headpart(s), %zu tint(s), "
                "%zu perk(s), %zu buff(s)%s", e.name.c_str(),
                n.unique ? " UNIQUE" : "", n.dead ? " DEAD" : "",
                n.female ? "female" : "male", n.race.empty() ? "?" : n.race.c_str(),
                n.headParts.size(), n.tints.size(), n.perks.size(), n.activeEffects.size(),
                e.base.empty() ? "  (runtime base)" : "");
        } else {
            ImGuiMCP::Text("  %s  [%s]  %zu effect(s)%s", e.name.c_str(),
                ::Captures::KindName(e.kind), e.effects.size(),
                e.base.empty() ? "  (runtime base)" : "");
        }
        ImGuiMCP::PopID();
    }
    if (doUndo) ::Captures::UndoEntry(undoSeq);
}

void __stdcall UI::PalettePage::Render() {
    UI::ModeLine();
    constexpr ImGuiMCP::ImVec4 kWarn{1.f, 0.55f, 0.25f, 1.f};

    auto& slots = ::Palette::All();
    ImGuiMCP::Text("%zu slot(s). Pick mode (sc pk) eyedrops the crosshair "
                   "target; place mode (sc pl) spawns the selected slot where "
                   "you aim. Slots persist across saves.", slots.size());

    // OWNERSHIP (2026-07-14). The export emits a dynamic ref only if this registry
    // says we placed it — "dynamic" alone was never proof (the engine PlaceAtMe's
    // fish and critters, and nine of them once shipped in a ten-placement export).
    // This button is the explicit way back in for things we did NOT place but you
    // DO want: a console `placeatme`, or anything placed before ownership existed.
    static std::size_t adopted = 0;
    ImGuiMCP::Text("%zu placement(s) recorded as ours (only these export)",
        ::Palette::Placed().size());
    ImGuiMCP::SameLine();
    if (ImGuiMCP::Button("adopt dynamic refs in this cell")) adopted = ::Palette::AdoptDynamicInCell();
    if (adopted) {
        ImGuiMCP::TextColored(kWarn, "  adopted %zu ref(s) — they WILL export now. Adopt is "
            "indiscriminate: if the engine's fish were standing here, you just adopted the fish.",
            adopted);
    }
    ImGuiMCP::Separator();

    // Named palette file (in the SKSE folder). Two load flavours, one save:
    //   load from file (append)  — the file's slots land ON TOP, keeping yours
    //   replace from file        — the file BECOMES the palette (yours are dropped)
    // The file lists slots in this list's order (top first), so it reads like
    // what you see here.
    static char fileName[128] = "";
    ImGuiMCP::SetNextItemWidth(260.f);
    ImGuiMCP::InputText("##palfile", fileName, sizeof(fileName));
    ImGuiMCP::SameLine();
    if (ImGuiMCP::Button("load from file (append)")) {
        if (fileName[0]) { ::Palette::LoadFromFile(fileName); UI::ForgetEdits(); }
    }
    ImGuiMCP::SameLine();
    if (ImGuiMCP::Button("replace from file")) {
        if (fileName[0]) { ::Palette::ReplaceFromFile(fileName); UI::ForgetEdits(); }
    }
    ImGuiMCP::SameLine();
    if (ImGuiMCP::Button("save to file")) {
        if (fileName[0]) ::Palette::SaveToFile(fileName);
    }
    ImGuiMCP::TextWrapped("(SKSE folder; e.g. my-palette.json) — append: the "
                          "loaded slots go to the top of the list. replace: "
                          "clears the current slots first (a missing or empty "
                          "file changes nothing).");

    // Clear the whole palette. The slots are DISK-persisted and save-agnostic —
    // this throws away work from every playthrough, and unlike `replace from
    // file` there is no incoming file to make it worth it. Hence two guards: a
    // confirmation click, and an undo that survives until the game closes.
    // `save to file` above is the way to keep a copy first, and it says so.
    static bool confirmClear = false;
    if (!confirmClear) {
        if (ImGuiMCP::Button("clear all slots")) confirmClear = true;
    } else {
        ImGuiMCP::TextColored(kWarn, "really clear all %zu slot(s)?", slots.size());
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("yes, clear")) {
            ::Palette::Clear();
            UI::ForgetEdits();
            confirmClear = false;
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("cancel")) confirmClear = false;
    }
    if (const auto undoable = ::Palette::ClearedCount(); undoable) {
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("undo clear")) {
            ::Palette::UndoClear();
            UI::ForgetEdits();
        }
        ImGuiMCP::SameLine();
        ImGuiMCP::TextDisabled("(%zu slot(s) recoverable until you quit)", undoable);
    }
    ImGuiMCP::Separator();

    std::size_t removeIdx = SIZE_MAX;
    // Newest first — the slot you just eyedropped is the one you want to name.
    for (std::size_t n = 0; n < slots.size(); ++n) {
        const std::size_t i = slots.size() - 1 - n;
        auto& s = slots[i];
        ImGuiMCP::PushID(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(i + 1)));

        const bool selected = (i == ::Palette::SelectedIndex());
        if (ImGuiMCP::Button(selected ? "[use]" : " use ")) ::Palette::Select(i);
        ImGuiMCP::SameLine();

        // Name + note are freely editable (Bed -> GoodBed). Enter, `apply` OR
        // clicking away commits, and each write persists the palette json on the
        // spot — so what you can see is what is on disk, and `save to file`
        // carries the notes with it.
        std::string edit;
        if (UI::BoundText(kSlotName, i, s.name, 64, 200.f, edit)) ::Palette::Rename(i, edit);
        ImGuiMCP::SameLine();
        if (UI::BoundText(kSlotNote, i, s.note, 256, 200.f, edit)) ::Palette::SetNote(i, edit);
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("apply")) {
            ::Palette::Rename(i, UI::Shown(kSlotName, i));
            ::Palette::SetNote(i, UI::Shown(kSlotNote, i));
        }
        ImGuiMCP::SameLine();
        if (!s.base) {
            // The slot's plugin left the load order — kept, but F7 refuses it.
            ImGuiMCP::TextColored(kWarn, "%s (unavailable)", s.baseId.c_str());
        } else if (s.addsMaster) {
            ImGuiMCP::TextColored(kWarn, "%s", s.baseId.c_str());
        } else {
            ImGuiMCP::Text("%s", s.baseId.c_str());
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("del")) removeIdx = i;

        ImGuiMCP::PopID();
    }
    if (removeIdx != SIZE_MAX) {
        ::Palette::Remove(removeIdx);
        UI::ForgetEdits();  // indices shifted — an in-flight edit would land on another slot
    }
}

void __stdcall UI::EditorPage::Render() {
    UI::ModeLine();
    constexpr ImGuiMCP::ImVec4 kWarn{1.f, 0.55f, 0.25f, 1.f};

    const auto st = ::Editor::Current();
    if (!st.active) {
        ImGuiMCP::TextWrapped(
            "Edit mode (sc ed): aim and press the action key to edit the "
            "target (numpad * ray-selects trees/statics the crosshair "
            "misses). Your own refs export their live pose; an authored "
            "(vanilla/mod) ref becomes an overrides[] entry when you commit.");
    } else {
        ImGuiMCP::Text("Editing: %s", st.name);
        ImGuiMCP::BulletText("pos (%.1f, %.1f, %.1f)", st.pos.x, st.pos.y, st.pos.z);
        ImGuiMCP::BulletText("yaw %.1f deg   scale %.2f", st.yawDeg, st.scale);
        ImGuiMCP::Separator();
        if (::Editor::RotateMode()) {
            ImGuiMCP::TextWrapped(
                "ROTATE mode (sc ed ax): 4/6 yaw - 1/3 pitch - 7/9 roll - "
                "per-axis revert: 5 yaw, 2 pitch, 8 roll (back to the pre-edit "
                "angle) - +/- scale - 0 commit - . cancel (restores everything)");
        } else {
            ImGuiMCP::TextWrapped(
                "numpad: 8/2 fwd/back - 4/6 left/right - 1/3 down/up - 7/9 yaw - "
                "+/- scale - 5 reset - 0 commit - . cancel  (sc ed ax = rotate mode)");
        }
        // The rotate/move/scale keys REPEAT while held (and speed up); the
        // reverts and commit/cancel deliberately do not.
        ImGuiMCP::TextDisabled(
            "hold a move/rotate/scale key to keep going (it accelerates); "
            "revert, commit and cancel are one-per-press");
    }

    // Committed edits of AUTHORED refs — these export as overrides[]. Revert
    // moves the ref back to its pre-edit baseline and unregisters it.
    ImGuiMCP::Separator();
    auto& moved = ::Overrides::All();
    ImGuiMCP::Text("%zu authored ref override(s)", moved.size());
    if (!moved.empty()) {
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("revert all")) { ::Overrides::Clear(); }
    }
    std::size_t revert = SIZE_MAX;
    for (std::size_t i = 0; i < moved.size(); ++i) {
        const auto& e = moved[i];
        ImGuiMCP::PushID(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(i + 1)));
        // Keyed by the durable id, not by i: reverting a row above must not
        // shift every field below it onto the wrong override.
        const auto row = UI::RowKey(e.id);
        std::string edit;

        if (ImGuiMCP::Button("revert")) revert = i;
        ImGuiMCP::SameLine();
        if (UI::BoundText(kOvLabel, row, e.label, 64, 150.f, edit)) ::Overrides::SetLabel(e.id, edit);
        ImGuiMCP::SameLine();
        if (UI::BoundText(kOvNote, row, e.note, 256, 220.f, edit)) ::Overrides::SetNote(e.id, edit);
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("apply")) {
            ::Overrides::SetLabel(e.id, UI::Shown(kOvLabel, row));
            ::Overrides::SetNote(e.id, UI::Shown(kOvNote, row));
        }

        const auto& col = e.addsMaster ? kWarn : ImGuiMCP::ImVec4{1.f, 1.f, 1.f, 1.f};
        ImGuiMCP::TextColored(col, "  %s  %s  (%.0f, %.0f, %.0f)%s",
            e.id.c_str(), e.name.empty() ? "" : e.name.c_str(),
            e.pos.x, e.pos.y, e.pos.z,
            e.addsMaster ? "  -- adds a master" : "");
        ImGuiMCP::PopID();
    }
    if (revert != SIZE_MAX) ::Overrides::Revert(revert);
}
