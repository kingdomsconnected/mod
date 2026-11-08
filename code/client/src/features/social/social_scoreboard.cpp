#include <utils/safe_win32.h>

#include "social_scoreboard.h"

#include "core/application.h"
#include "core/input/native_keybinds.h"
#include "core/ui/native_ui/native_ui.h"
#include "core/ui/native_ui/native_ui_host.h"

#include "sdk/cryengine/system/i_system.h"
#include "sdk/wh/entitymodule/c_player.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <core_modules.h>
#include <cstring>
#include <logging/logger.h>
#include <memory>
#include <networking/network_peer.h>
#include <unordered_map>
#include <utility>

namespace KCDC::Features::Social {
    namespace {
        constexpr const char *kLogger = "Scoreboard";

        constexpr char kResourceName[] = "kcdc-scoreboard";
        constexpr char kMapName[]      = "kcdc_scoreboard_scroll";
        constexpr char kFilterName[]   = "kcdc_scoreboard_wheel";

        /** Any value does: nothing else holds a reason on this filter. */
        constexpr std::uint8_t kFilterReason = 0;

        /** The wheel, at the rank the key itself lives at, so it scrolls wherever the list can be up. */
        constexpr char kMapXml[] = "<actionmap name=\"kcdc_scoreboard_scroll\" priority=\"gameplay\" exclusivity=\"0\">"
                                   "<action name=\"kcdc_scoreboard_up\" onPress=\"1\" onRelease=\"0\" onHold=\"0\" keyboard=\"mwheel_up\"/>"
                                   "<action name=\"kcdc_scoreboard_down\" onPress=\"1\" onRelease=\"0\" onHold=\"0\" keyboard=\"mwheel_down\"/>"
                                   "</actionmap>";

        /**
         * On foot the wheel is free; in combat it switches opponent
         * (keybindSuperactions.xml `opponent_next`/`opponent_prev`), which a
         * player scrolling the list does not mean.
         */
        constexpr char kFilterXml[] = "<actionfilter name=\"kcdc_scoreboard_wheel\" type=\"actionFail\"><action name=\"combat_switch_opp_next\"/><action name=\"combat_switch_opp_prev\"/></actionfilter>";

        /** The game's combat map, the one `opponent_lock` is on. */
        constexpr char kCombatActionMapName[] = "combat";

        /**
         * The tutorial note's frame, cut by the game into a head, a body that
         * repeats and a foot. The head carries the carved cornice and 380 px
         * of parchment, the foot 36 px of parchment and the base moulding.
         */
        constexpr char kTopTexture[]    = "img://Libs/UI/Textures/tutorial_top_wide.dds";
        constexpr char kMiddleTexture[] = "img://Libs/UI/Textures/tutorial_middle_wide.dds";
        constexpr char kBottomTexture[] = "img://Libs/UI/Textures/tutorial_bottom_wide.dds";

        /** The codex's red pen flourish, and the gold hairline the menus rule their hints with. */
        constexpr char kFlourishTexture[] = "img://Libs/UI/Textures/skillBook_line_ornament.dds";
        constexpr char kRuleTexture[]     = "img://Libs/UI/Textures/menu_hint_line.dds";

        constexpr float kTextureWidth      = 1024.0F;
        constexpr float kTopTextureHeight  = 512.0F;
        constexpr float kTileTextureHeight = 128.0F;
        constexpr float kFlourishWidth     = 128.0F;
        constexpr float kRuleTextureHeight = 16.0F;

        /**
         * Layout units: a 1080-line screen. The root carries the conversion to
         * the stage and the aspect correction, so everything below is written
         * once, in these.
         */
        constexpr float kLayoutHeight = 1080.0F;

        constexpr float kFrameScale      = 0.6F;
        constexpr float kFrameWidth      = kTextureWidth * kFrameScale;
        constexpr float kTopHeight       = kTopTextureHeight * kFrameScale;
        constexpr float kTileHeight      = kTileTextureHeight * kFrameScale;
        constexpr float kBottomParchment = 36.0F * kFrameScale;

        /** The frame hangs from here and grows down, so the title stays put as players come and go. */
        constexpr float kPanelTop = 96.0F;

        /** The parchment starts 70 px inside the texture's edge; the writing sits a margin inside that. */
        constexpr float kLeft  = -0.5F * kFrameWidth + 70.0F;
        constexpr float kRight = 0.5F * kFrameWidth - 70.0F;
        constexpr float kWidth = kRight - kLeft;

