#include "UI.Fields.h"

#include "SKSEMenuFramework.h"

#include <cstdio>
#include <unordered_map>
#include <vector>

namespace {
    // Cross-frame buffers, keyed by (slot, row). Nothing is ever erased from
    // here: a stale buffer costs a few dozen bytes and re-seeds itself the
    // moment its row reappears (RULE 1), whereas an erase-on-delete protocol is
    // precisely what the six pages kept getting wrong.
    std::unordered_map<std::uint64_t, std::vector<char>> g_bufs;

    // The one field being typed in, or 0. ImGui has a single active item, so
    // this is an id and not a set — everything else mirrors the registry.
    std::uint64_t g_active = 0;

    std::uint64_t Fnv1a(const char* p, std::size_t n) {
        std::uint64_t h = 1469598103934665603ull;
        for (std::size_t i = 0; i < n; ++i) {
            h ^= static_cast<std::uint8_t>(p[i]);
            h *= 1099511628211ull;
        }
        return h;
    }

    std::uint64_t KeyOf(const char* slot, std::uint64_t row) {
        std::uint64_t h = Fnv1a(slot, std::char_traits<char>::length(slot));
        h ^= row + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
        return h ? h : 1;  // 0 is the "nothing is active" sentinel
    }

    // RULE 1 — mirror the registry unless this is the field under the cursor.
    char* Buffer(std::uint64_t key, const std::string& value, std::size_t cap) {
        auto& buf = g_bufs[key];
        if (buf.size() != cap) buf.assign(cap, '\0');
        if (key != g_active) std::snprintf(buf.data(), cap, "%s", value.c_str());
        return buf.data();
    }

}

namespace UI {

    bool BoundText(const char* slot, std::uint64_t row, const std::string& value,
                   std::size_t cap, float width, std::string& out) {
        const auto key = KeyOf(slot, row);
        const bool wasActive = (key == g_active);
        char* buf = Buffer(key, value, cap);  // reseeds unless this is the active field

        ImGuiMCP::SetNextItemWidth(width);
        const bool enter = ImGuiMCP::InputText(slot, buf, cap,
            ImGuiMCP::ImGuiInputTextFlags_EnterReturnsTrue);
        const bool active = ImGuiMCP::IsItemActive();

        // RULE 2 — every way of leaving a field you changed.
        //   (1) Enter.
        //   (2) ImGui saw the deactivation: you clicked another widget, tabbed out.
        //   (3) NOBODY saw the deactivation — because the row was not DRAWN for a
        //       frame (you switched panel page, closed the panel, or the `this cell
        //       only` filter hid the row). ImGui clears its ActiveId in NewFrame when
        //       an active item is not submitted, without running the widget's flush,
        //       so that edge is reported to no one and IsItemDeactivatedAfterEdit()
        //       never fires. We would then re-seed the buffer and the typing would
        //       vanish without trace. So: if we were the active field, are not any
        //       more, and the buffer still disagrees with the entry — that IS the
        //       user leaving a field they changed. Honour it.
        const bool lostTheEdge = wasActive && !active && value != buf;
        const bool commit = enter || ImGuiMCP::IsItemDeactivatedAfterEdit() || lostTheEdge;

        if (active) g_active = key;
        else if (g_active == key) g_active = 0;

        if (!commit) return false;
        out = buf;
        return true;
    }

    std::uint64_t RowKey(const std::string& id) {
        return Fnv1a(id.data(), id.size());
    }

    std::string Shown(const char* slot, std::uint64_t row) {
        const auto it = g_bufs.find(KeyOf(slot, row));
        return it == g_bufs.end() ? std::string{} : std::string(it->second.data());
    }

    void ForgetEdits() {
        g_bufs.clear();
        // Clearing the latch is the load-bearing half: it is what makes the
        // still-focused field re-seed from its (now different) entry before its
        // pending deactivate-commit fires, so that commit writes the row's own
        // value back to itself instead of the typing meant for the old row.
        g_active = 0;
    }

}  // namespace UI
