#include <utils/safe_win32.h>

#include "character_creator_service.h"

#include "core/application.h"
#include "core/ui/native_ui/native_ui.h"
#include "core/ui/native_ui/native_ui_host.h"
#include "features/character_creator/character_creator_scripting.h"
#include "features/world/world_service.h"
#include "common/spawn/actor_spawner.h"

#include "sdk/cryengine/action/i_action_map_action.h"
#include "sdk/cryengine/action/i_action_map_action_iterator.h"
#include "sdk/cryengine/action/i_actor_system.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_link.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/input/c_base_input.h"
#include "sdk/cryengine/input/c_keyboard.h"
#include "sdk/cryengine/system/i_localization_manager.h"
#include "sdk/cryengine/system/i_system.h"
#include "sdk/wh/c_wh_global_environment.h"
#include "sdk/wh/entitymodule/c_equipment_manager.h"
#include "sdk/wh/entitymodule/c_player.h"
#include "sdk/wh/framework/i_ui_apse.h"
#include "sdk/wh/framework/s_ui_apse_show_params.h"
#include "sdk/wh/guimodule/c_ui_action_hint_manager.h"
#include "sdk/wh/guimodule/c_ui_state_blockers.h"
#include "sdk/wh/guimodule/e_ui_state.h"
#include "sdk/wh/rpgmodule/c_soul.h"
#include "sdk/wh/rpgmodule/s_character_body_description.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <logging/logger.h>
#include <random>
#include <utility>

namespace KCDC::Features::CharacterCreator {
    namespace {
        constexpr const char *kLogger = "CharacterCreator";

        constexpr char kResourceName[] = "kcdc-character-creator";
        constexpr char kMapName[]      = "kcdc_character_creator";

        /** Input action activation flags. */
        constexpr int kOnPress = 1 << 0;
        constexpr int kOnHold  = 1 << 2;

        /**
         * Exclusive, at `menu` (11): an enabled exclusive map deactivates
         * every map of strictly lower priority, which takes the pause
         * screen's own `apse` (7) and `apse_player` (8) out of play -- the
         * latter's `focus_prev`/`focus_next` sit on the same arrows and would
         * walk the skills list under the panel. With `apse:close` gone too,
         * Escape is the creator's own cancel. Keys are named by position, as
         * CryInput names them, so `q`/`e` are the two either side of the
         * movement keys on any layout. The arrows repeat while held.
         */
        constexpr char kMapXml[] = "<actionmap name=\"kcdc_character_creator\" priority=\"menu\" exclusivity=\"1\">"
                                   "<action name=\"kcdc_cc_up\" onPress=\"1\" onHold=\"1\" holdTriggerDelay=\"0.4\" holdRepeatDelay=\"0.12\" keyboard=\"up\"/>"
                                   "<action name=\"kcdc_cc_down\" onPress=\"1\" onHold=\"1\" holdTriggerDelay=\"0.4\" holdRepeatDelay=\"0.12\" keyboard=\"down\"/>"
                                   "<action name=\"kcdc_cc_left\" onPress=\"1\" onHold=\"1\" holdTriggerDelay=\"0.4\" holdRepeatDelay=\"0.06\" keyboard=\"left\"/>"
                                   "<action name=\"kcdc_cc_right\" onPress=\"1\" onHold=\"1\" holdTriggerDelay=\"0.4\" holdRepeatDelay=\"0.06\" keyboard=\"right\"/>"
                                   "<action name=\"kcdc_cc_tab_prev\" onPress=\"1\" keyboard=\"q\"/>"
                                   "<action name=\"kcdc_cc_tab_next\" onPress=\"1\"><keyboard><inputdata input=\"e\"/><inputdata input=\"tab\"/></keyboard></action>"
                                   "<action name=\"kcdc_cc_gender\" onPress=\"1\" keyboard=\"g\"/>"
                                   "<action name=\"kcdc_cc_random\" onPress=\"1\" keyboard=\"r\"/>"
                                   "<action name=\"kcdc_cc_reset\" onPress=\"1\" keyboard=\"backspace\"/>"
                                   "<action name=\"kcdc_cc_accept\" onPress=\"1\"><keyboard><inputdata input=\"enter\"/><inputdata input=\"np_enter\"/></keyboard></action>"
                                   "<action name=\"kcdc_cc_cancel\" onPress=\"1\" keyboard=\"escape\"/>"
                                   "</actionmap>";

        /**
         * How the camera frames the figure: metres of height on screen and
         * their centre above his feet -- his whole height on the body tab,
         * his head on the others -- with him this far across the screen, in
         * the free space left of the panel. Each frame closes this share of
         * the way to the tab's framing.
         */
        constexpr float kBodyFrameHeight   = 2.1F;
        constexpr float kBodyFrameCentre   = 0.95F;
        constexpr float kHeadFrameHeight   = 0.62F;
        constexpr float kMaleHeadCentre    = 1.62F;
        constexpr float kFemaleHeadCentre  = 1.52F;
        constexpr float kFigureScreenX     = 0.33F;
        constexpr float kFrameEaseFraction = 0.14F;

        /** The stage hub whose links are the panels the screen draws its pages on. */
        constexpr char kPlatesHubLink[] = "FlashPlatesHub";

        /** The prefab places no woman; this class is the one the game ships for her (`Scripts/Entities/AI/InventoryDummyPlayerFemale.lua`). */
        constexpr char kFemaleDummyClass[] = "InventoryDummyPlayerFemale";

        /** The soul archetype that class names, `NPC_Female.lua`: it decides the soul's default body, head and hair. */
        constexpr char kFemaleSoulArchetype[] = "NPC_Female";

        /** How long a creator may wait for the screen to come free before it gives up. */
        constexpr std::uint32_t kMaxOpeningTicks = 600;

        /** The tutorial note's frame, as the player list uses it, and the codex's flourish. */
        constexpr char kTopTexture[]      = "img://Libs/UI/Textures/tutorial_top_wide.dds";
        constexpr char kMiddleTexture[]   = "img://Libs/UI/Textures/tutorial_middle_wide.dds";
        constexpr char kBottomTexture[]   = "img://Libs/UI/Textures/tutorial_bottom_wide.dds";
        constexpr char kFlourishTexture[] = "img://Libs/UI/Textures/skillBook_line_ornament.dds";

        constexpr float kTextureWidth      = 1024.0F;
        constexpr float kTopTextureHeight  = 512.0F;
        constexpr float kTileTextureHeight = 128.0F;
        constexpr float kFlourishWidth     = 128.0F;

        /** Layout units: a 1080-line screen, converted on the root. */
        constexpr float kLayoutHeight = 1080.0F;

        constexpr float kFrameScale      = 0.6F;
        constexpr float kFrameWidth      = kTextureWidth * kFrameScale;
        constexpr float kTopHeight       = kTopTextureHeight * kFrameScale;
        constexpr float kTileHeight      = kTileTextureHeight * kFrameScale;
        constexpr float kBottomHeight    = kTileTextureHeight * kFrameScale;
        constexpr float kBottomParchment = 36.0F * kFrameScale;
        constexpr std::size_t kMaxTiles  = 4;

        /** The panel stands in the right third, where the skills panel was. */
        constexpr float kPanelMargin = 96.0F;

        /** The parchment starts 70 px inside the texture's edge; the writing sits a margin inside that. */
        constexpr float kLeft  = -0.5F * kFrameWidth + 74.0F;
        constexpr float kRight = 0.5F * kFrameWidth - 74.0F;
        constexpr float kWidth = kRight - kLeft;

        // The panel, top to bottom.
        constexpr float kTitleTop      = 84.0F;
        constexpr float kFlourishTop   = 128.0F;
        constexpr float kGenderTop     = 150.0F;
        constexpr float kTabsTop       = 196.0F;
        constexpr float kTabsRuleTop   = 230.0F;
        constexpr float kRowsTop       = 248.0F;
        constexpr float kRowHeight     = 76.0F;
        constexpr float kHintsGap      = 10.0F;
        constexpr float kHintRow       = 30.0F;
        constexpr float kHintGap       = 16.0F;
        constexpr float kCapPadding    = 7.0F;
        constexpr float kCapGap        = 4.0F;
        constexpr float kHintTextWidth = 240.0F;

        constexpr float kLabelWidth = 110.0F;
        constexpr float kArrowWidth = 24.0F;
        constexpr float kValueLeft  = kLeft + kLabelWidth;
        constexpr float kValueRight = kRight - 4.0F;
        constexpr float kValueWidth = kValueRight - kValueLeft;

        /** Under each value: the position bar, or the swatches when the members are hair colours. */
        constexpr float kGraphicsTop = 40.0F;
        constexpr float kBarInset    = 30.0F;
        constexpr float kBarHeight   = 4.0F;
        constexpr float kSwatchSize  = 22.0F;
        constexpr float kSwatchGap   = 6.0F;
        constexpr float kCountWidth  = 70.0F;

        constexpr double kTitleSize  = 34.0;
        constexpr double kGenderSize = 21.0;
        constexpr double kTabSize    = 17.0;
        constexpr double kLabelSize  = 17.0;
        constexpr double kValueSize  = 21.0;
        constexpr double kArrowSize  = 24.0;
        constexpr double kCountSize  = 13.0;
        constexpr double kKeySize    = 13.0;
        constexpr double kHintSize   = 14.0;

        constexpr int kTitleInk     = 0x2A1D0E;
        constexpr int kInk          = 0x3B2A16;
        constexpr int kFadedInk     = 0x7B6545;
        constexpr int kGold         = 0xB07A22;
        constexpr int kMarkFill     = 0xC8912E;
        constexpr int kMarkEdge     = 0x8A6A2A;
        constexpr double kMarkAlpha = 20.0;
        constexpr double kEdgeAlpha = 45.0;

