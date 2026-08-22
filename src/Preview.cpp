#include "Preview.h"

#include "Aim.h"
#include "Editor.h"   // YawStep() / ScaleStep() — the ghost turns by the SAME step the editor does
#include "Modes.h"
#include "Numpad.h"
#include "Palette.h"
#include "Physics.h"
#include "SceneExporter.h"
#include "log.h"

#include <algorithm>
#include <cmath>

namespace {
    constexpr float kDegToRad = 0.0174532925f;
    constexpr float kRadToDeg = 57.2957795f;

    // The sentinel that makes a ghost a ghost, forever — see Preview.h. It is a
    // display name, so the one place it can surface is a console selection, which
    // is exactly where you would want to be told.
    constexpr const char* kSentinel = "[SCB preview ghost]";

    // A fresh ghost is scaled to about a NINTH OF THE SCREEN — a third in each
    // dimension (user 2026-07-14: a farmhouse at arm's length fills the view and
    // you can judge nothing). Only ever DOWN: a goblet is already comfortable at
    // its real size, and silently inflating it would be a lie about what you are
    // placing. numpad 0 restores 1.0.
    constexpr float kScreenFraction = 1.f / 3.f;   // linear (1/3 * 1/3 = 1/9 of the area)
    constexpr float kDefaultFOV = 75.f;            // degrees — Skyrim's fDefaultWorldFOV

    RE::ObjectRefHandle g_handle;
    RE::TESBoundObject* g_base = nullptr;
    std::string g_label;

    // SOURCE: the palette's selected slot, unless the Browser pinned a catalogue
    // entry. g_lastSeenSel lets Update() notice that you changed the palette
    // selection (which takes the ghost back off a pinned catalogue entry).
    bool g_fromCatalog = false;
    std::size_t g_lastSeenSel = SIZE_MAX;
    std::size_t g_failedSel = SIZE_MAX;   // slot we could not show — don't retry it every frame

    RE::NiPoint3 g_angle;      // radians
    float g_scale = 1.f;
    RE::NiPoint3 g_spawnAngle; // what the per-axis reverts (2/5/8) go back to
    float g_spawnScale = 1.f;

    bool g_follow = true;
    bool g_ghostForced = false;  // WE turned `gh1` on (the Browser did) -> put it back on Clear

    bool HasSentinel(RE::TESObjectREFR* ref) {
        if (!ref) return false;
        auto* x = ref->extraList.GetByType<RE::ExtraTextDisplayData>();
        return x && x->displayName == kSentinel;
    }

    void Destroy(RE::TESObjectREFR* ref) {
        if (!ref) return;
        ref->Disable();
        ref->SetDelete(true);  // no trace: it was never content (`markfordelete` semantics)
    }

    // Take the current ghost out of the world — and NOTHING else. Distinct from
    // Preview::Clear(), which is the user saying "put it away" and therefore also
    // gives back the `gh` setting the Browser borrowed. Swapping one ghost for the
    // next must NOT do that: Spawn() calls this, and if it called Clear() the
    // Browser's temporary gh1 would be handed back mid-spawn, the invariant would
    // read "ghosts are off", and the ghost you just asked for would be destroyed on
    // the very next frame.
    void Vanish() {
        if (auto ref = g_handle.get()) {
            Destroy(ref.get());
            SKSE::log::info("Preview: ghost cleared");
        }
        g_handle = {};
        g_base = nullptr;
        g_label.clear();
    }

    // How big is this thing, really? OBND (the record's own bounds) is available
    // BEFORE the 3D loads, which is when we need it — the ghost must appear at a
    // sane size on frame one, not pop a moment later.
    float BoundRadius(RE::TESBoundObject* base) {
        const auto& b = base->boundData;
        const float x = (static_cast<float>(b.boundMax.x) - static_cast<float>(b.boundMin.x)) * 0.5f;
        const float y = (static_cast<float>(b.boundMax.y) - static_cast<float>(b.boundMin.y)) * 0.5f;
        const float z = (static_cast<float>(b.boundMax.z) - static_cast<float>(b.boundMin.z)) * 0.5f;
        return std::sqrt(x * x + y * y + z * z);   // half-diagonal
    }