        constexpr float kTitleTop     = 86.0F;
        constexpr float kFlourishTop  = 132.0F;
        constexpr float kCountTop     = 162.0F;
        constexpr float kRuleTop      = 196.0F;
        constexpr float kHeadingTop   = 206.0F;
        constexpr float kRowsTop      = 236.0F;
        constexpr float kRowHeight    = 32.0F;
        constexpr float kTextInset    = 10.0F;
        constexpr float kIdWidth      = 70.0F;
        constexpr float kPingWidth    = 110.0F;
        constexpr float kNameLeft     = kLeft + kTextInset + kIdWidth + kTextInset;
        constexpr float kNameWidth    = kRight - kTextInset - kPingWidth - kNameLeft;
        constexpr float kFooterHeight = 26.0F;

        constexpr double kTitleSize   = 34.0;
        constexpr double kCountSize   = 19.0;
        constexpr double kHeadingSize = 17.0;
        constexpr double kNameSize    = 21.0;
        constexpr double kPingSize    = 20.0;

        /** Ink on parchment, as the game's notes and the codex write it. */
        constexpr int kTitleInk     = 0x2A1D0E;
        constexpr int kInk          = 0x3B2A16;
        constexpr int kFadedInk     = 0x6B5335;
        constexpr int kPingGood     = 0x5A452A;
        constexpr int kPingFair     = 0x8A5A1A;
        constexpr int kPingPoor     = 0x8A2A1E;
        constexpr int kMarkFill     = 0xC8912E;
        constexpr int kMarkEdge     = 0x8A6A2A;
        constexpr double kMarkAlpha = 22.0;
        constexpr double kEdgeAlpha = 45.0;
        constexpr double kRuleAlpha = 12.0;
        constexpr double kPinAlpha  = 30.0;

        /** Pings above these are worth a second look, and then worth a red one. */
        constexpr std::uint16_t kFairPing = 120;
        constexpr std::uint16_t kPoorPing = 250;

        /** A name longer than this is cut and marked, rather than running into the ping. */
        constexpr std::size_t kNameCharacters = 26;

        /** Rows one notch of the wheel moves. */
        constexpr int kWheelStep = 3;

        constexpr std::chrono::milliseconds kCombatHoldDelay {200};
        constexpr std::chrono::milliseconds kRefreshInterval {2000};

        /** An open finds an answer older than this stale and asks again at once. */
        constexpr std::chrono::milliseconds kStaleAfter {1000};

        constexpr float kFadeInSeconds  = 0.12F;
        constexpr float kFadeOutSeconds = 0.09F;

        SDK::SFlashVarValue Coordinate(float value) {
            return SDK::SFlashVarValue::Double(static_cast<double>(value));
        }

        bool Place(KCDC::Core::NativeUI &page, const std::string &clip, float x, float y) {
            SDK::SFlashDisplayInfo placement;
            placement.SetX(x);
            placement.SetY(y);
            return page.SetDisplayInfo(clip, placement);
        }

        /** A game texture at its top-left, scaled per axis in percent of its natural size. */
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

        bool DrawRectangle(KCDC::Core::NativeUI &page, const std::string &clip, float width, float height) {
            bool ok = page.InvokeScalars(clip, "lineStyle", {SDK::SFlashVarValue::Double(1.0), SDK::SFlashVarValue::Int(kMarkEdge), SDK::SFlashVarValue::Double(kEdgeAlpha)});
            ok      = ok && page.InvokeScalars(clip, "beginFill", {SDK::SFlashVarValue::Int(kMarkFill), SDK::SFlashVarValue::Double(kMarkAlpha)});
            ok      = ok && page.InvokeScalars(clip, "moveTo", {Coordinate(0.0F), Coordinate(0.0F)});
            ok      = ok && page.InvokeScalars(clip, "lineTo", {Coordinate(width), Coordinate(0.0F)});
            ok      = ok && page.InvokeScalars(clip, "lineTo", {Coordinate(width), Coordinate(height)});
            ok      = ok && page.InvokeScalars(clip, "lineTo", {Coordinate(0.0F), Coordinate(height)});
            ok      = ok && page.InvokeScalars(clip, "lineTo", {Coordinate(0.0F), Coordinate(0.0F)});
            return ok && page.InvokeScalars(clip, "endFill", {});
        }

        bool Restyle(KCDC::Core::NativeUI &page, const std::string &key, const char *font, double size, int colour, const char *align) {
            KCDC::Core::NativeUI::TextStyle style;
            style.font  = font;
            style.size  = size;
            style.color = colour;
            style.align = align;
            return page.SetTextStyle(key, style);
        }

