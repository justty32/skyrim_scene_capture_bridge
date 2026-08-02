// UI.Markers — the Markers page + the marker-edit window (split from UI.cpp
// per the 300-line convention). The window is the E-interaction surface:
// activating a marker gem opens it (plugin.cpp's ActivateSink), the page's
// per-row `edit` button is the hotkey-free equivalent.

#include "UI.h"

#include "Markers.h"
#include "SceneExporter.h"
#include "UI.Fields.h"
#include "log.h"

#include "SKSEMenuFramework.h"

#include <cstdio>
#include <string>

namespace {
    // Row fields are bound to the registry (UI.Fields) — no per-row buffers and
    // no invalidation: a row that is renamed, refused or deleted re-seeds itself.
    constexpr const char* kLabel = "##mk.label";
    constexpr const char* kKind = "##mk.kind";

    bool g_thisCellOnly = false;

    // ---- marker-edit window state ----
    SKSEMenuFramework::Model::WindowInterface* g_win = nullptr;
    std::uint32_t g_editSeq = 0;
    char g_label[64]{};
    char g_kind[24]{};
    char g_note[512]{};  // free-form brief for the agent -> annotations[].note
}

void UI::MarkerEditor::Init() {
    if (!SKSEMenuFramework::IsInstalled()) return;
    // Pauses the game and grabs input while open — it is a typing surface.
    g_win = SKSEMenuFramework::AddWindow(Render);
}

void UI::MarkerEditor::Open(std::uint32_t seq) {
    auto* e = ::Markers::FindBySeq(seq);
    if (!e || !g_win) return;
    g_editSeq = seq;
    std::snprintf(g_label, sizeof(g_label), "%s", e->label.c_str());
    std::snprintf(g_kind, sizeof(g_kind), "%s", e->kind.c_str());
    std::snprintf(g_note, sizeof(g_note), "%s", e->note.c_str());
    g_win->IsOpen = true;
}

void __stdcall UI::MarkerEditor::Render() {
    auto* e = ::Markers::FindBySeq(g_editSeq);
    if (!e) {  // deleted under us
        if (g_win) g_win->IsOpen = false;
        return;
    }
    bool open = true;
    ImGuiMCP::SetNextWindowSize({420.f, 260.f}, ImGuiMCP::ImGuiCond_FirstUseEver);
    if (ImGuiMCP::Begin("SCB Marker", &open)) {
        ImGuiMCP::Text("#%u  %s", e->seq,
            e->cellOrWs.empty() ? "(unresolved)" : e->cellOrWs.c_str());
        ImGuiMCP::InputText("label", g_label, sizeof(g_label));
        ImGuiMCP::InputText("kind", g_kind, sizeof(g_kind));
        // The note rides into annotations[].note — extra instructions for the
        // agent ("face the door", "a vendor here"). Registry-only: a reload
        // after a full game restart recovers the label, not the note.
        ImGuiMCP::InputTextMultiline("note", g_note, sizeof(g_note), {380.f, 80.f});

        // The page's rows mirror the registry every frame (UI.Fields RULE 1), so
        // a save here needs no cache-busting: the row shows the new label next
        // frame on its own. Same for a delete.
        if (ImGuiMCP::Button("save")) {
            ::Markers::Rename(e->seq, g_label);
            ::Markers::SetKind(e->seq, g_kind);
            ::Markers::SetNote(e->seq, g_note);
            open = false;
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("delete marker")) {
            ::Markers::Remove(e->seq);  // true deletion — gem + registry entry
            open = false;
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("cancel")) open = false;
    }
    ImGuiMCP::End();
    if (!open && g_win) g_win->IsOpen = false;
}

void __stdcall UI::MarkersPage::Render() {
    UI::ModeLine();
    auto& all = ::Markers::All();
    ImGuiMCP::Text("%zu marker(s). Marker mode (sc mk): the action key places "
                   "a gem where you aim; E on a gem opens its editor.", all.size());
    if (ImGuiMCP::Button("adopt this cell")) {
        // Recover markers from a previous session: their proxies + display
        // names live in the savegame, only this registry was lost.
        ::Markers::AdoptOrphans();
    }
    ImGuiMCP::SameLine();
    ImGuiMCP::Checkbox("this cell only", &g_thisCellOnly);
    ImGuiMCP::Separator();

    // One anchor lookup per frame, shared by every row's filter test.
    const std::string here = g_thisCellOnly ? SceneExporter::AnchorOf(nullptr).id : "";

    std::uint32_t removeSeq = 0;
    // Newest first — the one you just placed is the one you want to rename.
    for (auto it = all.rbegin(); it != all.rend(); ++it) {
        auto& e = *it;
        if (g_thisCellOnly && e.cellOrWs != here) continue;
        ImGuiMCP::PushID(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(e.seq)));

        std::string edit;

        ImGuiMCP::Text("#%u", e.seq);
        ImGuiMCP::SameLine();
        if (UI::BoundText(kLabel, e.seq, e.label, 64, 180.f, edit)) {
            ::Markers::Rename(e.seq, edit);
        }
        ImGuiMCP::SameLine();
        if (UI::BoundText(kKind, e.seq, e.kind, 24, 90.f, edit)) {
            ::Markers::SetKind(e.seq, edit);
        }
        ImGuiMCP::SameLine();
        // Redundant now that clicking away commits, but kept as the affordance
        // people reach for — it commits what the fields are SHOWING.
        if (ImGuiMCP::Button("apply")) {
            ::Markers::Rename(e.seq, UI::Shown(kLabel, e.seq));
            ::Markers::SetKind(e.seq, UI::Shown(kKind, e.seq));
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("edit")) {
            UI::MarkerEditor::Open(e.seq);  // full editor incl. the note field
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("del")) {
            removeSeq = e.seq;
        }
        ImGuiMCP::SameLine();
        if (e.note.empty()) {
            ImGuiMCP::Text("%s", e.cellOrWs.empty() ? "(unresolved)" : e.cellOrWs.c_str());
        } else {
            ImGuiMCP::Text("[note] %s", e.cellOrWs.empty() ? "(unresolved)" : e.cellOrWs.c_str());
        }

        ImGuiMCP::PopID();
    }
    if (removeSeq != 0) ::Markers::Remove(removeSeq);
}
