#include "Physics.h"

#include "log.h"

namespace Physics {

    bool HavokMovable(RE::TESBoundObject* base) {
        switch (base ? base->GetFormType() : RE::FormType::None) {
        case RE::FormType::MovableStatic:
        case RE::FormType::Misc:
        case RE::FormType::Weapon:
        case RE::FormType::Ammo:
        case RE::FormType::Book:
        case RE::FormType::AlchemyItem:
        case RE::FormType::Ingredient:
        case RE::FormType::SoulGem:
            return true;
        default:
            return false;
        }
    }

    void FreezeDeferred(RE::ObjectRefHandle handle, int retries) {
        auto* task = SKSE::GetTaskInterface();
        if (!task) {  // no queue (very early) — best-effort immediate
            if (auto r = handle.get())
                r->SetMotionType(RE::hkpMotion::MotionType::kKeyframed, false);
            return;
        }
        task->AddTask([handle, retries]() {
            auto ref = handle.get();
            if (!ref) return;
            if (ref->Get3D()) {
                ref->SetMotionType(RE::hkpMotion::MotionType::kKeyframed, false);
                return;
            }
            if (retries > 0) FreezeDeferred(handle, retries - 1);
        });
    }

    bool Release(RE::TESObjectREFR* ref) {
        if (!ref) return false;
        ref->SetMotionType(RE::hkpMotion::MotionType::kDynamic, true);
        return true;
    }

}  // namespace Physics