        bool CreateText(KCDC::Core::NativeUI &page, const std::string &key, const std::string &parent, float x, float y, float width, float height, const char *font, double size, int colour, const char *align) {
            return page.CreateTextField(key, parent, key.c_str(), static_cast<double>(x), static_cast<double>(y), static_cast<double>(width), static_cast<double>(height)) && Restyle(page, key, font, size, colour, align) && page.SetText(key, "", false);
        }

        /**
         * Cut at a character, not a byte: a name cut through a UTF-8 sequence
         * draws a replacement glyph where its last letter was.
         */
        std::string ShortName(const std::string &name) {
            std::size_t characters = 0;
            for (std::size_t index = 0; index < name.size(); ++index) {
                if ((static_cast<unsigned char>(name[index]) & 0xC0U) == 0x80U) {
                    continue;
                }
                if (characters == kNameCharacters) {
                    return name.substr(0, index) + "...";
                }
                ++characters;
            }
            return name;
        }

        int PingColour(std::uint16_t ping) {
            if (ping > kPoorPing) {
                return kPingPoor;
            }
            return ping > kFairPing ? kPingFair : kPingGood;
        }

        KCDC::Features::UI::NativeUIService *Screens() {
            return KCDC::Core::gApplication != nullptr ? &KCDC::Core::gApplication->GetNativeUIService() : nullptr;
        }

        /** Byte-wise, folding ASCII case only, so a name with accents still has one fixed place. */
        int CompareNames(const std::string &left, const std::string &right) {
            const std::size_t length = std::min(left.size(), right.size());
            for (std::size_t index = 0; index < length; ++index) {
                const int a = std::tolower(static_cast<unsigned char>(left[index]));
                const int b = std::tolower(static_cast<unsigned char>(right[index]));
                if (a != b) {
                    return a < b ? -1 : 1;
                }
            }
            if (left.size() == right.size()) {
                return 0;
            }
            return left.size() < right.size() ? -1 : 1;
        }
    } // namespace

    void ScoreboardService::Install() {
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<KCDC::Shared::RPC::Scoreboard>([](const KCDC::Shared::RPC::Scoreboard &answer, MafiaNet::Packet *) {
            if (!answer.IsSane() || KCDC::Core::gApplication == nullptr) {
                return;
            }
            KCDC::Core::gApplication->GetScoreboardService().Receive(answer);
        });
    }

    void ScoreboardService::Receive(const KCDC::Shared::RPC::Scoreboard &answer) {
        // Copied before the lock: a full roster is a few hundred strings, and the game thread takes this mutex every tick.
        KCDC::Shared::RPC::Scoreboard copy = answer;
        std::lock_guard lock(_inboxMutex);
        _inbox = std::move(copy);
    }

    void ScoreboardService::Drain() {
        std::optional<KCDC::Shared::RPC::Scoreboard> answer;
        {
            std::lock_guard lock(_inboxMutex);
            answer.swap(_inbox);
        }
        if (answer) {
            Apply(*answer);
        }
    }

    void ScoreboardService::Apply(KCDC::Shared::RPC::Scoreboard &answer) {
        if (answer.names) {
            _entries   = std::move(answer.rows);
            _revision  = answer.revision;
            _localSlot = answer.localSlot;
            Sort();
        }
        else if (answer.revision == _revision) {
            std::unordered_map<std::uint16_t, std::size_t> bySlot;
            bySlot.reserve(_entries.size());
            for (std::size_t index = 0; index < _entries.size(); ++index) {
                bySlot.emplace(_entries[index].slot, index);
            }
            for (const KCDC::Shared::RPC::ScoreboardRow &row : answer.rows) {
                if (const auto found = bySlot.find(row.slot); found != bySlot.end()) {
                    _entries[found->second].ping = row.ping;
                }
            }
        }
        else {
            // Pings for a roster this client does not hold. Forgetting the
            // revision makes the next answer carry the names again.
            _revision = 0;
            return;
        }

        _maxPlayers = answer.maxPlayers;
        _scroll     = std::min(_scroll, MaxScroll());
        _dirty      = true;
    }

    void ScoreboardService::Sort() {
        std::sort(_entries.begin(), _entries.end(), [this](const KCDC::Shared::RPC::ScoreboardRow &left, const KCDC::Shared::RPC::ScoreboardRow &right) {
            const bool leftLocal  = left.slot == _localSlot;
            const bool rightLocal = right.slot == _localSlot;
            if (leftLocal != rightLocal) {
                return leftLocal;
            }
            const int order = CompareNames(left.name, right.name);
            return order != 0 ? order < 0 : left.slot < right.slot;
        });
    }

