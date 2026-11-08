#include <utils/safe_win32.h>

#include "social_emote_wheel.h"

#include "core/application.h"
#include "core/input/native_keybinds.h"
#include "core/ui/native_ui/native_ui.h"
#include "core/ui/native_ui/native_ui_host.h"

#include "features/social/social_emote_service.h"
#include "features/world/world_service.h"

#include "sdk/cryengine/scaleform/i_flash_player.h"
#include "sdk/wh/entitymodule/c_player.h"

#include "shared/game/emote_catalog.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <logging/logger.h>
#include <initializer_list>
#include <memory>
#include <utility>

namespace KCDC::Features::Social {
    namespace {
        constexpr const char *kLogger = "Emotes";

        constexpr char kResourceName[] = "kcdc-emote-wheel";
        constexpr char kMapName[]      = "kcdc_emote_wheel";
        constexpr char kFilterName[]   = "kcdc_emote_wheel_look";

        /** Any value does: nothing else holds a reason on this filter. */
        constexpr std::uint8_t kFilterReason = 0;

        /**
         * Deliberately not exclusive: an exclusive map takes the whole of the
         * player's input, and what has to stop is named in the filter below.
         * `keyboard` carries the mouse in CryEngine's vocabulary; `maxis_x`
         * and `maxis_y` are the raw deltas the combat picker reads.
         */
        constexpr char kMapXml[] = "<actionmap name=\"kcdc_emote_wheel\" priority=\"gameplay\" exclusivity=\"0\">"
                                   "<action name=\"kcdc_wheel_x\" onPress=\"0\" onRelease=\"0\" onHold=\"0\" keyboard=\"maxis_x\"/>"
                                   "<action name=\"kcdc_wheel_y\" onPress=\"0\" onRelease=\"0\" onHold=\"0\" keyboard=\"maxis_y\"/>"
                                   "</actionmap>";

        /**
         * Without this the wheel works and the camera spins with it. Attack
         * and block are left alone on purpose: taking the player's defence
         * away for the length of a gesture is worse than a stray click.
         */
        constexpr char kFilterXml[] = "<actionfilter name=\"kcdc_emote_wheel_look\" type=\"actionFail\">"
                                      "<action name=\"rotateyaw\"/>"
                                      "<action name=\"rotatepitch\"/>"
                                      "<action name=\"mouse_looking_x\"/>"
                                      "<action name=\"mouse_looking_y\"/>"
                                      "</actionfilter>";

        /**
         * Layout units. Everything is drawn inside one container whose
         * horizontal scale cancels the movie's stretch, so a radius here is a
         * radius on screen. See `NativeUIHost::GetSquareUnitCorrection`.
         */
        constexpr float kOuterRadius = 204.0F;
        constexpr float kInnerRadius = 92.0F;

        /**
         * `pickpocket_background` is the pickpocketing minigame's dial: a
         * carved gothic rose window, 1024 square, with a gold-ticked bezel at
         * its rim, radially symmetric enough not to fight six segments.
         *
         * The vellum is the mean colour of `menu_selector_small_middle`
         * rather than that texture, because a nine-slice middle cannot be
         * laid over a wheel without cropping its own edge hairlines.
         */
        constexpr char kBezelTexture[]    = "img://Libs/UI/Textures/Minigames/pickpocket_background.dds";
        constexpr float kBezelTextureSize = 1024.0F;

        /**
         * Where the carved rim sits. The ratio between these two is the
         * texture's own: its wood reaches the edge of the square, and the
         * tracery it frames starts at about 0.72 of the radius, so an
         * annulus with that ratio crops exactly the bezel.
         */
        constexpr float kBezelInner = kOuterRadius + 4.0F;
        constexpr float kBezelOuter = kBezelInner / 0.72F;

        /** Degrees of clear space either side of a segment, so the ring reads as one. */
        constexpr float kSegmentGap = 1.4F;

        /** Straight lines per arc. Ten is past the point the eye can find a corner. */
        constexpr int kArcSteps = 10;

        /**
         * Labels sit outside the ring because inside it the widest a label
         * may be is the segment's chord, and "Respectful nod" does not fit at
         * any readable size. Outside there is nothing to overflow into.
         */
        constexpr float kLabelAnchor = kBezelOuter + 28.0F;
        constexpr float kLabelWidth  = 300.0F;
        constexpr float kLabelHeight = 34.0F;
        constexpr double kLabelSize  = 24.0;

        /** The readout in the hub: what the pointer is on, spelled out once. */
        constexpr float kReadoutWidth  = 2.0F * kInnerRadius * 0.94F;
        constexpr float kReadoutHeight = 30.0F;
        constexpr double kReadoutSize  = 21.0;

