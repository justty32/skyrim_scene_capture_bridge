#include "Modes.h"

#include "Captures.h"
#include "Editor.h"
#include "Eraser.h"
#include "Markers.h"
#include "Palette.h"
#include "Preview.h"
#include "Referrer.h"
#include "log.h"

#include <algorithm>
#include <cctype>
#include <chrono>

using namespace std::chrono_literals;

namespace {
    constexpr std::uint32_t kDefaultBind = 0x57;  // F11 — in-game verified free
    constexpr std::uint32_t kEsc = 0x01;

    Modes::Mode g_mode = Modes::Mode::kOff;
    // Index by Mode; slot 0 (kOff) exists but is never read.
    std::uint32_t g_binds[static_cast<std::size_t>(Modes::Mode::kTotal)] = {
        0, kDefaultBind, kDefaultBind, kDefaultBind, kDefaultBind, kDefaultBind, kDefaultBind,
        kDefaultBind};
    // Per-mode aim source (false = crosshair). Same indexing as g_binds.
    bool g_useRay[static_cast<std::size_t>(Modes::Mode::kTotal)] = {false};

    // Per-mode physics ("physics is KEPT" — py1 = true, so it reads straight off
    // the command). The defaults are NOT uniform, which is the whole point:
    // a PLACED object keeps its physics (py1), an EDITED one loses it while you
    // drive it (py0 — the P3 freeze-on-select behaviour, now switchable).
    // Indices: off, marker, delete, pick, place, edit, capture, referrer.
    void ApplyPhysicsDefaults(bool (&p)[static_cast<std::size_t>(Modes::Mode::kTotal)]) {
        for (auto& v : p) v = true;  // "keep physics" is the neutral value
        p[static_cast<std::size_t>(Modes::Mode::kEdit)] = false;  // py0 = freeze while editing
    }
    bool g_physics[static_cast<std::size_t>(Modes::Mode::kTotal)] = {
        true, true, true, true, true, /*kEdit*/ false, true, true};
    // Per-mode extra data (pick/place). Off = durable base only (historic).
    bool g_extraData[static_cast<std::size_t>(Modes::Mode::kTotal)] = {false};

    // Place mode's ghost preview (`sc pl gh0/gh1`). DEFAULT ON: place mode showing
    // you what it is about to place is the better default, and it is what the
    // Browser's whole flow rests on (Preview.h's invariant).
    bool g_ghost = true;

    // The bind SceneCaptureBridge.ini gave each mode (0 = the ini said nothing
    // about it). Kept SEPARATELY from g_binds because it is configuration, not
    // save state: it must survive OnRevert (which wipes everything a savegame
    // owns) and it must beat whatever bind the loaded save carries.
    std::uint32_t g_iniBinds[static_cast<std::size_t>(Modes::Mode::kTotal)] = {0};

    std::chrono::steady_clock::time_point g_lastAction{};

