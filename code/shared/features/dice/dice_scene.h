#pragma once

#include "shared/game/prop_models.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <networking/replication/network_entity.h>
#include <networking/rpc/rpc_identifier.h>

namespace KCDC::Shared::Game::Dice {
    // Presentation only. Scores and face deals remain server-owned. Exactly
    // one participant publishes both sets and the cup, so observers never
    // combine two independently animated native tables.
    struct SceneProp {
        std::uint32_t model = 0; // Catalog index + 1; zero means hidden.
        std::array<float, 3> position {};
        std::array<float, 4> rotation {0, 0, 0, 1};
        std::array<float, 3> scale {1, 1, 1};

        bool IsSane(std::size_t slot) const {
            if (model == 0) {
                return true;
            }
            const auto *asset = PropModelAt(model - 1);
            if (asset == nullptr || !asset->path.starts_with("objects/manmade/task_specific_props/entertainment/games/dice/")) {
                return false;
            }
            if (slot == 12 ? asset->name != "dice_cup_b" : (!asset->name.starts_with("die_") && asset->name != "dice_basic" && asset->name != "dice_skull")) {
                return false;
            }
            float length = 0;
            for (float value : rotation) {
                if (!std::isfinite(value)) {
                    return false;
                }
                length += value * value;
            }
            if (length < 0.95F || length > 1.05F) {
                return false;
            }
            for (std::size_t i = 0; i < 3; ++i) {
                if (!std::isfinite(position[i]) || !std::isfinite(scale[i]) || scale[i] < 0.01F || scale[i] > 2.0F) {
                    return false;
                }
            }
            return true;
        }
        void Serialize(Framework::Networking::Replication::FieldSerializer &fields) {
            fields.Field(model);
            for (float &value : position) {
                fields.Field(value);
            }
            for (float &value : rotation) {
                fields.Field(value);
            }
            for (float &value : scale) {
                fields.Field(value);
            }
        }
    };

    struct Scene {
        std::uint32_t match    = 0;
        std::uint32_t revision = 0;
        std::array<SceneProp, 13> props;
        bool IsSane() const {
            for (std::size_t i = 0; i < props.size(); ++i) {
                if (!props[i].IsSane(i)) {
                    return false;
                }
            }
            return true;
        }
        bool Near(float x, float y, float z) const {
            for (const auto &prop : props) {
                if (prop.model == 0) {
                    continue;
                }
                const float dx = prop.position[0] - x, dy = prop.position[1] - y, dz = prop.position[2] - z;
                if (dx * dx + dy * dy + dz * dz > 25.0F) {
                    return false;
                }
            }
            return true;
        }
        void Serialize(Framework::Networking::Replication::FieldSerializer &fields) {
            fields.Field(match);
            fields.Field(revision);
            for (auto &prop : props) {
                prop.Serialize(fields);
            }
        }
    };
} // namespace KCDC::Shared::Game::Dice

namespace KCDC::Shared::RPC {
    struct DiceScene {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::DiceScene");
        Game::Dice::Scene scene;
        std::uint32_t epoch       = 0;
        std::uint32_t incarnation = 0;
        bool valid                = true;
        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, epoch) && bs->Serialize(write, incarnation) && bs->Serialize(write, scene.match) && bs->Serialize(write, scene.revision);
            for (auto &prop : scene.props) {
                valid = bs->Serialize(write, prop.model) && valid;
                for (float &value : prop.position) {
                    valid = bs->Serialize(write, value) && valid;
                }
                for (float &value : prop.rotation) {
                    valid = bs->Serialize(write, value) && valid;
                }
                for (float &value : prop.scale) {
                    valid = bs->Serialize(write, value) && valid;
                }
            }
        }
        bool IsSane() const {
            return valid && scene.match != 0 && scene.revision != 0 && scene.IsSane();
        }
    };
} // namespace KCDC::Shared::RPC
