#pragma once

#include "../../types.h"
#include "c_buff_instance.h"
#include "i_buff_init_params.h"
#include "s_buff_definition.h"

#include <cstdint>

namespace SDK::wh::rpgmodule {
    class C_Soul;

    /**
     * The process-wide owner of every buff on every soul.
     *
     * The twelve slots below are the whole native table. Slot 2 is the
     * create-and-attach funnel every add reaches; it returns through a hidden
     * sret pointer, so it is left unnamed here and detoured in
     * `core/hooks/buff.cpp` instead.
     *
     * The two `AddBuff` overloads are given different names on purpose: MSVC
     * emits same-name virtuals in reverse declaration order, which quietly
     * swaps their slots. Deliberately no virtual destructor either -- the
     * native table has none in front of these.
     */
    class C_BuffManager {
      public:
        /** The singleton, built on first use. Never null once the RPG module is up. */
        static C_BuffManager *GetInstance();

        /**
         * Attaches a buff to a soul and answers the instance, or null.
         *
         * Null is not a failure: six implementations fold the incoming
         * instance into the one already on the soul and refuse the attach
         * afterwards, so a successful stack looks exactly like this. Ask the
         * soul what it is wearing rather than retrying.
         *
         * `initParams` may be null. `sourceGuid` is resolved against the
         * soul's own perk list, so pass a null GUID for no source.
         */
        virtual C_BuffInstanceBase *AddBuffToSoul(C_Soul *soul, const CryGUID &buff, I_BuffInitParams *initParams, const CryGUID &sourceGuid) = 0; // slot 0

        /** The same, resolving the soul through the registry first. */
        virtual C_BuffInstanceBase *AddBuffToWuid(std::uint64_t soulWuid, const CryGUID &buff, I_BuffInitParams *initParams, const CryGUID &sourceGuid) = 0; // slot 1

        virtual void unk_fn0002() = 0; // create-and-attach, 0x1804675A8, sret

        virtual bool RemoveBuff(const std::uint64_t &handle) = 0; // slot 3

        virtual void unk_fn0004() = 0; // retire instance, 0x18063A664

        virtual int RemoveAllBuffsByGuid(C_Soul *soul, const CryGUID &buff) = 0; // slot 5

        /**
         * The game's own cure-all: empties a whole `buff_ai_tag` family at once.
         *
         * The tag is read through a reference: the slot forwards R8 to
         * C_Soul::RemoveAllBuffsByAITag, which compares every worn buff's tag
         * against `*aiTagId`. Passed by value, the tag itself is dereferenced
         * the moment the soul wears anything.
         */
        virtual int RemoveAllBuffsByAITag(C_Soul *soul, const int &aiTagId) = 0; // slot 6

        virtual C_BuffInstanceBase *FindBuffInstance(const std::uint64_t &handle) = 0; // slot 7

        /** Never null: an unknown GUID answers a shared empty row whose `buffId` is null. */
        virtual const S_BuffDefinition &GetDefinitionOrDefault(const CryGUID &buff) = 0; // slot 8

        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() = 0;
    };

    static_assert(sizeof(C_BuffManager) == 0x8, "C_BuffManager size mismatch");
} // namespace SDK::wh::rpgmodule