        constexpr char kDefaultTitle[] = "Character";

        /** The tabs' names, in `Tab` order. */
        constexpr const char *kTabNames[] = {"Body", "Face", "Hair", "Beard"};

        /**
         * The key strip at the foot of the panel: the creator's actions whose
         * keys it shows, and what they do. The keys themselves are read off
         * the action map and named the way the game's own hints name them.
         */
        struct KeyHint {
            const char *actions[2];
            const char *text;

            /** Only shown when the server offers both genders: the key does nothing otherwise. */
            bool bothGenders = false;
        };

        constexpr KeyHint kKeyHints[] = {
            {{"kcdc_cc_tab_prev", "kcdc_cc_tab_next"}, "Tab"},
            {{"kcdc_cc_up", "kcdc_cc_down"}, "Choose"},
            {{"kcdc_cc_left", "kcdc_cc_right"}, "Change"},
            {{"kcdc_cc_gender", nullptr}, "Gender", true},
            {{"kcdc_cc_random", nullptr}, "Random"},
            {{"kcdc_cc_reset", nullptr}, "Reset"},
            {{"kcdc_cc_accept", nullptr}, "Accept"},
            {{"kcdc_cc_cancel", nullptr}, "Cancel"},
        };

        /**
         * Rebuilds CryInput's key-name table for the layout the game window
         * types with now. The engine builds it once, at keyboard init, from
         * whatever layout that thread had, and never follows a change -- on
         * an AZERTY machine it was left naming the keys as QWERTY does.
         */
        void FollowKeyboardLayout() {
            SDK::CBaseInput *input   = SDK::CBaseInput::GetInstance();
            SDK::CKeyboard *keyboard = input != nullptr ? input->GetKeyboard() : nullptr;
            if (keyboard == nullptr) {
                return;
            }
            const HWND window = KCDC::Core::gGlobals.window;
            const HKL layout  = GetKeyboardLayout(window != nullptr ? GetWindowThreadProcessId(window, nullptr) : 0);
            if (layout == nullptr || layout == keyboard->GetLayout()) {
                return;
            }
            Framework::Logging::GetLogger(kLogger)->info("Key names follow keyboard layout {:x} (were built for {:x})", reinterpret_cast<std::uintptr_t>(layout), reinterpret_cast<std::uintptr_t>(keyboard->GetLayout()));
            keyboard->ChangeLanguage(layout);
        }

        /** The game's hair colours, as the catalog's names end, and a swatch for each. */
        struct HairColour {
            std::string_view suffix;
            int rgb;
        };

        constexpr HairColour kHairColours[] = {
            {"dark_brown", 0x3B2618},
            {"light_brown", 0x7A5534},
            {"dark_ginger", 0x7A3A1E},
            {"dark_grey", 0x4F4C4A},
            {"black", 0x1C1714},
            {"blonde", 0xC9A86A},
            {"ginger", 0xA8552A},
            {"grey", 0x8A8783},
            {"white", 0xD8D4CC},
        };

        SDK::SFlashVarValue Coordinate(float value) {
            return SDK::SFlashVarValue::Double(static_cast<double>(value));
        }

        bool Place(KCDC::Core::NativeUI &page, const std::string &clip, float x, float y) {
            SDK::SFlashDisplayInfo placement;
            placement.SetX(x);
            placement.SetY(y);
            return page.SetDisplayInfo(clip, placement);
        }

        bool LoadTexture(KCDC::Core::NativeUI &page, const std::string &clip, const char *url, float x, float y, float xPercent, float yPercent, float alpha = 100.0F) {
            if (!page.LoadClip(clip, url)) {
                return false;
            }
            SDK::SFlashDisplayInfo placement;
            placement.SetX(x);
            placement.SetY(y);
            placement.SetXScale(xPercent);
            placement.SetYScale(yPercent);
            placement.SetAlpha(alpha);
            placement.SetVisible(true);
            return page.SetDisplayInfo(clip, placement);
        }

        /** A filled rectangle with an optional edge, added to whatever the clip already draws. */
        bool FillRectangle(KCDC::Core::NativeUI &page, const std::string &clip, float x, float y, float width, float height, int fill, double fillAlpha, int edge = -1, double edgeAlpha = 0.0) {
            bool ok = edge >= 0 ? page.InvokeScalars(clip, "lineStyle", {SDK::SFlashVarValue::Double(1.0), SDK::SFlashVarValue::Int(edge), SDK::SFlashVarValue::Double(edgeAlpha)}) : page.InvokeScalars(clip, "lineStyle", {});
            ok      = ok && page.InvokeScalars(clip, "beginFill", {SDK::SFlashVarValue::Int(fill), SDK::SFlashVarValue::Double(fillAlpha)});
            ok      = ok && page.InvokeScalars(clip, "moveTo", {Coordinate(x), Coordinate(y)});
            ok      = ok && page.InvokeScalars(clip, "lineTo", {Coordinate(x + width), Coordinate(y)});
            ok      = ok && page.InvokeScalars(clip, "lineTo", {Coordinate(x + width), Coordinate(y + height)});
            ok      = ok && page.InvokeScalars(clip, "lineTo", {Coordinate(x), Coordinate(y + height)});
            ok      = ok && page.InvokeScalars(clip, "lineTo", {Coordinate(x), Coordinate(y)});
            return ok && page.InvokeScalars(clip, "endFill", {});
        }

        bool CreateText(KCDC::Core::NativeUI &page, const std::string &key, const std::string &parent, float x, float y, float width, float height, const char *font, double size, int colour, const char *align) {
            KCDC::Core::NativeUI::TextStyle style;
            style.font  = font;
            style.size  = size;
            style.color = colour;
            style.align = align;
            return page.CreateTextField(key, parent, key.c_str(), static_cast<double>(x), static_cast<double>(y), static_cast<double>(width), static_cast<double>(height)) && page.SetTextStyle(key, style) && page.SetText(key, "", false);
        }

        bool Restyle(KCDC::Core::NativeUI &page, const std::string &key, const char *font, double size, int colour, const char *align) {
            KCDC::Core::NativeUI::TextStyle style;
            style.font  = font;
            style.size  = size;
            style.color = colour;
            style.align = align;
            return page.SetTextStyle(key, style);
        }

        KCDC::Features::UI::NativeUIService *Screens() {
            return KCDC::Core::gApplication != nullptr ? &KCDC::Core::gApplication->GetNativeUIService() : nullptr;
        }

        SDK::wh::framework::I_UIApse *Apse() {
            SDK::wh::C_GlobalEnvironment *environment = SDK::wh::C_GlobalEnvironment::GetInstance();
            return environment != nullptr ? environment->GetUIApse() : nullptr;
        }

        SDK::wh::entitymodule::C_Actor *ActorOf(SDK::EntityId entityId) {
            SDK::IActorSystem *actors = entityId != 0 ? SDK::IActorSystem::GetInstance() : nullptr;
            return actors != nullptr ? static_cast<SDK::wh::entitymodule::C_Actor *>(actors->GetActor(entityId)) : nullptr;
        }

        void HideActor(SDK::wh::entitymodule::C_Actor *actor, bool hide) {
            SDK::IEntity *entity = actor != nullptr ? actor->GetEntity() : nullptr;
            if (entity != nullptr && entity->IsHidden() != hide) {
                entity->Hide(hide);
            }
        }

        /** The hair colour a name ends with, or null. */
        const HairColour *HairColourOf(std::string_view name) {
            for (const HairColour &colour : kHairColours) {
                if (name.size() > colour.suffix.size() && name.substr(name.size() - colour.suffix.size()) == colour.suffix && name[name.size() - colour.suffix.size() - 1] == '_') {
                    return &colour;
                }
            }
            return nullptr;
        }

        /**
         * A catalog name a player can read: the gender and part prefixes
         * dropped, the words title-cased, numbers without their leading
         * zeros. A name that is only a number takes `noun` in front of it.
         */
        std::string Readable(std::string_view name, const char *noun) {
            static constexpr std::string_view kDropped[] = {"m", "f", "x", "uc", "male", "female", "hair", "head", "beard", "body", "npc"};
            std::string out;
            bool onlyNumbers  = true;
            std::size_t start = 0;
            while (start <= name.size()) {
                const std::size_t end = std::min(name.find('_', start), name.size());
                std::string word(name.substr(start, end - start));
                start = end + 1;
                if (word.empty()) {
                    continue;
                }
                std::string lower = word;
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (std::find(std::begin(kDropped), std::end(kDropped), lower) != std::end(kDropped)) {
                    continue;
                }
                if (std::all_of(word.begin(), word.end(), [](unsigned char c) {
                        return std::isdigit(c) != 0;
                    })) {
                    word = std::to_string(std::stoi(word));
                }
                else {
                    onlyNumbers = false;
                    word        = lower;
                    word[0]     = static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
                }
                if (!out.empty()) {
                    out += ' ';
                }
                out += word;
            }
            if (out.empty()) {
                return std::string(noun);
            }
            return onlyNumbers ? std::string(noun) + " " + out : out;
        }

