// UI.Browser — the Browser page: the CK's Object Window, in-game (user 2026-07-14).
//
// "要擺山脈，我得先去世界上找一座來吸" — that was the gap. This page lists
// everything the load order can place, and the PREVIEW is the world itself: click
// an entry and it is standing at your aim point, real size, real light (Preview.h).
//
// TWO THINGS ABOUT THE LIST THAT LOOK LIKE BUGS AND ARE NOT:
//
//   * MOST STATICS HAVE NO NAME. Not a lookup failure — STAT records simply have
//     no FULL name (EDID/OBND/MODL is the whole record). The model path is their
//     name, which is why it is the column that matters.
//   * THERE ARE NO EDITOR IDs in Skyrim's runtime records. An optional ModForge
//     scene-catalog.json beside the exports supplies them as search/display
//     metadata; the runtime form remains authoritative for placement.

#include "UI.h"

#include "Catalog.h"
#include "Modes.h"
#include "Palette.h"
#include "Preview.h"

#include "SKSEMenuFramework.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {
    // The list is capped rather than clipped: 30k Selectables submitted per frame
    // is a real cost, and a cap we ANNOUNCE is honest where a silent truncation
    // would read as "that's everything the game has".
    constexpr int kMaxRows = 500;

    char g_query[128]{};
    RE::FormType g_type = RE::FormType::None;   // None = every type
    std::string g_plugin;                        // "" = every plugin
    bool g_livePreview = true;

    // The filter is a linear scan of the whole catalogue, so it runs when a
    // filter CHANGES, not every frame.
    std::vector<std::size_t> g_hits;
    std::string g_lastQuery = "\x01";  // impossible value => first render always filters
    RE::FormType g_lastType = RE::FormType::None;
    std::string g_lastPlugin;

    std::size_t g_selected = SIZE_MAX;  // index into Catalog::All()

    void Refilter() {
        g_hits = Catalog::Filter(g_query, g_type, g_plugin);
        g_lastQuery = g_query;
        g_lastType = g_type;
        g_lastPlugin = g_plugin;
    }

    // What a placement made from this entry is called. Statics have no name, so
    // fall back to the model's file stem — "MountainCliff01", which is exactly
    // what a person would have called it.
    std::string LabelOf(const Catalog::Entry& e) {
        if (!e.name.empty()) return e.name;
        if (!e.editorId.empty()) return e.editorId;
        auto slash = e.model.find_last_of("\\/");
        std::string file = slash == std::string::npos ? e.model : e.model.substr(slash + 1);
        if (auto dot = file.rfind('.'); dot != std::string::npos) file = file.substr(0, dot);
        return file.empty() ? e.id : file;
    }

    // Clicking an entry does NOT open a preview of its own — it makes place mode
    // point at this thing. The user's framing (2026-07-14): "browser 本質上是
    // sc pl 的一個附屬品". So: switch to place mode, turn the ghost on (temporarily,
    // if they had it off), and pin this entry as what the action key will place.
    void PickForPlacing(const Catalog::Entry& e) {
        Modes::Set(Modes::Mode::kPlace);
        Preview::ForceGhostOn();
        Preview::ShowBase(e.base, LabelOf(e));
    }
}