    std::size_t ScoreboardService::Pinned() const {
        return !_entries.empty() && _entries.front().slot == _localSlot ? 1 : 0;
    }

    std::size_t ScoreboardService::MaxScroll() const {
        const std::size_t pinned   = Pinned();
        const std::size_t scrolled = _entries.size() - pinned;
        const std::size_t room     = kVisibleRows - pinned;
        return scrolled > room ? scrolled - room : 0;
    }

    std::shared_ptr<KCDC::Core::NativeUI> ScoreboardService::Page() const {
        KCDC::Features::UI::NativeUIService *screens = Screens();
        return screens != nullptr && _composed ? screens->GetPage(_screen) : nullptr;
    }

    void ScoreboardService::Request() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        KCDC::Shared::RPC::ScoreboardRequest request;
        request.knownRevision = _revision;
        net->BroadcastRPC(request, MafiaNet::Priority::Medium, MafiaNet::Reliability::ReliableOrdered);
        _requestedAt   = std::chrono::steady_clock::now();
        _requestedOnce = true;
    }

    /** Both failures latch, for the emote wheel's reason: a retry every tick would leak the clips it made. */
    void ScoreboardService::EnsureScreen() {
        KCDC::Features::UI::NativeUIService *screens = Screens();
        if (screens == nullptr) {
            return;
        }

        // A level change unloads the host under a live session; every handle
        // died with it, so the composition is rebuilt on the new page.
        if (_composed && !screens->IsMounted(_screen)) {
            if (_open) {
                Close();
            }
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
                Framework::Logging::GetLogger(kLogger)->warn("The player list could not open a screen: {}", error);
            }
            return; // Its host has to load and mount before anything can be drawn.
        }

        if (screens->IsMounted(_screen)) {
            _composed = Compose();
            _failed   = !_composed;
        }
    }

    bool ScoreboardService::Compose() {
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
        _unit       = static_cast<float>(stageHeight) / kLayoutHeight;
        _stageWidth = static_cast<float>(stageWidth);

        const float framePercent = kFrameScale * 100.0F;
        const float frameLeft    = -0.5F * kFrameWidth;

        _root   = screens->NextKey(_screen);
        bool ok = page->CreateClip(_root, "$root", _root.c_str());
        ok      = ok && page->SetVisible(_root, false);

        // The body first and the head over it, so where they meet the
        // cornice's shadow falls on parchment rather than under it.
        for (std::string &tile : _tiles) {
            tile = screens->NextKey(_screen);
            ok   = ok && page->CreateClip(tile, _root, tile.c_str());
            ok   = ok && LoadTexture(*page, tile, kMiddleTexture, frameLeft, kTopHeight, framePercent, framePercent);
            ok   = ok && page->SetVisible(tile, false);
        }

        const std::string top = screens->NextKey(_screen);
        ok                    = ok && page->CreateClip(top, _root, top.c_str());
        ok                    = ok && LoadTexture(*page, top, kTopTexture, frameLeft, 0.0F, framePercent, framePercent);

        _bottom = screens->NextKey(_screen);
        ok      = ok && page->CreateClip(_bottom, _root, _bottom.c_str());
        ok      = ok && LoadTexture(*page, _bottom, kBottomTexture, frameLeft, kTopHeight, framePercent, framePercent);

        // Under the writing: the wash that marks the local player's line, and
        // the faint rules between lines.
        _mark = screens->NextKey(_screen);
        ok    = ok && page->CreateClip(_mark, _root, _mark.c_str());
        ok    = ok && DrawRectangle(*page, _mark, kWidth + 12.0F, kRowHeight);
        ok    = ok && page->SetVisible(_mark, false);

        _rules = screens->NextKey(_screen);
        ok     = ok && page->CreateClip(_rules, _root, _rules.c_str());

        const std::string flourish = screens->NextKey(_screen);
        ok                         = ok && page->CreateClip(flourish, _root, flourish.c_str());
        ok                         = ok && LoadTexture(*page, flourish, kFlourishTexture, -0.5F * kFlourishWidth, kFlourishTop, 100.0F, 100.0F, 90.0F);

        const std::string rule = screens->NextKey(_screen);
        ok                     = ok && page->CreateClip(rule, _root, rule.c_str());
        ok                     = ok && LoadTexture(*page, rule, kRuleTexture, kLeft, kRuleTop, kWidth / kTextureWidth * 100.0F, 8.0F / kRuleTextureHeight * 100.0F, 85.0F);

        const std::string title = screens->NextKey(_screen);
        ok                      = ok && CreateText(*page, title, _root, kLeft, kTitleTop, kWidth, 46.0F, "DisplayFont", kTitleSize, kTitleInk, "center");
        ok                      = ok && page->SetText(title, "Players", false);

        _count = screens->NextKey(_screen);
        ok     = ok && CreateText(*page, _count, _root, kLeft, kCountTop, kWidth, 28.0F, "LightFont", kCountSize, kFadedInk, "center");

        const std::string idHeading = screens->NextKey(_screen);
        ok                          = ok && CreateText(*page, idHeading, _root, kLeft + kTextInset, kHeadingTop, kIdWidth, 26.0F, "LightFont", kHeadingSize, kFadedInk, "left");
        ok                          = ok && page->SetText(idHeading, "ID", false);

        const std::string nameHeading = screens->NextKey(_screen);
        ok                            = ok && CreateText(*page, nameHeading, _root, kNameLeft, kHeadingTop, kNameWidth, 26.0F, "LightFont", kHeadingSize, kFadedInk, "left");
        ok                            = ok && page->SetText(nameHeading, "Name", false);

        const std::string pingHeading = screens->NextKey(_screen);
        ok                            = ok && CreateText(*page, pingHeading, _root, kRight - kTextInset - kPingWidth, kHeadingTop, kPingWidth, 26.0F, "LightFont", kHeadingSize, kFadedInk, "right");
        ok                            = ok && page->SetText(pingHeading, "Ping", false);

        _lines.assign(kVisibleRows, Line {});
        for (std::size_t index = 0; index < _lines.size() && ok; ++index) {
            Line &line    = _lines[index];
            const float y = kRowsTop + kRowHeight * static_cast<float>(index);
            line.id       = screens->NextKey(_screen);
            line.name     = screens->NextKey(_screen);
            line.ping     = screens->NextKey(_screen);
            ok            = ok && CreateText(*page, line.id, _root, kLeft + kTextInset, y, kIdWidth, kRowHeight - 2.0F, "DefaultFont", kNameSize, kInk, "left");
            ok            = ok && CreateText(*page, line.name, _root, kNameLeft, y, kNameWidth, kRowHeight - 2.0F, "DefaultFont", kNameSize, kInk, "left");
            ok            = ok && CreateText(*page, line.ping, _root, kRight - kTextInset - kPingWidth, y + 1.0F, kPingWidth, kRowHeight - 2.0F, "DefaultFont", kPingSize, kPingGood, "right");
        }

        _footer = screens->NextKey(_screen);
        ok      = ok && CreateText(*page, _footer, _root, kLeft, kRowsTop, kWidth, kFooterHeight, "LightFont", kHeadingSize, kFadedInk, "center");

        if (!ok) {
            Framework::Logging::GetLogger(kLogger)->warn("The player list could not be composed");
            _lines.clear();
            return false;
        }

        _laidOutRows = static_cast<std::size_t>(-1);
        _shownCount.clear();
        _shownFooter.clear();
        _shownMark = -2;
        _dirty     = true;
        Framework::Logging::GetLogger(kLogger)->debug("Composed the player list on a {}x{} stage", stageWidth, stageHeight);
        return true;
    }

    void ScoreboardService::ForgetComposition() {
        _composed = false;
        _root.clear();
        _bottom.clear();
        for (std::string &tile : _tiles) {
            tile.clear();
        }
        _count.clear();
        _rules.clear();
        _mark.clear();
        _footer.clear();
        _lines.clear();
        _visible = false;
        _alpha   = 0.0F;
    }

    /**
     * Stretches the frame to hold `shown` lines: the head is fixed, the foot
     * sits under the last line, and the gap between them is bridged by as
     * many body slices as keep each one between two-thirds and one and a
     * half of its own height, so the parchment's grain never smears.
     */
    void ScoreboardService::Layout(KCDC::Core::NativeUI &page, std::size_t shown, bool overflow) {
        const float rowsEnd    = kRowsTop + kRowHeight * static_cast<float>(shown);
        const float contentEnd = overflow ? rowsEnd + kFooterHeight + 18.0F : rowsEnd + 14.0F;
        const float bottomTop  = std::max(contentEnd - kBottomParchment, kTopHeight);
        const float gap        = bottomTop - kTopHeight;

        const std::size_t tiles = gap < 0.5F ? 0 : std::clamp<std::size_t>(static_cast<std::size_t>(gap / kTileHeight + 0.5F), 1, kMaxTiles);
        const float tileHeight  = tiles > 0 ? gap / static_cast<float>(tiles) : 0.0F;
        for (std::size_t index = 0; index < _tiles.size(); ++index) {
            SDK::SFlashDisplayInfo placement;
            placement.SetVisible(index < tiles);
            if (index < tiles) {
                placement.SetY(kTopHeight + tileHeight * static_cast<float>(index));
                placement.SetYScale(tileHeight / kTileTextureHeight * 100.0F);
            }
            page.SetDisplayInfo(_tiles[index], placement);
        }
        Place(page, _bottom, -0.5F * kFrameWidth, bottomTop);
        Place(page, _footer, kLeft, rowsEnd + 6.0F);

        // The line under a pinned local player is drawn a little firmer: it
        // is where the list the wheel scrolls begins.
        const bool pinned = Pinned() == 1;
        page.InvokeScalars(_rules, "clear", {});
        for (std::size_t index = 1; index < shown; ++index) {
            const float y      = kRowsTop + kRowHeight * static_cast<float>(index);
            const double alpha = pinned && index == 1 ? kPinAlpha : kRuleAlpha;
            page.InvokeScalars(_rules, "lineStyle", {SDK::SFlashVarValue::Double(1.0), SDK::SFlashVarValue::Int(kInk), SDK::SFlashVarValue::Double(alpha)});
            page.InvokeScalars(_rules, "moveTo", {Coordinate(kLeft), Coordinate(y)});
            page.InvokeScalars(_rules, "lineTo", {Coordinate(kRight), Coordinate(y)});
        }

        _laidOutRows     = shown;
        _laidOutOverflow = overflow;
    }

    void ScoreboardService::WriteLines(KCDC::Core::NativeUI &page) {
        const std::size_t pinned = Pinned();
        int mark                 = -1;
        for (std::size_t index = 0; index < _lines.size(); ++index) {
            Line &line = _lines[index];

            const KCDC::Shared::RPC::ScoreboardRow *entry = nullptr;
            if (index < pinned) {
                entry = &_entries.front();
            }
            else if (const std::size_t at = pinned + _scroll + (index - pinned); at < _entries.size()) {
                entry = &_entries[at];
            }

            std::string id;
            std::string name;
            std::string ping;
            int pingColour = kPingGood;
            bool local     = false;
            if (entry != nullptr) {
                id         = std::to_string(entry->slot);
                name       = ShortName(entry->name);
                ping       = entry->ping == 0 ? "-" : std::to_string(entry->ping);
                pingColour = PingColour(entry->ping);
                local      = entry->slot == _localSlot;
            }
            else if (index == 0 && _entries.empty()) {
                // Only until the first answer lands, one round trip after the session starts.
                name = "Asking the server...";
            }

            if (local) {
                mark = static_cast<int>(index);
            }
            if (line.shownLocal != static_cast<int>(local)) {
                Restyle(page, line.id, local ? "DefaultFontBold" : "DefaultFont", kNameSize, local ? kTitleInk : kInk, "left");
                Restyle(page, line.name, local ? "DefaultFontBold" : "DefaultFont", kNameSize, local ? kTitleInk : kInk, "left");
                line.shownLocal = static_cast<int>(local);
            }
            if (line.shownPingColour != pingColour) {
                Restyle(page, line.ping, "DefaultFont", kPingSize, pingColour, "right");
                line.shownPingColour = pingColour;
            }
            if (line.shownId != id) {
                page.SetText(line.id, id.c_str(), false);
                line.shownId = std::move(id);
            }
            if (line.shownName != name) {
                page.SetText(line.name, name.c_str(), false);
                line.shownName = std::move(name);
            }
            if (line.shownPing != ping) {
                page.SetText(line.ping, ping.c_str(), false);
                line.shownPing = std::move(ping);
            }
        }

        if (mark != _shownMark) {
            if (mark >= 0) {
                Place(page, _mark, kLeft - 6.0F, kRowsTop + kRowHeight * static_cast<float>(mark));
            }
            page.SetVisible(_mark, mark >= 0);
            _shownMark = mark;
        }
    }

    void ScoreboardService::Refresh() {
        std::shared_ptr<KCDC::Core::NativeUI> page = Page();
        if (!page) {
            return;
        }

        const std::size_t shown = std::clamp<std::size_t>(_entries.size(), 1, kVisibleRows);
        const bool overflow     = _entries.size() > kVisibleRows;
        if (shown != _laidOutRows || overflow != _laidOutOverflow) {
            Layout(*page, shown, overflow);
        }
        WriteLines(*page);

        std::string count;
        if (!_entries.empty()) {
            count = std::to_string(_entries.size());
            if (_maxPlayers > 0) {
                count += " of " + std::to_string(_maxPlayers);
            }
        }
        if (count != _shownCount) {
            page->SetText(_count, count.c_str(), false);
            _shownCount = std::move(count);
        }

        // Counted as the reader counts them, from the top of the whole list;
        // the pinned line is always number one and never part of the range.
        std::string footer;
        if (overflow) {
            const std::size_t pinned = Pinned();
            const std::size_t first  = pinned + _scroll + 1;
            const std::size_t last   = std::min(_entries.size(), pinned + _scroll + (kVisibleRows - pinned));
            footer                   = "Showing " + std::to_string(first) + " to " + std::to_string(last) + " of " + std::to_string(_entries.size()) + " - scroll for more";
        }
        if (footer != _shownFooter) {
            page->SetText(_footer, footer.c_str(), false);
            _shownFooter = std::move(footer);
        }

        _dirty = false;
    }

    bool ScoreboardService::StillAllowed() const {
        return KCDC::Core::gApplication != nullptr && !KCDC::Core::gApplication->AreControlsLocked() && SDK::wh::entitymodule::C_Player::GetInstance() != nullptr;
    }

    /**
     * Whether the game's combat map is live, which is where `opponent_lock`
     * reads Tab. Not yet checked in game: that the map is only active in a
     * fight stance. If it turns out to be active all the time, the only cost
     * is that every press waits `kCombatHoldDelay`.
     */
    bool ScoreboardService::InCombat() {
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();
        SDK::IActionMap *combat         = manager != nullptr ? manager->GetActionMap(kCombatActionMapName) : nullptr;
        return combat != nullptr && combat->IsActive();
    }

    void ScoreboardService::AcquireInput() {
        SDK::ISystem *system            = SDK::ISystem::GetInstance();
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();
        if (system == nullptr || manager == nullptr || manager->GetActionMap(kMapName) != nullptr) {
            return;
        }

        SDK::XmlNodeRef node = system->LoadXmlFromBuffer(kMapXml, sizeof(kMapXml) - 1, false, false);
        SDK::IActionMap *map = node.IsValid() ? manager->CreateActionMap(kMapName) : nullptr;
        if (map == nullptr) {
            return;
        }
        if (!map->LoadFromXML(node) || !manager->AddExtraActionListener(this, kMapName)) {
            manager->RemoveActionMap(kMapName);
            return;
        }

        _inputManager = manager;
        _inputMap     = map;
        _listening    = true;
        manager->EnableActionMap(kMapName, true);

        _filter = manager->FindOrCreateActionFilter(kFilterName, kFilterXml, sizeof(kFilterXml) - 1);
        if (_filter != nullptr) {
            _filter->Enable(kFilterReason);
        }
    }

    void ScoreboardService::ReleaseInput() {
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();

        // Only while it is still the manager this service took: a level
        // change replaces it, and the old one is freed.
        if (manager != nullptr && manager == _inputManager) {
            if (_filter != nullptr && manager->GetActionFilter(kFilterName) == _filter) {
                _filter->Disable(kFilterReason, true);
            }
            if (_inputMap != nullptr && manager->GetActionMap(kMapName) == _inputMap) {
                manager->EnableActionMap(kMapName, false);
                if (_listening) {
                    manager->RemoveExtraActionListener(this, kMapName);
                }
                manager->RemoveActionMap(kMapName);
            }
        }

        _inputManager  = nullptr;
        _inputMap      = nullptr;
        _filter        = nullptr;
        _listening     = false;
        _pendingScroll = 0;
    }

    void ScoreboardService::Open() {
        std::shared_ptr<KCDC::Core::NativeUI> page = Page();
        KCDC::Core::NativeUIHost *host             = page ? Screens()->GetHost(_screen) : nullptr;
        if (host == nullptr) {
            return;
        }

        // Measured on every open: the window can be resized between one look
        // at the list and the next.
        SDK::SFlashDisplayInfo placement;
        placement.SetX(0.5F * _stageWidth);
        placement.SetY(kPanelTop * _unit);
        placement.SetXScale(_unit * host->GetSquareUnitCorrection() * 100.0F);
        placement.SetYScale(_unit * 100.0F);
        placement.SetAlpha(_visible ? _alpha * 100.0F : 0.0F);
        placement.SetVisible(true);
        page->SetDisplayInfo(_root, placement);

        AcquireInput();
        _open    = true;
        _visible = true;
        _scroll  = 0;
        _dirty   = true;
        Refresh();

        if (!_requestedOnce || _revision == 0 || std::chrono::steady_clock::now() - _requestedAt > kStaleAfter) {
            Request();
        }
    }

    void ScoreboardService::Close() {
        _open = false;
        ReleaseInput();
    }

    void ScoreboardService::Fade(float elapsedSeconds) {
        if (!_visible) {
            return;
        }
        std::shared_ptr<KCDC::Core::NativeUI> page = Page();
        if (!page) {
            _visible = false;
            _alpha   = 0.0F;
            return;
        }

        const float before = _alpha;
        _alpha             = _open ? std::min(1.0F, _alpha + elapsedSeconds / kFadeInSeconds) : std::max(0.0F, _alpha - elapsedSeconds / kFadeOutSeconds);
        if (_alpha == before) {
            return;
        }

        SDK::SFlashDisplayInfo placement;
        placement.SetAlpha(_alpha * 100.0F);
        if (!_open && _alpha <= 0.0F) {
            placement.SetVisible(false);
            _visible = false;
        }
        page->SetDisplayInfo(_root, placement);
    }

    void ScoreboardService::Update() {
        // A server that turned the list off gets no screen and no asks, and
        // Tab is left to the game's opponent lock alone.
        if (!KCDC::Core::gApplication->IsSessionScoreboardEnabled()) {
            return;
        }

        EnsureScreen();
        Drain();

        // Once per session, so the first press already has names to show.
        if (!_requestedOnce) {
            Request();
        }

        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const float elapsed                             = _lastTick.time_since_epoch().count() == 0 ? 0.0F : std::clamp(std::chrono::duration<float>(now - _lastTick).count(), 0.0F, 0.1F);
        _lastTick                                       = now;

        const bool held = KCDC::Core::NativeKeybinds::Get().IsHeld(KCDC::Core::ModKeybind::Scoreboard);
        if (held && !_heldLast) {
            _heldSince = now;
        }
        _heldLast = held;

        if (_open) {
            if (!held || !_composed || !StillAllowed()) {
                Close();
            }
            else {
                if (_pendingScroll != 0) {
                    // Up the wheel is up the list, towards row one.
                    const long long target = std::clamp(static_cast<long long>(_scroll) - static_cast<long long>(_pendingScroll) * kWheelStep, 0LL, static_cast<long long>(MaxScroll()));
                    _pendingScroll         = 0;
                    if (static_cast<std::size_t>(target) != _scroll) {
                        _scroll = static_cast<std::size_t>(target);
                        _dirty  = true;
                    }
                }
                if (now - _requestedAt > kRefreshInterval) {
                    Request();
                }
            }
        }
        else if (held && _composed && StillAllowed() && (!InCombat() || now - _heldSince >= kCombatHoldDelay)) {
            Open();
        }

        if (_dirty && _open) {
            Refresh();
        }
        Fade(elapsed);
    }

    void ScoreboardService::Reset() {
        if (_open) {
            Close(); // Releases the input it took.
        }

        if (_screen != 0) {
            if (KCDC::Features::UI::NativeUIService *screens = Screens(); screens != nullptr) {
                screens->CloseScreen(_screen);
            }
        }
        _screen = 0;
        _failed = false;
        ForgetComposition();

        // The roster was that server's.
        _entries.clear();
        _revision      = 0;
        _localSlot     = 0;
        _maxPlayers    = 0;
        _requestedOnce = false;
        _heldLast      = false;
        _scroll        = 0;
        _dirty         = true;
        {
            std::lock_guard lock(_inboxMutex);
            _inbox.reset();
        }
    }

    /** Runs inside the engine's input dispatch, so it only counts notches. */
    void ScoreboardService::OnAction(const SDK::CCryName &action, int, float) {
        if (!_open) {
            return;
        }
        const char *name = action.c_str();
        if (std::strcmp(name, "kcdc_scoreboard_up") == 0) {
            ++_pendingScroll;
        }
        else if (std::strcmp(name, "kcdc_scoreboard_down") == 0) {
            --_pendingScroll;
        }
    }
} // namespace KCDC::Features::Social