        /** What a member of a family is called next to the family: its colour, its version, or its own readable name. */
        std::string MemberLabel(const std::string_view key, const std::string_view member) {
            if (member == key) {
                return "Plain";
            }
            if (const HairColour *colour = HairColourOf(member); colour != nullptr) {
                return Readable(colour->suffix, "");
            }
            if (member.size() > key.size() + 1 && member.substr(0, key.size()) == key && member[key.size()] == '_') {
                const std::string_view suffix = member.substr(key.size() + 1);
                if (suffix.size() == 3 && suffix[0] == 'b' && std::isdigit(static_cast<unsigned char>(suffix[1])) != 0) {
                    return "Version " + std::to_string(std::stoi(std::string(suffix.substr(1))) + 2);
                }
                return Readable(suffix, "Variant");
            }
            const std::size_t underscore = member.find_last_of('_');
            if (underscore != std::string_view::npos && underscore + 1 < member.size() && std::isdigit(static_cast<unsigned char>(member[underscore + 1])) != 0) {
                return "Variant " + std::to_string(std::stoi(std::string(member.substr(underscore + 1))));
            }
            return Readable(member, "Variant");
        }

        /** The frame's shape: the head, as many body slices as the rows need near their own height, the foot. */
        struct FrameLayout {
            float hintsTop;
            float bottomTop;
            float height;
            std::size_t tiles;
            float tileSpan;
        };

        FrameLayout MeasureFrame(std::size_t rows, std::size_t hintRows) {
            FrameLayout frame {};
            const float rowsEnd = kRowsTop + kRowHeight * static_cast<float>(rows);
            frame.hintsTop      = rowsEnd + kHintsGap;
            frame.bottomTop     = std::max(frame.hintsTop + kHintRow * static_cast<float>(hintRows) + 12.0F - kBottomParchment, kTopHeight);
            frame.height        = frame.bottomTop + kBottomHeight;
            const float gap     = frame.bottomTop - kTopHeight;
            frame.tiles         = gap < 0.5F ? 0 : std::clamp<std::size_t>(static_cast<std::size_t>(gap / kTileHeight + 0.5F), 1, kMaxTiles);
            frame.tileSpan      = frame.tiles > 0 ? gap / static_cast<float>(frame.tiles) : 0.0F;
            return frame;
        }

        /** The first input `action` is bound to on `map`, as CryInput names it (`q`, `tab`, `mwheel_up`), or empty. */
        std::string BoundInput(SDK::IActionMap *map, const char *action) {
            SDK::IActionMapActionIterator *iterator = nullptr;
            map->CreateActionIterator(&iterator);
            if (iterator == nullptr) {
                return {};
            }
            std::string input;
            while (const SDK::IActionMapAction *candidate = iterator->Next()) {
                if (std::strcmp(candidate->GetActionId().c_str(), action) != 0) {
                    continue;
                }
                if (const SDK::SActionInput *first = candidate->GetActionInput(0); first != nullptr && first->GetInput() != nullptr) {
                    input = first->GetInput();
                }
                break;
            }

            // The iterator arrives holding one reference.
            iterator->Release();
            return input;
        }

        /**
         * A key as the game's own hints name it: the hint manager's label for
         * the input, resolved through the string table. A letter comes out as
         * the keyboard layout's character for that key -- CryInput's `q` is
         * `A` on AZERTY -- and a named key in the game's language.
         */
        std::string KeyLabel(const std::string &input) {
            SDK::wh::guimodule::C_UIActionHintManager *hints = SDK::wh::guimodule::C_UIActionHintManager::GetInstance();
            SDK::ILocalizationManager *strings               = SDK::ILocalizationManager::GetInstance();
            if (hints == nullptr || strings == nullptr) {
                return input;
            }
            SDK::ReturnedCryString label;
            hints->GetKeyLabel(label.Slot(), input.c_str());
            const std::string key = label.c_str();
            if (key.empty() || key[0] != '@') {
                return key.empty() ? input : key;
            }

            // A letter or digit comes back as `@ui_control_uc_<keyId>`, an
            // entry the string table registered once, from the keyboard's
            // table as it stood then. `@cc_<input>` is the table's own special
            // case that asks the keyboard now (`GetInputCharUnicode` over the
            // layout table CKeyboard rebuilds on every layout change).
            const bool character     = key.rfind("@ui_control_uc_", 0) == 0;
            const std::string lookup = character ? "@cc_" + input : key;
            SDK::ScopedCryString text("");
            const bool resolved = strings->LocalizeLabel(lookup.c_str(), *text.Out()) && text.c_str()[0] != '\0';
            return resolved ? std::string(text.c_str()) : input;
        }

        /** The width a text field's text takes, as Flash measures it. */
        float TextWidth(KCDC::Core::NativeUI &page, const std::string &key, const std::string &text) {
            SDK::SFlashVarValue value {};
            std::string scratch;
            if (page.GetValue(key, "textWidth", value, scratch)) {
                switch (value.type) {
                case SDK::EFlashVariableType::Double: return static_cast<float>(value.d);
                case SDK::EFlashVariableType::Float: return value.f;
                case SDK::EFlashVariableType::Int: return static_cast<float>(value.i);
                case SDK::EFlashVariableType::UInt: return static_cast<float>(value.u);
                default: break;
                }
            }
            return 7.5F * static_cast<float>(text.size());
        }
    } // namespace

    CharacterCreatorService &CharacterCreatorService::Get() {
        static CharacterCreatorService instance;
        return instance;
    }

    const char *CharacterCreatorService::CloseReasonName(CloseReason reason) {
        switch (reason) {
        case CloseReason::Accepted: return "accepted";
        case CloseReason::Cancelled: return "cancelled";
        case CloseReason::Closed: return "closed";
        case CloseReason::Replaced: return "replaced";
        case CloseReason::Unavailable: return "unavailable";
        default: return "interrupted";
        }
    }

    // --- The session -----------------------------------------------------------

    void CharacterCreatorService::Open(const OpenRequest &request) {
        if (_phase != Phase::Idle) {
            Finish(CloseReason::Replaced);
        }
        ReportEnding();

        // The key strip names the keys as they are bound now; a panel
        // composed for an earlier creator may show a binding since changed.
        if (_screen != 0) {
            if (KCDC::Features::UI::NativeUIService *screens = Screens(); screens != nullptr) {
                screens->CloseScreen(_screen);
            }
            _screen = 0;
            _failed = false;
            ForgetComposition();
        }
        _look          = request.appearance;
        _openedLook    = request.appearance;
        _genders       = request.genders;
        _title         = request.title.empty() ? std::string(kDefaultTitle) : request.title;
        _tab           = 0;
        _row           = 0;
        _frameHeight   = kBodyFrameHeight;
        _frameCentre   = kBodyFrameCentre;
        _openingTicks  = 0;
        _figureMissing = false;
        _phase         = Phase::Opening;
        _dirty         = true;
    }

    bool CharacterCreatorService::Close() {
        if (_phase == Phase::Idle) {
            return false;
        }
        Finish(CloseReason::Closed);
        return true;
    }

    void CharacterCreatorService::Finish(CloseReason reason) {
        if (_phase == Phase::Shown) {
            if (SDK::wh::framework::I_UIApse *apse = Apse(); apse != nullptr) {
                _closingApse = true;
                apse->Hide(true);
                _closingApse = false;
            }
        }
        Conclude(reason);
    }

    void CharacterCreatorService::Conclude(CloseReason reason) {
        RestoreStage();
        ShowPlates();
        ReleaseInput();
        ShowPanel(false);
        _phase         = Phase::Idle;
        _pendingRow    = 0;
        _pendingStep   = 0;
        _pendingTab    = 0;
        _pendingGender = false;
        _pendingRandom = false;
        _pendingReset  = false;
        _pendingAccept = false;
        _pendingCancel = false;
        _figureMissing = false;
        _ending        = reason;
    }

    void CharacterCreatorService::ReportEnding() {
        if (!_ending) {
            return;
        }
        // Taken first, so a handler that opens the next creator starts clean.
        const CloseReason reason                           = *_ending;
        const KCDC::Shared::Game::CharacterAppearance look = _look;
        _ending.reset();
        KCDC::Scripting::Events::CharacterCreatorClosed(reason == CloseReason::Accepted ? &look : nullptr, CloseReasonName(reason));
    }

    bool CharacterCreatorService::StillAllowed() const {
        return KCDC::Features::World::WorldService::IsSessionActive() && SDK::wh::entitymodule::C_Player::GetInstance() != nullptr;
    }

    void CharacterCreatorService::TickOpening() {
        if (!StillAllowed() || ++_openingTicks > kMaxOpeningTicks) {
            Finish(CloseReason::Unavailable);
            return;
        }

        EnsureScreen();
        if (_failed) {
            Finish(CloseReason::Unavailable);
            return;
        }
        SDK::wh::framework::I_UIApse *apse = Apse();
        if (!_composed || apse == nullptr || Character() == nullptr) {
            return;
        }

        // Another page of the screen is up -- the inventory, the map. It is
        // closed at once and the creator shows on a later tick, once the
        // close has let go of the screen's UI state.
        SDK::wh::guimodule::C_UIStateBlockers *blockers = SDK::wh::guimodule::C_UIStateBlockers::GetInstance();
        if (blockers != nullptr && blockers->IsHeld(SDK::wh::guimodule::E_UIState::Apse)) {
            _closingApse = true;
            apse->Hide(true);
            _closingApse = false;
            return;
        }

        // Shown before the screen, so the bind and copy hooks find the
        // creator when the page stages and dresses the figure.
        _phase                                        = Phase::Shown;
        SDK::wh::framework::S_UIApseShowParams params = SDK::wh::framework::S_UIApseShowParams::Make();
        if (!apse->Show(SDK::wh::framework::E_UIApseView::Type::Player, &params)) {
            // Refused for a reason that may pass -- a fade, a cutscene.
            _phase = Phase::Opening;
            return;
        }

        HidePlates();
        AcquireInput();
        ShowPanel(true);
        _dirty = true;
        Refresh();
    }