    // DIK scancode <-> human name. ONE table, both directions: the panel shows
    // Name(code), the ini is written with it and parsed back through Code(name),
    // so a round trip through the file is lossless and nobody types hex.
    struct KeyRow { std::uint32_t code; const char* name; };
    constexpr KeyRow kKeyTable[] = {
        {0x01, "Esc"}, {0x0E, "Backspace"}, {0x0F, "Tab"}, {0x1C, "Enter"},
        {0x39, "Space"}, {0x3A, "CapsLock"}, {0x29, "`"},
        {0x1D, "LCtrl"}, {0x9D, "RCtrl"}, {0x2A, "LShift"}, {0x36, "RShift"},
        {0x38, "LAlt"}, {0xB8, "RAlt"},
        {0x3B, "F1"}, {0x3C, "F2"}, {0x3D, "F3"}, {0x3E, "F4"}, {0x3F, "F5"},
        {0x40, "F6"}, {0x41, "F7"}, {0x42, "F8"}, {0x43, "F9"}, {0x44, "F10"},
        {0x57, "F11"}, {0x58, "F12"},
        {0x02, "1"}, {0x03, "2"}, {0x04, "3"}, {0x05, "4"}, {0x06, "5"},
        {0x07, "6"}, {0x08, "7"}, {0x09, "8"}, {0x0A, "9"}, {0x0B, "0"},
        {0x0C, "-"}, {0x0D, "="}, {0x1A, "["}, {0x1B, "]"}, {0x27, ";"},
        {0x28, "'"}, {0x2B, "\\"}, {0x33, ","}, {0x34, "."}, {0x35, "/"},
        {0x1E, "A"}, {0x30, "B"}, {0x2E, "C"}, {0x20, "D"}, {0x12, "E"},
        {0x21, "F"}, {0x22, "G"}, {0x23, "H"}, {0x17, "I"}, {0x24, "J"},
        {0x25, "K"}, {0x26, "L"}, {0x32, "M"}, {0x31, "N"}, {0x18, "O"},
        {0x19, "P"}, {0x10, "Q"}, {0x13, "R"}, {0x1F, "S"}, {0x14, "T"},
        {0x16, "U"}, {0x2F, "V"}, {0x11, "W"}, {0x2D, "X"}, {0x15, "Y"},
        {0x2C, "Z"},
        {0x52, "numpad 0"}, {0x4F, "numpad 1"}, {0x50, "numpad 2"},
        {0x51, "numpad 3"}, {0x4B, "numpad 4"}, {0x4C, "numpad 5"},
        {0x4D, "numpad 6"}, {0x47, "numpad 7"}, {0x48, "numpad 8"},
        {0x49, "numpad 9"}, {0x53, "numpad ."}, {0x37, "numpad *"},
        {0x4A, "numpad -"}, {0x4E, "numpad +"}, {0xB5, "numpad /"},
        {0x9C, "numpad Enter"},
        {0xC7, "Home"}, {0xCF, "End"}, {0xC9, "PageUp"}, {0xD1, "PageDown"},
        {0xD2, "Insert"}, {0xD3, "Delete"},
        {0xC8, "Up"}, {0xD0, "Down"}, {0xCB, "Left"}, {0xCD, "Right"},
        {0x45, "NumLock"}, {0x46, "ScrollLock"},
    };

    // Fold a written name onto the table's own spelling: lower-case, drop spaces
    // and underscores ("NumPad 5" / "numpad_5" / "numpad5" all collide), and let
    // "num5" mean "numpad5". Punctuation names ("-", "numpad -") survive because
    // only whitespace/underscore is stripped, never the glyph itself.
    std::string Normalise(std::string s) {
        std::string out;
        for (char c : s) {
            if (c == ' ' || c == '\t' || c == '_') continue;
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        if (out.starts_with("num") && !out.starts_with("numpad") && out != "numlock")
            out = "numpad" + out.substr(3);
        return out;
    }

    void RunAction(Modes::Mode m) {
        const bool ray = Modes::UseRay(m);
        switch (m) {
        case Modes::Mode::kMarker: Markers::PlaceAimed(); break;
        case Modes::Mode::kDelete: ray ? Eraser::MarkByRay() : Eraser::MarkCrosshair(); break;
        case Modes::Mode::kPick:   ray ? Palette::PickByRay() : Palette::PickCrosshair(); break;
        // A ghost is up => THAT is what you are placing: it is the thing you can
        // see standing at your aim point, and placing anything else would be a
        // lie about what the key does. With no ghost, the selected palette slot —
        // the historic behaviour, unchanged.
        case Modes::Mode::kPlace:
            Preview::Active() ? Preview::Commit() : Palette::PlaceSelected();
            break;
        case Modes::Mode::kEdit:   ray ? Editor::SelectByRay() : Editor::EnterSelect(); break;
        case Modes::Mode::kCapture: ray ? Captures::CaptureByRay() : Captures::CaptureCrosshair(); break;
        // No label on the action key — the row gets "ref-<seq>" and is renamed in
        // the panel. `sc ref <Label>` is the one-shot labelled path.
        case Modes::Mode::kReferrer: ray ? Referrer::MarkByRay("") : Referrer::MarkCrosshair(""); break;
        default: break;
        }
    }
}

namespace Modes {

    Mode Current() { return g_mode; }

