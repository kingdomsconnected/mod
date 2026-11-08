#pragma once

#include "human_soul_ut.h"
#include "shared/features/player/player_set_resource.h"

MODULE(player_resource_protocol, {
    IT("validates JS doubles before narrowing and refuses the native health no-op range", {
        for (const std::uint8_t resource : {0, 1, 2, 3}) {
            EQUALS(KCDC::Shared::RPC::SetPlayerResource::IsValueSane(resource, std::numeric_limits<double>::denorm_min()), false);
            EQUALS(KCDC::Shared::RPC::SetPlayerResource::IsValueSane(resource, 1e-100), false);
            EQUALS(KCDC::Shared::RPC::SetPlayerResource::IsValueSane(resource, 1000000.0001), false);
            EQUALS(KCDC::Shared::RPC::SetPlayerResource::IsValueSane(resource, -0.0), true);
        }
        EQUALS(KCDC::Shared::RPC::SetPlayerResource::IsValueSane(0, 1e-6), false);
        EQUALS(KCDC::Shared::RPC::SetPlayerResource::IsValueSane(0, 1e-5), false);
        EQUALS(KCDC::Shared::RPC::SetPlayerResource::IsValueSane(0, 1.000000001e-5), false);
        EQUALS(KCDC::Shared::RPC::SetPlayerResource::IsValueSane(0, std::nextafter(1e-5F, 1.0F)), true);
        EQUALS(KCDC::Shared::RPC::SetPlayerResource::IsValueSane(3, 1e-6), true);
        KCDC::Shared::RPC::SetPlayerResource request {42, 1, 3, 0, 1e-6F};
        EQUALS(request.IsSane(), false);
        request.value = 0.0F;
        EQUALS(request.IsSane(), true);
    });

    IT("round trips every writable stat including zero and the protocol ceiling", {
        for (const std::uint8_t resource : {0, 1, 2, 3}) {
            for (const float value : {0.0F, 37.5F, 1000000.0F}) {
                KCDC::Shared::RPC::SetPlayerResource request {42, 1, 3, resource, value};
                MafiaNet::BitStream stream;
                request.Serialize(&stream, true);
                KCDC::Shared::RPC::SetPlayerResource received;
                received.Serialize(&stream, false);
                EQUALS(received.AppliesTo(42, 3, HumanSoulTests::Sample()), true);
                EQUALS(received.resource, resource);
                EQUALS(received.value, value);
            }
        }
    });

    IT("rejects another connection ownership epoch life and dead or missing soul", {
        KCDC::Shared::RPC::SetPlayerResource request {42, 1, 3, 0, 50.0F};
        auto soul = HumanSoulTests::Sample();
        EQUALS(request.AppliesTo(43, 3, soul), false);
        EQUALS(request.AppliesTo(42, 4, soul), false);
        soul.incarnation++;
        EQUALS(request.AppliesTo(42, 3, soul), false);
        soul.incarnation--;
        soul.resources[0].current = 0.0F;
        EQUALS(request.AppliesTo(42, 3, soul), false);
        soul.resources[0].current = 50.0F;
        soul.valid                = false;
        EQUALS(request.AppliesTo(42, 3, soul), false);
    });

    IT("rejects nonfinite out of range unknown and truncated requests", {
        KCDC::Shared::RPC::SetPlayerResource request {42, 1, 3, 0, 50.0F};
        for (const float value : {-1.0F, 1000001.0F, std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            request.value = value;
            EQUALS(request.IsSane(), false);
        }
        request.value = 50.0F;
        for (const std::uint8_t resource : {4, 255}) {
            request.resource = resource;
            EQUALS(request.IsSane(), false);
        }
        request.resource = 0;
        MafiaNet::BitStream complete;
        request.Serialize(&complete, true);
        for (unsigned int bytes = 0; bytes < complete.GetNumberOfBytesUsed(); ++bytes) {
            MafiaNet::BitStream truncated(complete.GetData(), bytes, true);
            KCDC::Shared::RPC::SetPlayerResource received;
            received.Serialize(&truncated, false);
            EQUALS(received.IsSane(), false);
        }
        request.networkId = 0;
        EQUALS(request.IsSane(), false);
        request.networkId   = 42;
        request.incarnation = 0;
        EQUALS(request.IsSane(), false);
    });
});