    void CharacterCreatorService::Update() {
        ReportEnding();

        if (_phase == Phase::Opening) {
            TickOpening();
            return;
        }
        if (_phase != Phase::Shown) {
            return;
        }
        if (!StillAllowed() || Character() == nullptr || !_composed) {
            Finish(CloseReason::Interrupted);
            return;
        }
        if (_figureMissing) {
            Finish(CloseReason::Unavailable);
            return;
        }
        if (_pendingAccept) {
            _pendingAccept = false;
            Finish(CloseReason::Accepted);
            return;
        }
        if (_pendingCancel) {
            _pendingCancel = false;
            Finish(CloseReason::Cancelled);
            return;
        }

        // Whole-look changes first, which void the rest of this tick's
        // presses: they were aimed at the look being replaced.
        if (_pendingGender || _pendingReset || _pendingRandom) {
            const bool gender = _pendingGender;
            const bool reset  = _pendingReset;
            const bool random = _pendingRandom;
            _pendingGender    = false;
            _pendingReset     = false;
            _pendingRandom    = false;
            _pendingStep      = 0;
            _pendingRow       = 0;
            _pendingTab       = 0;
            if (gender && _genders == kBoth) {
                KCDC::Shared::Game::CharacterAppearance other;
                other.gender = _look.gender == KCDC::Shared::Game::AppearanceGender::Female ? KCDC::Shared::Game::AppearanceGender::Male : KCDC::Shared::Game::AppearanceGender::Female;
                ApplyLook(other);
            }
            else if (reset) {
                ApplyLook(_openedLook);
            }
            else if (random) {
                Randomize();
            }
            _dirty = true;
        }

        if (_pendingTab != 0) {
            const long long count = static_cast<long long>(TabCount());
            _tab                  = static_cast<std::size_t>(((static_cast<long long>(_tab) + _pendingTab) % count + count) % count);
            _row                  = 0;
            _pendingTab           = 0;
            _dirty                = true;
        }
        if (_pendingRow != 0) {
            const long long count = static_cast<long long>(Select().Rows());
            _row                  = static_cast<std::size_t>(((static_cast<long long>(_row) + _pendingRow) % count + count) % count);
            _pendingRow           = 0;
            _dirty                = true;
        }
        if (_pendingStep != 0) {
            const int direction = _pendingStep > 0 ? 1 : -1;
            for (int step = 0; step < std::abs(_pendingStep); ++step) {
                Step(direction);
            }
            _pendingStep = 0;
            _dirty       = true;
        }
        if (_dirty) {
            Refresh();
        }
    }

    void CharacterCreatorService::Reset() {
        if (_phase != Phase::Idle) {
            Finish(CloseReason::Interrupted);
        }
        ReportEnding();
        if (_femaleDummyId != 0 && KCDC::Core::gApplication != nullptr) {
            KCDC::Core::gApplication->GetActorSpawner().DespawnNow(_femaleDummyId);
        }
        _femaleDummyId = 0;
        _prefabDummyId = 0;
        _femaleOwnParts.fill({});
        _femaleArchetypeId = -1;

        if (_screen != 0) {
            if (KCDC::Features::UI::NativeUIService *screens = Screens(); screens != nullptr) {
                screens->CloseScreen(_screen);
            }
        }
        _screen = 0;
        _failed = false;
        ForgetComposition();
    }

    // --- Questions the hooks ask ---------------------------------------------

    void CharacterCreatorService::DressFigure(SDK::wh::entitymodule::C_Actor *dummy) {
        if (_phase != Phase::Shown || _figureMissing || dummy == nullptr || dummy != Figure()) {
            return;
        }

        // The copy has just made the figure the player's double: his soul,
        // his inventory cloned and worn, his look where the genders agree.
        // The creator keeps all of that -- the page reads it, and the copy's
        // item map points into it -- and only takes the clothes off and
        // writes its own look over his. On the woman the copy also handed her
        // soul the player's archetype; hers goes back first, so her own
        // defaults and a woman's part names resolve.
        SDK::wh::rpgmodule::C_Soul *soul = dummy->GetSoul();
        if (dummy == FemaleDummy() && soul != nullptr && _femaleArchetypeId >= 0 && soul->GetArchetypeId() != _femaleArchetypeId) {
            soul->SetArchetypeId(_femaleArchetypeId);
        }
        if (SDK::wh::entitymodule::C_EquipmentManager *equipment = dummy->GetEquipmentManagerRuntime(); equipment != nullptr) {
            equipment->InvokeUnequipAllArmor();
        }
        for (std::size_t part = 0; part < KCDC::Shared::Game::CharacterAppearance::kPartCount; ++part) {
            DressPart(dummy, static_cast<KCDC::Shared::Game::AppearancePart>(part));
        }

        // The underwear is no part of a look, but on the woman it too would
        // resolve to the man's shorts.
        const std::string underwear = FemaleFallback(dummy, SDK::wh::rpgmodule::S_CharacterBodyDescription::kUnderwear);
        if (soul != nullptr && !underwear.empty()) {
            soul->SetBodyDescriptionPart(SDK::wh::rpgmodule::S_CharacterBodyDescription::kUnderwear, SDK::ScopedCryString(underwear.c_str()), SDK::wh::rpgmodule::S_CharacterBodyDescription::kLayerLookAs);
        }
        dummy->RebuildClothing();
        if (dummy == PrefabDummy()) {
            _dressedPrefab = true;
        }
    }

    void CharacterCreatorService::FrameStage(SDK::SViewParams &params) {
        if (_phase != Phase::Shown || _figureMissing || params.fov < 0.01F || _stageHeight <= 0.0F) {
            return;
        }
        SDK::wh::entitymodule::C_Actor *figure = Figure();
        SDK::IEntity *entity                   = figure != nullptr ? figure->GetEntity() : nullptr;
        if (entity == nullptr) {
            return;
        }

        // The tab decides what fills the frame; the framing closes part of
        // the way there each frame, so a tab change glides rather than cuts.
        const bool body          = CurrentTab() == Tab::Body;
        const float headCentre   = _look.gender == KCDC::Shared::Game::AppearanceGender::Female ? kFemaleHeadCentre : kMaleHeadCentre;
        const float targetHeight = body ? kBodyFrameHeight : kHeadFrameHeight;
        const float targetCentre = body ? kBodyFrameCentre : headCentre;
        _frameHeight += (targetHeight - _frameHeight) * kFrameEaseFraction;
        _frameCentre += (targetCentre - _frameCentre) * kFrameEaseFraction;

        // The screen's camera keeps its identity rotation -- it looks down +Y
        // at the panels and the stage -- and is only moved: back from the
        // figure as far as the FOV needs to fit the framed height, and across
        // so he stands at `kFigureScreenX`. CryEngine's FOV is vertical.
        const SDK::Vec3 feet     = entity->GetWorldPos();
        const float distance     = 0.5F * _frameHeight / std::tan(0.5F * params.fov);
        const float visibleWidth = _frameHeight * (_stageWidth / _stageHeight);
        params.position          = SDK::Vec3 {feet.x + (0.5F - kFigureScreenX) * visibleWidth, feet.y - distance, feet.z + _frameCentre};
    }

    bool CharacterCreatorService::AllowsViewChange(SDK::wh::framework::E_UIApseView::Type view) const {
        return _phase != Phase::Shown || view == SDK::wh::framework::E_UIApseView::Type::Player;
    }

    bool CharacterCreatorService::ClaimsHide() const {
        return _phase == Phase::Shown && !_closingApse;
    }

    void CharacterCreatorService::OnApseCancelled() {
        Conclude(CloseReason::Cancelled);
    }

    void CharacterCreatorService::OnStageBound(SDK::wh::guimodule::C_UIApseCharacter *character) {
        // The prefab's man, as the page binds him: the one moment the slot is
        // known to hold him rather than the woman this service stands there.
        SDK::wh::entitymodule::C_Actor *slot = character != nullptr ? character->GetDummyPlayer() : nullptr;
        if (slot != nullptr && slot != FemaleDummy()) {
            _prefabDummyId = slot->GetEntityId();
        }
        if (_phase != Phase::Shown) {
            return;
        }
        if (!StageFigure(_look.gender)) {
            // The page dresses the man as the player instead, and the next
            // tick ends the creator: a woman's look on him resolves to nothing.
            _figureMissing = true;
            Framework::Logging::GetLogger(kLogger)->warn("No figure for the creator's {}", _look.gender == KCDC::Shared::Game::AppearanceGender::Female ? "woman" : "man");
        }
    }

    // --- The stage -------------------------------------------------------------

    SDK::wh::guimodule::C_UIApseCharacter *CharacterCreatorService::Character() {
        SDK::wh::guimodule::C_UIApse *apse = SDK::wh::guimodule::C_UIApse::GetInstance();
        return apse != nullptr ? apse->GetCharacter() : nullptr;
    }

    SDK::wh::entitymodule::C_Actor *CharacterCreatorService::PrefabDummy() const {
        return ActorOf(_prefabDummyId);
    }

    SDK::wh::entitymodule::C_Actor *CharacterCreatorService::FemaleDummy() const {
        return ActorOf(_femaleDummyId);
    }

    SDK::wh::entitymodule::C_Actor *CharacterCreatorService::Figure() const {
        SDK::wh::guimodule::C_UIApseCharacter *character = Character();
        SDK::wh::entitymodule::C_Actor *figure           = character != nullptr ? character->GetDummyPlayer() : nullptr;
        if (figure == nullptr) {
            return nullptr;
        }
        return figure == PrefabDummy() || figure == FemaleDummy() ? figure : nullptr;
    }

