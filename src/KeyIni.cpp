#include "KeyIni.h"

#include "Modes.h"
#include "log.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace {
    KeyIni::Status g_status;

    constexpr Modes::Mode kActionModes[] = {
        Modes::Mode::kMarker, Modes::Mode::kDelete, Modes::Mode::kPick,
        Modes::Mode::kPlace, Modes::Mode::kEdit, Modes::Mode::kCapture,
        Modes::Mode::kReferrer,
    };

    std::filesystem::path IniPath() {
        auto dir = SKSE::log::log_directory();
        return dir ? (*dir / "SceneCaptureBridge.ini") : std::filesystem::path{};
    }

    std::string Trim(std::string s) {
        const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
        return s;
    }

    std::string Lower(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // A mode is addressable by either name — "delete" (panel) or "del" (console).
    // Whichever the player has in their head is the one that works.
    Modes::Mode ModeOf(const std::string& key) {
        const auto k = Lower(key);
        for (auto m : kActionModes)
            if (k == Modes::Name(m) || k == Modes::Cmd(m)) return m;
        return Modes::Mode::kOff;  // kOff = "not a mode line"
    }

    // The default file. Every line the parser accepts is present and set to the
    // current default, and the key vocabulary is right there in the comments —
    // the whole point of moving off the in-game capture is that the player can
    // SEE what they are allowed to type.
    void WriteDefault(const std::filesystem::path& path) {
        std::ofstream out(path, std::ios::trunc);
        if (!out) {
            SKSE::log::warn("KeyIni: cannot create {}", path.string());
            return;
        }
        out <<
R"(; SceneCaptureBridge.ini — the action key of each mode.
;
; One mode is active at a time (`sc mk`, `sc del`, `sc pk`, `sc pl`, `sc ed`,
; `sc cap`, `sc ref`, `sc off`) and its action key does that mode's work. Keys
; may repeat: with a single active mode, one key can drive every mode — which is
; why the default is F11 everywhere.
;
; Edit, save, then either restart the game or press "reload keys from ini" on the
; panel's Settings page (F1 -> Scene Capture Bridge -> Settings). No restart of
; anything else is needed, and nothing here touches your savegame.
;
; THIS FILE WINS over the key stored in a savegame. It is your configuration; the
; save only remembers what the game was doing. Delete a line and that mode falls
; back to whatever the loaded save had (or F11 on a fresh game).
;
; NAMES YOU CAN USE (case and spaces don't matter — "NumPad 5" == "numpad5"):
;   F1 .. F12
;   numpad 0 .. numpad 9, numpad . * - + /, numpad Enter
;   letters A..Z, digits 0..9 (the top row), - = [ ] ; ' \ , . /
;   Home End PageUp PageDown Insert Delete, Up Down Left Right
;   LAlt RAlt CapsLock Backspace, NumLock ScrollLock
;   or a raw DIK scancode: 0x57 (hex) or 87 (decimal)
;
; RESERVED — refused, the mode keeps its previous key: W A S D, Space, Shift,
; Ctrl, Esc, Tab, Enter, ` (console). Binding the game's movement keys to a
; world-editing action is the bug this file exists to make impossible.
;
; NOTE: the numpad keys drive the EDITOR while something is selected (8/2/4/6/1/3
; move, 7/9 yaw, +/- scale, 0 commit, . cancel, 5 reset, * ray-select). Binding a
; mode's action key to one of those still works, but the editor sees it first
; while an object is selected. F-keys stay out of everyone's way.

[Keys]
)";
        for (auto m : kActionModes)
            out << std::format("{:<9}= {}\n", Modes::Name(m), Modes::KeyName(Modes::Bind(m)));
        SKSE::log::info("KeyIni: wrote default {}", path.string());
    }
}

namespace KeyIni {

    std::string PathString() { return IniPath().string(); }

    const Status& Last() { return g_status; }

    std::size_t Load() {
        g_status = {};
        const auto path = IniPath();
        if (path.empty()) {
            SKSE::log::error("KeyIni: no SKSE folder — keys stay at their defaults");
            return 0;
        }
        if (!std::filesystem::exists(path)) WriteDefault(path);

        std::ifstream in(path);
        if (!in) {
            SKSE::log::warn("KeyIni: cannot read {}", path.string());
            return 0;
        }
        g_status.loaded = true;

        // Re-parsing REPLACES the previous ini binds rather than merging with
        // them: a line the player deleted must actually stop applying (it then
        // falls back to the co-save / F11), which a merge would silently defeat.
        Modes::ClearIniBinds();

        std::string line;
        while (std::getline(in, line)) {
            if (const auto c = line.find_first_of(";#"); c != std::string::npos) line.erase(c);
            line = Trim(line);
            if (line.empty() || line.front() == '[') continue;  // [Keys] is decoration
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;

            const auto key = Trim(line.substr(0, eq));
            const auto val = Trim(line.substr(eq + 1));
            const auto mode = ModeOf(key);
            if (mode == Modes::Mode::kOff) {
                SKSE::log::warn("KeyIni: unknown mode '{}' — line ignored", key);
                if (g_status.problem.empty())
                    g_status.problem = std::format("unknown mode '{}'", key);
                continue;
            }
            const auto code = Modes::KeyCode(val);
            if (!code) {
                SKSE::log::warn("KeyIni: {} = '{}' — unknown key name, ignored", key, val);
                if (g_status.problem.empty())
                    g_status.problem = std::format("{}: unknown key '{}'", key, val);
                continue;
            }
            if (!Modes::IsBindable(code)) {
                // The one rule the file cannot talk its way out of. Reserved keys
                // (WASD/Space/Shift/Ctrl/Esc/Tab/Enter/console) are the exact
                // failure the in-game rebind kept producing; a typo here must not
                // resurrect it.
                SKSE::log::warn("KeyIni: {} = '{}' is RESERVED (movement/console/menu) "
                    "— refused, {} stays on {}", key, val, Modes::Name(mode),
                    Modes::KeyName(Modes::Bind(mode)));
                if (g_status.problem.empty())
                    g_status.problem = std::format("{}: '{}' is reserved", key, val);
                continue;
            }
            Modes::SetIniBind(mode, code);
            ++g_status.applied;
        }

        SKSE::log::info("KeyIni: applied {} bind(s) from {}", g_status.applied, path.string());
        return g_status.applied;
    }

}  // namespace KeyIni
