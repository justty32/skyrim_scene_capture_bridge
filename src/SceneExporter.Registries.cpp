#include "SceneExporter.h"
#include "SceneExporter.Internal.h"

#include "Eraser.h"
#include "Markers.h"
#include "Overrides.h"
#include "Referrer.h"

#include "log.h"

namespace SceneExporter {

    // Append the three cell-independent registry segments ONCE. removals[],
    // overrides[] and annotations[] each span every cell (their registries do),
    // so ModForge resolves them via the master link cache regardless of which
    // cell was swept — exporting the player's cell already carries the lot.
    void AppendRegistries(nlohmann::json& scene) {
        if (const auto& marked = Eraser::All(); !marked.empty()) {
            auto arr = nlohmann::json::array();
            for (const auto& e : marked) {
                // A removal stays a BARE STRING unless it has something to say.
                // The object form {ref, label?, note?} only appears when the
                // author named or annotated the row, so an ordinary export is
                // byte-identical to what it always was and every old spec keeps
                // reading. (Same shorthand-collapse rule as `requires[]`.)
                if (e.label.empty() && e.note.empty()) {
                    arr.push_back(e.id);
                    continue;
                }
                nlohmann::json o;
                o["ref"] = e.id;
                if (!e.label.empty()) o["label"] = e.label;
                if (!e.note.empty()) o["note"] = e.note;  // WHY it goes — for the agent
                arr.push_back(std::move(o));
            }
            scene["removals"] = std::move(arr);
        }

        if (const auto& moved = Overrides::All(); !moved.empty()) {
            auto arr = nlohmann::json::array();
            for (const auto& e : moved) {
                RE::NiPoint3 pos = e.pos, ang = e.angle;
                float scale = e.scale;
                if (auto live = e.handle.get()) {  // prefer the settled live pose
                    pos = live->GetPosition();
                    ang = live->data.angle;
                    scale = live->GetScale();
                }
                nlohmann::json o;
                o["ref"] = e.id;
                o["position"] = Vec3(pos);
                o["rotation"] = nlohmann::json{
                    {"x", ang.x * kRadToDeg}, {"y", ang.y * kRadToDeg}, {"z", ang.z * kRadToDeg},
                };
                if (!e.isActor) o["scale"] = scale;
                if (!e.label.empty()) o["label"] = e.label;
                if (!e.note.empty()) o["note"] = e.note;  // WHY it moved — for the agent
                arr.push_back(std::move(o));
            }
            scene["overrides"] = std::move(arr);
        }

        if (const auto& marks = Markers::All(); !marks.empty()) {
            auto arr = nlohmann::json::array();
            for (const auto& m : marks) {
                nlohmann::json a;
                a["seq"] = m.seq;
                a["label"] = m.label;
                a["kind"] = m.kind;
                a["position"] = Vec3(m.position);
                a["angleZ"] = m.angleDeg.z;  // back-compat (== rotation.z)
                a["rotation"] = nlohmann::json{
                    {"x", m.angleDeg.x}, {"y", m.angleDeg.y}, {"z", m.angleDeg.z}};
                a["scale"] = m.scale;
                if (!m.note.empty()) a["note"] = m.note;  // free-form agent brief
                if (!m.cellOrWs.empty()) a[m.isInterior ? "cell" : "worldspace"] = m.cellOrWs;
                arr.push_back(std::move(a));
            }
            scene["annotations"] = std::move(arr);
        }
    }

    // references[] — the referrer registry (`sc ref` / `sc refc`): an EXISTING ref
    // NAMED by a free-form label, so the rest of the spec can point at it. Nothing
    // is created and nothing is changed; the three siblings are removals[] (erase
    // existing), overrides[] (move existing), references[] (NAME existing).
    //
    // Must run AFTER AppendPlacements: an in-file (B) target is only nameable once
    // its placement has actually been emitted (with the matching editorId).
    //
    // ⚠️ `anchor` is deliberately NEVER written (user-decided): the persistent-ref
    // escape hatch is ModForge's / the authoring agent's call, not the DLL's. An
    // absent anchor reads as "none" on the consumer side.
    void AppendReferences(nlohmann::json& scene, const PlacementCounters& counters) {
        const auto& refs = Referrer::All();
        if (refs.empty()) return;

        auto arr = nlohmann::json::array();
        std::size_t unreachable = 0;
        for (const auto& e : refs) {
            nlohmann::json r;
            if (e.id.empty()) {
                // (B) IN-FILE: `ref` = the editorId AppendPlacements stamped on our
                // own placement. If that placement did not make it into THIS export
                // (its cell wasn't swept, the object was erased, or the co-save
                // couldn't re-acquire the dynamic ref), emitting the row would point
                // at an editorId that isn't in the file — build would just warn and
                // drop it. Skip it here instead, loudly.
                if (!counters.inFileRefsEmitted.contains(e.seq)) {
                    ++unreachable;
                    SKSE::log::warn(
                        "AppendReferences: '{}' targets one of OUR refs that is not in this "
                        "export (cell not swept, object erased, or target lost across a "
                        "restart) — reference skipped", e.label);
                    continue;
                }
                r["ref"] = Referrer::EditorIdOf(e);
            } else {
                r["ref"] = e.id;  // (A) EXTERNAL: durable <plugin>:0xLOCALID
            }
            r["label"] = e.label;
            if (!e.base.empty()) r["base"] = e.base;  // anchor:"replace" needs it

            // Prefer the live pose when the ref is loaded (havok may have settled it),
            // exactly like overrides[] does.
            RE::NiPoint3 pos = e.position, angDeg = e.angleDeg;
            float scale = e.scale;
            if (auto live = e.handle.get()) {
                pos = live->GetPosition();
                angDeg = live->data.angle * kRadToDeg;
                scale = live->GetScale();
            }
            r["position"] = Vec3(pos);
            r["rotation"] = Vec3(angDeg);   // already degrees
            if (!e.isActor) r["scale"] = scale;  // XSCL is dead on ACHR
            if (!e.cellOrWs.empty()) r[e.isInterior ? "cell" : "worldspace"] = e.cellOrWs;
            if (!e.note.empty()) r["note"] = e.note;
            arr.push_back(std::move(r));
        }
        if (unreachable) {
            SKSE::log::warn("AppendReferences: {} reference(s) skipped — their in-file target "
                "was not exported (see the lines above)", unreachable);
        }
        if (!arr.empty()) scene["references"] = std::move(arr);
    }

}  // namespace SceneExporter