    // Scale so the object subtends ~1/3 of the view at the distance it is being
    // previewed. Never > 1 (see kScreenFraction). Falls back to 1.0 whenever the
    // numbers are not trustworthy (no OBND, degenerate distance).
    float AutoScale(RE::TESBoundObject* base, const RE::NiPoint3& pos) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !base) return 1.f;
        const float radius = BoundRadius(base);
        if (radius < 1.f) return 1.f;   // no usable bounds — leave it alone

        const float dist = (pos - player->GetPosition()).Length();
        if (dist < 1.f) return 1.f;

        // Skyrim's default world FOV. Read from a constant on purpose: PlayerCamera's
        // FOV member moved between runtimes (it does not compile against this
        // CommonLib), and "about a ninth of the screen" does not need three decimals
        // — a player who has changed their FOV gets a slightly different ninth.
        const float halfView = dist * std::tan(0.5f * kDefaultFOV * kDegToRad);
        const float wanted = halfView * kScreenFraction;                  // wanted RADIUS on screen
        return std::clamp(wanted / radius, 0.05f, 1.f);
    }

    void ApplyPose(RE::TESObjectREFR* ref) {
        ref->SetAngle(g_angle);
        ref->SetScale(g_scale);
        ref->Update3DPosition(true);
    }

    // Rotate / scale by `steps` of the EDITOR's step sizes (one owner for "how far
    // does one tap turn a thing" — the Settings page tunes both at once).
    void Nudge(std::uint32_t code, float steps) {
        auto ref = g_handle.get();
        if (!ref) return;
        const float rot = Editor::YawStep() * steps * kDegToRad;
        switch (code) {
        case Numpad::kLeft:    g_angle.z -= rot; break;   // 4/6 yaw
        case Numpad::kRight:   g_angle.z += rot; break;
        case Numpad::kDown:    g_angle.x -= rot; break;   // 1/3 pitch
        case Numpad::kUp:      g_angle.x += rot; break;
        case Numpad::kYawNeg:  g_angle.y -= rot; break;   // 7/9 roll
        case Numpad::kYawPos:  g_angle.y += rot; break;
        case Numpad::kScaleUp:
        case Numpad::kScaleDn: {
            const float d = Editor::ScaleStep() * steps * (code == Numpad::kScaleUp ? 1.f : -1.f);
            // Clamped: a tap could never reach zero, but a long press crosses it in
            // a second — and a zero/negative scale is a broken, invisible object.
            g_scale = std::clamp(g_scale + d, 0.05f, 10.f);
            break;
        }
        default: return;
        }
        ApplyPose(ref.get());
    }

    // Repeatable while held: the continuous ones. The reverts (2/5/8), the scale
    // reset (0) and the clear (.) are DISCRETE acts and must fire once per press —
    // the same rule the editor learned in-game.
    bool IsNudgeKey(std::uint32_t code) {
        switch (code) {
        case Numpad::kLeft: case Numpad::kRight:
        case Numpad::kDown: case Numpad::kUp:
        case Numpad::kYawNeg: case Numpad::kYawPos:
        case Numpad::kScaleUp: case Numpad::kScaleDn:
            return true;
        default:
            return false;
        }
    }

    // Spawn the ghost for `base`. Pose carries over from the previous ghost's
    // rotation (you spent effort on that angle; swapping the object should not
    // throw it away) but the SCALE is recomputed — it is a property of how big
    // this particular thing is, not of what you were looking at before.
    bool Spawn(RE::TESBoundObject* base, const std::string& label, bool fromCatalog,
        std::size_t slotIndex, float slotScale, const RE::NiPoint3& slotAngle) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!base || !player) return false;

        Vanish();  // one ghost at a time — switching what you place swaps what you see

        RE::NiPointer<RE::TESObjectREFR> ghost = player->PlaceObjectAtMe(base, false);
        if (!ghost) {
            SKSE::log::error("Preview: PlaceObjectAtMe failed for '{}'", label);
            return false;
        }
        // The sentinel goes on FIRST: from this line on the ref is recognisable as
        // a ghost by anything that looks at it, including a future session.
        ghost->extraList.Add(new RE::ExtraTextDisplayData(kSentinel));

        // And intangibility SECOND — this ONE LINE is the whole mechanism, and it
        // only works because of WHERE it is (🎮 confirmed 2026-07-14 after two
        // failed rounds). `SetCollision(false)` does not touch havok: it sets the
        // ref's kCollisionsDisabled record flag. But that flag is what the ENGINE
        // reads when it BUILDS a ref's collision — and PlaceObjectAtMe returns
        // before any 3D exists. Set the flag here, in the gap, and the collision is
        // NEVER BUILT. There is nothing to strip, disable or chase.
        //
        // Two rounds were lost trying to take collision away AFTER the fact:
        //   v1 Get3D()->SetCollisionLayer() — reaches only the root node's own
        //      collision object; a nif's rigid bodies hang off CHILD nodes.
        //   v2 walk the collision scenegraph and rewrite every body's filter info
        //      (po3 / Base Object Swapper's move) — and the log proved it never
        //      once touched a body, whatever the reason.
        // The lesson generalises: DON'T UNDO WHAT YOU CAN DECLINE TO CREATE.
        // (A havok body does not follow SetPosition either — a STAT's body stays
        // fixed where the ref first landed. That is why the user saw the visual walk
        // off with his aim while the collision box stayed behind.)
        ghost->SetCollision(false);

        RE::NiPoint3 pos;
        if (!Aim::RenderedCameraHit(pos)) pos = player->GetPosition();

        g_angle = slotAngle;
        g_scale = slotScale * AutoScale(base, pos);
        g_spawnAngle = g_angle;
        g_spawnScale = g_scale;

        ghost->SetPosition(pos);
        ApplyPose(ghost.get());

        // A ghost never falls, topples or settles: it is a picture of a decision,
        // not an object in the world. (Type-gated like every runtime freeze — a
        // STAT has no dynamic body to fall.)
        if (Physics::HavokMovable(base)) Physics::FreezeDeferred(ghost->GetHandle());

        g_handle = ghost->GetHandle();
        g_base = base;
        g_label = label;
        g_fromCatalog = fromCatalog;
        // Update() swaps the ghost when the palette selection CHANGES. So even a
        // pinned catalogue entry must remember what the selection was when it was
        // pinned — otherwise the very next frame reads "the selection is not what
        // the ghost is showing" and helpfully replaces the thing you just chose.
        g_lastSeenSel = fromCatalog ? Palette::SelectedIndex() : slotIndex;
        g_failedSel = SIZE_MAX;

        SKSE::log::info("Preview: ghost '{}' at ({:.1f}, {:.1f}, {:.1f}) yaw={:.0f} scale={:.2f}{}",
            label, pos.x, pos.y, pos.z, g_angle.z * kRadToDeg, g_scale,
            g_scale < 0.999f ? " (auto-scaled to ~1/9 screen; numpad 0 = real size)" : "");
        return true;
    }
}

