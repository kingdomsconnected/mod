#pragma once

#include "core/feature_registry.h"
#include "core/ui/native_ui/native_ui.h"

#include "sdk/cryengine/action/i_action_map.h"
#include "sdk/cryengine/action/i_action_map_manager.h"
#include "sdk/cryengine/action/i_game_object.h"
#include "sdk/cryengine/action/s_view_params.h"
#include "sdk/cryengine/system/cry_name.h"
#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/framework/e_ui_apse_view.h"
#include "sdk/wh/guimodule/c_ui_apse.h"
#include "sdk/wh/rpgmodule/s_character_body_description.h"

#include "shared/features/player/player_appearance.h"
#include "shared/game/appearance_catalog.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace KCDC::Features::CharacterCreator {
    /**
     * The character creator: the game's own character stage, with a body
     * the player dresses standing on it.
     *
     * The pause screen's character page frames a figure -- `DummyPlayer`,
     * an `InventoryDummyPlayer` standing in the level-placed
     * `Prefabs/ui/Apse.xml` -- under its own lights, and every show of the
     * page dresses that figure as the player through
     * `C_Actor::CopyForInventoryPreview`. While a creator is open, the copy
     * runs as it always does and is then finished differently: the figure's
     * clothes come off and the look being edited goes on the look-as layer
     * the copy wrote the player's to. The page is opened on the player view
     * and held there, its own panels are hidden, the camera is moved to
     * frame the figure, and this service's panel stands where the skills
     * panel was.
     *
     * The prefab only places a man. A woman is an
     * `InventoryDummyPlayerFemale` spawned once where he stands. Every show
     * of the page binds him into its figure slot afresh and then registers,
     * places and turns whoever is in it, so she is put in the slot between
     * the two, and he is hidden while she stands there. Changing gender
     * repeats that in place, with the screen still up.
     *
     * Client-only: a resource opens it from a client script and hears
     * once, through `characterCreatorClosed`, how it ended and what look
     * was accepted. Nothing here talks to the server; what the look means
     * -- and whether it goes anywhere -- is the resource's to decide.
     *
     * Game thread only.
     */
    class CharacterCreatorService final
        : public KCDC::Core::IFeatureService
        , public SDK::IActionListener {
      public:
        /** Which bodies a creator offers, as bits of `OpenRequest::genders`. */
        static constexpr std::uint8_t kMale   = 1U << 0U;
        static constexpr std::uint8_t kFemale = 1U << 1U;
        static constexpr std::uint8_t kBoth   = kMale | kFemale;

        /** The longest heading a creator takes, in bytes. */
        static constexpr std::size_t kMaxTitle = 64;

        /** How a creator ended, as `characterCreatorClosed` names it. */
        enum class CloseReason : std::uint8_t {
            /** The player accepted the look. The only ending that carries one. */
            Accepted,
            /** The player backed out. */
            Cancelled,
            /** `CharacterCreator.close`. */
            Closed,
            /** Another `CharacterCreator.open` took its place. */
            Replaced,
            /** The stage could not be put up: no body, no woman's figure, or a screen that never came free. */
            Unavailable,
            /** The session or the body went away under it. */
            Interrupted,
        };

        static const char *CloseReasonName(CloseReason reason);

        /** What a creator opens on. Checked by the caller: a look the catalog does not have, or of a gender not offered, is not one. */
        struct OpenRequest {
            KCDC::Shared::Game::CharacterAppearance appearance;
            std::uint8_t genders = kBoth;
            std::string title;
        };

        static CharacterCreatorService &Get();

        /** Opens a creator, ending one already open as replaced. */
        void Open(const OpenRequest &request);

        /** Ends the open creator as closed; false when none is. */
        bool Close();

        bool IsOpen() const {
            return _phase != Phase::Idle;
        }

        /** Whether `gender` is one of `genders`. */
        static bool Offers(std::uint8_t genders, KCDC::Shared::Game::AppearanceGender gender) {
            return (genders & (gender == KCDC::Shared::Game::AppearanceGender::Female ? kFemale : kMale)) != 0;
        }

        void Update() override;
        void Reset() override;

        void OnAction(const SDK::CCryName &action, int activationMode, float value) override;
        void AfterAction() override {}

        // --- Questions the hooks ask -------------------------------------------

        /**
         * `CopyForInventoryPreview` has just dressed `dummy` as the player.
         * When it is the figure of an open creator, its clothes come off and
         * the creator's look goes over the player's.
         */
        void DressFigure(SDK::wh::entitymodule::C_Actor *dummy);

        /**
         * The screen's camera has just written its pose. While a creator
         * holds the stage, the camera is moved so the figure fills the free
         * side of the screen -- his whole height on the body tab, his head
         * on the others -- easing between the two as the tab changes.
         */
        void FrameStage(SDK::SViewParams &params);

        /** Whether the pause screen may move to `view`. Only to the player view while a creator holds it. */
        bool AllowsViewChange(SDK::wh::framework::E_UIApseView::Type view) const;

        /**
         * The pause screen is about to close. True when that ends an open
         * creator -- the player backed out -- in which case the close is made
         * immediate, so the stage can be put back the moment it returns.
         */
        bool ClaimsHide() const;

        /** The close `ClaimsHide` claimed has run: the creator ends as cancelled. */
        void OnApseCancelled();

        /**
         * The page has just bound its stage, as every show does before it
         * dresses and places the figure. Records the prefab's man, and puts
         * the creator's figure in his slot while one is open.
         */
        void OnStageBound(SDK::wh::guimodule::C_UIApseCharacter *character);

      private:
        enum class Phase : std::uint8_t {
            Idle,

            /** Asked for: waiting for the panel to compose and the screen to come free. */
            Opening,

            /** The screen is up on the stage and the panel over it. */
            Shown,
        };

        /** The panel's tabs, one per part of the body. A woman has no beard tab. */
        enum class Tab : std::uint8_t {
            Body,
            Face,
            Hair,
            Beard,
        };

        /**
         * A family of choices for one part: a hair style and its colours, a
         * skin tone and its variants, a face and its versions. The catalog
         * already groups them -- a variant names its family in `group`, and a
         * family's base carries no group -- so a family is keyed by the group,
         * or by the base's own name. The first family of every part is the
         * soul's own, a single empty name.
         */
        struct Family {
            std::string_view key;
            std::vector<std::string_view> members;
        };

        /** Where a part's current name sits among its families. */
        struct Placement {
            std::size_t family = 0;
            std::size_t member = 0;
        };

        /** The open tab's part, its families and where the look sits in them: what every row reads, worked out once. */
        struct Selection {
            KCDC::Shared::Game::AppearancePart part;
            std::vector<Family> families;
            Placement at;

            /** The family row, and the member row when the family has a choice. */
            std::size_t Rows() const {
                return families[at.family].members.size() > 1 ? 2 : 1;
            }
        };

        /** One drawn row of a tab, and what it last showed, so an unchanged row costs no call. */
        struct Line {
            std::string label;
            std::string previous;
            std::string value;
            std::string next;
            std::string count;

            /** A drawing clip under the value: the position bar, or the colour swatches. */
            std::string graphics;

            std::string shownLabel;
            std::string shownValue;
            std::string shownCount;
            std::string shownGraphics;
            int shownArrows = -1;
        };

        /** A tab shows a family row and, when the family has more than one member, a member row. */
        static constexpr std::size_t kMaxLines = 2;
        static constexpr std::size_t kTabCount = 4;

        /** Ends the creator, taking the screen down first if it is up. */
        void Finish(CloseReason reason);

        /** Ends the creator once the screen is already down: the stage goes back and the ending is held for `ReportEnding`. */
        void Conclude(CloseReason reason);

        /**
         * Tells resources how the last creator ended. Never from `Conclude`
         * itself, which also runs inside the engine's `I_UIApse::Hide`: the
         * event waits for the next tick, or for whichever of `Open` and
         * `Reset` comes first.
         */
        void ReportEnding();
        bool StillAllowed() const;
        void TickOpening();

        // --- The stage ------------------------------------------------------------

        static SDK::wh::guimodule::C_UIApseCharacter *Character();

        /** The prefab's own figure, resolved afresh: the pointer in the page's slot may be ours. */
        SDK::wh::entitymodule::C_Actor *PrefabDummy() const;
        SDK::wh::entitymodule::C_Actor *FemaleDummy() const;

        /** Spawns the woman where the prefab's man stands, once per level. */
        SDK::wh::entitymodule::C_Actor *EnsureFemaleDummy();

        /** Puts the figure for `gender` in the page's slot and hides the other, before the page registers whoever is in it. */
        bool StageFigure(KCDC::Shared::Game::AppearanceGender gender);

        /** Hides the panels the screen draws its pages on, and shows the ones it hid again. */
        void HidePlates();
        void ShowPlates();

        /** Gives the page back its own figure, and him back his own look. */
        void RestoreStage();

        /** The figure the page shows, when it is one of the two this service stages. */
        SDK::wh::entitymodule::C_Actor *Figure() const;

        /** Writes one part of the look onto a figure, on the layer the copy writes. */
        void DressPart(SDK::wh::entitymodule::C_Actor *figure, KCDC::Shared::Game::AppearancePart part);

        /** What a part left to the soul is on the woman: her own, else the catalog's first. Empty on the man, whose soul is the player's. */
        std::string FemaleFallback(SDK::wh::entitymodule::C_Actor *figure, std::uint8_t part) const;

        // --- The look -------------------------------------------------------------

        /** The part a tab edits. */
        static KCDC::Shared::Game::AppearancePart PartOf(Tab tab);

        /** How many tabs the look's gender has: they are `Tab`'s first values, the beard last and a man's only. */
        std::size_t TabCount() const;
        Tab CurrentTab() const;

        /** Every family a part can take for the current gender, the soul's own first; beards only those the face carries. */
        std::vector<Family> Families(KCDC::Shared::Game::AppearancePart part) const;
        static Placement Locate(const std::vector<Family> &families, std::string_view name);
        Selection Select() const;

        /**
         * Steps the current tab's row. A family step keeps the member's own
         * name where the new family has it -- a hair colour survives a change
         * of style -- and falls back to the family's first member.
         */
        void Step(int direction);

        /** Puts one name on a part and dresses the figure, dropping a beard the face does not carry. */
        void SetPart(KCDC::Shared::Game::AppearancePart part, std::string_view name);

        /** A random family and member for every part the gender has. */
        void Randomize();

        /**
         * Replaces the whole look. Another gender swaps the figure on the
         * stage the way a page hide and show do; the same gender is dressed
         * in place.
         */
        void ApplyLook(const KCDC::Shared::Game::CharacterAppearance &look);

        // --- The panel ------------------------------------------------------------

        void EnsureScreen();
        bool Compose();
        void ForgetComposition();
        std::shared_ptr<KCDC::Core::NativeUI> Page() const;
        void ShowPanel(bool visible);
        void Refresh();
        void RefreshTabs();
        void RefreshLine(KCDC::Core::NativeUI &page, Line &line, std::size_t index, const Selection &selection);

        /** The creator's action map, created from `kMapXml` and left disabled when it does not exist yet. */
        SDK::IActionMap *EnsureInputMap();
        void AcquireInput();
        void ReleaseInput();

        Phase _phase = Phase::Idle;

        KCDC::Shared::Game::CharacterAppearance _look;
        std::uint8_t _genders = kBoth;

        /** How the last creator ended, until resources have been told. */
        std::optional<CloseReason> _ending;
        std::string _title;

        /** The look the creator opened on, which Reset goes back to. */
        KCDC::Shared::Game::CharacterAppearance _openedLook;

        /** The tab shown and the row selected in it. */
        std::size_t _tab = 0;
        std::size_t _row = 0;

        /** The camera's framing as it eases between the body and the head: metres of height on screen, and their centre above the feet. */
        float _frameHeight = 0.0F;
        float _frameCentre = 0.0F;

        /** The screen is being closed by this service; its hide is not the player backing out. */
        bool _closingApse = false;

        /** Ticks spent waiting to show, so a screen that never comes free ends in `Unavailable`. */
        std::uint32_t _openingTicks = 0;

        /** Parts were written on the prefab's man, to be cleared off him when the creator ends. */
        bool _dressedPrefab = false;

        /** The prefab's man was hidden by this service, and is shown again when it lets go. */
        bool _hidPrefab = false;

        /** The show found no figure for the look's gender; the creator ends on the next tick. */
        bool _figureMissing = false;

        SDK::EntityId _prefabDummyId = 0;
        SDK::EntityId _femaleDummyId = 0;

        /**
         * The woman's own five parts, read as she spawns. The page's copy
         * gives her soul the player's archetype (`CopyFromSoul` copies the
         * block holding soul+0x374), after which every part left to the soul
         * resolves to a man's default; these are written in their place.
         */
        std::array<std::string, SDK::wh::rpgmodule::S_CharacterBodyDescription::kPartCount> _femaleOwnParts;

        /**
         * Her soul's archetype, read as she spawns. The copy overwrites the
         * id at soul+0x374 with the player's and refreshes, turning her soul
         * male: his look then copies onto her, and a woman's part names stop
         * resolving at all. `SetArchetypeId` (slot 176) puts hers back.
         */
        int _femaleArchetypeId = -1;

        /** The stage panels this service hid, shown again when the creator ends. */
        std::vector<SDK::EntityId> _hiddenPlates;

        /** Input the engine delivered since the last tick: tabs, rows, steps, whole-look keys, accept, cancel. */
        int _pendingRow     = 0;
        int _pendingStep    = 0;
        int _pendingTab     = 0;
        bool _pendingGender = false;
        bool _pendingRandom = false;
        bool _pendingReset  = false;
        bool _pendingAccept = false;
        bool _pendingCancel = false;

        std::uint32_t _screen = 0;
        bool _composed        = false;
        bool _failed          = false;
        bool _dirty           = true;

        /** Stage units per layout unit, the stage's size, and the frame's height in layout units. */
        float _unit        = 1.0F;
        float _stageWidth  = 1920.0F;
        float _stageHeight = 1080.0F;
        float _panelHeight = 0.0F;

        std::string _root;
        std::string _mark;
        std::string _heading;
        std::string _genderMale;
        std::string _genderFemale;
        std::string _genderMark;
        std::array<std::string, kTabCount> _tabLabels;
        std::string _tabMark;
        std::vector<Line> _lines;
        std::string _shownHeading;
        int _shownMark   = -1;
        int _shownGender = -1;
        std::string _shownTabs;

        SDK::IActionMapManager *_inputManager = nullptr;
        SDK::IActionMap *_inputMap            = nullptr;
        bool _listening                       = false;
    };
} // namespace KCDC::Features::CharacterCreator
