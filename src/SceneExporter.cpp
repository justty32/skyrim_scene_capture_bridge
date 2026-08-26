#include "SceneExporter.h"

#include "Captures.h"
#include "Eraser.h"
#include "Markers.h"
#include "Overrides.h"
#include "Palette.h"
#include "Preview.h"
#include "Referrer.h"

#include "log.h"

#include <cctype>
#include <ctime>
#include <fstream>
#include <unordered_set>

namespace {
    constexpr float kRadToDeg = 57.2957795f;

    // InitiallyDisabled record flag (bit 0x800) — a ref authored to spawn
    // disabled. We export this so ModForge round-trips the enable state.
    constexpr std::uint32_t kInitiallyDisabled = 0x00000800u;

    // Emit {x,y,z} as a json object matching PlacementSpec.Position/Rotation.
    nlohmann::json Vec3(const RE::NiPoint3& v) {
        return nlohmann::json{{"x", v.x}, {"y", v.y}, {"z", v.z}};
    }
}

namespace SceneExporter {

    namespace {
        Stats g_last;
        CaptureStats g_lastCaptures;
    }

    const Stats& LastExport() { return g_last; }
    const CaptureStats& LastCapturesExport() { return g_lastCaptures; }

    std::optional<std::string> ResolveDurableId(const RE::TESForm* form) {
        if (!form) {
            return std::nullopt;
        }
        // GetFile(0) = the file that first DEFINES this form (origin master),
        // not the last override — that is the reference ModForge needs as a
        // master. Runtime-only forms (PlaceAtMe dynamic refs) have no file.
        const RE::TESFile* file = form->GetFile(0);
        if (!file) {
            return std::nullopt;
        }

        const std::uint32_t rawId = form->GetFormID();
        // Light plugins (ESL/ESPFE) are 0xFExxxYYY -> 12-bit local id; full
        // plugins are 0xXXyyyyyy -> 24-bit. Verified offline against
        // ccBGSSSE037-Curios.esl, whose local ids top out at 0x88E (all < 0x1000),
        // and Skyrim.esm's 0x01605E (24-bit). Both round-trip through the
        // "{}:0x{:06X}" form ModForge expects.
        const std::uint32_t localId =
            file->IsLight() ? (rawId & 0x00000FFFu) : (rawId & 0x00FFFFFFu);

        return std::format("{}:0x{:06X}", file->fileName, localId);
    }

    Anchor AnchorOf(RE::TESObjectREFR* ref) {
        Anchor a;
        if (!ref) ref = RE::PlayerCharacter::GetSingleton();
        RE::TESObjectCELL* cell = ref ? ref->GetParentCell() : nullptr;
        if (!cell) return a;
        if (cell->IsInteriorCell()) {
            a.interior = true;
            if (auto id = ResolveDurableId(cell)) a.id = *id;
        } else if (auto* ws = cell->GetRuntimeData().worldSpace) {
            if (auto id = ResolveDurableId(ws)) a.id = *id;
        }
        return a;
    }

    namespace {
        // Running tallies while sweeping one or more cells for placements.
        struct PlacementCounters {
            std::size_t actorsExcluded = 0;  // player-placed actors, deliberately not emitted
            std::size_t preexisting = 0;
            std::size_t skipped = 0;
            std::size_t markerProxies = 0;
            std::size_t previewGhosts = 0;     // browser preview refs — never content
            std::size_t notOurs = 0;           // dynamic refs the ENGINE spawned (fish, critters…)
            std::size_t removalsPending = 0;   // in swept cells (log only)
            std::size_t overridesPending = 0;  // in swept cells (log only)
            // Referrer rows whose IN-FILE target actually made it into placements[]
            // this export — the only ones AppendReferences may emit (a `ref` pointing
            // at an editorId that is not in the file would be dropped by build).
            std::unordered_set<std::uint32_t> inFileRefsEmitted;
            // Palette placed-ref rows (`sc pl ed1`) whose placement was emitted with a
            // MINTED base — AppendMintedItems must emit exactly these as capturedItems[]
            // rows, or the placement's `base` would name an editorId that is not in the
            // file. Same in-file-dependency discipline as inFileRefsEmitted above.
            std::vector<const Palette::PlacedInfo*> mintedEmitted;
            std::size_t noHavokSettle = 0;   // placements exported with the flag (log/panel)
        };
    }

