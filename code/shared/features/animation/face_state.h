#pragma once

#include <networking/replication/network_entity.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace KCDC::Shared::Game {
    /**
     * The mouth movement a body is made to hold, independent of any sound.
     *
     * The game moves a mouth only for a recorded voice line: `C_ActorSpeakMan`
     * looks up a clip named after the line and plays it on the skeleton's
     * `LipSync` layer. These are the few generic loops the game ships beside
     * those lines, in the same `Animations/humans/facials` set every human
     * skeleton loads, so any body can play them without a line to match.
     */
    enum class TalkStyle : std::uint8_t {
        None,    // the mouth is the game's own
        Neutral, // plain conversation
        Happy,   // animated, smiling
        Drunk,   // slurred
        Chew,    // chewing, for eating
        Count,
    };

    /** The facial clip a style plays, by the name the animation set knows it under; null for `None`. */
    inline const char *TalkStyleClip(TalkStyle style) {
        switch (style) {
        case TalkStyle::Neutral: return "fa_cin_talk_neutral_01";
        case TalkStyle::Happy: return "fa_cin_talk_happy_01";
        case TalkStyle::Drunk: return "fa_cin_talk_drunk";
        case TalkStyle::Chew: return "fa_chew_loop";
        default: return nullptr;
        }
    }

    inline const char *TalkStyleName(TalkStyle style) {
        switch (style) {
        case TalkStyle::Neutral: return "neutral";
        case TalkStyle::Happy: return "happy";
        case TalkStyle::Drunk: return "drunk";
        case TalkStyle::Chew: return "chew";
        default: return "none";
        }
    }

    /** The style a name means, or `Count` when it names none. */
    inline TalkStyle TalkStyleFromName(std::string_view name) {
        for (std::uint8_t raw = 0; raw < static_cast<std::uint8_t>(TalkStyle::Count); ++raw) {
            const auto style = static_cast<TalkStyle>(raw);
            if (name == TalkStyleName(style)) {
                return style;
            }
        }
        return TalkStyle::Count;
    }

    /**
     * What the server makes a face do, apart from whatever the body plays.
     *
     * A human face is two Mannequin scopes the body's own animations leave
     * alone. `FacialExpression` carries the eyes, brows and mood -- the
     * `FE_*` fragments, which animate only the eyes, and the `ADLG_FA_*`
     * gestures. `LipSync` carries the mouth, and nothing in the animation
     * database ever fills it: that is `talk`.
     *
     * Held state, like `AnimationPlayback`, so a client that streams the body
     * in shows the face it has now. `expressionRevision` is what tells a new
     * expression from the same one read again, so asking for a gesture twice
     * plays it twice; changing `talk` does not touch it.
     */
    struct FaceState {
        static constexpr std::size_t kMaxFragmentLength = 96;
        static constexpr std::size_t kMaxTagsLength     = 192;

        /** Bumped by every expression request, including the one that clears it. */
        std::uint32_t expressionRevision = 0;

        /** A fragment on the `FacialExpression` scope, by name. Empty is the game's own face. */
        std::string expression;

        /** `+`-separated tags for it, looked up the way `AnimationPlayback::tags` are. */
        std::string tags;

        TalkStyle talk = TalkStyle::None;

        bool IsSane() const {
            return expression.size() <= kMaxFragmentLength && tags.size() <= kMaxTagsLength && (!expression.empty() || tags.empty()) && talk < TalkStyle::Count;
        }

        bool operator==(const FaceState &other) const = default;

        static std::uint32_t Advance(std::uint32_t revision) {
            return revision + 1 != 0 ? revision + 1 : 1;
        }

        /** One body for every channel that carries it, as `AnimationPlayback::Serialize` is. */
        void Serialize(Framework::Networking::Replication::FieldSerializer &fields) {
            auto wireTalk = static_cast<std::uint8_t>(talk);
            fields.Field(expressionRevision);
            fields.Field(expression);
            fields.Field(tags);
            fields.Field(wireTalk);
            if (!fields.Writing()) {
                // A style this build has no clip for is read as the game's own mouth.
                talk = wireTalk < static_cast<std::uint8_t>(TalkStyle::Count) ? static_cast<TalkStyle>(wireTalk) : TalkStyle::None;
            }
        }
    };
} // namespace KCDC::Shared::Game
