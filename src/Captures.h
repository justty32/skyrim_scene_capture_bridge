#pragma once

// Captures — the eyedropper for DEFINITIONS (Idea #24 addendum, 2026-07-11).
//
// The Palette eyedropper captures a durable BASE so ModForge can re-PLACE copies
// (a reusable stamp). Captures is its sibling for content that has NO durable
// base to reference — a player-enchanted weapon, a home-brewed potion, or (a
// follow-up increment) a PROTEUS-cloned NPC. It reads the live form's SEMANTIC
// content (enchantment effects, alchemy effects) and records it so ModForge can
// MINT A FRESH authored record (a new ENCH + WEAP, an ALCH, …).
//
// This IS scene content, not a reusable library: an entry exports into
// `capturedItems[]` (a net-new scene.json section ModForge consumes), so the
// registry rides the co-save exactly like Eraser/Overrides/Markers. Entries hold
// only durable ids (MGEF/ENCH/base) + plain data — no ObjectRefHandles — so they
// restore across saves with no re-resolution.
//
// Scope: increment ① ITEMS (weapon/armour enchant + potion/ingredient effects);
// increment ② NPC appearance (the PROTEUS payoff — race/sex/weight + head parts,
// tint layers, face morphs, hair/skin colour). UNIQUE NPCs are captured too
// (user-decided 2026-07-11, reversing the earlier skip); the `unique` flag rides
// along so ModForge can decide what to do with a one-of-a-kind actor.
//
// NPC caveat (IN-GAME TBD): the capture reads the actor's TESNPC record. If a
// tool (PROTEUS) applies its look via live NiNode overrides WITHOUT writing the
// TESNPC, the captured face is the base's, not the applied one — must be checked
// in-game against the exported json. Reproducing a real face also needs facegen
// baking (FaceGeom nif + tint dds), which is ModForge's downstream job — the DLL
// only records the source data.

#include <cstdint>
#include <string>
#include <vector>

namespace Captures {

    // One magic effect, shaped to ModForge's EffectSpec {MagicEffect, Magnitude,
    // Area, Duration}. `magicEffect` is a durable MGEF id "<plugin>:0xLOCALID"
    // (MGEFs are practically always authored, so this resolves).
    struct Effect {
        std::string magicEffect;
        float magnitude = 0.f;
        std::int32_t area = 0;
        std::int32_t duration = 0;
    };

    // One TESNPC tint layer (TINC/TINI/TIAS/TINV) — a captured face's makeup /
    // war-paint / skin-tone layer.
    struct TintLayer {
        std::uint16_t index = 0;   // TINI — which layer slot
        std::uint16_t preset = 0;  // TIAS — palette preset
        std::uint16_t value = 0;   // TINV — interpolation value (CK value * 100)
        std::uint8_t r = 0, g = 0, b = 0, a = 0;  // TINC colour
    };

    // A perk the actor's base carries (durable BGSPerk id + rank).
    struct PerkEntry {
        std::string perk;
        std::int32_t rank = 0;
    };

    // One active magic effect on the actor at capture time (a "current buff"):
    // the source spell/ability + its base MGEF, with the live magnitude/timing.
    // This is a runtime SNAPSHOT, not a durable trait — ModForge decides whether
    // to bake it as an ability, ignore transient ones, etc.
    struct ActiveEffect {
        std::string source;       // durable id of the source spell/ability (SPEL/ENCH/…), if any
        std::string magicEffect;  // durable MGEF id
        float magnitude = 0.f;
        float duration = 0.f;     // <=0 = ability / no timer
        float elapsed = 0.f;      // seconds already elapsed
    };

