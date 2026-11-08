#pragma once

#include "../../cryengine/system/xml_node_ref.h"
#include "../../types.h"
#include "c_object_database.h"

#include <cstddef>

namespace SDK::wh::databasemodule {
    /**
     * Tree-indexed Warhorse object database.
     *
     * The weather-profile specialization's table at 0x183AB8858 contains the
     * 21 inherited database entries plus one tree operation. Its constructor
     * and scalar deletion prove the complete 0x78-byte layout.
     */
    template <typename TObject,
              template <typename, typename> class TContainer>
    class C_ObjectTreeDatabase
        : public C_ObjectDatabase<TObject, TContainer> {
      public:
        ~C_ObjectTreeDatabase() override = default;

        /**
         * Appends the rows of a `<database>` node with exactly one table child
         * (+0xA8; `AppendFromXml_1817DF088` for clothing presets,
         * `AppendFromXml_18178D504` for weapon presets).
         *
         * Rows are deserialized through RTTR, so their attributes are the
         * table's own column names. It always appends; `replaceDuplicateKeys`
         * makes a row whose key already exists overwrite that row in place,
         * which is what `wh_db_AllowPatching` does for table parts. Follow it
         * with `RebuildIndex`. The middle argument is not read.
         */
        virtual bool AppendFromXml(const XmlNodeRef &database, void *unused, bool replaceDuplicateKeys) = 0;

      private:
        // The weather instantiation's ctor (0x180E35A1C) copies its three CryString arguments here.
        SDK::CryStringT<char> m_name0048; // 0x48
        SDK::CryStringT<char> m_name0050; // 0x50
        SDK::CryStringT<char> m_name0058; // 0x58
        bool m_flag0060;                  // 0x60, ctor clears
        std::byte m_padding0061[0x7];     // 0x61
        void *m_pTreeHead;                // 0x68, std::set head node
        std::size_t m_treeSize;           // 0x70
    };

    using C_WeatherProfileObjectTreeDatabase =
        C_ObjectTreeDatabase<environmentmodule::S_WeatherProfileData,
                             std::vector>;

    static_assert(sizeof(C_WeatherProfileObjectTreeDatabase) == 0x78,
                  "weather object-tree database size mismatch");
} // namespace SDK::wh::databasemodule
