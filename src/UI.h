#pragma once

// UI — the in-game editor panel (Idea #24, the §B/§D/§E surface).
//
// Rendered by SKSE Menu Framework 3 (Dear ImGui over the game's D3D11). That
// framework is a SOFT dependency: SKSEMenuFramework::IsInstalled() is a
// GetModuleHandleW probe, so a player without it still gets the F10 hotkey.
//
// The panel lives here rather than in a separate sub-project because
// scene-capture-bridge already IS the consumer SKSE plugin the framework
// needs — see workflows/idea/tools/24-ingame-editor.md and
// sub_projs/mod-survey/findings/skse-menu-framework-3.md.

#include <cstdint>

namespace UI {

    // Register the panel. Safe to call when the framework is absent (no-op).
    void Register();

    // "Mode: <name>" + separator — every page calls it first so the current
    // mode is always in sight (P5).
    void ModeLine();

    namespace Export {
        void __stdcall Render();
    }

    namespace SettingsPage {  // modes, keybinds, gem visibility (UI.Settings.cpp)
        void __stdcall Render();
    }

    namespace MarkersPage {
        void __stdcall Render();
    }

    // The standalone marker-edit window (E on a marker gem opens it; the
    // Markers page's per-row `edit` button is the hotkey-free path). Lives in
    // UI.Markers.cpp with the page.
    namespace MarkerEditor {
        void Init();                     // AddWindow (no-op sans framework)
        void Open(std::uint32_t seq);    // fill buffers + show
        void __stdcall Render();
    }

    namespace EraserPage {
        void __stdcall Render();
    }

    namespace PalettePage {
        void __stdcall Render();
    }

    // The catalogue: every placeable base in the load order, searchable, with the
    // world itself as the preview (UI.Browser.cpp + Catalog.h + Preview.h).
    namespace BrowserPage {
        void __stdcall Render();
    }

    namespace EditorPage {
        void __stdcall Render();
    }

    namespace CapturesPage {  // captured item enchant/effects -> capturedItems[]
        void __stdcall Render();
    }

    namespace ReferencesPage {  // referrer registry (`sc ref`) -> references[]
        void __stdcall Render();
    }

}  // namespace UI
