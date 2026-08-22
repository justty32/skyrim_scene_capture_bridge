#include "Aim.h"

#include "log.h"

#include <cmath>

namespace {

    constexpr float kRange = 4096.f;

    // A centre-screen ray starts behind the player in third person, so the
    // ordinary closest-hit collector often returns the avatar. Keep the nearest
    // non-player hit instead. The body handed to AddRayHit may be a child shape;
    // TESHavokUtilities expects the root collidable.
    class PlayerIgnoringRayCollector final : public RE::hkpRayHitCollector {
    public:
        void AddRayHit(const RE::hkpCdBody& body,
            const RE::hkpShapeRayCastCollectorOutput& hitInfo) override {
            const RE::hkpCdBody* root = &body;
            while (root->parent) root = root->parent;
            const auto* collidable = static_cast<const RE::hkpCollidable*>(root);
            auto* ref = RE::TESHavokUtilities::FindCollidableRef(*collidable);
            if (ref && ref->IsPlayerRef()) return;
            if (hitInfo.hitFraction >= hitFraction) return;

            hitFraction = hitInfo.hitFraction;
            rootCollidable = collidable;
            earlyOutHitFraction = hitFraction;
        }

        float hitFraction = 1.f;
        const RE::hkpCollidable* rootCollidable = nullptr;
    };

    bool CastRay(const RE::NiPoint3& from, const RE::NiPoint3& dir,
        RE::NiPoint3& outPoint, const RE::hkpCollidable*& outCollidable,
        bool ignorePlayer) {
        outCollidable = nullptr;
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* cell = player ? player->GetParentCell() : nullptr;
        auto* world = cell ? cell->GetbhkWorld() : nullptr;
        if (!world) return false;

        const RE::NiPoint3 to = from + dir * kRange;
        const float scale = RE::bhkWorld::GetWorldScale();
        RE::bhkPickData pick;
        bool hit = false;
        float fraction = 1.f;
        PlayerIgnoringRayCollector collector;

        if (ignorePlayer) {
            // This is the same collector entry used by SmoothCam's own broad
            // world ray. PickObject consumes a scaled origin plus delta here.
            pick.rayInput.from = RE::hkVector4(from * scale);
            pick.rayInput.to = {};
            pick.ray = RE::hkVector4((to - from) * scale);
            pick.rayHitCollectorA8 =
                reinterpret_cast<RE::hkpClosestRayHitCollector*>(&collector);
            {
                RE::BSReadLockGuard lock(world->worldLock);
                world->PickObject(pick);
            }
            hit = collector.rootCollidable && collector.hitFraction < 1.f;
            fraction = collector.hitFraction;
            outCollidable = collector.rootCollidable;
        } else {
            pick.rayInput.from = RE::hkVector4(from * scale);
            pick.rayInput.to = RE::hkVector4(to * scale);
            {
                RE::BSReadLockGuard lock(world->worldLock);
                hit = world->PickObject(pick) && pick.rayOutput.HasHit();
            }
            if (hit) {
                fraction = pick.rayOutput.hitFraction;
                outCollidable = pick.rayOutput.rootCollidable;
            }
        }
        if (!hit) return false;
        outPoint = from + dir * (kRange * fraction);
        return true;
    }

    // One havok ray along the player's facing. On a hit, `outPoint` gets the
    // world-space hit point and `outCollidable` the hit collidable (read it
    // right away, while the world can't restructure under us).
    bool CastLookRay(RE::NiPoint3& outPoint, const RE::hkpCollidable*& outCollidable) {
        outCollidable = nullptr;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;
        RE::NiPoint3 from = player->GetPosition();
        from.z += 120.f;  // eye-ish; good enough for a ground pick
        const float pitch = player->data.angle.x;  // radians; positive = down
        const float yaw = player->data.angle.z;
        const RE::NiPoint3 dir{
            std::sin(yaw) * std::cos(pitch),
            std::cos(yaw) * std::cos(pitch),
            -std::sin(pitch),
        };
        return CastRay(from, dir, outPoint, outCollidable, false);
    }
}

namespace Aim {

    bool LookHit(RE::NiPoint3& out) {
        const RE::hkpCollidable* ignored = nullptr;
        return CastLookRay(out, ignored);
    }

    bool RenderedCameraHit(RE::NiPoint3& out) {
        auto* camera = RE::Main::WorldRootCamera();
        if (!camera) return false;

        // Gamebryo's NiCamera basis is Direction(+X), Up(+Y), Right(+Z).
        // Reading the final NiCamera (rather than a particular TESCameraState)
        // covers first person, third person, and SmoothCam's post-state offsets.
        const RE::NiPoint3 from = camera->world.translate;
        const RE::NiPoint3 dir = camera->world.rotate.GetVectorX();
        if (dir.SqrLength() < 0.5f) return false;

        const RE::hkpCollidable* ignored = nullptr;
        return CastRay(from, dir, out, ignored, true);
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
