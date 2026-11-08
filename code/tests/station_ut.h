#pragma once

#include "human_soul_ut.h"
#include "shared/features/dice/dice_rpc.h"
#include "shared/features/player/player_action.h"
#include "shared/features/player/station_animation.h"
#include "shared/features/player/station_sharpening.h"
#include "shared/features/player/station_tools.h"
#include "shared/game/animation_catalog.h"
#include "shared/game/crafting_catalog.h"

#include <cmath>
#include <limits>
#include <set>
#include <utility>

#include <algorithm>
#include <string_view>

MODULE(stations, {
    IT("round trips observer props and rejects unsafe or truncated scenes", {
        KCDC::Shared::RPC::DiceScene report;
        report.scene.match           = 7;
        report.scene.revision        = 1;
        auto &die                    = report.scene.props[0];
        die.model                    = static_cast<std::uint32_t>(KCDC::Shared::Game::FindPropModelIndex("die_normal") + 1);
        die.position                 = {600, 3200, 145};
        report.scene.props[12]       = die;
        report.scene.props[12].model = static_cast<std::uint32_t>(KCDC::Shared::Game::FindPropModelIndex("dice_cup_b") + 1);
        for (const auto &asset : KCDC::Shared::Game::g_PropModels) {
            if (!asset.path.starts_with("objects/manmade/task_specific_props/entertainment/games/dice/")) {
                continue;
            }
            KCDC::Shared::Game::Dice::SceneProp prop;
            prop.model       = static_cast<std::uint32_t>(&asset - KCDC::Shared::Game::g_PropModels.data() + 1);
            const bool isDie = asset.name.starts_with("die_") || asset.name == "dice_basic" || asset.name == "dice_skull";
            EQUALS(prop.IsSane(0), isDie);
            EQUALS(prop.IsSane(12), (asset.name == "dice_cup_b"));
        }
        MafiaNet::BitStream stream;
        report.Serialize(&stream, true);
        KCDC::Shared::RPC::DiceScene received;
        received.Serialize(&stream, false);
        EQUALS(received.IsSane(), true);
        EQUALS(received.scene.props[0].model, die.model);
        EQUALS(received.scene.Near(600, 3200, 145), true);
        EQUALS(received.scene.Near(610, 3200, 145), false);
        received.scene.props[0].model = received.scene.props[12].model;
        EQUALS(received.IsSane(), false);
        received                         = report;
        received.scene.props[0].rotation = {0, 0, 0, 0};
        EQUALS(received.IsSane(), false);
        received                            = report;
        received.scene.props[0].position[0] = std::numeric_limits<float>::quiet_NaN();
        EQUALS(received.IsSane(), false);
        MafiaNet::BitStream empty;
        received.Serialize(&empty, false);
        EQUALS(received.IsSane(), false);
    });

    IT("replicates scene construction movement and withdrawal without accepting owner writes", {
        KCDC::Shared::Entities::HumanEntity server, observer;
        server.soul                     = HumanSoulTests::Sample();
        server.diceScene.match          = 9;
        server.diceScene.revision       = 1;
        server.diceScene.props[0].model = static_cast<std::uint32_t>(KCDC::Shared::Game::FindPropModelIndex("die_normal") + 1);
        HumanSoulTests::Channel channel;
        EQUALS(channel.Send(server, observer, true), true);
        EQUALS(observer.diceScene.props[0].model, server.diceScene.props[0].model);
        server.diceScene.props[0].position[0] = 0.3F;
        EQUALS(channel.Send(server, observer), true);
        EQUALS(observer.diceScene.props[0].position[0], 0.3F);
        server.diceScene = {};
        EQUALS(channel.Send(server, observer), true);
        EQUALS(observer.diceScene.match, 0U);
        EQUALS(observer.diceScene.props[0].model, 0U);
        server.diceScene.match = 10;
        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true, false);
        server.SerializeFields(writer);
        Framework::Networking::Replication::FieldSerializer reader(&stream, false, false);
        observer.SerializeFields(reader);
        EQUALS(reader.Good(), true);
        EQUALS(observer.diceScene.match, 0U);
    });

    IT("keeps dice names bounded and preserves UTF-8 through the wire", {
        KCDC::Shared::RPC::DiceNames names {12, {"Jind\xC5\x99ich", "Opponent"}};
        MafiaNet::BitStream stream;
        names.Serialize(&stream, true);
        KCDC::Shared::RPC::DiceNames received;
        received.Serialize(&stream, false);
        EQUALS(received.IsSane(), true);
        EQUALS(received.names[0], names.names[0]);
        names.names[0] = std::string(65, 'a');
        EQUALS(names.IsSane(), false);
        names.names[0] = "bad\nname";
        EQUALS(names.IsSane(), false);
        MafiaNet::BitStream empty;
        received.Serialize(&empty, false);
        EQUALS(received.IsSane(), false);
    });

    IT("uses a one-shot authored dice throw for each body type", {
        KCDC::Shared::Game::StationState state {KCDC::Shared::Game::StationActivity::Dice, 123, 1, 9, 0, false, 42};
        for (bool female : {false, true}) {
            const auto playback = KCDC::Shared::Game::StationAnimation(state);
            EQUALS(playback.fragment, std::string("DiceShake"));
            EQUALS(playback.loop, false);
            EQUALS(playback.IsSane(), true);
            const auto supported = std::find_if(KCDC::Shared::Game::g_Animations.begin(), KCDC::Shared::Game::g_Animations.end(), [&](const auto &entry) {
                return playback.fragment == entry.fragment && std::string(entry.tags).empty() && entry.oneShot && (entry.bodies & (female ? KCDC::Shared::Game::kAnimationBodyFemale : KCDC::Shared::Game::kAnimationBodyMale)) != 0;
            });
            EQUALS(supported != KCDC::Shared::Game::g_Animations.end(), true);
        }
    });

    IT("sends a held station to late observers and clears it on the delta channel", {
        KCDC::Shared::Entities::HumanEntity server, observer;
        server.soul    = HumanSoulTests::Sample();
        server.station = {KCDC::Shared::Game::StationActivity::Dice, 0x123456789ABCDEF0ULL, 42, 7, 1, false};
        HumanSoulTests::Channel channel;
        EQUALS(channel.Send(server, observer, true), true);
        EQUALS(observer.station == server.station, true);
        EQUALS(channel.Send(server, observer), false);
        const auto playback = KCDC::Shared::Game::StationAnimation(observer.station);
        EQUALS(playback.active && playback.loop && !playback.endsWithFragment, true);
        EQUALS(playback.fragment, std::string("DiceIdle"));
        server.station = {};
        EQUALS(channel.Send(server, observer), true);
        EQUALS(observer.station.Active(), false);
    });

    IT("does not accept station state through owner authored entity fields", {
        KCDC::Shared::Entities::HumanEntity owner, server;
        owner.soul    = HumanSoulTests::Sample();
        owner.station = {KCDC::Shared::Game::StationActivity::Alchemy, 123, 1, 0, 0, true};
        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true, false);
        owner.SerializeFields(writer);
        Framework::Networking::Replication::FieldSerializer reader(&stream, false, false);
        server.SerializeFields(reader);
        EQUALS(reader.Good(), true);
        EQUALS(server.station.Active(), false);
    });

    IT("round trips station reports and rejects truncation and malformed slots", {
        KCDC::Shared::RPC::PlayerStation report;
        report.state       = {KCDC::Shared::Game::StationActivity::Dice, 123, 1, 9, 1, false};
        report.epoch       = 8;
        report.incarnation = 3;
        MafiaNet::BitStream stream;
        report.Serialize(&stream, true);
        KCDC::Shared::RPC::PlayerStation received;
        received.Serialize(&stream, false);
        EQUALS(received.IsSane(), true);
        EQUALS(received.state == report.state, true);
        EQUALS(received.epoch, 8U);
        EQUALS(received.incarnation, 3U);
        MafiaNet::BitStream empty;
        received.Serialize(&empty, false);
        EQUALS(received.IsSane(), false);
        report.state.slot = 2;
        EQUALS(report.IsSane(), false);
        report.state.slot  = 0;
        report.state.match = 0;
        EQUALS(report.IsSane(), false);
        report.state.activity = static_cast<KCDC::Shared::Game::StationActivity>(255);
        EQUALS(report.IsSane(), false);
        report.state = {};
        EQUALS(report.IsSane(), false);
        report.state.revision = 1;
        EQUALS(report.IsSane(), true);
    });

    IT("orders reports across wrap and does not restart an unchanged heartbeat", {
        EQUALS(KCDC::Shared::Game::StationState::Advance(0xFFFFFFFFU), 1U);
        EQUALS(KCDC::Shared::Game::StationState::Newer(1, 0xFFFFFFFFU), true);
        EQUALS(KCDC::Shared::Game::StationState::Newer(0xFFFFFFFFU, 1), false);
        EQUALS(KCDC::Shared::Game::StationState::Newer(5, 5), false);
        KCDC::Shared::Game::StationState first {KCDC::Shared::Game::StationActivity::Dice, 123, 42, 9, 0, false};
        auto heartbeat = first;
        heartbeat.revision++;
        EQUALS(first.SamePose(heartbeat), true);
        heartbeat.slot = 1;
        EQUALS(first.SamePose(heartbeat), false);
        heartbeat = first;
        heartbeat.guid++;
        EQUALS(first.SamePose(heartbeat), false);
    });

    IT("selects authored loops for both body types and puts no prop in a smith's hands", {
        for (bool female : {false, true}) {
            for (auto activity : {KCDC::Shared::Game::StationActivity::Dice, KCDC::Shared::Game::StationActivity::Smithing, KCDC::Shared::Game::StationActivity::Alchemy}) {
                KCDC::Shared::Game::StationState state {activity, 123, 1, activity == KCDC::Shared::Game::StationActivity::Dice ? 9U : 0U, 0, activity != KCDC::Shared::Game::StationActivity::Dice};
                const auto playback = KCDC::Shared::Game::StationAnimation(state);
                EQUALS(playback.IsSane(), true);
                const auto supported = std::find_if(KCDC::Shared::Game::g_Animations.begin(), KCDC::Shared::Game::g_Animations.end(), [&](const auto &entry) {
                    return playback.fragment == entry.fragment && std::string(entry.tags).empty() && !entry.oneShot && (entry.bodies & (female ? KCDC::Shared::Game::kAnimationBodyFemale : KCDC::Shared::Game::kAnimationBodyMale)) != 0;
                });
                EQUALS(supported != KCDC::Shared::Game::g_Animations.end(), true);
                for (const auto &prop : playback.props) {
                    EQUALS(prop.IsSet(), false);
                }
            }
        }
    });

    // The client replays a smith's steps by the Blacksmithing* fragments the
    // catalog lists; these are the sixteen SMannequinBlacksmithParams
    // resolves (0x181938C04), one per C_Blacksmithing action selector.
    IT("lists every native smithing step fragment for male bodies", {
        for (const char *fragment : {"BlacksmithingGrabWorkpiece", "BlacksmithingToAnvil", "BlacksmithingToForge", "BlacksmithingToHardening", "BlacksmithingForgeIdle", "BlacksmithingForgeInspection", "BlacksmithingForgeInspectionStop", "BlacksmithingAnvilIdle",
                 "BlacksmithingFlipWorkpiece", "BlacksmithingFlipWorkpieceBack", "BlacksmithingStrokeStart", "BlacksmithingStrokeFinish", "BlacksmithingFlippedWorkpiece", "BlacksmithingPerfectStroke", "BlacksmithingFinishedReturnHammer", "BlacksmithingFinishedIdle"}) {
            const auto listed = std::find_if(KCDC::Shared::Game::g_Animations.begin(), KCDC::Shared::Game::g_Animations.end(), [&](const auto &entry) {
                return std::string_view(entry.fragment) == fragment && (entry.bodies & KCDC::Shared::Game::kAnimationBodyMale) != 0;
            });
            EQUALS(listed != KCDC::Shared::Game::g_Animations.end(), true);
        }
        std::size_t smithing = 0;
        for (const auto &entry : KCDC::Shared::Game::g_Animations) {
            if (std::string_view(entry.fragment).starts_with("Blacksmithing")) {
                EQUALS((entry.bodies & KCDC::Shared::Game::kAnimationBodyFemale) == 0, true);
                const std::string_view tags(entry.tags);
                const std::size_t tagCount = tags.empty() ? 0 : static_cast<std::size_t>(std::count(tags.begin(), tags.end(), '+')) + 1;
                EQUALS(tagCount <= KCDC::Shared::Game::MinigameActions::kMaxTags, true);
                ++smithing;
            }
        }
        EQUALS(smithing != 0, true);
    });

    // ParseBlacksmithingAnimationEvent (0x180857548) codes 3-6, and the hands
    // and links C_Blacksmithing::Start (0x180B674A4) gives the two tools.
    IT("reads the smithery tool events and puts each tool in its native hand", {
        EQUALS(KCDC::Shared::Game::ParseStationToolEvent("AttachHammer").has_value(), true);
        EQUALS(KCDC::Shared::Game::ParseStationToolEvent("AttachHammer")->tool == KCDC::Shared::Game::StationTool::Hammer, true);
        EQUALS(KCDC::Shared::Game::ParseStationToolEvent("AttachHammer")->attach, true);
        EQUALS(KCDC::Shared::Game::ParseStationToolEvent("DetachHammer").has_value(), true);
        EQUALS(KCDC::Shared::Game::ParseStationToolEvent("DetachHammer")->tool == KCDC::Shared::Game::StationTool::Hammer, true);
        EQUALS(KCDC::Shared::Game::ParseStationToolEvent("DetachHammer")->attach, false);
        EQUALS(KCDC::Shared::Game::ParseStationToolEvent("AttachTongs").has_value(), true);
        EQUALS(KCDC::Shared::Game::ParseStationToolEvent("AttachTongs")->tool == KCDC::Shared::Game::StationTool::Tongs, true);
        EQUALS(KCDC::Shared::Game::ParseStationToolEvent("AttachTongs")->attach, true);
        EQUALS(KCDC::Shared::Game::ParseStationToolEvent("DetachTongs").has_value(), true);
        EQUALS(KCDC::Shared::Game::ParseStationToolEvent("DetachTongs")->tool == KCDC::Shared::Game::StationTool::Tongs, true);
        EQUALS(KCDC::Shared::Game::ParseStationToolEvent("DetachTongs")->attach, false);
        for (const char *other : {"SpawnWorkpiece", "AttachWorkpiece", "DetachWorkpiece", "StrokeHit", "ToggleFlip", "ForgeBagPull", "attachhammer", "AttachHammer ", ""}) {
            EQUALS(KCDC::Shared::Game::ParseStationToolEvent(other).has_value(), false);
        }
        EQUALS(KCDC::Shared::Game::StationToolHand(KCDC::Shared::Game::StationTool::Hammer), KCDC::Shared::Entities::HumanEntity::kRightHand);
        EQUALS(KCDC::Shared::Game::StationToolHand(KCDC::Shared::Game::StationTool::Tongs), KCDC::Shared::Entities::HumanEntity::kLeftHand);
        EQUALS(std::string(KCDC::Shared::Game::StationToolLink(KCDC::Shared::Game::StationTool::Hammer)), std::string("hammer"));
        EQUALS(std::string(KCDC::Shared::Game::StationToolLink(KCDC::Shared::Game::StationTool::Tongs)), std::string("tongs"));
    });

    IT("reports a grindstone as a station and rejects one carrying another activity's fields", {
        KCDC::Shared::RPC::PlayerStation report;
        report.state       = {KCDC::Shared::Game::StationActivity::Sharpening, 0x023AAC1CB16E45DEULL, 1, 0, 0, false};
        report.epoch       = 2;
        report.incarnation = 4;
        EQUALS(report.IsSane(), true);
        MafiaNet::BitStream stream;
        report.Serialize(&stream, true);
        KCDC::Shared::RPC::PlayerStation received;
        received.Serialize(&stream, false);
        EQUALS(received.IsSane(), true);
        EQUALS(received.state == report.state, true);
        for (auto broken : {KCDC::Shared::Game::StationState {KCDC::Shared::Game::StationActivity::Sharpening, 0, 1, 0, 0, false}, KCDC::Shared::Game::StationState {KCDC::Shared::Game::StationActivity::Sharpening, 1, 1, 9, 0, false},
                 KCDC::Shared::Game::StationState {KCDC::Shared::Game::StationActivity::Sharpening, 1, 1, 0, 1, false}, KCDC::Shared::Game::StationState {KCDC::Shared::Game::StationActivity::Sharpening, 1, 1, 0, 0, true}}) {
            EQUALS(broken.IsSane(), false);
        }
        const auto playback = KCDC::Shared::Game::StationAnimation(report.state);
        EQUALS(playback.IsSane(), true);
        EQUALS(playback.fragment, std::string("MotionIdle"));
        EQUALS(playback.loop, true);
    });

    IT("sends the blade and wheel with both ends exact and costs one bit away from a grindstone", {
        const auto roundTrip = [](KCDC::Shared::Game::SharpeningMotion motion) {
            MafiaNet::BitStream stream;
            Framework::Networking::Replication::FieldSerializer writer(&stream, true, false);
            motion.Serialize(writer);
            Framework::Networking::Replication::FieldSerializer reader(&stream, false, false);
            KCDC::Shared::Game::SharpeningMotion received;
            received.Serialize(reader);
            // A truncated read is a failed one: it may not leave a motion behind.
            if (!reader.Good()) {
                received.active     = false;
                received.wheelSpeed = -1.0F;
            }
            return std::make_pair(received, stream.GetNumberOfBitsUsed());
        };
        const auto [idle, idleBits] = roundTrip({});
        EQUALS(idle == KCDC::Shared::Game::SharpeningMotion {}, true);
        EQUALS(static_cast<int>(idleBits) <= 8, true);
        const auto [edges, edgeBits] = roundTrip({true, -0.5F, 1.0F, 0.0F});
        EQUALS(edges.active, true);
        EQUALS(edges.swordPosition, -0.5F);
        EQUALS(edges.swordRotation, 1.0F);
        EQUALS(edges.wheelSpeed, 0.0F);
        EQUALS(edgeBits > idleBits, true);
        const auto [middle, middleBits] = roundTrip({true, 0.0F, 0.5F, 1.0F});
        EQUALS(middle.swordPosition, 0.0F);
        EQUALS(middle.swordRotation, 0.5F);
        EQUALS(middle.wheelSpeed, 1.0F);
        const auto [clamped, clampedBits] = roundTrip({true, 3.0F, -1.0F, std::numeric_limits<float>::quiet_NaN()});
        EQUALS(clamped.swordPosition, 0.5F);
        EQUALS(clamped.swordRotation, 0.0F);
        EQUALS(clamped.wheelSpeed, 0.0F);
        const auto [sampled, sampledBits] = roundTrip({true, 0.123F, 0.777F, 0.31F});
        EQUALS(std::abs(sampled.swordPosition - 0.123F) <= 0.5F / KCDC::Shared::Game::SharpeningMotion::kSteps, true);
        EQUALS(std::abs(sampled.swordRotation - 0.777F) <= 0.5F / KCDC::Shared::Game::SharpeningMotion::kSteps, true);
        EQUALS(std::abs(sampled.wheelSpeed - 0.31F) <= 0.5F / KCDC::Shared::Game::SharpeningMotion::kSteps, true);
    });

    // StartGrindstoneAnimation (0x182EB1C10) and SetGrindstoneAnimationSpeed
    // (0x182EB0888); SetGrindstone (0x182EB0204) reads the multiplier.
    IT("names the wheel's clip for a blade and scales it by the grindstone's multiplier", {
        EQUALS(KCDC::Shared::Game::GrindstoneAnimationName("lngsw"), std::string("grindstone_spinning_lngsw"));
        EQUALS(KCDC::Shared::Game::GrindstonePlaybackScale(0.5F, -2.0F), -1.0F);
        EQUALS(KCDC::Shared::Game::GrindstonePlaybackScale(std::numeric_limits<float>::infinity(), 1.0F), 0.0F);
        std::set<std::pair<std::string_view, std::uint64_t>> seen;
        for (const auto &grindstone : KCDC::Shared::Game::g_SharpeningStations) {
            EQUALS(grindstone.guid != 0, true);
            EQUALS(seen.insert({grindstone.level, grindstone.guid}).second, true);
            EQUALS(grindstone.speedMultiplier > -10.0F && grindstone.speedMultiplier < 10.0F, true);
            EQUALS(KCDC::Shared::Game::FindSharpeningStation(grindstone.level, grindstone.guid) == &grindstone, true);
        }
        EQUALS(seen.empty(), false);
        EQUALS(KCDC::Shared::Game::FindSharpeningStation("kutnohorsko", 0) == nullptr, true);
    });

    // ResolveItemHandlingFragmentIds (0x180D81378) resolves exactly these for
    // C_Sharpening's three actions; Grindstone.lua turns women away.
    IT("lists the grindstone player's three fragments for male bodies only", {
        for (const char *fragment : {KCDC::Shared::Game::kSharpeningInFragment, KCDC::Shared::Game::kSharpeningLoopFragment, KCDC::Shared::Game::kSharpeningOutFragment}) {
            const auto listed = std::find_if(KCDC::Shared::Game::g_Animations.begin(), KCDC::Shared::Game::g_Animations.end(), [&](const auto &entry) {
                return std::string_view(entry.fragment) == fragment && (entry.bodies & KCDC::Shared::Game::kAnimationBodyMale) != 0;
            });
            EQUALS(listed != KCDC::Shared::Game::g_Animations.end(), true);
        }
        for (const auto &entry : KCDC::Shared::Game::g_Animations) {
            if (std::string_view(entry.fragment).starts_with("SharpeningMinigame")) {
                EQUALS((entry.bodies & KCDC::Shared::Game::kAnimationBodyFemale) == 0, true);
            }
        }
    });
});