    SDK::wh::entitymodule::C_Actor *CharacterCreatorService::EnsureFemaleDummy() {
        if (SDK::wh::entitymodule::C_Actor *female = FemaleDummy(); female != nullptr) {
            return female;
        }
        _femaleDummyId = 0;

        SDK::wh::entitymodule::C_Actor *male = PrefabDummy();
        SDK::IEntity *maleEntity             = male != nullptr ? male->GetEntity() : nullptr;
        if (maleEntity == nullptr || KCDC::Core::gApplication == nullptr) {
            return nullptr;
        }

        KCDC::Common::Spawn::SpawnRequest request;
        request.actorClass = kFemaleDummyClass;
        // Named, not left to the class: without it the soul is a blank one
        // whose default body is a man's, which then stands on her skeleton.
        // Remote women are spawned the same way (Human, player.cpp).
        request.soulArchetype                         = kFemaleSoulArchetype;
        request.world                                 = KCDC::Common::Spawn::WorldPlacement {maleEntity->GetWorldPos(), maleEntity->GetRotation()};
        const KCDC::Common::Spawn::SpawnOutcome outcome = KCDC::Core::gApplication->GetActorSpawner().SpawnNow(request);
        if (outcome.actor.entityId == 0) {
            Framework::Logging::GetLogger(kLogger)->warn("The woman's figure could not be spawned: {}", KCDC::Common::Spawn::SpawnResultToString(outcome.result));
            return nullptr;
        }

        _femaleDummyId                         = outcome.actor.entityId;
        SDK::wh::entitymodule::C_Actor *female = FemaleDummy();
        HideActor(female, true);

        // Before any copy reaches her soul, which is the only time these
        // are still hers.
        if (SDK::wh::rpgmodule::C_Soul *soul = female != nullptr ? female->GetSoul() : nullptr; soul != nullptr) {
            for (std::uint8_t part = 0; part < SDK::wh::rpgmodule::S_CharacterBodyDescription::kPartCount; ++part) {
                _femaleOwnParts[part] = soul->BodyDescriptionPart(part);
            }
            _femaleArchetypeId = soul->GetArchetypeId();
        }
        Framework::Logging::GetLogger(kLogger)->debug("The woman's own soul: archetype {}, body '{}', head '{}', hair '{}', beard '{}', underwear '{}'", _femaleArchetypeId, _femaleOwnParts[0], _femaleOwnParts[1], _femaleOwnParts[2], _femaleOwnParts[3], _femaleOwnParts[4]);
        return female;
    }

    bool CharacterCreatorService::StageFigure(KCDC::Shared::Game::AppearanceGender gender) {
        SDK::wh::guimodule::C_UIApseCharacter *character = Character();
        SDK::wh::entitymodule::C_Actor *male             = PrefabDummy();
        if (character == nullptr || male == nullptr) {
            return false;
        }

        if (gender == KCDC::Shared::Game::AppearanceGender::Female) {
            SDK::wh::entitymodule::C_Actor *female = EnsureFemaleDummy();
            if (female == nullptr) {
                return false;
            }
            SDK::IEntity *maleEntity = male->GetEntity();
            _hidPrefab               = _hidPrefab || (maleEntity != nullptr && !maleEntity->IsHidden());
            HideActor(male, true);
            HideActor(female, false);
            character->SetDummyPlayer(female);
            return true;
        }

        HideActor(FemaleDummy(), true);
        if (_hidPrefab) {
            HideActor(male, false);
            _hidPrefab = false;
        }
        return true;
    }

    void CharacterCreatorService::HidePlates() {
        // The screen draws its pages on panels standing in the level, each
        // a link of the stage's plates hub; they are hidden rather than
        // emptied, which also takes them out of the mouse's reach. The walk
        // is `ForEachApseHubLink`'s (0x181E26930).
        SDK::wh::guimodule::C_UIApse *apse = SDK::wh::guimodule::C_UIApse::GetInstance();
        SDK::IEntitySystem *entities       = SDK::IEntitySystem::GetInstance();
        SDK::IEntity *root                 = apse != nullptr && entities != nullptr && apse->GetStageRootEntityId() != 0 ? entities->GetEntity(apse->GetStageRootEntityId()) : nullptr;
        if (root == nullptr) {
            return;
        }
        SDK::IEntity *hub = nullptr;
        for (SDK::IEntityLink *link = root->GetEntityLinks(); link != nullptr && hub == nullptr; link = link->next) {
            if (link->name.m_pData != nullptr && std::strcmp(link->name.m_pData, kPlatesHubLink) == 0) {
                hub = entities->GetEntity(link->entityId);
            }
        }
        if (hub == nullptr) {
            return;
        }
        for (SDK::IEntityLink *link = hub->GetEntityLinks(); link != nullptr; link = link->next) {
            SDK::IEntity *plate = entities->GetEntity(link->entityId);
            if (plate == nullptr || plate->IsHidden()) {
                continue;
            }
            plate->Hide(true);
            _hiddenPlates.push_back(link->entityId);
        }
    }

    void CharacterCreatorService::ShowPlates() {
        SDK::IEntitySystem *entities = SDK::IEntitySystem::GetInstance();
        for (const SDK::EntityId plateId : _hiddenPlates) {
            if (SDK::IEntity *plate = entities != nullptr ? entities->GetEntity(plateId) : nullptr; plate != nullptr) {
                plate->Hide(false);
            }
        }
        _hiddenPlates.clear();
    }

    void CharacterCreatorService::RestoreStage() {
        SDK::wh::guimodule::C_UIApseCharacter *character = Character();
        SDK::wh::entitymodule::C_Actor *male             = PrefabDummy();
        if (character != nullptr && male != nullptr) {
            character->SetDummyPlayer(male);
        }
        HideActor(FemaleDummy(), true);
        if (_hidPrefab) {
            HideActor(male, false);
            _hidPrefab = false;
        }

        // He keeps the creator's look on his look-as layer otherwise, and a
        // player the inventory cannot copy -- a woman -- would see it there.
        if (_dressedPrefab && male != nullptr) {
            if (SDK::wh::rpgmodule::C_Soul *soul = male->GetSoul(); soul != nullptr) {
                for (std::size_t part = 0; part < KCDC::Shared::Game::CharacterAppearance::kPartCount; ++part) {
                    soul->ClearOverrideBodyDescriptionPart(static_cast<std::uint8_t>(part));
                }
            }
        }
        _dressedPrefab = false;
    }

    void CharacterCreatorService::DressPart(SDK::wh::entitymodule::C_Actor *figure, KCDC::Shared::Game::AppearancePart part) {
        SDK::wh::rpgmodule::C_Soul *soul = figure != nullptr ? figure->GetSoul() : nullptr;
        if (soul == nullptr) {
            return;
        }
        const std::string &chosen = _look.Part(part);
        const std::string name    = chosen.empty() ? FemaleFallback(figure, static_cast<std::uint8_t>(part)) : chosen;
        if (name.empty()) {
            // Cleared rather than written empty, so the figure's own soul answers the part.
            soul->ClearOverrideBodyDescriptionPart(static_cast<std::uint8_t>(part));
            return;
        }
        soul->SetBodyDescriptionPart(static_cast<std::uint8_t>(part), SDK::ScopedCryString(name.c_str()), SDK::wh::rpgmodule::S_CharacterBodyDescription::kLayerLookAs);
    }

    std::string CharacterCreatorService::FemaleFallback(SDK::wh::entitymodule::C_Actor *figure, std::uint8_t part) const {
        if (figure == nullptr || figure != FemaleDummy()) {
            return {};
        }
        if (!_femaleOwnParts[part].empty()) {
            return _femaleOwnParts[part];
        }
        for (const KCDC::Shared::Game::AppearanceOption &option : KCDC::Shared::Game::g_AppearanceOptions) {
            if (static_cast<std::uint8_t>(option.part) == part && option.gender == KCDC::Shared::Game::AppearanceGender::Female) {
                return std::string(option.name);
            }
        }
        return {};
    }

    // --- The look --------------------------------------------------------------

    KCDC::Shared::Game::AppearancePart CharacterCreatorService::PartOf(Tab tab) {
        switch (tab) {
        case Tab::Body: return KCDC::Shared::Game::AppearancePart::Body;
        case Tab::Face: return KCDC::Shared::Game::AppearancePart::Head;
        case Tab::Hair: return KCDC::Shared::Game::AppearancePart::Hair;
        default: return KCDC::Shared::Game::AppearancePart::Beard;
        }
    }

    std::size_t CharacterCreatorService::TabCount() const {
        return _look.gender == KCDC::Shared::Game::AppearanceGender::Male ? kTabCount : kTabCount - 1;
    }

    CharacterCreatorService::Tab CharacterCreatorService::CurrentTab() const {
        return static_cast<Tab>(std::min(_tab, TabCount() - 1));
    }

    std::vector<CharacterCreatorService::Family> CharacterCreatorService::Families(KCDC::Shared::Game::AppearancePart part) const {
        std::vector<Family> families {Family {std::string_view(), {std::string_view()}}};
        const auto add = [&families](std::string_view key, std::string_view name) {
            const auto found = std::find_if(families.begin() + 1, families.end(), [key](const Family &family) {
                return family.key == key;
            });
            if (found != families.end()) {
                found->members.push_back(name);
            }
            else {
                families.push_back(Family {key, {name}});
            }
        };

        // A beard is modelled per face, so only those the face carries.
        if (part == KCDC::Shared::Game::AppearancePart::Beard) {
            for (const std::string_view name : KCDC::Shared::Game::AppearanceBeardsForHead(_look.gender, _look.Part(KCDC::Shared::Game::AppearancePart::Head))) {
                add(name, name);
            }
            return families;
        }
        for (const KCDC::Shared::Game::AppearanceOption &option : KCDC::Shared::Game::g_AppearanceOptions) {
            if (option.part == part && option.gender == _look.gender) {
                add(option.group.empty() ? option.name : option.group, option.name);
            }
        }
        return families;
    }