        /** Stage units the pointer travels per unit of mouse delta. */
        constexpr float kPointerSensitivity = 1.0F;

        /** How far out the pointer is allowed to sit, which is just inside the rim. */
        constexpr float kPointerReach = kOuterRadius - 14.0F;

        /** A wheel whose release event never arrived closes itself. */
        constexpr std::chrono::seconds kHoldCeiling {20};

        /** Nothing here is bright except the thing being pointed at. */
        constexpr int kShadowColour   = 0x000000;
        constexpr int kVellumColour   = 0xE0CE8C;
        constexpr int kSegmentEdge    = 0x5A452A;
        constexpr int kSegmentFillLit = 0xC8912E;
        constexpr int kSegmentEdgeLit = 0x4A3413;
        constexpr int kHubEdge        = 0x6B5335;
        constexpr int kLabelColour    = 0xD9CDB2;
        constexpr int kLabelColourLit = 0xFFF3D6;
        constexpr int kInkColour      = 0x3B2A16;
        constexpr int kPointerColour  = 0x2A1D0E;
        constexpr int kPointerEdge    = 0xEBD9A8;
        constexpr int kGlyphColour    = kInkColour;

        constexpr double kBackdropAlpha   = 26.0;
        constexpr double kBezelAlpha      = 96.0;
        constexpr double kVellumAlpha     = 95.0;
        constexpr double kSegmentAlphaLit = 52.0;
        constexpr double kEdgeAlpha       = 60.0;
        constexpr double kEdgeAlphaLit    = 90.0;
        constexpr double kGlyphAlpha      = 90.0;

        /**
         * The stroke is deliberately heavy: a hairline figure at this size is
         * a stick man, a thick one with round caps is a pictogram.
         */
        constexpr float kGlyphRadius = 0.5F * (kOuterRadius + kInnerRadius);
        constexpr float kGlyphScale  = 1.6F;
        constexpr float kGlyphStroke = 4.4F;
        constexpr float kGlyphHead   = 4.6F;

        /** Lifts the figure so its own middle, not its origin, sits on the radius. */
        constexpr float kGlyphCentring = 4.8F;

        constexpr float kPi = 3.14159265358979323846F;

        float Radians(float degrees) {
            return degrees * kPi / 180.0F;
        }

        /** Angles run clockwise from straight up, which is where segment zero sits. */
        float SegmentSweep(std::size_t count) {
            return count > 0 ? 360.0F / static_cast<float>(count) : 360.0F;
        }

        SDK::SFlashVarValue Coordinate(float value) {
            return SDK::SFlashVarValue::Double(static_cast<double>(value));
        }

        bool PathTo(KCDC::Core::NativeUI &page, const std::string &clip, const char *method, float degrees, float radius) {
            const float radians = Radians(degrees);
            return page.InvokeScalars(clip, method, {Coordinate(std::sin(radians) * radius), Coordinate(-std::cos(radians) * radius)});
        }

        bool Stroke(KCDC::Core::NativeUI &page, const std::string &clip, float thickness, int colour, double alpha) {
            return page.InvokeScalars(clip, "lineStyle", {SDK::SFlashVarValue::Double(static_cast<double>(thickness)), SDK::SFlashVarValue::Int(colour), SDK::SFlashVarValue::Double(alpha)});
        }

        bool Fill(KCDC::Core::NativeUI &page, const std::string &clip, int colour, double alpha) {
            return page.InvokeScalars(clip, "beginFill", {SDK::SFlashVarValue::Int(colour), SDK::SFlashVarValue::Double(alpha)});
        }

        /**
         * One ring segment: an arc out at the rim, an arc back at the hub,
         * and straight sides between them, stroked and filled in one pass.
         */
        bool DrawSegment(KCDC::Core::NativeUI &page, const std::string &clip, float centreDegrees, float sweepDegrees, int fill, double fillAlpha, int edge, double edgeAlpha, float thickness) {
            const float half  = 0.5F * sweepDegrees - kSegmentGap;
            const float start = centreDegrees - half;
            const float end   = centreDegrees + half;
            const float step  = (end - start) / static_cast<float>(kArcSteps);

            bool ok = Stroke(page, clip, thickness, edge, edgeAlpha);
            ok      = ok && Fill(page, clip, fill, fillAlpha);
            ok      = ok && PathTo(page, clip, "moveTo", start, kInnerRadius);
            ok      = ok && PathTo(page, clip, "lineTo", start, kOuterRadius);
            for (int index = 1; index <= kArcSteps; ++index) {
                ok = ok && PathTo(page, clip, "lineTo", start + step * static_cast<float>(index), kOuterRadius);
            }
            ok = ok && PathTo(page, clip, "lineTo", end, kInnerRadius);
            for (int index = kArcSteps - 1; index >= 0; --index) {
                ok = ok && PathTo(page, clip, "lineTo", start + step * static_cast<float>(index), kInnerRadius);
            }
            return ok && page.InvokeScalars(clip, "endFill", {});
        }

