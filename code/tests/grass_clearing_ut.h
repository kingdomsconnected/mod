#pragma once

#include "shared/features/grass/grass_clearing.h"
#include "shared/features/level_edit/level_edit_definition.h"
#include "shared/features/level_edit/level_edit_entity.h"

#include <mafianet/bit_stream.h>
#include <networking/replication/network_entity.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <limits>
#include <string>

MODULE(grass_clearing, {
    IT("holds the plants rooted in its box, faces included, and no others", {
        KCDC::Shared::Game::GrassClearing box;
        box.center   = {100.0F, 200.0F, 50.0F};
        box.halfSize = {4.0F, 2.0F, 1.5F};
        EQUALS(box.IsSane(), true);
        EQUALS(box.Contains({100.0F, 200.0F, 50.0F}), true);
        EQUALS(box.Contains({104.0F, 202.0F, 51.5F}), true);
        EQUALS(box.Contains({104.1F, 200.0F, 50.0F}), false);
        EQUALS(box.Contains({100.0F, 202.1F, 50.0F}), false);
        // Rooted on the terrace above, or in the cellar of the hill, it is not under the house.
        EQUALS(box.Contains({100.0F, 200.0F, 51.6F}), false);
        EQUALS(box.Contains({100.0F, 200.0F, 48.4F}), false);
    });

    IT("turns with its yaw, counter-clockwise seen from above", {
        KCDC::Shared::Game::GrassClearing box;
        box.center     = {10.0F, 10.0F, 0.0F};
        box.halfSize   = {5.0F, 1.0F, 2.0F};
        box.yawDegrees = 90.0F;
        // The long side now runs along the world's Y.
        EQUALS(box.Contains({10.0F, 14.5F, 0.0F}), true);
        EQUALS(box.Contains({14.5F, 10.0F, 0.0F}), false);
        EQUALS(box.Contains({10.9F, 6.0F, 0.0F}), true);
        EQUALS(box.Contains({11.2F, 6.0F, 0.0F}), false);

        box.yawDegrees = 45.0F;
        EQUALS(box.Contains({13.0F, 13.0F, 0.0F}), true);
        EQUALS(box.Contains({13.0F, 7.0F, 0.0F}), false);

        // The world box a sector is weighed against holds the turned one.
        glm::vec3 low {}, high {};
        box.Bounds(low, high);
        const float reach = (5.0F + 1.0F) * std::sqrt(0.5F);
        EQUALS(std::fabs(high.x - (10.0F + reach)) < 1.0e-4F, true);
        EQUALS(std::fabs(low.y - (10.0F - reach)) < 1.0e-4F, true);
        EQUALS(low.z, -2.0F);
        EQUALS(high.z, 2.0F);
    });

    IT("refuses a box that is not finite or outside its limits", {
        KCDC::Shared::Game::GrassClearing box;
        EQUALS(box.IsSane(), false);
        box.halfSize = {1.0F, 1.0F, 1.0F};
        EQUALS(box.IsSane(), true);
        box.halfSize.y = KCDC::Shared::Game::GrassClearing::kMaxHalfSize + 1.0F;
        EQUALS(box.IsSane(), false);
        box.halfSize.y = -1.0F;
        EQUALS(box.IsSane(), false);
        box.halfSize.y = 1.0F;
        box.center.x   = std::numeric_limits<float>::infinity();
        EQUALS(box.IsSane(), false);
        box.center.x   = 0.0F;
        box.yawDegrees = std::numeric_limits<float>::quiet_NaN();
        EQUALS(box.IsSane(), false);
    });

    IT("writes a grass edit into a map entry and reads the same box back", {
        KCDC::Shared::Game::LevelEditDefinition grass;
        grass.kind       = KCDC::Shared::Game::LevelEditKind::Grass;
        grass.at         = {1234.5F, 678.25F, 91.0F};
        grass.halfSize   = {6.1F, 4.3F, 2.5F};
        grass.yawDegrees = 33.3F;
        grass.hidden     = true;
        EQUALS(grass.HasKey(), true);
        EQUALS(grass.IsRemoved(), true);

        const nlohmann::json entry = KCDC::Shared::Game::LevelEditDefinitionToJson(grass);
        EQUALS(entry.at("kind").get<std::string>(), std::string("grass"));
        EQUALS(entry.contains("name"), false);

        // Through text, as a project file carries it: the box is its own key, so it has to come back to the bit.
        KCDC::Shared::Game::LevelEditDefinition read;
        std::string error;
        EQUALS(KCDC::Shared::Game::LevelEditDefinitionFromJson(nlohmann::json::parse(entry.dump()), read, error), true);
        EQUALS(read.kind, KCDC::Shared::Game::LevelEditKind::Grass);
        EQUALS(read.Clearing() == grass.Clearing(), true);
        EQUALS(read.hidden, true);
        EQUALS(KCDC::Shared::Game::LevelEditDefinitionToJson(read) == entry, true);

        // A script's own Vector3 reads too, and the yaw may be left out.
        const nlohmann::json scripted = {{"kind", "grass"}, {"at", {{"x", 1.0}, {"y", 2.0}, {"z", 3.0}}}, {"halfSize", {{"x", 4.0}, {"y", 5.0}, {"z", 6.0}}}, {"hidden", true}};
        EQUALS(KCDC::Shared::Game::LevelEditDefinitionFromJson(scripted, read, error), true);
        EQUALS(read.halfSize.y, 5.0F);
        EQUALS(read.yawDegrees, 0.0F);
    });

    IT("refuses a grass entry with no usable box", {
        KCDC::Shared::Game::LevelEditDefinition read;
        std::string error;
        EQUALS(KCDC::Shared::Game::LevelEditDefinitionFromJson({{"kind", "grass"}, {"at", {1, 2, 3}}}, read, error), false);
        EQUALS(error.find("halfSize") != std::string::npos, true);
        EQUALS(KCDC::Shared::Game::LevelEditDefinitionFromJson({{"kind", "grass"}, {"at", {1, 2, 3}}, {"halfSize", {1, 0, 1}}}, read, error), false);
        EQUALS(KCDC::Shared::Game::LevelEditDefinitionFromJson({{"kind", "grass"}, {"at", {1, 2, 3}}, {"halfSize", {1, 1, 1}}, {"yaw", "east"}}, read, error), false);
        EQUALS(KCDC::Shared::Game::LevelEditDefinitionFromJson({{"kind", "lawn"}, {"at", {1, 2, 3}}}, read, error), false);
        EQUALS(error.find("'grass'") != std::string::npos, true);
    });

    IT("replicates a grass edit's box with its key, and its clearing switched off", {
        KCDC::Shared::Entities::LevelEditEntity sent;
        sent.kind       = KCDC::Shared::Game::LevelEditKind::Grass;
        sent.at         = {10.0F, 20.0F, 30.0F};
        sent.halfSize   = {3.0F, 4.0F, 2.0F};
        sent.yawDegrees = -12.5F;
        sent.hidden     = true;

        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true);
        sent.OnSerializeConstruction(writer);

        KCDC::Shared::Entities::LevelEditEntity received;
        Framework::Networking::Replication::FieldSerializer reader(&stream, false);
        received.OnSerializeConstruction(reader);
        EQUALS(reader.Good(), true);
        EQUALS(received.kind, KCDC::Shared::Game::LevelEditKind::Grass);
        EQUALS(received.Describe().Clearing() == sent.Describe().Clearing(), true);
        EQUALS(received.Describe().IsRemoved(), true);

        sent.hidden = false;
        MafiaNet::BitStream update;
        Framework::Networking::Replication::FieldSerializer updateWriter(&update, true);
        sent.SerializeFields(updateWriter);
        Framework::Networking::Replication::FieldSerializer updateReader(&update, false);
        received.SerializeFields(updateReader);
        EQUALS(updateReader.Good(), true);
        EQUALS(received.Describe().IsRemoved(), false);
        EQUALS(received.halfSize.y, 4.0F);
    });
});