    CharacterCreatorService::Placement CharacterCreatorService::Locate(const std::vector<Family> &families, std::string_view name) {
        for (std::size_t family = 0; family < families.size(); ++family) {
            const std::vector<std::string_view> &members = families[family].members;
            const auto found                             = std::find(members.begin(), members.end(), name);
            if (found != members.end()) {
                return Placement {family, static_cast<std::size_t>(found - members.begin())};
            }
        }
        return Placement {};
    }

    CharacterCreatorService::Selection CharacterCreatorService::Select() const {
        Selection selection;
        selection.part     = PartOf(CurrentTab());
        selection.families = Families(selection.part);
        selection.at       = Locate(selection.families, _look.Part(selection.part));
        return selection;
    }

    void CharacterCreatorService::Step(int direction) {
        const Selection selection                     = Select();
        const KCDC::Shared::Game::AppearancePart part = selection.part;
        const std::vector<Family> &families           = selection.families;
        const Placement at                            = selection.at;

        if (_row == 0) {
            const long long count  = static_cast<long long>(families.size());
            const std::size_t next = static_cast<std::size_t>(((static_cast<long long>(at.family) + direction) % count + count) % count);

            // Keep what the member is -- a colour, a version -- where the new
            // family has the same; otherwise its first.
            const std::string kept  = MemberLabel(families[at.family].key, families[at.family].members[at.member]);
            std::string_view chosen = families[next].members.front();
            for (const std::string_view member : families[next].members) {
                if (MemberLabel(families[next].key, member) == kept) {
                    chosen = member;
                    break;
                }
            }
            SetPart(part, chosen);
            return;
        }

        const std::vector<std::string_view> &members = families[at.family].members;
        const long long count                        = static_cast<long long>(members.size());
        SetPart(part, members[static_cast<std::size_t>(((static_cast<long long>(at.member) + direction) % count + count) % count)]);
    }

    void CharacterCreatorService::SetPart(KCDC::Shared::Game::AppearancePart part, std::string_view name) {
        _look.Part(part)                       = std::string(name);
        SDK::wh::entitymodule::C_Actor *figure = Figure();
        DressPart(figure, part);

        // A beard is modelled per face; one the new face does not carry would load nothing.
        if (part == KCDC::Shared::Game::AppearancePart::Head && !KCDC::Shared::Game::IsBeardOnHead(_look.gender, _look.Part(KCDC::Shared::Game::AppearancePart::Head), _look.Part(KCDC::Shared::Game::AppearancePart::Beard))) {
            _look.Part(KCDC::Shared::Game::AppearancePart::Beard).clear();
            DressPart(figure, KCDC::Shared::Game::AppearancePart::Beard);
        }
        if (figure != nullptr && figure == PrefabDummy()) {
            _dressedPrefab = true;
        }
    }

    void CharacterCreatorService::Randomize() {
        static std::mt19937 generator {std::random_device {}()};
        const auto pick = [](std::size_t count) {
            return std::uniform_int_distribution<std::size_t>(0, count - 1)(generator);
        };

        // The face before the beard, which depends on it; past the soul's
        // own entry, which would only repeat what the figure already wears.
        for (const KCDC::Shared::Game::AppearancePart part : {KCDC::Shared::Game::AppearancePart::Body, KCDC::Shared::Game::AppearancePart::Head, KCDC::Shared::Game::AppearancePart::Hair, KCDC::Shared::Game::AppearancePart::Beard}) {
            if (part == KCDC::Shared::Game::AppearancePart::Beard && _look.gender != KCDC::Shared::Game::AppearanceGender::Male) {
                continue;
            }
            const std::vector<Family> families = Families(part);
            if (families.size() < 2) {
                continue;
            }
            const Family &family = families[1 + pick(families.size() - 1)];
            SetPart(part, family.members[pick(family.members.size())]);
        }
        _row = 0;
    }

    void CharacterCreatorService::ApplyLook(const KCDC::Shared::Game::CharacterAppearance &look) {
        if (look.gender == _look.gender) {
            for (std::size_t part = 0; part < KCDC::Shared::Game::CharacterAppearance::kPartCount; ++part) {
                SetPart(static_cast<KCDC::Shared::Game::AppearancePart>(part), look.parts[part]);
            }
            _row = 0;
            return;
        }

        // The other gender stands on the stage in place, the way a page
        // show stands its figure: the mirrors come off the old one as a page
        // hide takes them off (slot 26), the other figure goes into the slot,
        // and the page's own SetupDummies (slot 25) registers the player onto
        // it -- which runs the copy and so DressFigure -- and places and turns
        // it. The screen stays up throughout.
        SDK::wh::guimodule::C_UIApseCharacter *character = Character();
        if (character == nullptr) {
            return;
        }
        _look = look;
        character->UnregisterAllLookMirrors();
        RestoreStage();
        if (!StageFigure(_look.gender)) {
            _figureMissing = true;
            Framework::Logging::GetLogger(kLogger)->warn("No figure for the creator's {}", _look.gender == KCDC::Shared::Game::AppearanceGender::Female ? "woman" : "man");
            return;
        }
        character->SetupDummies();
        _tab = 0;
        _row = 0;
    }

    // --- The panel -------------------------------------------------------------

    std::shared_ptr<KCDC::Core::NativeUI> CharacterCreatorService::Page() const {
        KCDC::Features::UI::NativeUIService *screens = Screens();
        return screens != nullptr && _composed ? screens->GetPage(_screen) : nullptr;
    }

    /** Both failures latch, as the player list's do: a retry every tick would leak the clips it made. */
    void CharacterCreatorService::EnsureScreen() {
        KCDC::Features::UI::NativeUIService *screens = Screens();
        if (screens == nullptr) {
            return;
        }
        if (_composed && !screens->IsMounted(_screen)) {
            ForgetComposition();
        }
        if (_composed || _failed) {
            return;
        }
        if (_screen == 0) {
            std::string error;
            _screen = screens->OpenLibraryScreen(kResourceName, {}, error);
            if (_screen == 0) {
                _failed = true;
                Framework::Logging::GetLogger(kLogger)->warn("The creator could not open a screen: {}", error);
            }
            return;
        }
        if (screens->IsMounted(_screen)) {
            _composed = Compose();
            _failed   = !_composed;
        }
    }