        bool DrawDisc(KCDC::Core::NativeUI &page, const std::string &clip, float radius, int fill, double fillAlpha, int edge, double edgeAlpha, float thickness, int steps = 32) {
            bool ok = thickness > 0.0F ? Stroke(page, clip, thickness, edge, edgeAlpha) : true;
            ok      = ok && Fill(page, clip, fill, fillAlpha);
            ok      = ok && PathTo(page, clip, "moveTo", 0.0F, radius);
            for (int index = 1; index <= steps; ++index) {
                ok = ok && PathTo(page, clip, "lineTo", 360.0F * static_cast<float>(index) / static_cast<float>(steps), radius);
            }
            return ok && page.InvokeScalars(clip, "endFill", {});
        }

        /**
         * `lineStyle`'s long form, for the round caps: at this thickness butt
         * caps leave every limb ending in a chopped rectangle.
         */
        bool GlyphStroke(KCDC::Core::NativeUI &page, const std::string &clip) {
            return page.InvokeScalars(clip, "lineStyle",
                {SDK::SFlashVarValue::Double(static_cast<double>(kGlyphStroke * kGlyphScale)), SDK::SFlashVarValue::Int(kGlyphColour), SDK::SFlashVarValue::Double(kGlyphAlpha), SDK::SFlashVarValue::Bool(false), SDK::SFlashVarValue::String("normal"), SDK::SFlashVarValue::String("round"),
                    SDK::SFlashVarValue::String("round"), SDK::SFlashVarValue::Double(3.0)});
        }

        bool Limb(KCDC::Core::NativeUI &page, const std::string &clip, float x1, float y1, float x2, float y2) {
            bool ok = page.InvokeScalars(clip, "moveTo", {Coordinate(x1 * kGlyphScale), Coordinate(y1 * kGlyphScale)});
            return ok && page.InvokeScalars(clip, "lineTo", {Coordinate(x2 * kGlyphScale), Coordinate(y2 * kGlyphScale)});
        }

        /** A filled head. Drawn last, so it need not put the limb stroke back. */
        bool Head(KCDC::Core::NativeUI &page, const std::string &clip, float x, float y) {
            constexpr int kSteps = 16;
            bool ok              = page.InvokeScalars(clip, "lineStyle", {});
            ok                   = ok && Fill(page, clip, kGlyphColour, kGlyphAlpha);
            for (int index = 0; index <= kSteps; ++index) {
                const float radians = 2.0F * kPi * static_cast<float>(index) / static_cast<float>(kSteps);
                const float px      = (x + std::sin(radians) * kGlyphHead) * kGlyphScale;
                const float py      = (y - std::cos(radians) * kGlyphHead) * kGlyphScale;
                ok                  = ok && page.InvokeScalars(clip, index == 0 ? "moveTo" : "lineTo", {Coordinate(px), Coordinate(py)});
            }
            return ok && page.InvokeScalars(clip, "endFill", {});
        }

        /**
         * A filled outline in glyph units, which is how a garment is drawn.
         * It leaves the limb stroke set behind it, because clearing the line
         * style to fill is otherwise a trap for whatever draws next.
         */
        bool Garment(KCDC::Core::NativeUI &page, const std::string &clip, std::initializer_list<std::pair<float, float>> points) {
            bool ok    = page.InvokeScalars(clip, "lineStyle", {});
            ok         = ok && Fill(page, clip, kGlyphColour, kGlyphAlpha);
            bool first = true;
            for (const std::pair<float, float> &point : points) {
                ok    = ok && page.InvokeScalars(clip, first ? "moveTo" : "lineTo", {Coordinate(point.first * kGlyphScale), Coordinate(point.second * kGlyphScale)});
                first = false;
            }
            ok = ok && page.InvokeScalars(clip, "endFill", {});
            return ok && GlyphStroke(page, clip);
        }

