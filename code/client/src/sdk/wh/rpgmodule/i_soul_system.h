#pragma once

#include <cstddef>

namespace SDK {
    class IActor;
    class IEntity;
} // namespace SDK

namespace SDK::wh::rpgmodule {
    class I_Soul;

    /**
     * RPG soul system owned by the Warhorse global environment at +0x1D0.
     *
     * A soul carries an actor's identity: name, stats, faction and the shared
     * soul its appearance comes from. Its object layout remains vptr-only;
     * the opaque slots below preserve the proven table order through the
     * named C_Actor::AcquireSoul dispatch at byte +0xB8. There is deliberately
     * no C++ virtual destructor because the
     * native table has none before that
     * slot.
     */
    class I_SoulSystem {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual void unk_fn0016() = 0;
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;
        virtual void unk_fn0019() = 0;
        virtual void unk_fn0020() = 0;
        virtual void unk_fn0021() = 0;
        virtual void unk_fn0022() = 0;

        /** Proven C_Actor::AcquireSoul dispatch at vtable byte +0xB8. */
        virtual I_Soul *GetOrCreateSoul(IEntity *entity) = 0;

        /** Null until the RPG module has constructed the soul system. */
        static I_SoulSystem *GetInstance();
    };

    static_assert(sizeof(I_SoulSystem) == 0x8, "I_SoulSystem size mismatch");

    /**
     * The soul belonging to an entity, built and initialized if it has none.
     *
     * This is the whole identity pipeline in one call, and it is what
     * C_Actor::AcquireSoul itself invokes. On a miss it allocates a soul and
     * runs C_Soul::InitFromEntity, which reads `Properties.guidSharedSoulId`
     * off the entity, copies the matching row of the `soul` table into the
     * soul, and finishes by building the soul's inventory runtime. On a hit --
     * the lookup is keyed by the entity's WUID -- it returns the existing soul
     * untouched.
     *
     * Preferred over an actor's cached soul for anything that then reads
     * it: this cannot answer with a soul that init never ran on.
     */
    I_Soul *GetOrCreateSoul(I_SoulSystem *soulSystem, IEntity *entity);

    /**
     * Builds the soul's inventory and equipment manager if it has none.
     *
     * The engine's own guarded entry point, not the raw builder underneath it:
     * besides constructing the pair it bumps the soul's inventory refcount and
     * registers the inventory's listener, and a body whose inventory was built
     * without those is a body the rest of the engine does not know about.
     *
     * Its guard is valid after C_Soul construction, which establishes the
     * embedded C_InventorySoul before any entity initialization reaches it.
     */
    void EnsureSoulInventory(I_Soul *soul);
} // namespace SDK::wh::rpgmodule