    // Appearance/identity of a captured actor — everything an NPC_ face record
    // carries, plus the placement transform so ModForge can position the rebuild.
    struct NpcData {
        std::string race;          // durable RACE id
        // EXPLICIT stats (DNAM), captured for every actor — not just the player. The base
        // actor values are the truth the engine actually runs on; class+autocalc is only an
        // ESTIMATE of them (and a PROTEUS-style clone reports a flat level-1 50/50/50). With
        // these present ModForge writes DNAM directly and leaves autoCalcStats OFF.
        float health = 0.f;   // kHealth base AV (0 = not captured)
        float magicka = 0.f;  // kMagicka base AV
        float stamina = 0.f;  // kStamina base AV
        // The 18 skills, in ActorValue order 6..23 (kOneHanded..kEnchanting) — which is
        // exactly Mutagen's `Skill` enum order, so the C# side maps index→Skill 1:1.
        // Empty when unavailable; otherwise always 18 entries.
        std::vector<std::int32_t> skills;
        bool female = false;
        bool unique = false;       // ACBS Unique flag — ModForge decides how to treat a one-of-a-kind
        // This entry IS the player character (base FormID Skyrim.esm:0x000007), set via
        // actor->As<PlayerCharacter>() (same test ReadNpc already uses for the addedPerks route —
        // works for `sc capp` AND a `sc capc` that happens to land on the player). Pure identity flag:
        // ModForge does not fall back a voiceType for it (2026-07-12 user-decided "as-captured, no
        // fallback") — it only lets the consumer WARN instead of silently shipping a mute clone.
        bool isPlayer = false;
        bool dead = false;         // live death state at capture time
        bool essential = false;    // ACBS Essential
        bool protectedActor = false;  // ACBS Protected
        float weight = 0.f;        // NAM7 (0..100)
        float height = 1.f;        // NAM6
        std::uint8_t bodyR = 0, bodyG = 0, bodyB = 0;  // QNAM skin tone
        std::string hairColor;     // durable HCLF id (empty if none)
        std::uint8_t hairR = 0, hairG = 0, hairB = 0;
        std::string faceTexture;   // durable FTST id (empty if none)
        std::string defaultOutfit; // durable outfit id (empty if none)
        std::vector<std::string> headParts;  // durable BGSHeadPart ids
        std::vector<TintLayer> tints;
        std::vector<float> morphs;         // faceData NAM9 (19 sliders)
        std::vector<std::int32_t> parts;   // faceData NAMA (4 part presets)
        std::vector<PerkEntry> perks;      // base perks ＋ (player) runtime addedPerks, deduped
        std::vector<ActiveEffect> activeEffects;  // current buffs — runtime snapshot
        std::string npcClass;              // durable CLAS id (drives ModForge autoCalcStats)
        std::int16_t level = 0;            // actor's effective level at capture time
        std::string combatStyle;           // durable CSTY id — HOW the AI fights (magic vs melee)
        std::string voiceType;             // durable VTYP id — without one the clone is mute
        std::vector<std::string> spells;   // durable SPEL ids: base spell list + actor addedSpells
                                           // (→ NpcSpec.spells / ActorEffect — what combat AI casts)
        // One carried-inventory row: durable base + stack count + role flags + the item
        // INSTANCE's enchantment (ExtraEnchantment extra data — a player-crafted staff/armour
        // enchant that lives on the instance, not the base). A durable instance-ENCH is
        // referenced (`enchBase`); a runtime one is captured as MGEF effects for ModForge to
        // mint. `name` = instance display name (only filled when enchanted — the minted clone
        // needs it, a plain base ref keeps its own name).
        struct InvItem {
            std::string item;
            std::int32_t count = 1;
            bool worn = false;         // worn armour → outfit route downstream
            bool armorTarget = false;  // ench family when enchanted: armor(apparel) vs weapon
            std::string name;
            std::string enchBase;
            std::uint16_t enchAmount = 0;
            std::vector<Effect> enchEffects;
        };
        // The actor's full carry. WORN armour rows become an OUTFIT downstream (the engine
        // only auto-wears outfit armour — inventory armour stays in the pocket, in-game
        // confirmed); everything else (weapons/staves/food/potions/gold…) is plain inventory
        // (weapons auto-equip). Dresses + stocks the clone even when defaultOutfit is a
        // runtime shell.
        std::vector<InvItem> inventory;
        RE::NiPoint3 position;     // world coords at capture time
        RE::NiPoint3 angleDeg;     // facing (degrees)
        std::string cellOrWs;      // durable anchor at capture time
        bool isInterior = false;
    };

    // Which scene.json shape the entry serialises to and which fields matter.
    enum class Kind : std::uint8_t { kWeapon, kArmor, kPotion, kIngredient, kNpc };

    struct Entry {
        std::uint32_t seq = 0;
        Kind kind = Kind::kWeapon;
        std::string name;         // display name at capture time (row label)
        // Free identity label the player typed (`sc capp Hero`, `sc capc Sword`). Case is
        // PRESERVED (the console parser lower-cases its args — the label must not go through
        // that path). Exported as `editorId: "MFCap_<sanitised label>"`, which ModForge's
        // "explicit editorId wins" rule turns into the record's stable identity.
        std::string label;
        // Free-form brief for the agent, panel-editable ("the innkeeper's own
        // sword — give it to the clone"). Rides the co-save and the export.
        std::string note;
        std::string base;         // origin base durable id (physical template); "" if runtime-only
        // Item payload (weapon/armour/potion/ingredient).
        std::string enchantBase;  // durable ENCH id when the enchant itself is authored; else ""
        std::uint16_t enchantAmount = 0;  // enchant charge / amount (weapon/armour)
        std::vector<Effect> effects;      // enchant effects, or the alchemy effect list
        // NPC payload (kind == kNpc).
        NpcData npc;
    };

    // Outcome of a capture attempt — the console/panel word things by this.
    enum class Result { kNone, kCaptured, kNothing, kNotItem, kMarkerProxy };

    Result CaptureCrosshair();  // capture mode, crosshair aim — the activatable target
    Result CaptureByRay();      // capture mode, ray aim — the look-ray target
    // `sc capc [label]` — the console-selected ref (items AND actors).
    Result CaptureConsoleRef(const std::string& label = "");
    // `sc capp [label]` — THE PLAYER, read straight off its base TESNPC (0x7). The engine
    // writes chargen (race/tints/morphs/head parts) onto that record, so no PROTEUS clone is
    // needed as an intermediary; perks are the base array PLUS PlayerCharacter's runtime
    // addedPerks (the spent skill-tree perks live only there), and stats come from the
    // permanent actor values.
    Result CapturePlayer(const std::string& label = "");

    [[nodiscard]] std::vector<Entry>& All();
    [[nodiscard]] const char* KindName(Kind k);  // "weapon"/"armor"/"potion"/"ingredient"

    // Panel row naming (the label was console-only — `sc capp <label>` — until
    // the panel got a field for it). Keyed by seq; a no-op on an unknown one.
    void SetLabel(std::uint32_t seq, const std::string& label);
    void SetNote(std::uint32_t seq, const std::string& note);

    bool Undo();                        // drop the most recent capture
    bool UndoEntry(std::uint32_t seq);  // per-row undo (panel button)
    void Clear();

    // Co-save plumbing (CoSave.cpp), mirroring Eraser/Overrides.
    void DropAll();             // clear the registry (no world touch on revert)
    void OnRegistryRestored();  // reseed the seq counter after entries are loaded

}  // namespace Captures