        /**
         * A figure in a cotte, drawn rather than loaded because the game has
         * no gesture art. Its 5466 icons are items, perks, map pins and stat
         * badges; the perk set is the right language -- flat brown
         * silhouettes, shape in alpha -- but it is authored for a 64-pixel
         * grid and turns to mud at the size a segment gives it.
         *
         * They do not try to be unambiguous: a nod and a respectful nod
         * cannot be told apart as pictures, and the label names them. They
         * give each direction a shape the hand learns.
         */
        bool DrawGlyph(KCDC::Core::NativeUI &page, const std::string &clip, std::uint16_t emoteId) {
            // The cotte: narrow at the shoulder, flared at the hem, leaning
            // by however much this gesture bends the body forward.
            const auto cotte = [&](float lean, float hem) {
                return Garment(page, clip, {{-5.0F + lean * 0.8F, -9.0F}, {5.0F + lean * 0.8F, -9.0F}, {8.5F, hem}, {-8.5F, hem}});
            };
            const auto legs = [&](float hem) {
                return Limb(page, clip, -3.5F, hem - 1.0F, -5.0F, 16.0F) && Limb(page, clip, 3.5F, hem - 1.0F, 5.0F, 16.0F);
            };

            bool ok     = true;
            float headX = 0.0F;
            float headY = -15.0F;
            switch (emoteId) {
            case 1: // Wave: a forearm raised clear of the head.
                ok = cotte(0.0F, 9.0F) && legs(9.0F);
                ok = ok && Limb(page, clip, -5.0F, -8.0F, -8.0F, 1.0F);
                ok = ok && Limb(page, clip, 5.0F, -8.0F, 8.0F, -11.0F);
                ok = ok && Limb(page, clip, 8.0F, -11.0F, 10.0F, -17.0F);
                break;
            case 2: // Big wave: the same arm, thrown higher and wider.
                ok = cotte(0.0F, 9.0F) && legs(9.0F);
                ok = ok && Limb(page, clip, -5.0F, -8.0F, -8.0F, 1.0F);
                ok = ok && Limb(page, clip, 5.0F, -8.0F, 9.0F, -13.0F);
                ok = ok && Limb(page, clip, 9.0F, -13.0F, 12.0F, -21.0F);
                break;
            case 3: // Nod: the body barely leans, the head carries forward.
                headX = 3.0F;
                ok    = cotte(1.5F, 9.0F) && legs(9.0F);
                ok    = ok && Limb(page, clip, -5.0F, -8.0F, -7.0F, 1.0F);
                ok    = ok && Limb(page, clip, 5.0F, -8.0F, 7.0F, 1.0F);
                break;
            case 4: // Respectful nod: further over, one hand to the chest.
                headX = 6.0F;
                headY = -13.0F;
                ok    = cotte(3.0F, 9.0F) && legs(9.0F);
                ok    = ok && Limb(page, clip, 4.0F, -8.0F, 0.0F, -2.0F);
                ok    = ok && Limb(page, clip, -4.0F, -8.0F, -6.0F, 2.0F);
                break;
            case 5: // Bow: folded at the waist, the cotte folding with it.
                headX = 10.0F;
                headY = -10.0F;
                ok    = Garment(page, clip, {{-5.0F, -2.0F}, {4.0F, -9.0F}, {9.0F, 2.0F}, {-2.0F, 7.0F}}) && legs(6.0F);
                ok    = ok && Limb(page, clip, 5.0F, -4.0F, 3.0F, 5.0F);
                break;
            default: // Cheer: both arms up.
                ok = cotte(0.0F, 9.0F) && legs(9.0F);
                ok = ok && Limb(page, clip, -5.0F, -8.0F, -10.0F, -17.0F);
                ok = ok && Limb(page, clip, 5.0F, -8.0F, 10.0F, -17.0F);
                break;
            }

            // Last, so the head sits over the shoulders rather than under them.
            return ok && Head(page, clip, headX, headY);
        }

        bool DrawRectangle(KCDC::Core::NativeUI &page, const std::string &clip, float width, float height, int colour, double alpha) {
            bool ok = Fill(page, clip, colour, alpha);
            ok      = ok && page.InvokeScalars(clip, "moveTo", {Coordinate(0.0F), Coordinate(0.0F)});
            ok      = ok && page.InvokeScalars(clip, "lineTo", {Coordinate(width), Coordinate(0.0F)});
            ok      = ok && page.InvokeScalars(clip, "lineTo", {Coordinate(width), Coordinate(height)});
            ok      = ok && page.InvokeScalars(clip, "lineTo", {Coordinate(0.0F), Coordinate(height)});
            return ok && page.InvokeScalars(clip, "endFill", {});
        }

        bool Place(KCDC::Core::NativeUI &page, const std::string &clip, float x, float y, bool visible) {
            SDK::SFlashDisplayInfo placement;
            placement.SetX(x);
            placement.SetY(y);
            placement.SetVisible(visible);
            return page.SetDisplayInfo(clip, placement);
        }

        bool Scale(KCDC::Core::NativeUI &page, const std::string &clip, float horizontal) {
            SDK::SFlashDisplayInfo placement;
            placement.SetXScale(horizontal * 100.0F);
            placement.SetYScale(100.0F);
            return page.SetDisplayInfo(clip, placement);
        }

