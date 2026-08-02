// UI.References — the References page (referrer registry, `sc ref` / `sc refc`).
// Split from UI.cpp per the 300-line convention, shaped like the Markers page:
// newest first, in-place rename, per-row delete.
//
// The one rule this page has to enforce: a LABEL IS A GLOBAL NAME in ModForge
// (build registers it as a resolvable id, so any ref field of the spec can point
// at it). Two rows with the same label would fail validate on the whole spec —
// so a duplicate rename is REFUSED here, in front of the user, not discovered
// four steps later in a build log.

#include "UI.h"

#include "Referrer.h"
#include "SceneExporter.h"
#include "UI.Fields.h"

#include "SKSEMenuFramework.h"

#include <string>
#include <unordered_map>

namespace {
    constexpr const char* kLabel = "##ref.label";
    constexpr const char* kNote = "##ref.note";

    // Whether this row's last rename was REFUSED (duplicate label). Page state,
    // not field state: the field re-seeds from the registry (UI.Fields RULE 1),
    // so a refused label visibly snaps back to the stored one on its own — this
    // flag is only here to say WHY it did.
    std::unordered_map<std::uint32_t, bool> g_clash;
    bool g_thisCellOnly = false;

    void Rename(const Referrer::Entry& e, const std::string& label) {
        g_clash[e.seq] = !::Referrer::Rename(e.seq, label);
    }
}

void __stdcall UI::ReferencesPage::Render() {
    UI::ModeLine();
    constexpr ImGuiMCP::ImVec4 kWarn{1.f, 0.55f, 0.25f, 1.f};
    constexpr ImGuiMCP::ImVec4 kOurs{0.55f, 0.85f, 0.55f, 1.f};

    auto& all = ::Referrer::All();
    ImGuiMCP::TextWrapped(
        "%zu reference(s). Referrer mode (sc ref): the action key NAMES the ref you "
        "are aiming at — nothing in the world changes. `sc ref <Label>` names it and "
        "labels it in one go; `sc refc [Label]` uses the console selection instead. "
        "The label becomes a name ModForge can point at from any ref field of the "
        "spec (a package's sandbox location, an alias, a linked ref...).",
        all.size());
    ImGuiMCP::TextWrapped(
        "Labels must be UNIQUE — the label IS the name. A ref YOU placed (green) is "
        "exported as an in-file dependency: its placement gets a stable editorId and "
        "ModForge makes it persistent. A vanilla/mod ref is exported by its durable "
        "id, and ModForge warns if it is a temporary ref.");
    ImGuiMCP::SameLine();
    ImGuiMCP::Checkbox("this cell only", &g_thisCellOnly);
    ImGuiMCP::Separator();

    const std::string here = g_thisCellOnly ? SceneExporter::AnchorOf(nullptr).id : "";

    std::uint32_t removeSeq = 0;
    // Newest first — the one you just marked is the one you want to name.
    for (auto it = all.rbegin(); it != all.rend(); ++it) {
        auto& e = *it;
        if (g_thisCellOnly && e.cellOrWs != here) continue;
        ImGuiMCP::PushID(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(e.seq)));

        std::string edit;

        ImGuiMCP::Text("#%u", e.seq);
        ImGuiMCP::SameLine();
        if (UI::BoundText(kLabel, e.seq, e.label, 64, 180.f, edit)) Rename(e, edit);
        ImGuiMCP::SameLine();
        if (UI::BoundText(kNote, e.seq, e.note, 256, 220.f, edit)) ::Referrer::SetNote(e.seq, edit);
        ImGuiMCP::SameLine();
        // Redundant now that clicking away commits, but kept as the affordance
        // people reach for — it commits what the fields are SHOWING.
        if (ImGuiMCP::Button("apply")) {
            Rename(e, UI::Shown(kLabel, e.seq));
            ::Referrer::SetNote(e.seq, UI::Shown(kNote, e.seq));
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("del")) removeSeq = e.seq;  // registry row only; the world is untouched

        if (const auto c = g_clash.find(e.seq); c != g_clash.end() && c->second) {
            ImGuiMCP::TextColored(kWarn,
                "  label already used by another reference — labels are unique names; "
                "not renamed (the field is back to the stored one)");
        }

        // What the row actually points at. In-file = one of our own placements (no
        // durable id exists), exported as `editorId` — show the very editorId the
        // exporter will write, so it can be matched against the json by eye.
        if (e.id.empty()) {
            const auto ed = ::Referrer::EditorIdOf(e);
            const bool lost = !e.handle.get();
            ImGuiMCP::TextColored(lost ? kWarn : kOurs,
                "  ours -> %s%s  %s  base %s  (%.0f, %.0f, %.0f)  %s", ed.c_str(),
                lost ? "  [TARGET LOST — not exportable; re-place it and re-mark]" : "",
                e.name.c_str(), e.base.empty() ? "?" : e.base.c_str(),
                e.position.x, e.position.y, e.position.z,
                e.cellOrWs.empty() ? "(unresolved)" : e.cellOrWs.c_str());
        } else {
            ImGuiMCP::Text("  %s  %s  base %s  (%.0f, %.0f, %.0f)  %s", e.id.c_str(),
                e.name.c_str(), e.base.empty() ? "?" : e.base.c_str(),
                e.position.x, e.position.y, e.position.z,
                e.cellOrWs.empty() ? "(unresolved)" : e.cellOrWs.c_str());
        }

        ImGuiMCP::PopID();
    }
    if (removeSeq != 0) {
        ::Referrer::Remove(removeSeq);
        g_clash.erase(removeSeq);
    }
}