namespace Preview {

    bool Active() { return static_cast<bool>(g_handle.get()); }
    const std::string& Label() { return g_label; }
    float Yaw() { return g_angle.z * kRadToDeg; }
    float Scale() { return g_scale; }
    bool Follow() { return g_follow; }
    void SetFollow(bool on) { g_follow = on; }

    bool IsGhost(RE::TESObjectREFR* ref) {
        if (!ref) return false;
        if (auto live = g_handle.get(); live && live.get() == ref) return true;
        return HasSentinel(ref);  // an orphan from a reloaded save — still not content
    }

    void SetYaw(float degrees) {
        g_angle.z = std::fmod(degrees, 360.f) * kDegToRad;
        if (auto ref = g_handle.get()) ApplyPose(ref.get());
    }

    void SetScale(float scale) {
        g_scale = std::clamp(scale, 0.05f, 10.f);
        if (auto ref = g_handle.get()) ApplyPose(ref.get());
    }

    bool ShowSlot(std::size_t index) {
        auto& slots = Palette::All();
        if (index >= slots.size() || !slots[index].base) {
            g_failedSel = index;   // remember, so Update() doesn't retry every frame
            return false;
        }
        const auto& s = slots[index];
        return Spawn(s.base, s.name, false, index, s.scale, s.angle);
    }

