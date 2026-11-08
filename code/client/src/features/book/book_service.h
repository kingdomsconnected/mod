#pragma once

#include "core/feature_registry.h"

#include "sdk/cryengine/system/xml_node_ref.h"
#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_document_item.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/guimodule/c_ui_book.h"
#include "sdk/wh/playermodule/c_reading.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace KCDC::Features::Book {
    /** One picture on a page, as the book movie's `Images` array carries it. */
    struct BookImage {
        std::uint32_t page = 0;
        std::string image;
    };

    /**
     * A book a resource composed: its pages, how it looks, and which vanilla
     * document carries it into the player's hands.
     */
    struct BookDefinition {
        /** Which unreferenced vanilla document lends its model and cover. */
        enum class Carrier : std::uint8_t {
            /** `fairyStory_HouseOfRuthard`: book_01.cdf, red cover. */
            Book,
            /** `utokNebakov_poetryProkopFlorian`: book_01.cdf, plain cover. */
            PlainBook,
            /** `poustevnik_oldPaperInquisitionAmbroz`: folded parchment. */
            Letter,
        };

        Carrier carrier = Carrier::Book;

        /** `document_class` id the movie lays out for: 1 book, 2 recipe, 3 skill book, 4 map, 5 letter, 6 plan. */
        int documentType = 1;

        /** `DocumentVisualCategory` id: 1 plain ... 7 embellished. */
        int visualCategory = 1;

        /**
         * The movie's `DyslexiaLevel`, 0 fully scrambled to 1 fully legible.
         * GeneralBook keeps a character class readable while `level^0.25`
         * is above that class's threshold (`(k/10)^0.25`), so the level
         * takes effect in tenths, and the substitution is deterministic.
         */
        float legibility = 1.0F;

        /**
         * A CryPak path replacing the carrier's diffuse texture (cover and
         * pages, one atlas), or empty to keep the vanilla one. It must follow
         * the carrier's UV layout: `book_alchemy_diff` for the books,
         * `scroll_diff` for the letter.
         */
        std::string texture;

        /**
         * One string per page, in GeneralBook markup; a page break follows
         * each but the last. `Open` moves them into the text the
         * localization detour serves, so they are held once.
         */
        std::vector<std::string> pages;

        /** Pictures for the skill-book and map layouts, by name under the game's own Books folders. */
        std::vector<BookImage> images;
    };

    /**
     * Opens resource-composed books through the game's own reading
     * minigame, so the book in hand, its camera, the page-turn animations,
     * the prompts and the pagination are all the native ones.
     *
     * A book borrows a carrier: one of three vanilla documents no table,
     * placement, Lua or quest graph refers to, created in world inventory
     * for the session and spawned into the hands by `C_Reading::Start`.
     * What the book shows is replaced at `C_DocumentItem::PushContentToUI`,
     * the one point every document hands its content to the screen; the
     * carrier's first read is kept away from the RPG layer; and the carrier
     * is deleted again once the minigame ends.
     *
     * Page text is served through the localization detour as
     * `kcdc_book_<id>_<page>`, so it never passes through a localization
     * file and any markup the movie understands works.
     *
     * Client-only: nothing here crosses the network. A book is local to the
     * machine that opened it.
     */
    class BookService final: public KCDC::Core::IFeatureService {
      public:
        static constexpr std::size_t kMaxPages      = 512;
        static constexpr std::size_t kMaxPageLength = 64 * 1024;
        static constexpr std::size_t kMaxImages     = 64;

        /** How long a requested book may take to reach the hands before it is given up on. */
        static constexpr std::uint64_t kOpenTimeoutMs = 5000;

        static BookService &Get();

        /**
         * Opens `definition` in the local player's hands. Returns the book's
         * id, or 0 when there is no body, a book is already open, the player
         * cannot read where they are, or the game refused it; `GetLastError`
         * then says which.
         */
        std::uint32_t Open(BookDefinition definition);

        /** Why the last `Open` returned 0; empty after one that succeeded. */
        const std::string &GetLastError() const {
            return _lastError;
        }

        /** Closes the open book the way the player's own exit does. False when none is open. */
        bool Close();

        /**
         * Changes the open book's legibility (clamped to 0..1). The movie
         * applies it while laying the text out, so the whole book is laid
         * out again. False when no book is in the player's hands yet.
         */
        bool SetLegibility(float legibility);

        /** The id of the book in the player's hands, or 0. */
        std::uint32_t GetOpenBookId() const;

        /** Answers `kcdc_book_<id>_<page>` from the localization detour; any thread. */
        bool ResolveLabel(const char *label, SDK::CryStringT<char> &out) const;

        /**
         * Gives the reading minigame the forge builder's exit: a tap of the
         * `back` key (Escape) closes the book instead of opening the pause
         * menu on release, and a hold still closes it too. Applied to the
         * game's own config as `CXmlUtils::LoadXmlFromFile` reads it, so the
         * action map and its hint are built from data exactly as if Warhorse
         * had authored them that way, and a profile reload keeps it. True
         * when `file` was one of the two documents and `document` now holds
         * the edited copy.
         */
        static bool ExtendGameConfig(const char *file, SDK::XmlNodeRef &document);

        // --- Native detours ---------------------------------------------

        /** True when `document` is this service's carrier and its content was pushed. */
        bool PushContent(SDK::wh::entitymodule::C_DocumentItem *document);

        /** True when `item` is the open book's carrier, whose first read the RPG layer must not see. */
        bool IsCarrier(const SDK::wh::entitymodule::C_Item *item) const;

        /** The reading minigame ended; called after the native `Finish`. */
        void ReadingFinished(SDK::wh::playermodule::C_Reading *reading);

        /**
         * True when `reading` is reading this service's carrier. Such a
         * book never came out of the inventory, so the native return to
         * the inventory after it (`C_Reading::ReturnToInventory`) must not run.
         */
        bool IsReadingCarrier(const SDK::wh::playermodule::C_Reading *reading) const;

        // --- IFeatureService ---------------------------------------------

        void Update() override;
        void Reset() override;
        void NativeContextChanged() override;

      private:
        BookService() = default;

        enum class Phase : std::uint8_t {
            Idle,
            /** `Start` accepted the carrier; the pickable is on its way to the hands. */
            Opening,
            Open,
            /** The minigame finished; the carrier goes on the next tick, outside the native stack. */
            Closing,
        };

        struct Session {
            Phase phase                               = Phase::Idle;
            std::uint32_t id                          = 0;
            std::uint64_t carrierWuid                 = 0;
            SDK::wh::playermodule::C_Reading *reading = nullptr;
            std::uint64_t requestedAtMs               = 0;
            const char *closeReason                   = "player";
            std::size_t pageCount                     = 0;
            /** What `Open` was given, less the pages: those live in `_text`. */
            BookDefinition definition;
        };

        SDK::wh::entitymodule::C_Item *FindCarrier() const;
        void SendLayout(SDK::wh::guimodule::C_UIBook *screen) const;
        void ApplyTexture(SDK::wh::entitymodule::C_DocumentItem *document) const;
        std::uint64_t CreateCarrier(BookDefinition::Carrier carrier);
        void DeleteCarrier(std::uint64_t carrierWuid) const;
        void EndSession(const char *reason);

        /** What the localization detour reads, possibly off the main thread. */
        struct PublishedText {
            std::uint32_t id = 0;
            std::vector<std::string> pages;
        };

        void PublishText(std::uint32_t id, std::vector<std::string> pages);

        // The session is touched only from the main thread: the script
        // binding, the feature sweep and the native detours all run there,
        // and a native `Start` can re-enter `PushContent` synchronously, so
        // nothing may hold a lock across a native call. Only the page text
        // is shared, and it has its own lock.
        Session _session;
        std::uint32_t _nextId = 1;
        std::string _lastError;

        mutable std::mutex _textMutex;
        PublishedText _text;
    };
} // namespace KCDC::Features::Book
