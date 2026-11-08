#pragma once

#include <cstdint>

namespace SDK {
    class ICrySizer;
    class IEntity;
    struct SEntityEvent;
    struct SEntitySpawnParams;

    /**
     * IEntitySystemSink, vtable 0x183A650A0. The KCD2 interface has eight
     * entries. CEntitySystem's concrete spawn, initialization, and removal
     * paths prove the first three lifecycle callbacks. CActorSystem's reuse
     * forwarding and memory report, plus both concrete final-slot literals,
     * establish the remaining standard callbacks and KCD2's appended name.
     */
    class IEntitySystemSink {
      public:
        virtual ~IEntitySystemSink() = default; // +0x00
        virtual bool OnBeforeSpawn(SEntitySpawnParams &params) = 0;
        virtual void OnSpawn(IEntity *entity, SEntitySpawnParams &params) = 0;
        virtual bool OnRemove(IEntity *entity) = 0;
        virtual void OnReused(IEntity *entity,
                              SEntitySpawnParams &params) = 0; // +0x20
        virtual void OnEvent(IEntity *entity,
                             SEntityEvent &event) = 0; // +0x28
        virtual void GetMemoryUsage(ICrySizer *sizer) const = 0; // +0x30
        virtual const char *GetName() const = 0; // +0x38, KCD2 addition
    };

    static_assert(sizeof(IEntitySystemSink) == 0x8, "IEntitySystemSink size mismatch");

    /**
     * Subscription bits for IEntitySystem::AddSink's second argument.
     *
     * AddSink files the sink into one container per bit; OnBeforeSpawnSinks
     * walks bit 0's, which is what pins the order.
     */
    namespace EntitySystemSinkSubscriptions {
        inline constexpr std::uint32_t OnBeforeSpawn = 1U << 0;
        inline constexpr std::uint32_t OnSpawn = 1U << 1;
        inline constexpr std::uint32_t OnRemove = 1U << 2;
        inline constexpr std::uint32_t OnReused = 1U << 3;
        inline constexpr std::uint32_t OnEvent = 1U << 4;
    } // namespace EntitySystemSinkSubscriptions
} // namespace SDK
