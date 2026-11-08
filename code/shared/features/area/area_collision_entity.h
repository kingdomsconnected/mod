#pragma once

#include <networking/replication/network_entity.h>

namespace KCDC::Shared::Entities {
    /** One server-owned boundary. The fixed field layout survives changes of
     * shape type and polygon point count on the reliable state channel. */
    class AreaCollisionEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::AreaCollision";
        std::string definitionJson;

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(definitionJson);
        }
        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &) override {}
    };
} // namespace KCDC::Shared::Entities