    bool CharacterCreatorService::Compose() {
        KCDC::Features::UI::NativeUIService *screens = Screens();
        std::shared_ptr<KCDC::Core::NativeUI> page   = screens != nullptr ? screens->GetPage(_screen) : nullptr;
        KCDC::Core::NativeUIHost *host               = screens != nullptr ? screens->GetHost(_screen) : nullptr;
        if (!page || host == nullptr) {
            return false;
        }

        int stageWidth  = 0;
        int stageHeight = 0;
        if (!host->GetStageSize(stageWidth, stageHeight) || stageWidth <= 0 || stageHeight <= 0) {
            return false;
        }
        _unit        = static_cast<float>(stageHeight) / kLayoutHeight;
        _stageWidth  = static_cast<float>(stageWidth);
        _stageHeight = static_cast<float>(stageHeight);

        const float framePercent = kFrameScale * 100.0F;
        const float frameLeft    = -0.5F * kFrameWidth;

        _root   = screens->NextKey(_screen);
        bool ok = page->CreateClip(_root, "$root", _root.c_str());
        ok      = ok && page->SetVisible(_root, false);

        // The frame's clips come first, so everything else draws over them:
        // the body, then the head over it so the cornice's shadow falls on
        // parchment. They are filled at the end, once the key strip has said
        // how tall the panel is.
        std::array<std::string, kMaxTiles> tiles;
        for (std::string &tile : tiles) {
            tile = screens->NextKey(_screen);
            ok   = ok && page->CreateClip(tile, _root, tile.c_str());
        }
        const std::string top    = screens->NextKey(_screen);
        ok                       = ok && page->CreateClip(top, _root, top.c_str());
        const std::string bottom = screens->NextKey(_screen);
        ok                       = ok && page->CreateClip(bottom, _root, bottom.c_str());

        // The title and the codex's flourish under it.
        _heading                   = screens->NextKey(_screen);
        ok                         = ok && CreateText(*page, _heading, _root, kLeft, kTitleTop, kWidth, 46.0F, "DisplayFont", kTitleSize, kTitleInk, "center");
        const std::string flourish = screens->NextKey(_screen);
        ok                         = ok && page->CreateClip(flourish, _root, flourish.c_str());
        ok                         = ok && LoadTexture(*page, flourish, kFlourishTexture, -0.5F * kFlourishWidth, kFlourishTop, 100.0F, 100.0F, 90.0F);

        // The gender, as two words with a gold rule under the one worn.
        _genderMark   = screens->NextKey(_screen);
        ok            = ok && page->CreateClip(_genderMark, _root, _genderMark.c_str());
        _genderMale   = screens->NextKey(_screen);
        ok            = ok && CreateText(*page, _genderMale, _root, -150.0F, kGenderTop, 140.0F, 32.0F, "DefaultFont", kGenderSize, kFadedInk, "right");
        ok            = ok && page->SetText(_genderMale, "Man", false);
        _genderFemale = screens->NextKey(_screen);
        ok            = ok && CreateText(*page, _genderFemale, _root, 10.0F, kGenderTop, 140.0F, 32.0F, "DefaultFont", kGenderSize, kFadedInk, "left");
        ok            = ok && page->SetText(_genderFemale, "Woman", false);

        // The tabs, a highlight behind the open one, and a gold rule closing them off.
        _tabMark = screens->NextKey(_screen);
        ok       = ok && page->CreateClip(_tabMark, _root, _tabMark.c_str());
        for (std::string &label : _tabLabels) {
            label = screens->NextKey(_screen);
            ok    = ok && CreateText(*page, label, _root, kLeft, kTabsTop, kWidth / static_cast<float>(kTabCount), 28.0F, "LightFont", kTabSize, kFadedInk, "center");
        }
        const std::string rule = screens->NextKey(_screen);
        ok                     = ok && page->CreateClip(rule, _root, rule.c_str());
        ok                     = ok && FillRectangle(*page, rule, kLeft, kTabsRuleTop, kWidth, 1.0F, kGold, 70.0);

        // The rows: a highlight under the selected one, then each row's name,
        // its value between two arrows that never move, the position in the
        // list, and a drawing under the value -- a bar, or swatches.
        _mark = screens->NextKey(_screen);
        ok    = ok && page->CreateClip(_mark, _root, _mark.c_str());
        ok    = ok && FillRectangle(*page, _mark, 0.0F, 0.0F, kWidth + 12.0F, kRowHeight - 8.0F, kMarkFill, kMarkAlpha, kMarkEdge, kEdgeAlpha);
        ok    = ok && page->SetVisible(_mark, false);

        _lines.assign(kMaxLines, Line {});
        for (std::size_t index = 0; index < _lines.size() && ok; ++index) {
            Line &line    = _lines[index];
            const float y = kRowsTop + kRowHeight * static_cast<float>(index);
            line.label    = screens->NextKey(_screen);
            line.previous = screens->NextKey(_screen);
            line.value    = screens->NextKey(_screen);
            line.next     = screens->NextKey(_screen);
            line.count    = screens->NextKey(_screen);
            line.graphics = screens->NextKey(_screen);
            ok            = ok && CreateText(*page, line.label, _root, kLeft + 8.0F, y + 12.0F, kLabelWidth - 8.0F, 26.0F, "LightFont", kLabelSize, kFadedInk, "left");
            ok            = ok && CreateText(*page, line.previous, _root, kValueLeft, y + 6.0F, kArrowWidth, 32.0F, "DefaultFontBold", kArrowSize, kInk, "center");
            ok            = ok && CreateText(*page, line.value, _root, kValueLeft + kArrowWidth, y + 8.0F, kValueWidth - 2.0F * kArrowWidth, 30.0F, "DefaultFont", kValueSize, kInk, "center");
            ok            = ok && CreateText(*page, line.next, _root, kValueRight - kArrowWidth, y + 6.0F, kArrowWidth, 32.0F, "DefaultFontBold", kArrowSize, kInk, "center");
            ok            = ok && CreateText(*page, line.count, _root, kLeft + 8.0F, y + 38.0F, kCountWidth, 20.0F, "LightFont", kCountSize, kFadedInk, "left");
            ok            = ok && page->CreateClip(line.graphics, _root, line.graphics.c_str());
            ok            = ok && Place(*page, line.graphics, kValueLeft, y + kGraphicsTop);
        }

        // The key strip: a cap for each key an action is bound to, then what
        // it does, flowing onto a new line when the next one would not fit.
        // An action bound to nothing is left out.
        const std::string caps = screens->NextKey(_screen);
        ok                     = ok && page->CreateClip(caps, _root, caps.c_str());
        SDK::IActionMap *map   = EnsureInputMap();
        const float hintsTop   = kRowsTop + kRowHeight * static_cast<float>(kMaxLines) + kHintsGap;
        std::size_t hintRow    = 0;
        float x                = kLeft;
        FollowKeyboardLayout();
        for (const KeyHint &hint : kKeyHints) {
            if (!ok || map == nullptr) {
                break;
            }
            if (hint.bothGenders && _genders != kBoth) {
                continue;
            }
            std::vector<std::string> labels;
            for (const char *action : hint.actions) {
                const std::string input = action != nullptr ? BoundInput(map, action) : std::string();
                if (input.empty()) {
                    continue;
                }
                const std::string label = KeyLabel(input);
                if (std::find(labels.begin(), labels.end(), label) == labels.end()) {
                    labels.push_back(label);
                }
            }
            if (labels.empty()) {
                continue;
            }

            // Written first and measured, then placed.
            std::vector<std::pair<std::string, float>> keys;
            float width = 0.0F;
            for (const std::string &label : labels) {
                const std::string key = screens->NextKey(_screen);
                ok                    = ok && CreateText(*page, key, _root, 0.0F, 0.0F, kHintTextWidth, kHintRow - 8.0F, "DefaultFontBold", kKeySize, kInk, "left");
                ok                    = ok && page->SetText(key, label.c_str(), false);
                const float capWidth  = TextWidth(*page, key, label) + 2.0F * kCapPadding;
                keys.emplace_back(key, capWidth);
                width += capWidth + kCapGap;
            }
            const std::string action = screens->NextKey(_screen);
            ok                       = ok && CreateText(*page, action, _root, 0.0F, 0.0F, kHintTextWidth, kHintRow - 8.0F, "LightFont", kHintSize, kFadedInk, "left");
            ok                       = ok && page->SetText(action, hint.text, false);
            const float textWidth    = TextWidth(*page, action, hint.text);
            width += 2.0F + textWidth;

            if (x > kLeft && x + width > kRight) {
                ++hintRow;
                x = kLeft;
            }
            const float y = hintsTop + kHintRow * static_cast<float>(hintRow);
            for (const auto &[key, capWidth] : keys) {
                ok = ok && FillRectangle(*page, caps, x, y + 3.0F, capWidth, kHintRow - 8.0F, 0xE9DFC8, 70.0, kMarkEdge, 60.0);
                ok = ok && Place(*page, key, x + kCapPadding, y + 4.0F);
                x += capWidth + kCapGap;
            }
            ok = ok && Place(*page, action, x + 2.0F, y + 4.0F);
            x += 2.0F + textWidth + kHintGap;
        }

        // The frame, as tall as the key strip made it.
        const FrameLayout frame = MeasureFrame(kMaxLines, hintRow + 1);
        _panelHeight            = frame.height;
        for (std::size_t index = 0; index < tiles.size() && ok; ++index) {
            ok = ok && (index < frame.tiles ? LoadTexture(*page, tiles[index], kMiddleTexture, frameLeft, kTopHeight + frame.tileSpan * static_cast<float>(index), framePercent, frame.tileSpan / kTileTextureHeight * 100.0F) : page->SetVisible(tiles[index], false));
        }
        ok = ok && LoadTexture(*page, top, kTopTexture, frameLeft, 0.0F, framePercent, framePercent);
        ok = ok && LoadTexture(*page, bottom, kBottomTexture, frameLeft, frame.bottomTop, framePercent, framePercent);

        if (!ok) {
            Framework::Logging::GetLogger(kLogger)->warn("The creator panel could not be composed");
            _lines.clear();
            return false;
        }
        _shownHeading.clear();
        _shownTabs.clear();
        _shownMark   = -1;
        _shownGender = -1;
        return true;
    }

    void CharacterCreatorService::ForgetComposition() {
        _composed = false;
        _root.clear();
        _mark.clear();
        _heading.clear();
        _genderMale.clear();
        _genderFemale.clear();
        _genderMark.clear();
        for (std::string &label : _tabLabels) {
            label.clear();
        }
        _tabMark.clear();
        _lines.clear();
        _shownHeading.clear();
        _shownTabs.clear();
        _shownMark   = -1;
        _shownGender = -1;
    }

    void CharacterCreatorService::ShowPanel(bool visible) {
        std::shared_ptr<KCDC::Core::NativeUI> page = Page();
        KCDC::Core::NativeUIHost *host             = page ? Screens()->GetHost(_screen) : nullptr;
        if (host == nullptr) {
            return;
        }
        SDK::SFlashDisplayInfo placement;
        placement.SetVisible(visible);
        if (visible) {
            // Measured on every show: the window can be resized between two.
            const float correction = host->GetSquareUnitCorrection();
            placement.SetX(_stageWidth - (kPanelMargin + 0.5F * kFrameWidth) * _unit * correction);
            placement.SetY(0.5F * (kLayoutHeight - _panelHeight) * _unit);
            placement.SetXScale(_unit * correction * 100.0F);
            placement.SetYScale(_unit * 100.0F);
            placement.SetAlpha(100.0F);
        }
        page->SetDisplayInfo(_root, placement);
    }