    bool ShowBase(RE::TESBoundObject* base, const std::string& label) {
        return Spawn(base, label, true, SIZE_MAX, 1.f, RE::NiPoint3{});
    }

    void ForceGhostOn() {
        if (Modes::Ghost(Modes::Mode::kPlace)) return;
        Modes::SetGhost(Modes::Mode::kPlace, true);
        g_ghostForced = true;   // put it back when the ghost goes — that is "temporary"
    }

    void Clear() {
        Vanish();
        g_fromCatalog = false;   // back to the palette's selection next time
        // Give back what the Browser borrowed. This is the whole of "temporarily
        // turns gh1 on": the setting is restored the moment the ghost goes away.
        if (g_ghostForced) {
            g_ghostForced = false;
            Modes::SetGhost(Modes::Mode::kPlace, false);
        }
    }

    void Update() {
        // THE INVARIANT (Preview.h): a ghost exists iff place mode + gh1. Leave the
        // mode, or turn ghosts off, and it goes — so a ghost always means exactly
        // one thing: "this is what the action key will place".
        const bool want = Modes::Current() == Modes::Mode::kPlace &&
            Modes::Ghost(Modes::Mode::kPlace);
        if (!want) {
            if (g_base) Clear();
            return;
        }

        // SOURCE. Changing the palette selection always takes the ghost — even off
        // a catalogue entry the Browser pinned (you just told us what you want).
        const auto sel = Palette::SelectedIndex();
        if (sel != g_lastSeenSel && sel != g_failedSel) {
            g_lastSeenSel = sel;
            ShowSlot(sel);
        } else if (!Active() && !g_fromCatalog && sel != g_failedSel) {
            ShowSlot(sel);   // entered place mode with a slot selected and no ghost yet
        }

        auto ref = g_handle.get();
        if (!ref) {
            if (g_base) DropState();  // the ghost died with its cell — forget it
            return;
        }
        // Walked out of the cell we were previewing in: destroy the ghost NOW, while
        // the handle is still good. An orphan left in a cell we no longer watch is
        // export-safe (the sentinel sees to that) but it is still a mountain standing
        // in someone's inn. Update() runs every frame, so this fires on the first
        // frame after the transition, long before the old cell unloads.
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player && ref->GetParentCell() != player->GetParentCell()) {
            SKSE::log::info("Preview: left the cell — ghost cleared");
            Clear();
            return;
        }
        if (!g_follow) return;
        RE::NiPoint3 pos;
        if (!Aim::RenderedCameraHit(pos)) return;  // looking at the sky: leave it where it is
        ref->SetPosition(pos);
        ref->Update3DPosition(true);
    }

    bool HandleKey(std::uint32_t code) {
        auto ref = g_handle.get();
        if (!ref) return false;   // no ghost — the numpad means nothing here

        switch (code) {
        // Per-axis revert: each pair's MIDDLE key undoes its own pair, back to the
        // pose the ghost spawned with (same geometry as `sc ed ax`, so the two
        // modes' numpads feel like one instrument).
        case Numpad::kBack:    // 2, between 1/3 (pitch)
            g_angle.x = g_spawnAngle.x;
            ApplyPose(ref.get());
            RE::DebugNotification("SCB: pitch reverted");
            return true;
        case Numpad::kSelect:  // 5, between 4/6 (yaw)
            g_angle.z = g_spawnAngle.z;
            ApplyPose(ref.get());
            RE::DebugNotification("SCB: yaw reverted");
            return true;
        case Numpad::kFwd:     // 8, between 7/9 (roll)
            g_angle.y = g_spawnAngle.y;
            ApplyPose(ref.get());
            RE::DebugNotification("SCB: roll reverted");
            return true;
        case Numpad::kCommit:  // 0 — undo the auto-scale: place it at its REAL size
            g_scale = 1.f;
            ApplyPose(ref.get());
            RE::DebugNotification("SCB: scale 1.0 (real size)");
            return true;
        case Numpad::kCancel:  // . — put the ghost away
            Clear();
            return true;
        default:
            break;
        }
        if (!IsNudgeKey(code)) return false;   // not ours: let the action key through
        Numpad::OnTap(code);
        Nudge(code, 1.f);   // a tap is exactly ONE step of the same body the hold drives
        return true;
    }

    void HandleHold(std::uint32_t code, float heldSecs) {
        if (!g_handle.get() || !IsNudgeKey(code)) return;
        if (const float steps = Numpad::StepsFor(code, heldSecs); steps > 0.f)
            Nudge(code, steps);
    }

    bool Commit() {
        auto ref = g_handle.get();
        if (!ref || !g_base) {
            SKSE::log::info("Preview: nothing to commit — no ghost up");
            return false;
        }
        auto id = SceneExporter::ResolveDurableId(g_base);
        if (!id) {  // Catalog never admits one of these, but the ghost outlives the page
            SKSE::log::warn("Preview: ghost's base is runtime-only — cannot place");
            return false;
        }

        // Commit through the ONE place path (`sc pl`'s), at the ghost's exact pose.
        // Not re-aimed: what you see standing there is what gets placed — and
        // `sc pl py0` / `ed1` behave exactly as they do for a palette slot.
        Palette::Slot s;
        s.name = g_label;
        s.baseId = *id;
        s.base = g_base;
        s.angle = ref->data.angle;
        s.scale = ref->GetScale();
        const RE::NiPoint3 pos = ref->GetPosition();

        if (!Palette::PlaceSlot(s, &pos)) return false;
        // The ghost STAYS up — placing a row of trees is the same key, five times.
        SKSE::log::info("Preview: committed '{}' ({}) from ghost at ({:.1f}, {:.1f}, {:.1f}) scale={:.2f}",
            g_label, s.baseId, pos.x, pos.y, pos.z, s.scale);
        return true;
    }

    std::size_t SweepOrphans() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        RE::TESObjectCELL* cell = player ? player->GetParentCell() : nullptr;
        if (!cell) return 0;
        std::vector<RE::TESObjectREFR*> doomed;
        cell->ForEachReference([&](RE::TESObjectREFR* ref) -> RE::BSContainer::ForEachResult {
            if (ref && !ref->IsDeleted() && HasSentinel(ref)) doomed.push_back(ref);
            return RE::BSContainer::ForEachResult::kContinue;
        });
        for (auto* ref : doomed) Destroy(ref);
        if (!doomed.empty())
            SKSE::log::info("Preview: removed {} orphan ghost(s) from the loaded save",
                doomed.size());
        return doomed.size();
    }

    void DropState() {
        g_handle = {};
        g_base = nullptr;
        g_label.clear();
        g_fromCatalog = false;
        g_lastSeenSel = SIZE_MAX;
        g_failedSel = SIZE_MAX;
    }

}  // namespace Preview