    void Set(Mode m) {
        if (m >= Mode::kTotal) return;
        g_mode = m;
        const auto msg = std::format("SCB mode: {}", Name(m));
        RE::DebugNotification(msg.c_str());
        SKSE::log::info("Modes: -> {}", Name(m));
    }

    const char* Name(Mode m) {
        switch (m) {
        case Mode::kMarker: return "marker";
        case Mode::kDelete: return "delete";
        case Mode::kPick:   return "pick";
        case Mode::kPlace:  return "place";
        case Mode::kEdit:   return "edit";
        case Mode::kCapture: return "capture";
        case Mode::kReferrer: return "referrer";
        default:            return "off";
        }
    }

    const char* Cmd(Mode m) {
        switch (m) {
        case Mode::kMarker: return "mk";
        case Mode::kDelete: return "del";
        case Mode::kPick:   return "pk";
        case Mode::kPlace:  return "pl";
        case Mode::kEdit:   return "ed";
        case Mode::kCapture: return "cap";
        case Mode::kReferrer: return "ref";
        default:            return "off";
        }
    }

    std::uint32_t Bind(Mode m) {
        return m < Mode::kTotal ? g_binds[static_cast<std::size_t>(m)] : 0;
    }

    void SetBind(Mode m, std::uint32_t scancode) {
        if (m == Mode::kOff || m >= Mode::kTotal || scancode == 0) return;
        g_binds[static_cast<std::size_t>(m)] = scancode;
        SKSE::log::info("Modes: bind {} -> {} (0x{:X})", Name(m), KeyName(scancode), scancode);
    }

    void SetIniBind(Mode m, std::uint32_t scancode) {
        if (m == Mode::kOff || m >= Mode::kTotal || scancode == 0) return;
        g_iniBinds[static_cast<std::size_t>(m)] = scancode;
        SetBind(m, scancode);
    }

    void ClearIniBinds() {
        for (auto& b : g_iniBinds) b = 0;
    }

    bool BindFromIni(Mode m) {
        return m < Mode::kTotal && g_iniBinds[static_cast<std::size_t>(m)] != 0;
    }

    void ApplyCoSaveBind(Mode m, std::uint32_t scancode) {
        if (m == Mode::kOff || m >= Mode::kTotal || scancode == 0) return;
        if (BindFromIni(m)) {
            // The ini named this mode: it wins. Saying so out loud matters —
            // otherwise "I edited the ini and nothing changed" would be a silent
            // mystery whenever an older save carried a different key.
            if (scancode != Bind(m)) {
                // 🔴 KeyName() hands back a pointer into ONE shared static buffer
                // for any scancode that is not in kKeyTable — which is exactly the
                // raw hex/decimal binds the ini's escape hatch accepts. Two
                // KeyName() calls in the SAME statement therefore both end up
                // pointing at that one buffer, and both print whatever the second
                // call wrote: this line would claim the two keys are identical at
                // the very moment it exists to report that they differ. Copy the
                // first out before asking for the second.
                const std::string fromCoSave = KeyName(scancode);
                SKSE::log::info("Modes: co-save bind {} for {} ignored — the ini says {}",
                    fromCoSave, Name(m), KeyName(Bind(m)));
            }
            return;
        }
        if (!IsBindable(scancode)) {
            SKSE::log::warn("Modes: dropped reserved co-save bind 0x{:X} for {} "
                "(stale save data from the removed in-game rebind) — kept {}",
                scancode, Name(m), KeyName(Bind(m)));
            return;
        }
        SetBind(m, scancode);
    }

    bool UseRay(Mode m) {
        return m < Mode::kTotal ? g_useRay[static_cast<std::size_t>(m)] : false;
    }

    void SetUseRay(Mode m, bool useRay) {
        if (m == Mode::kOff || m >= Mode::kTotal) return;
        g_useRay[static_cast<std::size_t>(m)] = useRay;
        SKSE::log::info("Modes: {} aim source -> {}", Name(m), useRay ? "ray" : "crosshair");
    }

    bool Physics(Mode m) {
        return m < Mode::kTotal ? g_physics[static_cast<std::size_t>(m)] : true;
    }

