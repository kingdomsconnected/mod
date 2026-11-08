#pragma once

#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * What `C_Soul::GetGender` answers, read from `archetype+0x1C`.
     *
     * Every value is spelled in the RTTR registration at `0x18105055C`, as
     * `wh::entitymodule::ActorGender`. Same numbering as `soul_archetype.xml`
     * `gender_id`.
     */
    enum class E_ActorGender : std::uint8_t {
        NotDefined = 0,
        Male       = 1,
        Female     = 2,
        Unisex     = 3,
    };

    /**
     * `soul_archetype.xml` `race_id`, from `archetype+0x18`. Only the human
     * value is named; the rest are animals and nothing here tells a boar from
     * a hare. A full dword, as the native returns it.
     */
    enum class E_Race : std::int32_t {
        Human = 0,
    };
} // namespace SDK::wh::entitymodule
