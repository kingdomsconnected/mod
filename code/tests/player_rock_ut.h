#pragma once

#include "shared/features/player/player_rock.h"
#include "shared/features/player/player_rock_throw_history.h"

#include <mafianet/bit_stream.h>

#include <cstdint>
#include <limits>

namespace PlayerRockTests {
    inline KCDC::Shared::RPC::RockLaunched Throw(std::uint64_t sequence = 1) {
        KCDC::Shared::RPC::RockLaunched rock;
        rock.sequence    = sequence;
        rock.incarnation = 3;
        rock.epoch       = 7;
        rock.position    = {100.0F, 200.0F, 30.0F};
        rock.velocity    = {0.0F, 20.0F, 0.0F};
        return rock;
    }
} // namespace PlayerRockTests

MODULE(player_rock_protocol, {
    IT("round-trips a launch and rejects every truncation", {
        auto rock             = PlayerRockTests::Throw();
        rock.throwerNetworkId = 99;
        MafiaNet::BitStream stream;
        rock.Serialize(&stream, true);
        KCDC::Shared::RPC::RockLaunched launch;
        launch.Serialize(&stream, false);
        EQUALS(launch.IsSane() && launch.throwerNetworkId == 99 && launch.sequence == rock.sequence && launch.incarnation == rock.incarnation && launch.epoch == rock.epoch && launch.position == rock.position && launch.direction == rock.direction && launch.velocity == rock.velocity, true);

        // Every truncated prefix must be rejected, even one that ends on a
        // field boundary and leaves plausible defaults behind.
        bool rejected = true;
        for (unsigned int bytes = 0; bytes < stream.GetNumberOfBytesUsed(); ++bytes) {
            MafiaNet::BitStream truncated(stream.GetData(), bytes, false);
            KCDC::Shared::RPC::RockLaunched received = rock;
            received.Serialize(&truncated, false);
            rejected = rejected && !received.IsSane();
        }
        EQUALS(rejected, true);
    });

    IT("rejects implausible launches", {
        for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
            auto rock       = PlayerRockTests::Throw();
            rock.position.z = invalid;
            EQUALS(rock.IsSane(), false);
            rock             = PlayerRockTests::Throw();
            rock.direction.x = invalid;
            EQUALS(rock.IsSane(), false);
            rock            = PlayerRockTests::Throw();
            rock.velocity.y = invalid;
            EQUALS(rock.IsSane(), false);
        }
        auto rock       = PlayerRockTests::Throw();
        rock.velocity.y = 41.0F;
        EQUALS(rock.IsSane(), false);
        rock.velocity.y = -20.0F;
        EQUALS(rock.IsSane(), false);
        rock.velocity = {20.0F, 0.0F, 0.0F};
        EQUALS(rock.IsSane(), false);
        rock           = PlayerRockTests::Throw();
        rock.direction = {};
        EQUALS(rock.IsSane(), false);
        EQUALS(PlayerRockTests::Throw(0).IsSane(), false);
    });
});

MODULE(player_rock_history, {
    IT("orders, rate-limits and anchors launches to the thrower", {
        KCDC::Shared::Game::RockThrowHistory history;
        const glm::vec3 body {100.0F, 200.0F, 29.0F};
        EQUALS(history.Accept(PlayerRockTests::Throw(), body, 1000), true);
        EQUALS(history.Accept(PlayerRockTests::Throw(), body, 1001), false);
        auto rock = PlayerRockTests::Throw(2);
        rock.position.x += 7.0F;
        EQUALS(history.Accept(rock, body, 1001), false);
        EQUALS(history.Accept(PlayerRockTests::Throw(3), body, 1001), true);
        EQUALS(history.Accept(PlayerRockTests::Throw(4), body, 1001) && history.Accept(PlayerRockTests::Throw(5), body, 1001), true);
        EQUALS(history.Accept(PlayerRockTests::Throw(6), body, 1001), false);
        EQUALS(history.Accept(PlayerRockTests::Throw(6), body, 2000), false);
        EQUALS(history.Accept(PlayerRockTests::Throw(7), body, 2000), true);
        EQUALS(history.Accept(PlayerRockTests::Throw(8), {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, 3000), false);
    });

    IT("lets a clock that starts at zero refill", {
        KCDC::Shared::Game::RockThrowHistory history;
        const glm::vec3 body {100.0F, 200.0F, 29.0F};
        bool burst = true;
        for (std::uint64_t i = 1; i <= 4; ++i) {
            burst = burst && history.Accept(PlayerRockTests::Throw(i), body, 0);
        }
        EQUALS(burst, true);
        EQUALS(history.Accept(PlayerRockTests::Throw(5), body, 500), true);
    });
});