    void SetPhysics(Mode m, bool keepPhysics) {
        if (m == Mode::kOff || m >= Mode::kTotal) return;
        g_physics[static_cast<std::size_t>(m)] = keepPhysics;
        SKSE::log::info("Modes: {} physics -> {}", Name(m),
            keepPhysics ? "kept (py1)" : "OFF (py0)");
    }

    bool Ghost(Mode m) { return m == Mode::kPlace ? g_ghost : false; }

    void SetGhost(Mode m, bool on) {
        if (m != Mode::kPlace || g_ghost == on) return;
        g_ghost = on;
        SKSE::log::info("Modes: place ghost preview -> {}", on ? "on (gh1)" : "off (gh0)");
    }

    bool ExtraData(Mode m) {
        return m < Mode::kTotal ? g_extraData[static_cast<std::size_t>(m)] : false;
    }

    void SetExtraData(Mode m, bool on) {
        if (m == Mode::kOff || m >= Mode::kTotal) return;
        g_extraData[static_cast<std::size_t>(m)] = on;
        SKSE::log::info("Modes: {} extra data -> {}", Name(m),
            on ? "carried (ed1)" : "base only (ed0)");
    }

    bool IsBindable(std::uint32_t scancode) {
        switch (scancode) {
        case kEsc:              // Esc — menus
        case 0x29:               // ` / ~ — opens the console
        case 0x0F:                // Tab — ImGui focus navigation
        case 0x1C:                // Enter — confirms ImGui widgets / console lines
        case 0x11: case 0x1E:      // W, A
        case 0x1F: case 0x20:       // S, D
        case 0x39:                   // Space — jump
        case 0x2A: case 0x36:         // LShift, RShift — sprint
        case 0x1D:                     // LCtrl — sneak
            return false;
        default:
            return true;
        }
    }

    bool HandleKey(std::uint32_t scancode) {
        if (g_mode == Mode::kOff || scancode != Bind(g_mode)) return false;

        const auto now = std::chrono::steady_clock::now();
        if (now - g_lastAction < 200ms) return true;  // debounce, still consumed
        g_lastAction = now;

        SKSE::log::info("Modes: action key (0x{:X}) in mode {}", scancode, Name(g_mode));
        RunAction(g_mode);
        return true;
    }

    void ResetDefaults() {
        g_mode = Mode::kOff;
        for (std::size_t i = 1; i < static_cast<std::size_t>(Mode::kTotal); ++i) {
            g_binds[i] = kDefaultBind;
            g_useRay[i] = false;
            g_extraData[i] = false;
        }
        ApplyPhysicsDefaults(g_physics);  // place = py1, edit = py0
        g_ghost = true;                   // gh1 — place mode shows what it will place
        // The ini is CONFIGURATION, not save state — a revert (new game / a save
        // without our records) must not throw the player's keys away. Everything
        // else above legitimately goes back to defaults.
        for (std::size_t i = 1; i < static_cast<std::size_t>(Mode::kTotal); ++i)
            if (g_iniBinds[i]) g_binds[i] = g_iniBinds[i];
    }

    const char* KeyName(std::uint32_t scancode) {
        for (const auto& r : kKeyTable)
            if (r.code == scancode) return r.name;
        // 🔴 ONE SHARED BUFFER. A table hit returns a static string literal and is
        // safe forever, but this fallback (any raw scancode the ini's hex/decimal
        // escape hatch allows) returns the same address every time. NEVER call
        // KeyName() twice in one expression: the second call overwrites what the
        // first returned before either is read. Copy to a std::string first.
        static char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%X", scancode);
        return buf;
    }

    std::uint32_t KeyCode(const std::string& name) {
        const auto want = Normalise(name);
        if (want.empty()) return 0;
        for (const auto& r : kKeyTable)
            if (Normalise(r.name) == want) return r.code;
        // Escape hatch: a raw DIK scancode, hex ("0x57") or decimal ("87"), for
        // the odd keyboard key the table above doesn't have a name for.
        try {
            const bool hex = want.starts_with("0x");
            const auto v = std::stoul(hex ? want.substr(2) : want, nullptr, hex ? 16 : 10);
            if (v > 0 && v <= 0xFF) return static_cast<std::uint32_t>(v);
        } catch (...) {}
        return 0;
    }

}  // namespace Modes
