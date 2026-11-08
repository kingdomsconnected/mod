#pragma once

#include "c_linkable_object_extension.h"
#include "../framework/i_module_message_listener.h"
#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::xgenaimodule {
    /** Native 0x10-byte SpawnActorEntry stored by C_AnimalSpawner. */
    struct SpawnActorEntryLayout {
        std::uint32_t m_nextSpawnDay;       // 0x00
        std::int32_t m_spawnRequestId;      // 0x04, -1 when no request exists
        void *m_pActorReference;            // 0x08
    };

#pragma pack(push, 4)
    /** Position and actor-selection state consumed by each spawn request. */
    struct AnimalSpawnerQueryStateLayout {
        Vec3 m_requestedPosition; // 0x00
        std::uint64_t m_spawnDefinitionKey[2]; // 0x0C, exact semantics stripped
        const char *m_pActorNameFilter; // 0x1C, native CryString data pointer
    };
#pragma pack(pop)

    static_assert(sizeof(SpawnActorEntryLayout) == 0x10,
                  "SpawnActorEntryLayout size mismatch");
    static_assert(sizeof(AnimalSpawnerQueryStateLayout) == 0x24,
                  "AnimalSpawnerQueryStateLayout size mismatch");
    static_assert(offsetof(AnimalSpawnerQueryStateLayout, m_pActorNameFilter) ==
                      0x1C,
                  "AnimalSpawnerQueryStateLayout name-filter offset mismatch");

    /**
     * Dynamic XGenAI animal-spawner game-object extension.
     *
     * RTTI proves C_LinkableObjectExtension at +0x00 and
     * wh::framework::I_ModuleMessageListener at +0x58. Its primary table at
     * 0x183A652D8 contains exactly the 29 inherited extension entries, while
     * the secondary table at 0x183A65190 has the listener destructor and
     * callback. Factory allocation and deletion prove size 0xE8. The native
     * listener callback iterates the 0x10-byte SpawnActorEntry vector at +0x70.
     */
    class C_AnimalSpawner : public C_LinkableObjectExtension,
                            public framework::I_ModuleMessageListener {
      public:
        ~C_AnimalSpawner() override = default;

        SpawnActorEntryLayout *GetSpawnEntries() {
            return m_pSpawnEntriesBegin;
        }
        const SpawnActorEntryLayout *GetSpawnEntries() const {
            return m_pSpawnEntriesBegin;
        }
        std::size_t GetSpawnEntryCount() const {
            return m_pSpawnEntriesBegin != nullptr
                       ? static_cast<std::size_t>(m_pSpawnEntriesEnd -
                                                  m_pSpawnEntriesBegin)
                       : 0;
        }

        /**
         * Runs the native pass that creates or retires every spawn entry.
         * The original stripped member name is deliberately not asserted.
         */
        void InvokeProcessSpawnEntriesRaw() {
            hook::this_call<void>(gPatterns.C_AnimalSpawner__ProcessSpawnEntriesRaw, this);
        }

        /** Runs the native spawn/refresh path for one existing entry. */
        void InvokeProcessSpawnEntryRaw(std::uint32_t index) {
            if (index >= GetSpawnEntryCount()) {
                return;
            }

            hook::this_call<void>(gPatterns.C_AnimalSpawner__ProcessSpawnEntryRaw, this, index);
        }

        /** Cancels or retires one existing entry through the native path. */
        void InvokeRetireSpawnEntryRaw(std::uint32_t index, bool force) {
            if (index >= GetSpawnEntryCount()) {
                return;
            }

            hook::this_call<void>(gPatterns.C_AnimalSpawner__RetireSpawnEntryRaw, this, index, force);
        }

        bool IsSpawnProcessingEnabled() const { return m_spawnProcessingEnabled; }
        const Vec3 &GetRequestedSpawnPosition() const {
            return m_queryState.m_requestedPosition;
        }
        const char *GetActorNameFilter() const {
            return m_queryState.m_pActorNameFilter;
        }

      private:
        void *m_pState0060;
        std::uint16_t m_state0068;
        std::uint16_t m_state006A;
        std::byte m_padding006C[0x4];
        SpawnActorEntryLayout *m_pSpawnEntriesBegin;    // 0x70
        SpawnActorEntryLayout *m_pSpawnEntriesEnd;      // 0x78
        SpawnActorEntryLayout *m_pSpawnEntriesCapacity; // 0x80
        bool m_spawnProcessingEnabled; // 0x88
        std::byte m_padding0089[0x3];
        AnimalSpawnerQueryStateLayout m_queryState; // 0x8C
        std::uint32_t m_respawnDelayDays; // 0xB0, added to the current day
        bool m_reuseRequestedPosition; // 0xB4
        std::byte m_padding00B5[0x3];
        void *m_pState00B8;
        bool m_state00C0;
        std::byte m_padding00C1[0x7];
        void *m_pState00C8;
        bool m_state00D0;
        std::byte m_padding00D1[0x7];
        void *m_pState00D8;
        std::uint16_t m_state00E0;
        std::uint16_t m_state00E2;
        std::byte m_padding00E4[0x4];
    };

    static_assert(sizeof(C_AnimalSpawner) == 0xE8,
                  "C_AnimalSpawner size mismatch");
} // namespace SDK::wh::xgenaimodule
