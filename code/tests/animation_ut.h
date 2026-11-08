#pragma once

#include "sdk/wh/entitymodule/e_weapon_equip_slot.h"
#include "shared/features/animation/animation_playback.h"
#include "shared/features/animation/animation_rpc.h"
#include "shared/game/animation_catalog.h"

#include <mafianet/bit_stream.h>

#include <cstdint>
#include <string>

MODULE(animation_protocol, {
    IT("pairs a sword with its shield but never a previously equipped bow", {
        EQUALS(SDK::wh::entitymodule::E_WeaponEquipSlot::IsSameWeaponSet(SDK::wh::entitymodule::E_WeaponEquipSlot::PrimaryMainHand, SDK::wh::entitymodule::E_WeaponEquipSlot::PrimaryOffHand), true);
        EQUALS(SDK::wh::entitymodule::E_WeaponEquipSlot::IsSameWeaponSet(SDK::wh::entitymodule::E_WeaponEquipSlot::PrimaryMainHand, SDK::wh::entitymodule::E_WeaponEquipSlot::SecondaryOffHand), false);
        EQUALS(SDK::wh::entitymodule::E_WeaponEquipSlot::IsSameWeaponSet(SDK::wh::entitymodule::E_WeaponEquipSlot::SecondaryMainHand, SDK::wh::entitymodule::E_WeaponEquipSlot::PrimaryOffHand), false);
        EQUALS(SDK::wh::entitymodule::E_WeaponEquipSlot::IsSameWeaponSet(SDK::wh::entitymodule::E_WeaponEquipSlot::Torch, SDK::wh::entitymodule::E_WeaponEquipSlot::Dagger), false);
        EQUALS(SDK::wh::entitymodule::E_WeaponEquipSlot::IsSameWeaponSet(SDK::wh::entitymodule::E_WeaponEquipSlot::Count, SDK::wh::entitymodule::E_WeaponEquipSlot::Count), false);
    });

    IT("carries a stance exit request whole", {
        KCDC::Shared::Game::AnimationPlayback sent;
        sent.revision         = 7;
        sent.active           = true;
        sent.fragment         = "StandUp";
        sent.tags             = "sittingNoTable";
        sent.lockMovement     = true;
        sent.endsWithFragment = true;

        KCDC::Shared::RPC::PlayerAnimation message;
        message.playback = sent;
        MafiaNet::BitStream stream;
        message.Serialize(&stream, true);

        KCDC::Shared::RPC::PlayerAnimation received;
        received.Serialize(&stream, false);
        EQUALS(received.IsSane(), true);
        EQUALS(received.playback == sent, true);
        EQUALS(received.playback.endsWithFragment, true);
    });

    IT("carries an end report and refuses the reasons only the server may give", {
        KCDC::Shared::RPC::AnimationEnded sent;
        sent.entityNetworkId = 3868;
        sent.revision        = 12;
        sent.reason          = KCDC::Shared::Game::AnimationEndReason::Interrupted;
        MafiaNet::BitStream stream;
        sent.Serialize(&stream, true);

        KCDC::Shared::RPC::AnimationEnded received;
        received.Serialize(&stream, false);
        EQUALS(received.IsSane(), true);
        EQUALS(received.entityNetworkId, 3868ULL);
        EQUALS(received.revision, 12U);
        EQUALS(received.reason == KCDC::Shared::Game::AnimationEndReason::Interrupted, true);

        received.reason = KCDC::Shared::Game::AnimationEndReason::Stopped;
        EQUALS(received.IsSane(), false);
        received.reason = KCDC::Shared::Game::AnimationEndReason::Replaced;
        EQUALS(received.IsSane(), false);

        // A reason this build has no name for arrives as nothing it accepts.
        KCDC::Shared::RPC::AnimationEnded unknown = sent;
        unknown.reason                             = static_cast<KCDC::Shared::Game::AnimationEndReason>(9);
        MafiaNet::BitStream unknownStream;
        unknown.Serialize(&unknownStream, true);
        KCDC::Shared::RPC::AnimationEnded decoded;
        decoded.Serialize(&unknownStream, false);
        EQUALS(decoded.IsSane(), false);

        KCDC::Shared::RPC::AnimationEnded empty;
        EQUALS(empty.IsSane(), false); // no entity, no revision
    });

    IT("names every reason the contract spells", {
        EQUALS(std::string(KCDC::Shared::Game::AnimationEndReasonName(KCDC::Shared::Game::AnimationEndReason::Finished)), std::string("finished"));
        EQUALS(std::string(KCDC::Shared::Game::AnimationEndReasonName(KCDC::Shared::Game::AnimationEndReason::Interrupted)), std::string("interrupted"));
        EQUALS(std::string(KCDC::Shared::Game::AnimationEndReasonName(KCDC::Shared::Game::AnimationEndReason::Refused)), std::string("refused"));
        EQUALS(std::string(KCDC::Shared::Game::AnimationEndReasonName(KCDC::Shared::Game::AnimationEndReason::Stopped)), std::string("stopped"));
        EQUALS(std::string(KCDC::Shared::Game::AnimationEndReasonName(KCDC::Shared::Game::AnimationEndReason::Replaced)), std::string("replaced"));
    });

    IT("finds the way out of a sitting or lying stance tag", {
        const auto *sitting = KCDC::Shared::Game::FindAnimationStanceExit("sittingNoTable");
        EQUALS(sitting != nullptr, true);
        EQUALS(std::string(sitting->fragment), std::string("StandUp"));

        // Among other tags, in any case, with the table's own spacing.
        const auto *lying = KCDC::Shared::Game::FindAnimationStanceExit("r_bucket + LyingGround");
        EQUALS(lying != nullptr, true);
        EQUALS(std::string(lying->fragment), std::string("GetUp"));
        EQUALS(std::string(lying->stanceTag), std::string("lyingGround"));

        // A stance the game authors no way out of, and no stance at all.
        EQUALS(KCDC::Shared::Game::FindAnimationStanceExit("beggarKneel") == nullptr, true);
        EQUALS(KCDC::Shared::Game::FindAnimationStanceExit("r_axe") == nullptr, true);
        EQUALS(KCDC::Shared::Game::FindAnimationStanceExit("") == nullptr, true);
    });

    IT("carries a face whole and reads an unknown talk style as none", {
        KCDC::Shared::Game::FaceState sent;
        sent.expressionRevision = 4;
        sent.expression         = "FE_DialogueSpeaking";
        sent.tags               = "happy";
        sent.talk               = KCDC::Shared::Game::TalkStyle::Drunk;

        KCDC::Shared::RPC::PlayerFace message;
        message.face = sent;
        MafiaNet::BitStream stream;
        message.Serialize(&stream, true);

        KCDC::Shared::RPC::PlayerFace received;
        received.Serialize(&stream, false);
        EQUALS(received.IsSane(), true);
        EQUALS(received.face == sent, true);

        KCDC::Shared::RPC::PlayerFace unknown;
        unknown.face.talk = static_cast<KCDC::Shared::Game::TalkStyle>(9);
        MafiaNet::BitStream unknownStream;
        unknown.Serialize(&unknownStream, true);
        KCDC::Shared::RPC::PlayerFace decoded;
        decoded.Serialize(&unknownStream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.face.talk == KCDC::Shared::Game::TalkStyle::None, true);
    });

    IT("refuses tags without an expression and names a clip for every style", {
        KCDC::Shared::Game::FaceState face;
        face.tags = "happy";
        EQUALS(face.IsSane(), false);
        face.expression = "FE_Default";
        EQUALS(face.IsSane(), true);

        EQUALS(KCDC::Shared::Game::TalkStyleClip(KCDC::Shared::Game::TalkStyle::None) == nullptr, true);
        for (auto style = static_cast<std::uint8_t>(KCDC::Shared::Game::TalkStyle::Neutral); style < static_cast<std::uint8_t>(KCDC::Shared::Game::TalkStyle::Count); ++style) {
            const auto talk = static_cast<KCDC::Shared::Game::TalkStyle>(style);
            EQUALS(KCDC::Shared::Game::TalkStyleClip(talk) != nullptr, true);
            EQUALS(KCDC::Shared::Game::TalkStyleFromName(KCDC::Shared::Game::TalkStyleName(talk)) == talk, true);
        }
        EQUALS(KCDC::Shared::Game::TalkStyleFromName("shouting") == KCDC::Shared::Game::TalkStyle::Count, true);
    });
});
