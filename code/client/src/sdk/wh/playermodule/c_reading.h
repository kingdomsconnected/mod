#pragma once

#include "../../patterns.h"
#include "../../types.h"
#include "../entitymodule/c_actor.h"
#include "../entitymodule/s_item_class_guid.h"
#include "../framework/i_ui_book.h"
#include "../framework/wuid.h"
#include "../shared/c_signal.h"
#include "c_minigame.h"
#include "e_minigame_type.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * The reading minigame: one per actor, created on first use by
     * `C_PlayerManager::CreateMinigame` (kind 2, 0xE8 bytes, constructor
     * 0x182EA60E0) and kept registered afterwards -- its unregister slot (33)
     * is a no-op, so `Finish` is the whole of an ending.
     *
     * Every way into a book converges on `Start`: the inventory "Read" action
     * (`C_Player::ReadBookFromInventory`, spawn true) and a book on a stand
     * (`C_Human::StartReading`, spawn false). `Update` (slot 19) finishes the
     * session by itself once the book's item stops resolving.
     *
     * Leaving through `minigame_exit` runs `RequestExit` (slot 46, 0x182EAE478):
     * `Finish`, then `ReturnToInventory` (0x182EAF924). For a spawned book that
     * reopens the APSE inventory on it, which is how a book read from the
     * inventory hands the player back to the inventory.
     */
    class C_Reading: public C_Minigame {
      public:
        /**
         * The actor's reading minigame, created when missing and `create` is
         * set (`C_Actor::GetMinigame`, 0x1804CEDF8). Null otherwise.
         */
        static C_Reading *Of(entitymodule::C_Actor *actor, bool create) {
            return static_cast<C_Reading *>(hook::this_call<C_Minigame *>(gPatterns.C_Actor__GetMinigame, actor, E_MinigameType::Reading, create, false));
        }

        /**
         * Opens the document item `book` (0x182EB1184). It must be item class
         * 8; anything else is refused silently. `spawnBook` true spawns the
         * item's pickable into the actor's hands -- the inventory path, which
         * accepts an item held by any inventory -- and false binds the
         * pickable the item already has in the world.
         */
        void Start(framework::WUID book, bool spawnBook) {
            hook::this_call<void>(gPatterns.C_Reading__Start, this, book, spawnBook);
        }

        /**
         * The native end of a session, primary slot 5 (0x182EB26D8): input
         * context, camera, skip-time and obstacle torn down, and a spawned
         * pickable unbound from its item. The item itself is left where it was.
         */
        void Finish() {
            hook::this_call<void>(gPatterns.C_Reading__Finish, this);
        }

        /** The book's item, as `Start` stored it; `Update` re-resolves it every frame. */
        framework::WUID GetBookWuid() const {
            return m_bookWuid;
        }

        /** True when `Start` spawned the pickable in hand rather than binding a placed one. */
        bool IsSpawnedBook() const {
            return m_spawnBook;
        }

        /** The shared book screen, linked in by `C_UIBook::OnMinigameAdded`. */
        framework::I_UIBook *GetBookUI() const {
            return m_pBookUI;
        }

      private:
        SDK::wh::shared::C_Signal m_signal0068; // 0x68; ctor 0x182EA60E0 dispatch unk_185669450.
        SDK::wh::shared::C_Signal m_signal0078; // 0x78; dispatch unk_185669B50.
        SDK::wh::shared::C_Signal m_signal0088; // 0x88; dispatch unk_185669B50.
        SDK::wh::shared::C_Signal m_signal0098; // 0x98; dispatch unk_185669B50.
        framework::WUID m_bookWuid;                // 0xA8
        entitymodule::S_ItemClassGUID m_bookClass; // 0xB0
        std::uint32_t m_readingDuration;           // 0xC0, the descriptor's +0xD0
        std::byte m_opaque00C4[0xC];
        bool m_spawnBook; // 0xD0
        std::byte m_padding00D1[0x7]; // ctor 0x182EA60E0 zeroes +0xD0 as a dword
        framework::I_UIBook *m_pBookUI; // 0xD8
        std::int32_t m_value00E0; // 0xE0; ctor writes -1.
        std::byte m_padding00E4[0x4];

        static void AssertLayout() {
            static_assert(offsetof(C_Reading, m_bookWuid) == 0xA8, "C_Reading::m_bookWuid offset mismatch");
            static_assert(offsetof(C_Reading, m_bookClass) == 0xB0, "C_Reading::m_bookClass offset mismatch");
            static_assert(offsetof(C_Reading, m_readingDuration) == 0xC0, "C_Reading::m_readingDuration offset mismatch");
            static_assert(offsetof(C_Reading, m_spawnBook) == 0xD0, "C_Reading::m_spawnBook offset mismatch");
            static_assert(offsetof(C_Reading, m_pBookUI) == 0xD8, "C_Reading::m_pBookUI offset mismatch");
            static_assert(offsetof(C_Reading, m_signal0068) == 0x68, "C_Reading::m_signal0068 offset mismatch");
            static_assert(offsetof(C_Reading, m_signal0098) == 0x98, "C_Reading::m_signal0098 offset mismatch");
            static_assert(offsetof(C_Reading, m_value00E0) == 0xE0, "C_Reading::m_value00E0 offset mismatch");
        }
    };

    static_assert(sizeof(C_Reading) == 0xE8, "C_Reading size mismatch");
} // namespace SDK::wh::playermodule
