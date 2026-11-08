#pragma once

#include "shared/features/light/light_entity.h"
#include <limits>

MODULE(light_protocol, {
    IT("delivers all point-light controls to late arrivals and subsequent retuning", {
        KCDC::Shared::Entities::LightEntity sent;
        sent.tuning.color       = {0.2F, 0.4F, 0.8F};
        sent.tuning.intensity   = 7.0F;
        sent.tuning.radius      = 23.0F;
        sent.tuning.castShadows = true;
        MafiaNet::BitStream construction;
        Framework::Networking::Replication::FieldSerializer writer(&construction, true);
        sent.OnSerializeConstruction(writer);
        KCDC::Shared::Entities::LightEntity received;
        Framework::Networking::Replication::FieldSerializer reader(&construction, false);
        received.OnSerializeConstruction(reader);
        EQUALS(received.tuning == sent.tuning, true);
        sent.tuning.color       = {1.0F, 0.0F, 0.0F};
        sent.tuning.intensity   = 0.0F;
        sent.tuning.radius      = 0.1F;
        sent.tuning.castShadows = false;
        MafiaNet::BitStream update;
        Framework::Networking::Replication::FieldSerializer updateWriter(&update, true);
        sent.SerializeFields(updateWriter);
        Framework::Networking::Replication::FieldSerializer updateReader(&update, false);
        received.SerializeFields(updateReader);
        EQUALS(received.tuning == sent.tuning, true);
    });

    IT("rejects non-finite values on every renderer-facing control", {
        for (float invalid : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            KCDC::Shared::Game::LightTuning value;
            value.intensity = invalid;
            EQUALS(value.IsSane(), false);
            value        = {};
            value.radius = invalid;
            EQUALS(value.IsSane(), false);
            for (int channel = 0; channel < 3; ++channel) {
                value                = {};
                value.color[channel] = invalid;
                EQUALS(value.IsSane(), false);
            }
        }
    });
});