void __stdcall UI::BrowserPage::Render() {
    UI::ModeLine();
    Catalog::EnsureBuilt();   // first open pays for the sweep; every other is free
    Preview::Update();        // the ghost tracks your aim while the page is open too

    constexpr ImGuiMCP::ImVec4 kWarn{1.f, 0.55f, 0.25f, 1.f};
    constexpr ImGuiMCP::ImVec4 kDim{0.65f, 0.65f, 0.65f, 1.f};

    if (!Catalog::Built()) {
        ImGuiMCP::TextColored(kWarn, "catalogue not built yet (no data handler) — reopen this page");
        return;
    }

    ImGuiMCP::TextWrapped(
        "%zu placeable base(s) in the load order. Pick one and it appears where you are "
        "looking — that IS the preview: real size, real light, real spot. Close the panel "
        "and it follows your aim; the PLACE mode's action key (`sc pl`) drops a real copy "
        "and leaves the ghost up, so a row of trees is one key pressed five times.",
        Catalog::All().size());
    ImGuiMCP::TextColored(kDim,
        "Search matches EditorID (when scene-catalog.json is loaded), model path, name, and FormKey.");
    ImGuiMCP::TextColored(kDim, "%s; %zu runtime match(es).",
        Catalog::OfflineStatus().c_str(), Catalog::OfflineMatches());
    ImGuiMCP::Separator();

    // ---- filters -----------------------------------------------------------
    ImGuiMCP::SetNextItemWidth(240.f);
    ImGuiMCP::InputText("search", g_query, sizeof(g_query));
    ImGuiMCP::SameLine();
    ImGuiMCP::SetNextItemWidth(160.f);
    if (ImGuiMCP::BeginCombo("type", Catalog::TypeName(g_type))) {
        if (ImGuiMCP::Selectable("all types", g_type == RE::FormType::None))
            g_type = RE::FormType::None;
        for (const auto t : Catalog::Types())
            if (ImGuiMCP::Selectable(Catalog::TypeName(t), g_type == t)) g_type = t;
        ImGuiMCP::EndCombo();
    }
    ImGuiMCP::SameLine();
    ImGuiMCP::SetNextItemWidth(200.f);
    if (ImGuiMCP::BeginCombo("plugin", g_plugin.empty() ? "all plugins" : g_plugin.c_str())) {
        if (ImGuiMCP::Selectable("all plugins", g_plugin.empty())) g_plugin.clear();
        for (const auto& p : Catalog::Plugins())
            if (ImGuiMCP::Selectable(p.c_str(), g_plugin == p)) g_plugin = p;
        ImGuiMCP::EndCombo();
    }

    if (g_lastQuery != g_query || g_lastType != g_type || g_lastPlugin != g_plugin) Refilter();

    const int shown = static_cast<int>(std::min<std::size_t>(g_hits.size(), kMaxRows));
    if (g_hits.size() > kMaxRows) {
        ImGuiMCP::TextColored(kWarn, "%zu matches — showing the first %d. Narrow the search.",
            g_hits.size(), kMaxRows);
    } else {
        ImGuiMCP::Text("%zu match(es)", g_hits.size());
    }

    // ---- the list ----------------------------------------------------------
    ImGuiMCP::BeginChild("##browser.list", ImGuiMCP::ImVec2(0.f, 260.f), 1);
    for (int row = 0; row < shown; ++row) {
        const auto idx = g_hits[static_cast<std::size_t>(row)];
        const auto& e = Catalog::All()[idx];
        ImGuiMCP::PushID(row);
        const std::string identity = e.editorId.empty() ?
            (e.name.empty() ? std::string("—") : e.name) : e.editorId;
        const std::string line = identity + "   " +
            e.model + "   [" + Catalog::TypeName(e.type) + "]   " + e.id;
        if (ImGuiMCP::Selectable(line.c_str(), idx == g_selected)) {
            g_selected = idx;
            if (g_livePreview) PickForPlacing(e);
        }
        ImGuiMCP::PopID();
    }
    ImGuiMCP::EndChild();
    ImGuiMCP::Checkbox("live preview (clicking an entry switches to place mode and previews it)",
        &g_livePreview);

    // ---- the selected entry + its ghost -------------------------------------
    ImGuiMCP::Separator();
    if (g_selected >= Catalog::All().size()) {
        ImGuiMCP::TextColored(kDim, "nothing selected");
        return;
    }
    const auto& e = Catalog::All()[g_selected];
    ImGuiMCP::Text("selected: %s", LabelOf(e).c_str());
    if (!e.editorId.empty()) ImGuiMCP::Text("  EditorID: %s", e.editorId.c_str());
    ImGuiMCP::Text("  %s", e.model.c_str());
    ImGuiMCP::Text("  %s   [%s]", e.id.c_str(), Catalog::TypeName(e.type));

    if (ImGuiMCP::Button("preview here (-> place mode)")) PickForPlacing(e);
    ImGuiMCP::SameLine();
    if (ImGuiMCP::Button("clear preview")) Preview::Clear();
    ImGuiMCP::SameLine();
    // The palette is the CURATED list (disk-persisted, survives the playthrough);
    // the catalogue is everything. Sending an entry over is how a browse becomes
    // a kit you keep — and it costs the palette nothing new: a slot is a slot.
    if (ImGuiMCP::Button("add to palette")) {
        Palette::Slot s;
        s.name = LabelOf(e);
        s.baseId = e.id;
        s.base = e.base;
        s.angle = {0.f, 0.f, Preview::Yaw() * (3.14159265f / 180.f)};
        s.scale = Preview::Scale();
        Palette::AddSlot(s);
    }

    if (!Preview::Active()) {
        ImGuiMCP::TextColored(kDim, "no ghost up — click an entry (or `preview here`)");
        return;
    }

    bool follow = Preview::Follow();
    if (ImGuiMCP::Checkbox("follow my aim", &follow)) Preview::SetFollow(follow);
    ImGuiMCP::SameLine();
    ImGuiMCP::TextColored(kDim, follow ? "(the ghost tracks where you look)"
                                       : "(the ghost stays put — walk around it)");

    float yaw = Preview::Yaw();
    ImGuiMCP::SetNextItemWidth(200.f);
    if (ImGuiMCP::SliderFloat("yaw", &yaw, 0.f, 360.f, "%.0f deg")) Preview::SetYaw(yaw);
    float scale = Preview::Scale();
    ImGuiMCP::SetNextItemWidth(200.f);
    if (ImGuiMCP::SliderFloat("scale", &scale, 0.05f, 10.f, "%.2f x")) Preview::SetScale(scale);

    // Commits at the GHOST's pose, not a fresh aim — what you are looking at is
    // what gets placed. Same path as `sc pl` (physics/extra-data switches and all).
    if (ImGuiMCP::Button("place here (real)")) Preview::Commit();
    ImGuiMCP::SameLine();
    ImGuiMCP::TextColored(kDim, "or just press %s (place mode's action key) — the ghost stays up, "
        "so a row of trees is that key five times",
        Modes::KeyName(Modes::Bind(Modes::Mode::kPlace)));
    ImGuiMCP::TextColored(kDim,
        "numpad: 4/6 yaw, 1/3 pitch, 7/9 roll, 2/5/8 revert that axis, +/- scale, "
        "0 = real size (undo the auto-scale), . = clear the ghost. Position follows your aim.");
}
