#pragma once

// UI.Fields — one editable text field, BOUND to a registry string.
//
// Every page used to carry its own `unordered_map<seq, char[]>` of row buffers,
// seeded from the entry exactly once (`try_emplace`) and committed only on
// Enter. That shape has a silent failure mode, found in-game 2026-07-13: type a
// new name, then click away without pressing Enter. `Rename()` is never called,
// so the registry keeps the OLD value — but the page goes on drawing from the
// buffer, so it shows the NEW one, every frame, forever. What you see is not
// what exports, and nothing says so until you restart the game.
//
// The cure is to give both halves of that contract a single owner:
//
//   RULE 1  the buffer may differ from the registry ONLY while you are typing in
//           that very field. On every other frame it is re-seeded from the entry
//           — so a page is structurally incapable of showing a value the
//           registry does not hold.
//   RULE 2  it commits on EVERY way of leaving a field you changed: Enter, a
//           deactivate-after-edit that ImGui reports (clicking away, tabbing
//           out), AND the one nobody reports — the row simply stopping being
//           DRAWN mid-edit (you switched page, closed the panel, or a filter hid
//           the row). ImGui drops its ActiveId in NewFrame when an active item
//           is not submitted, and never runs the widget's flush, so that edge
//           reaches no one; a naive implementation re-seeds the buffer next time
//           the row appears and the typing is gone without trace. See BoundText.
//
// Rule 1 rests on an ImGui invariant: there is exactly ONE active item at a
// time, so "the field being typed in" is a single id, never a set. It also
// retires buffer invalidation as a concept — a deleted row, a refused rename or
// a shifted index all self-heal on the next frame, which is why the old
// `g_rows.erase(seq)` / `g_slotBufs.clear()` calls are gone rather than moved.

#include <cstddef>
#include <cstdint>
#include <string>

namespace UI {

    // Draw an InputText bound to `value`. Returns true when the edit should be
    // committed, and `out` then holds the new text.
    //
    // `slot` is BOTH the ImGui label (pass a "##..." id) and the buffer's
    // cross-frame identity, so it must be unique per field per page; `row`
    // separates the rows within one page (a seq, or an index).
    bool BoundText(const char* slot, std::uint64_t row, const std::string& value,
                   std::size_t cap, float width, std::string& out);

    // A stable row key for the pages whose entries are identified by a durable
    // id string instead of a seq (the eraser and the override list). Hashing the
    // id keeps the key attached to the ROW, so it survives the list being
    // reordered or shortened — which an index would not.
    [[nodiscard]] std::uint64_t RowKey(const std::string& id);

    // What a bound field is currently SHOWING (its in-progress text). This is
    // what an `apply` button commits — it must not wait for the field to lose
    // focus, since clicking the button is itself how you leave the field.
    [[nodiscard]] std::string Shown(const char* slot, std::uint64_t row);

    // Abandon every in-progress edit. A page whose rows are keyed by INDEX (the
    // palette — its slots carry no seq) must call this whenever the list is
    // restructured under it: after a remove or a file load the indices point at
    // different slots, so an in-flight buffer would commit onto the wrong row.
    // Pages keyed by a stable seq (markers, references) never need it.
    //
    // Dropping the buffers is safe by construction: every field re-seeds from
    // its entry on the next frame it is not being typed in (RULE 1).
    void ForgetEdits();

}  // namespace UI