        /**
         * A game texture, centred and blown up to `span`. Loaded content
         * arrives at its natural size with its top-left on the clip origin,
         * hence the half-span offset.
         */
        bool LoadTexture(KCDC::Core::NativeUI &page, const std::string &clip, const char *url, float naturalSize, float span, double alpha) {
            if (!page.LoadClip(clip, url)) {
                return false;
            }
            const float percent = span / naturalSize * 100.0F;
            SDK::SFlashDisplayInfo placement;
            placement.SetX(-span * 0.5F);
            placement.SetY(-span * 0.5F);
            placement.SetXScale(percent);
            placement.SetYScale(percent);
            placement.SetAlpha(static_cast<float>(alpha));
            placement.SetVisible(true);
            return page.SetDisplayInfo(clip, placement);
        }

        KCDC::Features::UI::NativeUIService *Screens() {
            return KCDC::Core::gApplication != nullptr ? &KCDC::Core::gApplication->GetNativeUIService() : nullptr;
        }
    } // namespace

    bool EmoteWheelService::SetSlot(std::size_t slot, std::uint16_t emoteId) {
        if (slot >= kSlotCount || Shared::Game::FindEmote(emoteId) == nullptr) {
            return false;
        }
        _slots[slot] = emoteId;
        return true;
    }

    void EmoteWheelService::ResetSlots() {
        _slots = kDefaultSlots;
    }

    void EmoteWheelService::RefreshSlots() {
        std::shared_ptr<KCDC::Core::NativeUI> page;
        for (std::size_t slot = 0; slot < _segments.size(); ++slot) {
            Segment &segment = _segments[slot];
            if (segment.emoteId == _slots[slot]) {
                continue;
            }
            if (!page) {
                KCDC::Features::UI::NativeUIService *screens = Screens();
                page                                         = screens != nullptr ? screens->GetPage(_screen) : nullptr;
                if (!page) {
                    return;
                }
            }

            // Taken before drawing, so a redraw that fails is logged once
            // rather than retried every tick.
            segment.emoteId  = _slots[slot];
            const char *name = Shared::Game::FindEmote(segment.emoteId)->name;

            bool ok = page->InvokeScalars(segment.glyph, "clear", {});
            ok      = ok && DrawGlyph(*page, segment.glyph, segment.emoteId);
            ok      = ok && page->SetText(segment.label, name, false);
            if (ok && _lit == static_cast<int>(slot)) {
                ok = page->SetText(_readout, name, false);
            }
            if (!ok) {
                Framework::Logging::GetLogger(kLogger)->warn("Emote wheel slot {} could not be redrawn for emote {}", slot, segment.emoteId);
            }
        }
    }

    /**
     * Both failures latch. Composition creates a few hundred clips, so a
     * retry every tick would leak them by the thousand and say so in the log
     * every frame; neither failure is one that a later tick would answer
     * differently.
     */
    void EmoteWheelService::EnsureScreen() {
        KCDC::Features::UI::NativeUIService *screens = Screens();
        if (screens == nullptr) {
            return;
        }

        // A host movie can unload under a live session -- a level change does
        // it. The service mounts a fresh page for the same screen afterwards,
        // but every clip handle here died with the old one, so the
        // composition is dropped and rebuilt rather than left pointing at
        // nothing for the rest of the session.
        if (_composed && !screens->IsMounted(_screen)) {
            if (_open) {
                Close(false);
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
                Framework::Logging::GetLogger(kLogger)->warn("The emote wheel could not open a screen: {}", error);
            }
            return; // Its host has to load and mount before anything can be drawn.
        }

        if (screens->IsMounted(_screen)) {
            _composed = Compose();
            _failed   = !_composed;
        }
    }

    bool EmoteWheelService::ApplyLabelStyle(KCDC::Core::NativeUI &page, const Segment &segment, bool lit) const {
        KCDC::Core::NativeUI::TextStyle style;
        style.font  = "DefaultFont";
        style.size  = kLabelSize;
        style.color = lit ? kLabelColourLit : kLabelColour;
        style.align = segment.align;
        return page.SetTextStyle(segment.label, style);
    }

