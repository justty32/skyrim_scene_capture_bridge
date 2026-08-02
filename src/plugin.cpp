#include "log.h"
#include "Console.h"
#include "CoSave.h"
#include "Editor.h"
#include "KeyIni.h"
#include "Markers.h"
#include "Modes.h"
#include "Palette.h"
#include "Preview.h"
#include "Referrer.h"
#include "SceneExporter.h"
#include "UI.h"

namespace {
    // E on a marker gem -> its edit window. TESActivateEvent is a NOTIFICATION
    // (fires after the fact, kStop only stops other sinks) — that is fine: the
    // proxy ACTI has no script/sound/name, so default activation is a no-op
    // and there is nothing to suppress. An orphaned proxy (previous session)
    // is adopted on the spot so the window can still open on it.
    class ActivateSink : public RE::BSTEventSink<RE::TESActivateEvent>
    {
    public:
        static ActivateSink* GetSingleton() { static ActivateSink s; return &s; }

        RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent* e,
            RE::BSTEventSource<RE::TESActivateEvent>*) override
        {
            if (!e || !e->objectActivated || !e->actionRef ||
                !e->actionRef->IsPlayerRef()) {
                return RE::BSEventNotifyControl::kContinue;
            }
            auto* ref = e->objectActivated.get();
            if (!Markers::IsProxy(ref)) {
                return RE::BSEventNotifyControl::kContinue;
            }
            auto seq = Markers::SeqOf(ref);
            if (!seq) seq = Markers::AdoptOne(ref);
            if (seq) {
                SKSE::log::info("Markers: proxy #{} activated -> edit window", seq);
                UI::MarkerEditor::Open(seq);
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // The P5 input surface — two layers, nothing else (the classic F6/F7/F8/
    // F10/F11 direct hotkeys are GONE, user-decided, not toggled off):
    //   1. edit-mode numpad internals (+ numpad * ray-select entry),
    //   2. the current mode's action key (per-mode binding, default F11).
    // Sink shape lifted from my_skyrim_plugin_1's FollowLight::HotkeySink
    // (in-game proven). One poll can carry several events chained through
    // `next`, so walk the list rather than reading only the head.
    //
    // There is NO key-capture layer any more (2026-07-12): rebinding moved to
    // SceneCaptureBridge.ini (KeyIni.cpp). Reading a keypress out of THIS stream
    // to bind it is precisely what failed in-game twice — the panel does not
    // pause the game, so the stream is full of the movement keys the player's
    // hand is still on. The sink is now read-only with respect to bindings.
    class HotkeySink : public RE::BSTEventSink<RE::InputEvent*>
    {
    public:
        static HotkeySink* GetSingleton() { static HotkeySink s; return &s; }

        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_events,
            RE::BSTEventSource<RE::InputEvent*>*) override
        {
            if (!a_events) {
                return RE::BSEventNotifyControl::kContinue;
            }
            for (auto* e = *a_events; e; e = e->next) {
                auto* btn = e->AsButtonEvent();
                if (!btn) continue;
                if (btn->GetDevice() != RE::INPUT_DEVICE::kKeyboard) continue;
                const auto code = btn->GetIDCode();

                // IsDown() is true only on the frame the key goes down, so one
                // press = one step. That is right for every DISCRETE act (an
                // action key, a commit) and it stays that way.
                if (btn->IsDown()) {
                    if (Editor::HandleKey(code)) continue;
                    // Place mode's ghost takes the numpad too (rotate + scale it
                    // before you drop it). It only ever consumes numpad keys and
                    // only while a ghost is up, so the action key still gets through
                    // to Modes below — which is what actually places the thing.
                    if (Preview::HandleKey(code)) continue;
                    Modes::HandleKey(code);
                    continue;
                }
                // ...but the engine re-dispatches the event every frame the key
                // stays down (heldDownSecs climbing), and edit mode's nudge keys
                // want exactly that: hold numpad 8 and the thing keeps moving.
                // Only edit mode sees held keys — an action key must never fire
                // 60 times because a finger lingered.
                if (btn->IsHeld()) {
                    Editor::HandleHold(code, btn->HeldDuration());
                    Preview::HandleHold(code, btn->HeldDuration());
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };
}

void OnDataLoaded() {
    // Action keys come from SceneCaptureBridge.ini (created with defaults on the
    // first run). Read BEFORE the sink goes up, and before any save is loaded —
    // the co-save's binds are then applied only where the ini stayed silent
    // (Modes::ApplyCoSaveBind).
    KeyIni::Load();
    if (auto* idm = RE::BSInputDeviceManager::GetSingleton()) {
        idm->AddEventSink(HotkeySink::GetSingleton());
        SKSE::log::info("SceneCaptureBridge: input sink registered (mode system, "
            "per-mode binds default F11)");
    } else {
        SKSE::log::error(
            "SceneCaptureBridge: BSInputDeviceManager null — action keys NOT registered");
    }
    if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
        holder->AddEventSink<RE::TESActivateEvent>(ActivateSink::GetSingleton());
    }
    Console::Install();  // the `sc` console command (mode switching)
    UI::Register();      // no-op when SKSE Menu Framework is absent
    Palette::Load();     // slots persist on disk, across saves and sessions
    SKSE::log::info("SceneCaptureBridge: data loaded, exporter ready");
}

void MessageHandler(SKSE::MessagingInterface::Message* a_msg) {
    switch (a_msg->type) {
    case SKSE::MessagingInterface::kDataLoaded:
        SKSE::log::info("kDataLoaded: game data loaded");
        OnDataLoaded();
        break;
    case SKSE::MessagingInterface::kPostLoadGame:
        // A load wipes pre-load dynamic refs; drop registry ghosts.
        Markers::PruneDeadProxies();
        // Marker proxies are dynamic refs — their FormIDs aren't reliably
        // remapped across a full restart, so the co-save drops them. The gems
        // DO persist in the savegame, so re-adopt them from the current cell
        // (deferred one frame so the cell's refs have settled) and merge back
        // any pending notes. No more manual "adopt this cell" for the common case.
        if (auto* task = SKSE::GetTaskInterface()) {
            task->AddTask([]() {
                if (auto n = Markers::AdoptOrphans())
                    SKSE::log::info("Markers: auto-adopted {} orphan(s) on load", n);
                // A preview ghost that was standing when the player saved is IN
                // that savegame. It is not content and never was — delete it. (The
                // export gate does not depend on this sweep: a ghost carries its own
                // sentinel, so it is refused even if we never get here — see
                // Preview.h. This is the cleanup, not the safety net.)
                Preview::SweepOrphans();
                // Same story for a referrer that names one of OUR placements: its
                // identity IS the dynamic ref's handle, and a dynamic FormID is not
                // reliably remapped across a full restart. The object survives in the
                // savegame — re-find it by base + position so the reference stays
                // exportable instead of silently dropping out of references[].
                if (auto n = Referrer::ReacquireOrphans())
                    SKSE::log::info("Referrer: re-acquired {} in-file target(s) on load", n);
            });
        }
        break;
    default:
        break;
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SetupLog();
    SKSE::log::info("SceneCaptureBridge loaded");

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging->RegisterListener("SKSE", MessageHandler)) {
        SKSE::log::error("Failed to register SKSE message listener");
        return false;
    }
    CoSave::Register();  // settings + registries ride along with every save
    return true;
}
