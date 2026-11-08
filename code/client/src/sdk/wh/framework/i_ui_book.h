#pragma once

#include "../../cryengine/entitysystem/i_entity.h"
#include "../entitymodule/s_document_content.h"

namespace SDK::wh::framework {
    /**
     * The readable-book screen as a document and the reading minigame see it.
     *
     * One object serves every book: `C_UIBook` implements this at +0x58 with
     * the table at 0x183D2BE08 and links itself to whichever reading (kind 2)
     * or alchemy (kind 3) minigame exists, writing this pointer into
     * `C_Reading+0xD8` and `C_Alchemy+0x6C8`. `C_DocumentItem::BindBookUI`
     * copies it to the document at +0xD8.
     *
     * Slots 6-10 drive AlchemyBook.gfx, the rest GeneralBook.gfx (their
     * declarations are in `Libs/UI/UIElements/*.xml`). The mod reaches the
     * screen through `C_UIBook` rather than this table.
     */
    class I_UIBook {
      public:
        /** Slot 0, the scalar deleting destructor. Never called from here. */
        virtual void unk_fn0000() = 0;

        /**
         * Slot 1 (0x182BBAE84): `Texts` from `content->parts` (each through
         * `wh::framework::Localize`, so as `@key`), `Images` from
         * `content->images`, then `fc_setFullBook(DyslexiaLevel, Type,
         * VisualCategory)`.
         */
        virtual void SetFullBook(const entitymodule::S_DocumentContent *content, float dyslexiaLevel, int documentType, int visualCategory) = 0;

        /** Slots 2-5 (0x182BB9CC0, 0x182BBAA74, 0x182BB9CF8, 0x182BBAAB0): the page-turn animation brackets. */
        virtual void NextPageBegin() = 0;
        virtual void PrevPageBegin() = 0;
        virtual void NextPageEnd()   = 0;
        virtual void PrevPageEnd()   = 0;

        /**
         * Slot 6 (0x1808C3E30): AlchemyBook `SetRecipe(Id, Name, Description,
         * IsOnRight, DyslexiaLevel)`, after `Steps`. The book page render
         * (0x1808C3148) passes the RecipeId as decimal text, the row's UIName
         * and UIUsage (each through `wh::framework::Localize` here), and the
         * steps it collected, a game-owned vector of step pointers. The movie
         * builds both of the page's pictures from `id`:
         * `Libs/UI/Textures/Books/Products/<id>_ui.dds` and `Recipes/<id>_ui.dds`.
         */
        virtual void SetRecipe(const CryStringT<char> &id, const CryStringT<char> &name, const CryStringT<char> &usage, const void *steps, bool isOnRight, float dyslexiaLevel) = 0;

        /** Slot 7 (0x182BBAE44): AlchemyBook `SetEmptyRecipe(IsOnRight)`. */
        virtual void SetEmptyRecipe(bool isOnRight) = 0;

        /** Slot 8 (0x182BBAFD0): AlchemyBook `SetHerb`; native parameter list not recovered. */
        virtual void unk_fn0008() = 0;

        /** Slots 9-10 (0x180A96F00, 0x182BB8C54): AlchemyBook page copies. */
        virtual void CopyContentFromRightSideToLeftSide() = 0;
        virtual void CopyContentFromLeftSideToRightSide() = 0;

        /**
         * Slot 11 (0x180A964A8), `BindToEntityTexture(CryStringT gfxName,
         * IEntity *entity, int texSlot)`: renders the movie onto the entity's
         * material as `<gfxName>@<entityId>.ui`. Left unnamed because the
         * string is taken by value through a caller-built temporary, which a
         * declaration over the trivial SDK `CryStringT` would pass in a
         * register instead.
         */
        virtual void unk_fn0011() = 0;

        /** Slot 12 (0x1819CA4AC): drops the render target when `entity` is the bound one. */
        virtual void UnbindFromEntity(SDK::IEntity *entity) = 0;

      protected:
        ~I_UIBook() = default;
    };
} // namespace SDK::wh::framework