    bool EmoteWheelService::Compose() {
        KCDC::Features::UI::NativeUIService *screens = Screens();
        std::shared_ptr<KCDC::Core::NativeUI> page   = screens != nullptr ? screens->GetPage(_screen) : nullptr;
        KCDC::Core::NativeUIHost *host               = screens != nullptr ? screens->GetHost(_screen) : nullptr;
        if (!page || host == nullptr) {
            return false;
        }

        int stageWidth  = 0;
        int stageHeight = 0;
        if (!host->GetStageSize(stageWidth, stageHeight)) {
            return false;
        }

        _backdrop = screens->NextKey(_screen);
        bool ok   = page->CreateClip(_backdrop, "$root", _backdrop.c_str());
        ok        = ok && DrawRectangle(*page, _backdrop, static_cast<float>(stageWidth), static_cast<float>(stageHeight), kShadowColour, kBackdropAlpha);
        ok        = ok && Place(*page, _backdrop, 0.0F, 0.0F, false);

        // One container at the middle of the stage, carrying the aspect
        // correction: inside it a unit is square.
        _root = screens->NextKey(_screen);
        ok    = ok && page->CreateClip(_root, "$root", _root.c_str());
        ok    = ok && Place(*page, _root, static_cast<float>(stageWidth) * 0.5F, static_cast<float>(stageHeight) * 0.5F, false);

        // The rose window is a circle on a transparent square, so it frames
        // the wheel with no mask: the segments drawn over it cover the tracery
        // and leave its carved rim showing. Do not use `KCDC_SetMask` here;
        // it produces a full-screen rectangle.
        const std::string bezel = screens->NextKey(_screen);
        ok                      = ok && page->CreateClip(bezel, _root, bezel.c_str());
        ok                      = ok && LoadTexture(*page, bezel, kBezelTexture, kBezelTextureSize, 2.0F * kBezelOuter, kBezelAlpha);

        const float sweep = SegmentSweep(kSlotCount);
        _segments.clear();
        _segments.reserve(kSlotCount);
        for (std::size_t index = 0; index < kSlotCount && ok; ++index) {
            const Shared::Game::EmoteEntry *entry = Shared::Game::FindEmote(_slots[index]);
            const float centre                    = sweep * static_cast<float>(index);

            Segment segment;
            segment.emoteId   = _slots[index];
            segment.glyph     = screens->NextKey(_screen);
            segment.highlight = screens->NextKey(_screen);
            segment.label     = screens->NextKey(_screen);

            // The face. Its own stroke is the segment's outline; there is no
            // separate pass of separators.
            const std::string face = screens->NextKey(_screen);
            ok                     = ok && page->CreateClip(face, _root, face.c_str());
            ok                     = ok && DrawSegment(*page, face, centre, sweep, kVellumColour, kVellumAlpha, kSegmentEdge, kEdgeAlpha, 1.0F);

            // A wash over the vellum rather than a second fill, in its own
            // clip: lighting a segment is then one visibility write.
            ok = ok && page->CreateClip(segment.highlight, _root, segment.highlight.c_str());
            ok = ok && DrawSegment(*page, segment.highlight, centre, sweep, kSegmentFillLit, kSegmentAlphaLit, kSegmentEdgeLit, kEdgeAlphaLit, 1.8F);
            ok = ok && page->SetVisible(segment.highlight, false);

            // After the wash, so the figure inks over either state. Its own
            // clip, so a slot a script changes is redrawn in place.
            ok = ok && page->CreateClip(segment.glyph, _root, segment.glyph.c_str());
            ok = ok && DrawGlyph(*page, segment.glyph, segment.emoteId);
            ok = ok && Place(*page, segment.glyph, std::sin(Radians(centre)) * kGlyphRadius, -std::cos(Radians(centre)) * kGlyphRadius + kGlyphCentring, true);

            // The side decides the anchor, so every label grows outwards and
            // none can run back over the ring.
            const float anchorX = std::sin(Radians(centre)) * kLabelAnchor;
            const float anchorY = -std::cos(Radians(centre)) * kLabelAnchor;
            float fieldX        = anchorX - kLabelWidth * 0.5F;
            segment.align       = "center";
            if (anchorX > 1.0F) {
                fieldX        = anchorX;
                segment.align = "left";
            }
            else if (anchorX < -1.0F) {
                fieldX        = anchorX - kLabelWidth;
                segment.align = "right";
            }

            ok = ok && page->CreateTextField(segment.label, _root, segment.label.c_str(), static_cast<double>(fieldX), static_cast<double>(anchorY - kLabelHeight * 0.5F), kLabelWidth, kLabelHeight);
            ok = ok && ApplyLabelStyle(*page, segment, false);
            ok = ok && page->SetText(segment.label, entry->name, false);

            _segments.push_back(std::move(segment));
        }

        // The hub: the one place a full name is spelled out.
        const std::string hub = screens->NextKey(_screen);
        ok                    = ok && page->CreateClip(hub, _root, hub.c_str());
        ok                    = ok && DrawDisc(*page, hub, kInnerRadius - 10.0F, kVellumColour, kVellumAlpha, kHubEdge, 80.0, 1.4F, 48);

        _readout = screens->NextKey(_screen);
        ok       = ok && page->CreateTextField(_readout, _root, _readout.c_str(), static_cast<double>(-kReadoutWidth * 0.5F), static_cast<double>(-kReadoutHeight * 0.5F), kReadoutWidth, kReadoutHeight);
        if (ok) {
            KCDC::Core::NativeUI::TextStyle style;
            style.font  = "DefaultFont";
            style.size  = kReadoutSize;
            style.color = kInkColour;
            style.align = "center";
            ok          = page->SetTextStyle(_readout, style);
        }
        ok = ok && page->SetText(_readout, "", false);

        _pointerClip = screens->NextKey(_screen);
        ok           = ok && page->CreateClip(_pointerClip, _root, _pointerClip.c_str());
        ok           = ok && DrawDisc(*page, _pointerClip, 5.0F, kPointerColour, 100.0, kPointerEdge, 85.0, 1.6F, 12);
        ok           = ok && Place(*page, _pointerClip, 0.0F, 0.0F, true);

        if (!ok) {
            Framework::Logging::GetLogger(kLogger)->warn("The emote wheel could not be composed");
            _segments.clear();
            return false;
        }
        Framework::Logging::GetLogger(kLogger)->debug("Composed an emote wheel of {} segment(s)", _segments.size());
        return true;
    }

