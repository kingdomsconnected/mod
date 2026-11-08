#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    struct IMaterial;
    struct IStatObj;

    /** Exact vegetation-group descriptor consumed by tree instance spawning. */
    struct IStatInstGroup {
        enum EPlayerHideable : std::int32_t {
            ePlayerHideable_None,
            ePlayerHideable_High,
            ePlayerHideable_Mid,
            ePlayerHideable_Low,
            ePlayerHideable_Count,
        };

        IStatObj *statObj;                         // 0x000
        char fileName[256];                        // 0x008
        bool hideability;                          // 0x108
        bool secondaryHideability;                 // 0x109
        bool pickable;                             // 0x10A
        std::byte padding010B;
        float bending;                             // 0x10C
        std::uint8_t castShadowMinSpec;            // 0x110
        bool dynamicDistanceShadows;               // 0x111
        bool giMode;                               // 0x112
        bool offlineProcedural;                    // 0x113
        bool instancing;                           // 0x114
        std::byte padding0115[0x3];
        float spriteDistanceRatio;                 // 0x118
        float lodDistanceRatio;                    // 0x11C
        float shadowDistanceRatio;                 // 0x120
        float maxViewDistanceRatio;                // 0x124
        float brightness;                          // 0x128
        bool useSprites;                           // 0x12C
        bool randomRotation;                       // 0x12D
        std::byte padding012E[0x2];
        std::int32_t rotationRangeToTerrainNormal; // 0x130
        float alignToTerrainCoefficient;           // 0x134
        bool useTerrainColor;                      // 0x138
        bool allowIndoor;                          // 0x139
        bool autoMerged;                           // 0x13A
        bool ignoreTerrainLayerBlend;              // 0x13B
        float density;                             // 0x140
        float elevationMax;                        // 0x144
        float elevationMin;                        // 0x148
        float size;                                // 0x14C
        float sizeVariation;                       // 0x150
        float slopeMax;                            // 0x154
        float slopeMin;                            // 0x158
        float stiffness;                           // 0x15C
        float damping;                             // 0x160
        float variance;                            // 0x164
        float airResistance;                       // 0x168
        float vegetationRadius;                    // 0x16C
        float vegetationVerticalRadius;            // 0x170
        float vegetationHorizontalRadius;          // 0x174
        EPlayerHideable playerHideable;            // 0x178
        std::int32_t id;                           // 0x17C
        std::int32_t minimumConfigSpec;            // 0x180
        std::uint32_t state0184;                   // 0x184
        IMaterial *material;                       // 0x188
        std::uint8_t materialLayers;               // 0x190
        std::uint8_t texturesAreStreamedIn;        // 0x191
        std::byte padding0192[0x6];
        std::uint64_t renderFlags;                 // 0x198
    };

    static_assert(offsetof(IStatInstGroup, autoMerged) == 0x13A,
                  "IStatInstGroup::autoMerged offset mismatch");
    static_assert(offsetof(IStatInstGroup, material) == 0x188,
                  "IStatInstGroup::material offset mismatch");
    static_assert(offsetof(IStatInstGroup, renderFlags) == 0x198,
                  "IStatInstGroup::renderFlags offset mismatch");
    static_assert(sizeof(IStatInstGroup) == 0x1A0,
                  "IStatInstGroup size mismatch");
} // namespace SDK