    void CharacterCreatorService::RefreshTabs() {
        std::shared_ptr<KCDC::Core::NativeUI> page = Page();
        if (!page) {
            return;
        }

        // The gender: the worn one in ink with a gold rule under it, the
        // other faded -- and only shown at all when both are on offer.
        const int gender = static_cast<int>(_look.gender);
        if (gender != _shownGender) {
            const bool female = _look.gender == KCDC::Shared::Game::AppearanceGender::Female;
            const bool both   = _genders == kBoth;
            Restyle(*page, _genderMale, female ? "DefaultFont" : "DefaultFontBold", kGenderSize, female ? kFadedInk : kInk, "right");
            Restyle(*page, _genderFemale, female ? "DefaultFontBold" : "DefaultFont", kGenderSize, female ? kInk : kFadedInk, "left");
            page->SetVisible(_genderMale, both || !female);
            page->SetVisible(_genderFemale, both || female);
            page->InvokeScalars(_genderMark, "clear", {});
            FillRectangle(*page, _genderMark, female ? 18.0F : -96.0F, kGenderTop + 31.0F, 78.0F, 2.0F, kGold, 90.0);
            _shownGender = gender;
        }

        // The tabs, spread over the width the open ones need.
        const std::size_t count     = TabCount();
        const std::size_t open      = static_cast<std::size_t>(CurrentTab());
        const std::string signature = std::to_string(count) + ":" + std::to_string(open);
        if (signature == _shownTabs) {
            return;
        }
        const float slot = kWidth / static_cast<float>(count);
        for (std::size_t index = 0; index < _tabLabels.size(); ++index) {
            const bool shown = index < count;
            page->SetVisible(_tabLabels[index], shown);
            if (!shown) {
                continue;
            }
            const bool active = index == open;
            page->SetText(_tabLabels[index], kTabNames[index], false);
            Restyle(*page, _tabLabels[index], active ? "DefaultFontBold" : "LightFont", kTabSize, active ? kTitleInk : kFadedInk, "center");
            SDK::SFlashDisplayInfo placement;
            placement.SetX(kLeft + slot * static_cast<float>(index));
            placement.SetY(kTabsTop);
            page->SetDisplayInfo(_tabLabels[index], placement);
        }
        page->InvokeScalars(_tabMark, "clear", {});
        FillRectangle(*page, _tabMark, kLeft + slot * static_cast<float>(open) + 10.0F, kTabsTop - 2.0F, slot - 20.0F, 30.0F, kMarkFill, kMarkAlpha, kMarkEdge, kEdgeAlpha);
        _shownTabs = signature;
    }

    void CharacterCreatorService::RefreshLine(KCDC::Core::NativeUI &page, Line &line, std::size_t index, const Selection &selection) {
        const KCDC::Shared::Game::AppearancePart part = selection.part;
        const std::vector<Family> &families           = selection.families;
        const Placement at                            = selection.at;
        const Family &family                          = families[at.family];
        const bool shown                              = index < selection.Rows();

        static constexpr const char *kFamilyNouns[] = {"Skin", "Face", "Style", "Beard"};
        static constexpr const char *kMemberNouns[] = {"Variant", "Version", "Colour", "Variant"};
        const std::size_t partIndex                 = static_cast<std::size_t>(part);
        const bool beard                            = part == KCDC::Shared::Game::AppearancePart::Beard;

        std::string label;
        std::string value;
        std::string count;
        std::string graphics;
        std::size_t position = 0;
        std::size_t total    = 0;
        if (shown && index == 0) {
            label    = kFamilyNouns[partIndex];
            value    = at.family == 0 ? (beard ? "None" : _look.gender == KCDC::Shared::Game::AppearanceGender::Female ? "Default" : "Your own") : Readable(family.key, kFamilyNouns[partIndex]);
            position = at.family;
            total    = families.size();
        }
        else if (shown) {
            label    = kMemberNouns[partIndex];
            value    = MemberLabel(family.key, family.members[at.member]);
            position = at.member;
            total    = family.members.size();
        }
        if (shown) {
            count = std::to_string(position + 1) + " / " + std::to_string(total);
        }

        // Hair colours draw as swatches, everything else as a bar of where
        // the choice sits in its list.
        const bool swatches = shown && index == 1 && std::all_of(family.members.begin(), family.members.end(), [](std::string_view member) {
            return HairColourOf(member) != nullptr;
        });
        if (shown) {
            graphics = (swatches ? "s" : "b") + std::to_string(position) + "/" + std::to_string(total);
        }

        if (line.shownLabel != label) {
            page.SetText(line.label, label.c_str(), false);
            line.shownLabel = label;
        }
        if (line.shownValue != value) {
            page.SetText(line.value, value.c_str(), false);
            line.shownValue = value;
        }
        if (line.shownCount != count) {
            page.SetText(line.count, count.c_str(), false);
            line.shownCount = count;
        }
        if (line.shownArrows != static_cast<int>(shown)) {
            page.SetText(line.previous, shown ? "<" : "", false);
            page.SetText(line.next, shown ? ">" : "", false);
            line.shownArrows = static_cast<int>(shown);
        }
        if (line.shownGraphics == graphics) {
            return;
        }
        page.InvokeScalars(line.graphics, "clear", {});
        if (swatches) {
            const float span  = static_cast<float>(family.members.size()) * (kSwatchSize + kSwatchGap) - kSwatchGap;
            const float start = 0.5F * (kValueWidth - span);
            for (std::size_t member = 0; member < family.members.size(); ++member) {
                const HairColour *colour = HairColourOf(family.members[member]);
                const float x            = start + static_cast<float>(member) * (kSwatchSize + kSwatchGap);
                const bool selected      = member == at.member;
                FillRectangle(page, line.graphics, x, selected ? -3.0F : 0.0F, kSwatchSize, kSwatchSize * 0.6F + (selected ? 6.0F : 0.0F), colour->rgb, 100.0, selected ? kGold : kMarkEdge, selected ? 100.0 : 50.0);
            }
        }
        else if (total > 1) {
            const float track = kValueWidth - 2.0F * kBarInset;
            const float fill  = std::max(6.0F, track / static_cast<float>(total));
            const float x     = kBarInset + (track - fill) * static_cast<float>(position) / static_cast<float>(total - 1);
            FillRectangle(page, line.graphics, kBarInset, 1.0F, track, 2.0F, kFadedInk, 30.0);
            FillRectangle(page, line.graphics, x, 0.0F, fill, kBarHeight, kGold, 95.0);
        }
        line.shownGraphics = graphics;
    }

    void CharacterCreatorService::Refresh() {
        std::shared_ptr<KCDC::Core::NativeUI> page = Page();
        if (!page) {
            return;
        }

        if (_shownHeading != _title) {
            page->SetText(_heading, _title.c_str(), false);
            _shownHeading = _title;
        }
        RefreshTabs();

        // A face change can take the member row away, so the selected row is held to what the tab now has.
        const Selection selection = Select();
        _row                      = std::min(_row, selection.Rows() - 1);
        for (std::size_t index = 0; index < _lines.size(); ++index) {
            RefreshLine(*page, _lines[index], index, selection);
        }

        const int mark = static_cast<int>(_row);
        if (mark != _shownMark) {
            Place(*page, _mark, kLeft - 6.0F, kRowsTop + kRowHeight * static_cast<float>(mark) + 2.0F);
            page->SetVisible(_mark, true);
            _shownMark = mark;
        }
        _dirty = false;
    }

    // --- Input -----------------------------------------------------------------

    SDK::IActionMap *CharacterCreatorService::EnsureInputMap() {
        SDK::ISystem *system            = SDK::ISystem::GetInstance();
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();
        if (system == nullptr || manager == nullptr) {
            return nullptr;
        }
        if (SDK::IActionMap *map = manager->GetActionMap(kMapName); map != nullptr) {
            return map;
        }

        // Created disabled; only `AcquireInput` turns it on.
        SDK::XmlNodeRef node = system->LoadXmlFromBuffer(kMapXml, sizeof(kMapXml) - 1, false, false);
        SDK::IActionMap *map = node.IsValid() ? manager->CreateActionMap(kMapName) : nullptr;
        if (map == nullptr) {
            return nullptr;
        }
        if (!map->LoadFromXML(node)) {
            manager->RemoveActionMap(kMapName);
            return nullptr;
        }
        return map;
    }

    void CharacterCreatorService::AcquireInput() {
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();
        SDK::IActionMap *map            = _listening ? nullptr : EnsureInputMap();
        if (manager == nullptr || map == nullptr || !manager->AddExtraActionListener(this, kMapName)) {
            return;
        }
        _inputManager = manager;
        _inputMap     = map;
        _listening    = true;
        manager->EnableActionMap(kMapName, true);
    }

    void CharacterCreatorService::ReleaseInput() {
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();

        // Only while it is still the manager this service took: a level
        // change replaces it, and the old one is freed.
        if (manager != nullptr && manager == _inputManager && _inputMap != nullptr && manager->GetActionMap(kMapName) == _inputMap) {
            manager->EnableActionMap(kMapName, false);
            if (_listening) {
                manager->RemoveExtraActionListener(this, kMapName);
            }
            manager->RemoveActionMap(kMapName);
        }
        _inputManager = nullptr;
        _inputMap     = nullptr;
        _listening    = false;
    }

    /** Runs inside the engine's input dispatch, so it only counts presses. */
    void CharacterCreatorService::OnAction(const SDK::CCryName &action, int activationMode, float) {
        if (_phase != Phase::Shown || (activationMode & (kOnPress | kOnHold)) == 0) {
            return;
        }
        const char *name = action.c_str();
        if (std::strcmp(name, "kcdc_cc_up") == 0) {
            --_pendingRow;
        }
        else if (std::strcmp(name, "kcdc_cc_down") == 0) {
            ++_pendingRow;
        }
        else if (std::strcmp(name, "kcdc_cc_left") == 0) {
            --_pendingStep;
        }
        else if (std::strcmp(name, "kcdc_cc_right") == 0) {
            ++_pendingStep;
        }
        else if (std::strcmp(name, "kcdc_cc_tab_prev") == 0) {
            --_pendingTab;
        }
        else if (std::strcmp(name, "kcdc_cc_tab_next") == 0) {
            ++_pendingTab;
        }
        else if (std::strcmp(name, "kcdc_cc_gender") == 0) {
            _pendingGender = true;
        }
        else if (std::strcmp(name, "kcdc_cc_random") == 0) {
            _pendingRandom = true;
        }
        else if (std::strcmp(name, "kcdc_cc_reset") == 0) {
            _pendingReset = true;
        }
        else if (std::strcmp(name, "kcdc_cc_accept") == 0) {
            _pendingAccept = true;
        }
        else if (std::strcmp(name, "kcdc_cc_cancel") == 0) {
            _pendingCancel = true;
        }
    }
} // namespace KCDC::Features::CharacterCreator