    bool EmoteWheelService::AcquireInput() {
        SDK::ISystem *system            = SDK::ISystem::GetInstance();
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();
        if (system == nullptr || manager == nullptr || manager->GetActionMap(kMapName) != nullptr) {
            return false;
        }

        SDK::XmlNodeRef node = system->LoadXmlFromBuffer(kMapXml, sizeof(kMapXml) - 1, false, false);
        SDK::IActionMap *map = node.IsValid() ? manager->CreateActionMap(kMapName) : nullptr;
        if (map == nullptr) {
            return false;
        }
        if (!map->LoadFromXML(node) || !manager->AddExtraActionListener(this, kMapName)) {
            manager->RemoveActionMap(kMapName);
            return false;
        }

        _inputManager = manager;
        _inputMap     = map;
        _listening    = true;
        manager->EnableActionMap(kMapName, true);

        _filter = manager->FindOrCreateActionFilter(kFilterName, kFilterXml, sizeof(kFilterXml) - 1);
        if (_filter != nullptr) {
            _filter->Enable(kFilterReason);
        }
        if (_filter == nullptr || !manager->IsFilterEnabled(kFilterName)) {
            Framework::Logging::GetLogger(kLogger)->warn("The emote wheel is open without its look filter: the camera will follow the pointer");
        }
        return true;
    }

    void EmoteWheelService::ReleaseInput() {
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

        _inputManager = nullptr;
        _inputMap     = nullptr;
        _filter       = nullptr;
        _listening    = false;
        _pendingX     = 0.0F;
        _pendingY     = 0.0F;
    }

    bool EmoteWheelService::StillAllowed() const {
        return KCDC::Core::gApplication != nullptr && !KCDC::Core::gApplication->AreControlsLocked() && SDK::wh::entitymodule::C_Player::GetInstance() != nullptr;
    }

    void EmoteWheelService::Open() {
        KCDC::Features::UI::NativeUIService *screens = Screens();
        std::shared_ptr<KCDC::Core::NativeUI> page   = screens != nullptr ? screens->GetPage(_screen) : nullptr;
        KCDC::Core::NativeUIHost *host               = screens != nullptr ? screens->GetHost(_screen) : nullptr;
        if (!page || host == nullptr || !AcquireInput()) {
            return;
        }

        _pointerX = 0.0F;
        _pointerY = 0.0F;
        _pendingX = 0.0F;
        _pendingY = 0.0F;
        _lit      = -1;
        _openedAt = std::chrono::steady_clock::now();
        _open     = true;

        // Measured here rather than at composition: the player can resize the
        // window between one wheel and the next, and this is one call.
        const float correction = host->GetSquareUnitCorrection();
        Scale(*page, _root, correction);
        if (std::abs(correction - _correction) > 0.001F) {
            _correction = correction;
            Framework::Logging::GetLogger(kLogger)->debug("Emote wheel horizontal correction is {:.3f}", correction);
        }

        for (const Segment &segment : _segments) {
            page->SetVisible(segment.highlight, false);
            ApplyLabelStyle(*page, segment, false);
        }
        page->SetText(_readout, "", false);
        Place(*page, _pointerClip, 0.0F, 0.0F, true);
        page->SetVisible(_backdrop, true);
        page->SetVisible(_root, true);
    }