    // Sweep ONE cell's placed refs (the vanilla diff) and append the
    // player-added ones to scene["placements"]. No registry/global segments —
    // those are emitted once by AppendRegistries so export-all doesn't repeat
    // them per cell.
    static void AppendPlacements(RE::TESObjectCELL* cell, nlohmann::json& scene,
        PlacementCounters& counters) {
        if (!cell) return;
        const bool isInterior = cell->IsInteriorCell();

        // Cell / worldspace attribution (§契約 coordinate contract):
        //  - interior: `cell` = the cell's durable id, positions are cell-local.
        //  - exterior: `worldspace` = the worldspace's durable id, positions
        //    are world-space (ModForge finds the right sub-cell to override).
        //
        // These live on EACH PlacementSpec, not at the top level — ModSpec has
        // no top-level `cell`/`worldspace`, and a placement carrying neither is
        // dropped with "cell '' not found in spec — skipped"
        // (Generator.Build.Placements.cs:48).
        std::string cellId;
        std::string worldspaceId;
        if (isInterior) {
            if (auto id = ResolveDurableId(cell)) {
                cellId = *id;
            }
        } else if (auto* ws = cell->GetRuntimeData().worldSpace) {
            if (auto id = ResolveDurableId(ws)) {
                worldspaceId = *id;
            }
        }
        if (cellId.empty() && worldspaceId.empty()) {
            SKSE::log::warn(
                "AppendPlacements: cell/worldspace unresolved — placements here "
                "would be dropped by build; skipping this cell");
            return;
        }

        // ForEachReference hands the callback a POINTER, not a reference.
        cell->ForEachReference([&](RE::TESObjectREFR* refPtr) -> RE::BSContainer::ForEachResult {
            if (!refPtr) {
                return RE::BSContainer::ForEachResult::kContinue;
            }
            RE::TESObjectREFR& ref = *refPtr;

            RE::TESBoundObject* base = ref.GetBaseObject();
            if (!base || ref.IsDeleted()) {
                return RE::BSContainer::ForEachResult::kContinue;
            }
            // Skip the player and refs whose base cannot be durably referenced.
            if (ref.IsPlayerRef()) {
                return RE::BSContainer::ForEachResult::kContinue;
            }
            // Marker proxies are editor chrome, not content — without this they
            // are dynamic refs and the vanilla diff would export them as
            // player-placed objects.
            if (Markers::IsProxy(refPtr)) {
                ++counters.markerProxies;
                return RE::BSContainer::ForEachResult::kContinue;
            }
            // The browser's preview ghost is the same story: a dynamic ref that
            // is NOT content. IsGhost recognises it by the sentinel on the ref
            // itself, so even a ghost orphaned in some old savegame — one this
            // session never spawned and knows nothing about — cannot ship.
            if (Preview::IsGhost(refPtr)) {
                ++counters.previewGhosts;
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // The vanilla diff. A cell sweep sees EVERY reference in it, so
            // exporting all of them would make ModForge re-place the whole
            // vanilla room on top of itself (Bannered Mare: 662 refs, every
            // chair doubled). Authored refs resolve to durable ids and are not
            // new placements: the registries route erased refs to `removals[]`
            // and moved/scaled refs to `overrides[]`; untouched refs are skipped.
            // A dynamic id alone does not prove player ownership — the ownership
            // gate below emits only refs recorded by Palette::PlacedInfoFor.
            if (auto refId = ResolveDurableId(&ref)) {
                // A ref marked by the eraser is not "pre-existing kept as-is" —
                // it exports through removals[], counted separately. Same for a
                // ref moved through the editor: it exports through overrides[].
                if (Eraser::MarkedIds().contains(*refId)) ++counters.removalsPending;
                else if (Overrides::Contains(*refId)) ++counters.overridesPending;
                else ++counters.preexisting;
                return RE::BSContainer::ForEachResult::kContinue;
            }
            // A disabled dynamic ref is one of our own placements the player
            // erased — true deletion semantics: it leaves no trace.
            if (ref.IsDisabled()) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // SCOPE REVERSAL (user-decided 2026-07-12): a cell export is pure
            // scene/object content. Actors used to land in `placements[]` with
            // kind:"npc"; they no longer do — ModForge places NPCs itself,
            // against the markers (annotations[]). An actor you actually want
            // rebuilt goes through `sc cap` -> the separate captures export,
            // which carries its identity, not just a base + transform.
            if (ref.GetFormType() == RE::FormType::ActorCharacter) {
                ++counters.actorsExcluded;
                return RE::BSContainer::ForEachResult::kContinue;
            }

            auto baseId = ResolveDurableId(base);
            if (!baseId) {
                ++counters.skipped;  // dynamic / runtime-only base — not esp-referenceable
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // 🔴 OWNERSHIP GATE (2026-07-14, in-game). Everything above this line
            // establishes "this ref is dynamic" — and the exporter used to treat
            // that AS "the player placed it". It is not. THE ENGINE PlaceAtMe's
            // things too: an exterior export emitted ten placements of which the
            // user had placed exactly one — the rest were six copies of
            // `DoNotPlaceSmallCritterLandingMarkerHelper` (what butterflies land
            // on) and three Fishing-CC fish. No property of the ref distinguishes
            // them; the fish is as dynamic as your chair.
            //
            // So ownership is RECORDED, not inferred: we emit a dynamic ref only
            // if OUR registry says we placed it (`sc pl`, the browser's commit, or
            // an explicit panel adopt). A fish cannot get a row, so a fish cannot
            // ship. Anything else in the cell is somebody else's — count it and
            // walk on.
            const auto* pi = Palette::PlacedInfoFor(refPtr);
            if (!pi) {
                ++counters.notOurs;
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // Authored transform (data.location/angle), not live physics pose.
            // Angle is radians in-engine; contract wants degrees.
            const RE::NiPoint3& pos = ref.data.location;
            const RE::NiPoint3& ang = ref.data.angle;
            nlohmann::json entry;
            entry["base"] = *baseId;

            {
                // py0 — physics off. The in-session SetMotionType freeze dies with the
                // savegame; THIS is the half that ships. `noHavokSettle` becomes the
                // REFR's DontHavokSettle record flag (0x20000000), which tells the
                // engine to skip the load-time havok settle pass — the pass that
                // launches a hand-placed cup across the room. Vanilla Skyrim.esm uses
                // it on 3791 refs (clutter AND statics), so it is NOT type-gated here.
                if (pi->noHavokSettle) {
                    entry["noHavokSettle"] = true;
                    ++counters.noHavokSettle;
                }
                // ed1 — the slot carried instance extra data (a player-applied
                // enchantment). The durable base is a PLAIN iron sword: pointing the
                // placement at it would ship the un-enchanted item. Instead point
                // `base` at a MINTED capturedItems[] record (emitted by
                // AppendMintedItems into THIS file) whose template IS that base and
                // whose enchantment is the captured one. Mint + reference — the same
                // in-file dependency the referrer uses for `references[]`.
                if (pi->extra.present) {
                    entry["base"] = Palette::MintedEditorIdOf(*pi);
                    counters.mintedEmitted.push_back(pi);
                }
            }

            // (B) IN-FILE DEPENDENCY — the referrer's core trick. This dynamic ref
            // has NO durable FormID, so a `references[]` row cannot name it by id
            // (the id is not portable and means nothing after the build). Give the
            // placement a STABLE editorId instead and let references[].ref point at
            // THAT — a dependency INSIDE the file. ModForge then owns the object and
            // forces it persistent, which is exactly what an alias/package anchor
            // needs. The reference row itself is emitted by AppendReferences, only
            // for the targets recorded here.
            if (const auto* rr = Referrer::InFileEntryFor(refPtr)) {
                entry["editorId"] = Referrer::EditorIdOf(*rr);
                counters.inFileRefsEmitted.insert(rr->seq);
            }
            if (!cellId.empty()) {
                entry["cell"] = cellId;
            } else {
                entry["worldspace"] = worldspaceId;
            }
            entry["position"] = Vec3(pos);
            entry["rotation"] = nlohmann::json{
                {"x", ang.x * kRadToDeg},
                {"y", ang.y * kRadToDeg},
                {"z", ang.z * kRadToDeg},
            };

            // Carry scale + the InitiallyDisabled state so ModForge reproduces
            // both. (Actors never get here — see the scope check above.)
            entry["scale"] = ref.GetScale();
            if ((ref.GetFormFlags() & kInitiallyDisabled) != 0) {
                entry["initiallyDisabled"] = true;
            }
            scene["placements"].push_back(std::move(entry));
            return RE::BSContainer::ForEachResult::kContinue;
        });
    }

    // Append the three cell-independent registry segments ONCE. removals[],
    // overrides[] and annotations[] each span every cell (their registries do),
    // so ModForge resolves them via the master link cache regardless of which
    // cell was swept — exporting the player's cell already carries the lot.
    static void AppendRegistries(nlohmann::json& scene) {
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
    static void AppendReferences(nlohmann::json& scene, const PlacementCounters& counters) {
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

    // One capturedItems[] enchantment block — shared by the Captures registry and
    // the palette's minted items (they expand through the SAME ModForge path:
    // ExpandCapturedItems -> WeaponSpec/ArmorSpec with a referenced-or-minted ENCH).
    static nlohmann::json EnchantJson(const std::string& target, const std::string& enchBase,
        std::uint16_t amount, const std::vector<Captures::Effect>& effects) {
        nlohmann::json ench;
        ench["target"] = target;                       // weapon | armor
        if (!enchBase.empty()) ench["base"] = enchBase;  // durable ENCH -> referenced
        if (amount) ench["amount"] = amount;
        if (!effects.empty()) {                        // runtime ENCH -> minted from MGEFs
            auto a = nlohmann::json::array();
            for (const auto& ef : effects)
                a.push_back({{"magicEffect", ef.magicEffect}, {"magnitude", ef.magnitude},
                    {"area", ef.area}, {"duration", ef.duration}});
            ench["effects"] = std::move(a);
        }
        return ench;
    }

    // capturedItems[] rows for objects placed with `sc pl ed1` — the MINT half of
    // the mint+reference path. AppendPlacements already pointed each such
    // placement's `base` at MintedEditorIdOf(row); this emits the record that
    // editorId names, INTO THE SAME FILE (a scene export otherwise carries no
    // capturedItems — the 2026-07-12 scope split sends the Captures registry to its
    // own file). It has to be the same file: an in-file dependency that lands in a
    // different json would be an unresolvable base, and build would drop the
    // placement.
    //
    // Must run AFTER AppendPlacements, and it emits ONLY the rows whose placement
    // was actually emitted — never a record nothing points at.
    static void AppendMintedItems(nlohmann::json& scene, const PlacementCounters& counters) {
        if (counters.mintedEmitted.empty()) return;
        auto arr = nlohmann::json::array();
        for (const auto* pi : counters.mintedEmitted) {
            nlohmann::json c;
            c["editorId"] = Palette::MintedEditorIdOf(*pi);  // what the placement's `base` names
            c["kind"] = pi->extra.kind;                      // weapon | armor
            c["name"] = pi->name;                            // the instance's display name
            if (!pi->baseId.empty()) c["base"] = pi->baseId; // physical template to clone
            c["enchantment"] = EnchantJson(pi->extra.kind, pi->extra.enchBase,
                pi->extra.enchAmount, pi->extra.effects);
            arr.push_back(std::move(c));
        }
        SKSE::log::info("AppendMintedItems: {} minted item(s) for `sc pl ed1` placements",
            arr.size());
        scene["capturedItems"] = std::move(arr);
    }

    // Captured DEFINITIONS — content with no durable base to reference, so
    // ModForge mints fresh authored records. Items (enchant/effects) go to
    // capturedItems[]; actors (appearance/identity) to capturedNpcs[].
    //
    // These are NOT part of a scene export any more (user-decided 2026-07-12):
    // the eyedropped definitions are a library, global and cell-independent,
    // and they get their own button + their own file so a cell export stays a
    // description of one place. Both keys are ModSpec members either way, so
    // whichever file carries them, `build` consumes them the same.
    static void AppendCaptures(nlohmann::json& scene) {
        if (const auto& caps = Captures::All(); !caps.empty()) {
            auto effJson = [](const std::vector<Captures::Effect>& effs) {
                auto a = nlohmann::json::array();
                for (const auto& ef : effs)
                    a.push_back({{"magicEffect", ef.magicEffect}, {"magnitude", ef.magnitude},
                        {"area", ef.area}, {"duration", ef.duration}});
                return a;
            };
            // A capture's LABEL (`sc capp Hero` / `sc capc Sword`) becomes the record's
            // editorId: "MFCap_<label>", with everything an EditorID can't hold folded to
            // '_'. ModForge's "explicit editorId wins" rule then makes the label the stable
            // identity of the generated record (re-capture the same hero → same editorId →
            // the same record, not a second one).
            auto editorIdOf = [](const std::string& label) {
                std::string out = "MFCap_";
                for (const char ch : label) {
                    const auto uc = static_cast<unsigned char>(ch);
                    out.push_back(std::isalnum(uc) ? ch : '_');
                }
                return out;
            };
            auto items = nlohmann::json::array();
            auto npcs = nlohmann::json::array();
            for (const auto& e : caps) {
                if (e.kind == Captures::Kind::kNpc) {
                    const auto& n = e.npc;
                    nlohmann::json c;
                    c["name"] = e.name;
                    if (!e.label.empty()) c["editorId"] = editorIdOf(e.label);
                    if (!e.note.empty()) c["note"] = e.note;   // panel-written brief for the agent
                    if (!e.base.empty()) c["base"] = e.base;   // origin NPC_ if durable
                    if (!n.race.empty()) c["race"] = n.race;
                    c["female"] = n.female;
                    if (n.unique) c["unique"] = true;
                    // This entry IS the player (sc capp, or a sc capc that landed on the player).
                    // Advisory identity flag only — ModForge does NOT fall back a voiceType for it
                    // (2026-07-12 user-decided "as-captured, no fallback"); it just lets the consumer
                    // warn instead of silently shipping a mute clone when the player's base has none.
                    if (n.isPlayer) c["isPlayer"] = true;
                    if (n.essential) c["essential"] = true;
                    if (n.protectedActor) c["protected"] = true;
                    if (n.dead) c["dead"] = true;
                    c["weight"] = n.weight;
                    c["height"] = n.height;
                    c["bodyTint"] = {{"r", n.bodyR}, {"g", n.bodyG}, {"b", n.bodyB}};
                    if (!n.hairColor.empty())
                        c["hairColor"] = {{"id", n.hairColor}, {"r", n.hairR}, {"g", n.hairG}, {"b", n.hairB}};
                    if (!n.faceTexture.empty()) c["faceTexture"] = n.faceTexture;
                    if (!n.defaultOutfit.empty()) c["defaultOutfit"] = n.defaultOutfit;
                    if (!n.headParts.empty()) c["headParts"] = n.headParts;
                    if (!n.tints.empty()) {
                        auto tj = nlohmann::json::array();
                        for (const auto& t : n.tints)
                            tj.push_back({{"index", t.index}, {"preset", t.preset}, {"value", t.value},
                                {"color", {{"r", t.r}, {"g", t.g}, {"b", t.b}, {"a", t.a}}}});
                        c["tintLayers"] = std::move(tj);
                    }
                    if (!n.morphs.empty()) c["faceMorphs"] = n.morphs;
                    if (!n.parts.empty()) c["faceParts"] = n.parts;
                    if (!n.npcClass.empty()) c["class"] = n.npcClass;
                    if (n.level > 0) c["level"] = n.level;
                    // EXPLICIT stats (DNAM): the base actor values the engine really runs on.
                    // Present → ModForge writes them straight and leaves autoCalcStats OFF
                    // (class+level only ESTIMATE these; a cloned actor reports 50/50/50).
                    if (n.health > 0.f) c["health"] = n.health;
                    if (n.magicka > 0.f) c["magicka"] = n.magicka;
                    if (n.stamina > 0.f) c["stamina"] = n.stamina;
                    if (!n.skills.empty()) c["skills"] = n.skills;  // 18, in Skill-enum order
                    if (!n.combatStyle.empty()) c["combatStyle"] = n.combatStyle;
                    if (!n.voiceType.empty()) c["voiceType"] = n.voiceType;
                    if (!n.spells.empty()) c["spells"] = n.spells;
                    if (!n.inventory.empty()) {
                        auto inv = nlohmann::json::array();
                        for (const auto& it : n.inventory) {
                            nlohmann::json o{{"item", it.item}, {"count", it.count}};
                            if (it.worn) o["worn"] = true;
                            if (!it.enchBase.empty() || !it.enchEffects.empty()) {
                                if (!it.name.empty()) o["name"] = it.name;
                                nlohmann::json ench;
                                ench["target"] = it.armorTarget ? "armor" : "weapon";
                                if (!it.enchBase.empty()) ench["base"] = it.enchBase;
                                if (it.enchAmount) ench["amount"] = it.enchAmount;
                                if (!it.enchEffects.empty()) ench["effects"] = effJson(it.enchEffects);
                                o["enchantment"] = std::move(ench);
                            }
                            inv.push_back(std::move(o));
                        }
                        c["inventory"] = std::move(inv);
                    }
                    if (!n.perks.empty()) {
                        auto pj = nlohmann::json::array();
                        for (const auto& p : n.perks)
                            pj.push_back({{"perk", p.perk}, {"rank", p.rank}});
                        c["perks"] = std::move(pj);
                    }
                    if (!n.activeEffects.empty()) {
                        auto aj = nlohmann::json::array();
                        for (const auto& a : n.activeEffects) {
                            nlohmann::json o{{"magicEffect", a.magicEffect}, {"magnitude", a.magnitude},
                                {"duration", a.duration}, {"elapsed", a.elapsed}};
                            if (!a.source.empty()) o["source"] = a.source;
                            aj.push_back(std::move(o));
                        }
                        c["activeEffects"] = std::move(aj);   // runtime buff snapshot
                    }
                    c["position"] = Vec3(n.position);
                    c["rotation"] = Vec3(n.angleDeg);   // already degrees
                    if (!n.cellOrWs.empty()) c[n.isInterior ? "cell" : "worldspace"] = n.cellOrWs;
                    npcs.push_back(std::move(c));
                    continue;
                }
                nlohmann::json c;
                c["name"] = e.name;
                c["kind"] = Captures::KindName(e.kind);
                if (!e.label.empty()) c["editorId"] = editorIdOf(e.label);
                if (!e.note.empty()) c["note"] = e.note;  // panel-written brief for the agent
                if (!e.base.empty()) c["base"] = e.base;  // physical template source
                if (e.kind == Captures::Kind::kWeapon || e.kind == Captures::Kind::kArmor) {
                    nlohmann::json ench;
                    ench["target"] = (e.kind == Captures::Kind::kWeapon) ? "weapon" : "armor";
                    if (!e.enchantBase.empty()) ench["base"] = e.enchantBase;
                    if (e.enchantAmount) ench["amount"] = e.enchantAmount;
                    ench["effects"] = effJson(e.effects);
                    c["enchantment"] = std::move(ench);
                } else {
                    c["effects"] = effJson(e.effects);
                }
                items.push_back(std::move(c));
            }
            if (!items.empty()) scene["capturedItems"] = std::move(items);
            if (!npcs.empty()) scene["capturedNpcs"] = std::move(npcs);
        }
    }

    static void RecordStats(const nlohmann::json& scene, const PlacementCounters& c,
        const std::string& cellLabel) {
        g_last.valid = true;
        g_last.placements = scene["placements"].size();
        g_last.actorsExcluded = c.actorsExcluded;
        g_last.preexisting = c.preexisting;
        g_last.skipped = c.skipped;
        g_last.cell = cellLabel;
        g_last.markers = c.markerProxies;
        g_last.removals = Eraser::All().size();
        g_last.overrides = Overrides::All().size();
        g_last.references = scene.contains("references") ? scene["references"].size() : 0;
        g_last.referencesSkipped = Referrer::All().size() - g_last.references;
        g_last.noHavokSettle = c.noHavokSettle;
        g_last.mintedItems = c.mintedEmitted.size();
        SKSE::log::info(
            "Export[{}]: {} placements, {} actors excluded (NPCs are ModForge's "
            "job), {} pre-existing, {} skipped (dynamic bases), {} marker "
            "proxies excluded, {} preview ghosts excluded, {} dynamic refs not ours "
            "(engine-spawned: fish/critters — adopt them in the panel if you want them), "
            "{} annotations, {} removals, "
            "{} overrides, {} references, {} noHavokSettle (sc pl py0), {} minted items (sc pl ed1)",
            cellLabel, scene["placements"].size(), c.actorsExcluded,
            c.preexisting, c.skipped, c.markerProxies, c.previewGhosts, c.notOurs, Markers::All().size(),
            Eraser::All().size(), Overrides::All().size(), g_last.references,
            c.noHavokSettle, c.mintedEmitted.size());
    }

    nlohmann::json ExportCell(RE::TESObjectCELL* cell) {
        // scene.json IS a ModSpec (see workflows/plans/ingame-scene-export.md):
        // `build scene.json out.esp` deserializes it straight into ModSpec, and
        // ModForge's reader SILENTLY IGNORES unknown keys, so every key must be
        // a real ModSpec member.
        nlohmann::json scene;
        scene["placements"] = nlohmann::json::array();
        if (!cell) {
            SKSE::log::warn("ExportCell: null cell, nothing to export");
            return scene;
        }
        PlacementCounters c;
        AppendPlacements(cell, scene, c);   // must precede AppendReferences (in-file targets)
        AppendRegistries(scene);
        AppendReferences(scene, c);   // in-file targets: needs AppendPlacements first
        AppendMintedItems(scene, c);  // in-file minted bases: likewise
        std::string label;
        if (cell->IsInteriorCell()) {
            if (auto id = ResolveDurableId(cell)) label = *id;
        } else if (auto* ws = cell->GetRuntimeData().worldSpace) {
            if (auto wid = ResolveDurableId(ws)) label = *wid;
        }
        RecordStats(scene, c, label);
        return scene;
    }

    nlohmann::json ExportAll() {
        // Sweep every LOADED cell for placements (interior = just this one;
        // exterior = the whole streamed grid), then the global registries once.
        // Objects placed in cells that have since unloaded can't be recovered —
        // logged so "export all" never silently under-reports.
        nlohmann::json scene;
        scene["placements"] = nlohmann::json::array();
        PlacementCounters c;
        std::size_t cells = 0;
        if (auto* tes = RE::TES::GetSingleton()) {
            tes->ForEachCell([&](RE::TESObjectCELL* cell) {
                if (cell && cell->IsAttached()) { AppendPlacements(cell, scene, c); ++cells; }
            });
        }
        AppendRegistries(scene);
        AppendReferences(scene, c);   // in-file targets: needs AppendPlacements first
        AppendMintedItems(scene, c);  // in-file minted bases: likewise
        RecordStats(scene, c, std::format("ALL/{} loaded cells", cells));
        SKSE::log::info("ExportAll: swept {} loaded cell(s) — placements in "
            "unloaded cells are not captured (visit them, or export per-cell)", cells);
        return scene;
    }

    nlohmann::json ExportPlayerCell() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        RE::TESObjectCELL* cell = player ? player->GetParentCell() : nullptr;
        return ExportCell(cell);
    }

    nlohmann::json ExportCaptures() {
        nlohmann::json scene;
        AppendCaptures(scene);
        std::size_t items = 0, npcs = 0;
        if (scene.contains("capturedItems")) items = scene["capturedItems"].size();
        if (scene.contains("capturedNpcs")) npcs = scene["capturedNpcs"].size();
        g_lastCaptures.valid = true;
        g_lastCaptures.items = items;
        g_lastCaptures.npcs = npcs;
        SKSE::log::info("ExportCaptures: {} captured item(s), {} captured npc(s)", items, npcs);
        return scene;
    }

    // A SCAN is a read of what an export would write — the requires report needs
    // exactly the same json (that is the whole point: it reports on the file you
    // are about to hand to `build`), but it writes no file, so the panel's
    // "Wrote <path>" line must keep describing the last REAL export.
    nlohmann::json ScanAll() {
        const Stats saved = g_last;
        auto scene = ExportAll();
        g_last = saved;
        return scene;
    }

    nlohmann::json ScanCaptures() {
        const CaptureStats saved = g_lastCaptures;
        auto scene = ExportCaptures();
        g_lastCaptures = saved;
        return scene;
    }

    bool WriteSceneFile(const nlohmann::json& scene, const std::filesystem::path& path) {
        try {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            std::ofstream out(path, std::ios::trunc);
            if (!out) {
                SKSE::log::error("WriteSceneFile: cannot open {}", path.string());
                return false;
            }
            out << scene.dump(2);
            SKSE::log::info("WriteSceneFile: wrote {}", path.string());
            return true;
        } catch (const std::exception& e) {
            SKSE::log::error("WriteSceneFile: {}", e.what());
            return false;
        }
    }

    namespace {
        // Every export used to land on a fixed `scene-export.json`, so two
        // exports in a row ate each other. A file name now says WHERE and WHEN
        // it came from — a cell EditorID (or worldspace + grid) plus a stamp.

        // Filename-safe: letters, digits, '-', '_' and '.' survive; anything
        // else (spaces, ':' from a durable id, path separators) becomes '_'.
        std::string SanitizeName(std::string_view in) {
            std::string out;
            out.reserve(in.size());
            for (const char ch : in) {
                const auto uc = static_cast<unsigned char>(ch);
                if (std::isalnum(uc) || ch == '-' || ch == '_' || ch == '.') out.push_back(ch);
                else if (!out.empty() && out.back() != '_') out.push_back('_');
            }
            while (!out.empty() && out.back() == '_') out.pop_back();
            if (out.size() > 48) out.resize(48);  // keep the whole name shell-friendly
            return out;
        }

        // Where an export came from, for the file name: interior -> the cell's
        // EditorID; exterior -> worldspace EditorID + the cell grid (an exterior
        // "cell" is one 4096-unit tile, so the grid is the only thing that
        // distinguishes two exports in the same worldspace).
        std::string SceneTag(RE::TESObjectCELL* cell) {
            if (!cell) return "unknown";
            if (cell->IsInteriorCell()) {
                const char* ed = cell->GetFormEditorID();
                if (ed && *ed) return SanitizeName(ed);
                const char* fn = cell->GetFullName();
                if (fn && *fn) return SanitizeName(fn);
                if (auto id = ResolveDurableId(cell)) return SanitizeName(*id);
                return "interior";
            }
            std::string ws = "exterior";
            if (auto* w = cell->GetRuntimeData().worldSpace) {
                const char* ed = w->GetFormEditorID();
                if (!ed || !*ed) ed = w->GetFullName();
                if (ed && *ed) ws = SanitizeName(ed);
            }
            if (auto* xy = cell->GetCoordinates()) {
                return std::format("{}_x{}y{}", ws, xy->cellX, xy->cellY);
            }
            return ws;
        }
    }

    // YYYYMMDD-HHMM, local time (the player's clock is what they'll match the
    // file against).
    std::string TimeStamp() {
        const std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_s(&tm, &t);
        return std::format("{:04}{:02}{:02}-{:02}{:02}", tm.tm_year + 1900, tm.tm_mon + 1,
            tm.tm_mday, tm.tm_hour, tm.tm_min);
    }

    // Same minute, same cell, two exports — still two files, never a silent
    // overwrite (the whole point of the rename). `ext` so the requires report
    // (a .txt) gets the identical never-clobber guarantee.
    std::filesystem::path UniquePath(const std::filesystem::path& dir,
        const std::string& stem, std::string_view ext) {
        std::error_code ec;
        auto path = dir / (stem + std::string(ext));
        for (int n = 2; std::filesystem::exists(path, ec) && n < 100; ++n) {
            path = dir / std::format("{}-{}{}", stem, n, ext);
        }
        return path;
    }

    void ExportPlayerCellToFile() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        RE::TESObjectCELL* cell = player ? player->GetParentCell() : nullptr;
        auto scene = ExportCell(cell);
        auto dir = SKSE::log::log_directory();
        if (!dir) {
            SKSE::log::error("ExportPlayerCellToFile: no log_directory");
            return;
        }
        const auto out = UniquePath(*dir,
            std::format("scene-export_{}_{}", SceneTag(cell), TimeStamp()));
        if (WriteSceneFile(scene, out)) {
            g_last.path = out.string();
        }
    }

    void ExportAllToFile() {
        auto scene = ExportAll();
        auto dir = SKSE::log::log_directory();
        if (!dir) {
            SKSE::log::error("ExportAllToFile: no log_directory");
            return;
        }
        // "all" is a sweep of every loaded cell, so no single cell names it —
        // anchor the name on where the player was standing when they pressed it.
        auto* player = RE::PlayerCharacter::GetSingleton();
        RE::TESObjectCELL* here = player ? player->GetParentCell() : nullptr;
        const auto out = UniquePath(*dir,
            std::format("scene-export_all-{}_{}", SceneTag(here), TimeStamp()));
        if (WriteSceneFile(scene, out)) {
            g_last.path = out.string();
        }
    }

    void ExportCapturesToFile() {
        auto scene = ExportCaptures();
        auto dir = SKSE::log::log_directory();
        if (!dir) {
            SKSE::log::error("ExportCapturesToFile: no log_directory");
            return;
        }
        const auto out = UniquePath(*dir, std::format("captures_{}", TimeStamp()));
        if (WriteSceneFile(scene, out)) {
            g_lastCaptures.path = out.string();
        }
    }

}  // namespace SceneExporter
