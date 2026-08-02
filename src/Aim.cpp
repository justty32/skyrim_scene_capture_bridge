#include "Aim.h"

#include "log.h"

#include <cmath>

namespace {

    // One havok ray along the player's facing. On a hit, `outPoint` gets the
    // world-space hit point and `outCollidable` the hit collidable (read it
    // right away, while the world can't restructure under us).
    bool CastLookRay(RE::NiPoint3& outPoint, const RE::hkpCollidable*& outCollidable) {
        outCollidable = nullptr;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;
        auto* cell = player->GetParentCell();
        auto* world = cell ? cell->GetbhkWorld() : nullptr;
        if (!world) return false;

        RE::NiPoint3 from = player->GetPosition();
        from.z += 120.f;  // eye-ish; good enough for a ground pick
        const float pitch = player->data.angle.x;  // radians; positive = down
        const float yaw = player->data.angle.z;
        const RE::NiPoint3 dir{
            std::sin(yaw) * std::cos(pitch),
            std::cos(yaw) * std::cos(pitch),
            -std::sin(pitch),
        };
        constexpr float kRange = 4096.f;
        const RE::NiPoint3 to = from + dir * kRange;

        const float scale = RE::bhkWorld::GetWorldScale();
        RE::bhkPickData pick;
        pick.rayInput.from = RE::hkVector4(from * scale);
        pick.rayInput.to = RE::hkVector4(to * scale);
        bool hit = false;
        {
            RE::BSReadLockGuard lock(world->worldLock);
            hit = world->PickObject(pick) && pick.rayOutput.HasHit();
        }
        if (!hit) return false;
        outPoint = from + dir * (kRange * pick.rayOutput.hitFraction);
        outCollidable = pick.rayOutput.rootCollidable;
        return true;
    }
}

namespace Aim {

    bool LookHit(RE::NiPoint3& out) {
        const RE::hkpCollidable* ignored = nullptr;
        return CastLookRay(out, ignored);
    }

    RE::NiPointer<RE::TESObjectREFR> CrosshairRef() {
        auto* pick = RE::CrosshairPickData::GetSingleton();
        // NG layout: per-VR-device arrays; flat runtime reads device 0.
        return pick ? pick->target[0].get() : nullptr;
    }

    RE::NiPointer<RE::TESObjectREFR> RayRef() {
        RE::NiPoint3 point;
        const RE::hkpCollidable* collidable = nullptr;
        if (!CastLookRay(point, collidable) || !collidable) return nullptr;
        RE::TESObjectREFR* hit = RE::TESHavokUtilities::FindCollidableRef(*collidable);
        if (!hit || hit->IsPlayerRef()) return nullptr;  // ground LAND or self
        return RE::NiPointer<RE::TESObjectREFR>(hit);
    }

}  // namespace Aim