    void EmoteWheelService::Close(bool chosen) {
        const int selected = chosen ? _lit : -1;
        _open              = false;
        _lit               = -1;

        if (KCDC::Features::UI::NativeUIService *screens = Screens(); screens != nullptr) {
            if (std::shared_ptr<KCDC::Core::NativeUI> page = screens->GetPage(_screen); page) {
                page->SetVisible(_root, false);
                page->SetVisible(_backdrop, false);
            }
        }
        ReleaseInput();

        if (selected >= 0 && static_cast<std::size_t>(selected) < _segments.size() && KCDC::Core::gApplication != nullptr) {
            KCDC::Core::gApplication->GetEmoteService().RequestLocal(_segments[static_cast<std::size_t>(selected)].emoteId);
        }
    }

    int EmoteWheelService::Selection() const {
        const float distance = std::hypot(_pointerX, _pointerY);
        if (_segments.empty() || distance < kInnerRadius) {
            return -1;
        }

        const float sweep = SegmentSweep(_segments.size());
        float degrees     = std::atan2(_pointerX, -_pointerY) * 180.0F / kPi + 0.5F * sweep;
        degrees           = std::fmod(degrees + 360.0F, 360.0F);
        const int index   = static_cast<int>(degrees / sweep);
        return std::min(index, static_cast<int>(_segments.size()) - 1);
    }

    void EmoteWheelService::Track() {
        KCDC::Features::UI::NativeUIService *screens = Screens();
        std::shared_ptr<KCDC::Core::NativeUI> page   = screens != nullptr ? screens->GetPage(_screen) : nullptr;
        if (!page) {
            return;
        }

        _pointerX += _pendingX * kPointerSensitivity;
        _pointerY += _pendingY * kPointerSensitivity;
        _pendingX = 0.0F;
        _pendingY = 0.0F;

        // Held at the rim rather than wrapped: a flick past the edge should
        // stay pointing where it was flicked.
        if (const float distance = std::hypot(_pointerX, _pointerY); distance > kPointerReach) {
            _pointerX *= kPointerReach / distance;
            _pointerY *= kPointerReach / distance;
        }
        Place(*page, _pointerClip, _pointerX, _pointerY, true);

        const int selected = Selection();
        if (selected == _lit) {
            return;
        }

        if (_lit >= 0 && static_cast<std::size_t>(_lit) < _segments.size()) {
            page->SetVisible(_segments[static_cast<std::size_t>(_lit)].highlight, false);
            ApplyLabelStyle(*page, _segments[static_cast<std::size_t>(_lit)], false);
        }
        if (selected >= 0 && static_cast<std::size_t>(selected) < _segments.size()) {
            const Segment &segment = _segments[static_cast<std::size_t>(selected)];
            page->SetVisible(segment.highlight, true);
            ApplyLabelStyle(*page, segment, true);
            const Shared::Game::EmoteEntry *entry = Shared::Game::FindEmote(segment.emoteId);
            page->SetText(_readout, entry != nullptr ? entry->name : "", false);
        }
        else {
            page->SetText(_readout, "", false);
        }
        _lit = selected;
    }

    void EmoteWheelService::Update() {
        EnsureScreen();
        if (_composed) {
            RefreshSlots();
        }

        const bool held = KCDC::Core::NativeKeybinds::Get().IsHeld(KCDC::Core::ModKeybind::EmoteWheel);
        if (_open) {
            const bool expired = std::chrono::steady_clock::now() - _openedAt > kHoldCeiling;
            if (!held) {
                Close(true);
            }
            else if (expired || !StillAllowed()) {
                Close(false);
            }
            else {
                Track();
            }
            return;
        }

        if (held && _composed && StillAllowed()) {
            Open();
        }
    }

    void EmoteWheelService::Reset() {
        if (_open) {
            Close(false); // Releases the input it took.
        }

        if (_screen != 0) {
            // The service's screens go with the session anyway; closing here
            // keeps the handles from outliving the page they name.
            if (KCDC::Features::UI::NativeUIService *screens = Screens(); screens != nullptr) {
                screens->CloseScreen(_screen);
            }
        }
        _screen = 0;
        _failed = false;
        ForgetComposition();

        // The layout was the session's scripts', and they have gone with it.
        ResetSlots();
    }

    void EmoteWheelService::ForgetComposition() {
        _composed = false;
        _segments.clear();
        _root.clear();
        _backdrop.clear();
        _readout.clear();
        _pointerClip.clear();
        _lit = -1;
    }

    /**
     * Runs inside the engine's input dispatch, so it only accumulates. Where
     * the pointer has got to is settled on the service's own tick.
     */
    void EmoteWheelService::OnAction(const SDK::CCryName &action, int, float value) {
        if (!_open) {
            return;
        }
        const char *name = action.c_str();
        if (std::strcmp(name, "kcdc_wheel_x") == 0) {
            _pendingX += value;
        }
        else if (std::strcmp(name, "kcdc_wheel_y") == 0) {
            _pendingY += value;
        }
    }
} // namespace KCDC::Features::Social
