#pragma once

#include "c_entity_layer.h"
#include "i_bsp_tree_3d.h"
#include "i_entity_class.h"
#include "i_entity_class_registry.h"
#include "i_entity.h"
#include "i_entity_iterator.h"
#include "i_entity_system_sink.h"
#include "s_entity_proximity_query.h"
#include "../../types.h"

namespace SDK {
    /** Native class-registry names accepted by IEntitySystem spawn params. */
    namespace EntityClassNames {
        inline constexpr const char *Default = "Default";
        inline constexpr const char *BasicEntity = "BasicEntity";
        inline constexpr const char *GeomEntity = "GeomEntity";
        inline constexpr const char *RopeEntity = "RopeEntity";
        inline constexpr const char *FlowgraphEntity = "FlowgraphEntity";
        inline constexpr const char *AudioListener = "AudioListener";

        /**
         * Declared with no `Script`, so nothing Lua runs when one is spawned
         * and there is no property table to neuter. Its `Invisible` is
         * `ECLF_INVISIBLE`, which only hides the class from the editor palette.
         */
        inline constexpr const char *AreaSphere = "AreaSphere";

        // WH-specific entity classes registered during game-framework setup.
        inline constexpr const char *PickableItem = "PickableItem";
        inline constexpr const char *DocumentItem = "DocumentItem";
        inline constexpr const char *InteractionTrigger = "InteractionTrigger";
        inline constexpr const char *SequenceTrigger = "SequenceTrigger";
        inline constexpr const char *SmartObjectTrigger = "SmartObjectTrigger";
        inline constexpr const char *ActionTrigger = "ActionTrigger";
        inline constexpr const char *KettleActionTrigger =
            "KettleActionTrigger";
        inline constexpr const char *FoodProcessingTrigger =
            "FoodProcessingTrigger";
        inline constexpr const char *WaterTubeActionTrigger =
            "WaterTubeActionTrigger";
        inline constexpr const char *BedTrigger = "BedTrigger";
        inline constexpr const char *AnimalSpawner = "AnimalSpawner";
        inline constexpr const char *CatHolder = "CatHolder";
        inline constexpr const char *CatWaypoint = "CatWaypoint";
        inline constexpr const char *AnimChar = "AnimChar";
        inline constexpr const char *StoneThrowingPile = "StoneThrowingPile";
        inline constexpr const char *Battlement = "Battlement";
        inline constexpr const char *WHCart = "WHCart";
        inline constexpr const char *WHCartMountPoint = "WHCartMountPoint";
        inline constexpr const char *PlayerWeapon = "PlayerWeapon";
        inline constexpr const char *Stash = "Stash";
        inline constexpr const char *StashCorpse = "StashCorpse";
        inline constexpr const char *CartStash = "CartStash";
        inline constexpr const char *Nest = "Nest";
        inline constexpr const char *DestroStash = "DestroStash";
        inline constexpr const char *Lockpickable = "Lockpickable";
        inline constexpr const char *CarryItemPile = "CarryItemPile";
        inline constexpr const char *AnimObject = "AnimObject";
        inline constexpr const char *RandomEvent = "RandomEvent";
        inline constexpr const char *RandomEventPlace = "RandomEventPlace";
        inline constexpr const char *TagPointWithScript = "TagPointWithScript";
        inline constexpr const char *AlchemyTable = "AlchemyTable";
        inline constexpr const char *Smithery = "Smithery";
        inline constexpr const char *AnimDoor = "AnimDoor";
        inline constexpr const char *AreaUnion = "AreaUnion";
        inline constexpr const char *Hole = "Hole";
        inline constexpr const char *Debris = "Debris";
        inline constexpr const char *RuntimePrefabAutoPhase =
            "RuntimePrefabAutoPhase";
        inline constexpr const char *LedgeObject = "LedgeObject";
        inline constexpr const char *LedgeObjectStatic = "LedgeObjectStatic";
        inline constexpr const char *WaterPuddle = "WaterPuddle";
        inline constexpr const char *Rain = "Rain";
        inline constexpr const char *InteractiveObjectEx =
            "InteractiveObjectEx";
    } // namespace EntityClassNames

    enum EEntityEvent : int;

    class IAreaManager;
    class IBreakableManager;
    class IEntityArchetype;
    class IEntityEventListener;
    class IEntityPoolManager;
    class IScriptTable;
    class ISystem;
    struct IDataReadStream;
    struct IDataWriteStream;

    /** Entity flags, as `SEntitySpawnParams::flags` takes them. */
    enum EEntityFlags : std::uint32_t {
        /**
         * The entity's render slots cast shadows (ERF_CASTSHADOWMAPS). The
         * level loader adds it from `CastShadowMinSpec` (0x18361F6BC); a
         * runtime spawn has it only when asked.
         */
        ENTITY_FLAG_CASTSHADOW = 1U << 1,

        /**
         * Spawned at runtime rather than placed by the level. `SpawnEntity`
         * mints a unique EntityGuid for it when none is given (0x1803D1F74);
         * `C_SoulList::InitSoulFromEntity` marks its soul as spawned
         * (0x180AD7F6C), which is what lets `C_SoulList::ReleaseSoul` free the
         * soul when the actor is released; and `C_Actor::ShouldStartPutAway`
         * leaves it awake at Init.
         */
        ENTITY_FLAG_SPAWNED = 1U << 24,
    };

    struct SEntitySpawnParams {
        EntityId id = 0;
        EntityId previousId = 0;
        EntityGUID guid = 0;
        EntityGUID previousGuid = 0;
        IEntityClass *entityClass = nullptr;
        IEntityArchetype *archetype = nullptr;
        const char *layerName = "";
        XmlNodeRef entityNode{};
        const char *name = "";
        std::uint32_t flags = 0;
        std::uint32_t extendedFlags = 0;
        std::uint8_t reserved_48[2]{};
        bool ignoreLock = false;
        bool staticEntityId = false;
        bool createdThroughPool = false;
        std::uint8_t reserved_4D[3]{};
        Vec3 position{};
        Quat rotation{0.0F, 0.0F, 0.0F, 1.0F};
        Vec3 scale{1.0F, 1.0F, 1.0F};
        void *userData = nullptr;
        IScriptTable *propertiesTable = nullptr;
        IScriptTable *propertiesInstanceTable = nullptr;
    };

    /**
     * IEntitySystem, vtable 0x183D268E8; CEntitySystem's implementation is at
     * 0x184077048. KCD2's interface is shuffled, so only slots proven directly
     * from their implementations are named.
     */
    class IEntitySystem {
      public:
        virtual ~IEntitySystem() = default; // +0x000
        virtual void Opaque01() = 0;
        virtual void Opaque02() = 0;
        virtual void Opaque03() = 0;
        virtual void Opaque04() = 0;
        virtual void Opaque05() = 0;
        virtual void Opaque06() = 0;
        virtual void Opaque07() = 0;
        virtual void Opaque08() = 0;
        virtual void Opaque09() = 0;
        virtual void Opaque10() = 0;
        virtual IEntityClassRegistry *GetClassRegistry() = 0; // +0x058
        virtual IEntity *SpawnEntity(SEntitySpawnParams &params, bool autoInit = true) = 0; // +0x060
        virtual bool InitEntity(IEntity *entity, SEntitySpawnParams &params) = 0; // +0x068
        virtual IEntity *GetEntity(EntityId id) const = 0; // +0x070
        virtual void Opaque15() = 0;
        virtual IEntity *FindEntityByName(const char *entityName) const = 0; // +0x080
        virtual void ReserveEntityId(EntityId id) = 0; // +0x088
        virtual EntityId ReserveUnknownEntityId() = 0; // +0x090
        virtual void RemoveEntity(EntityId id, bool forceRemoveNow = false) = 0; // +0x098
        virtual void UpdateDeletedEntities(bool profile, bool force) = 0; // +0x0A0
        virtual std::uint32_t GetNumEntities() const = 0; // +0x0A8
        virtual IEntityIt *GetEntityIterator() = 0; // +0x0B0
        virtual void Opaque23() = 0;
        virtual void Opaque24() = 0;

        /**
         * Every entity whose bounds meet `query.box`, filtered by the class
         * and flags it names. The partition grid answers, so it costs what
         * the box covers rather than what the level holds. The game's own
         * `System.GetPhysicalEntitiesInBox` is this call and nothing else,
         * which is what pins the slot.
         */
        virtual int QueryProximity(SEntityProximityQuery &query) = 0; // +0x0C8

        virtual void Opaque26() = 0;
        virtual void Opaque27() = 0;
        virtual void Opaque28() = 0;
        virtual void AddSink(IEntitySystemSink *sink, std::uint32_t subscriptions, std::uint64_t onEventSubscriptions) = 0; // +0x0E8
        virtual void RemoveSink(IEntitySystemSink *sink) = 0; // +0x0F0
        virtual void Opaque31() = 0;
        virtual bool IsIDUsed(EntityId id) const = 0; // +0x100
        virtual void Opaque33() = 0;
        virtual ISystem *GetSystem() const = 0; // +0x110
        virtual bool ExtractArcheTypeLoadParams(
            XmlNodeRef &entityNode, SEntitySpawnParams &spawnParams) const = 0; // +0x118
        virtual bool ExtractEntityLoadParams(
            XmlNodeRef &entityNode, SEntitySpawnParams &spawnParams) const = 0; // +0x120
        virtual void BeginCreateEntities(int amountToCreate) = 0; // +0x128
        virtual void Opaque38() = 0;
        virtual void EndCreateEntities() = 0; // +0x138
        virtual void LoadEntities(XmlNodeRef objectsNode,
                                  bool isLoadingLevelFile) = 0; // +0x140
        virtual void Opaque41() = 0;
        virtual void Opaque42() = 0;
        virtual void Opaque43() = 0;
        virtual void Opaque44() = 0;
        virtual void AddEntityEventListener(
            EntityId entityId, EEntityEvent event,
            IEntityEventListener *listener) = 0; // +0x168
        virtual void RemoveEntityEventListener(
            EntityId entityId, EEntityEvent event,
            IEntityEventListener *listener) = 0; // +0x170
        virtual EntityId FindEntityByGuid(const EntityGUID &guid) const = 0; // +0x178
        virtual EntityId FindEntityByEditorGuid(
            const char *editorGuid) const = 0; // +0x180
        virtual EntityId GenerateEntityIdFromGuid(
            const EntityGUID &guid) = 0; // +0x188
        virtual IAreaManager *GetAreaManager() const = 0; // +0x190
        virtual void Opaque51() = 0;
        virtual IBreakableManager *GetBreakableManager() const = 0; // +0x1A0
        virtual IEntityPoolManager *GetIEntityPoolManager() const = 0; // +0x1A8
        virtual IEntityArchetype *LoadEntityArchetype(const char *archetypeName) = 0; // +0x1B0
        virtual IEntityArchetype *LoadEntityArchetype(XmlNodeRef archetypeNode) = 0; // +0x1B8
        virtual IEntityArchetype *CreateEntityArchetype(IEntityClass *entityClass,
                                                         const char *archetypeName) = 0; // +0x1C0
        virtual void Serialize(TSerialize serializer) = 0; // +0x1C8
        virtual void SetNextSpawnId(EntityId entityId) = 0; // +0x1D0
        virtual void ResetAreas() = 0; // +0x1D8
        virtual void UnloadAreas() = 0; // +0x1E0
        virtual void DumpEntities() = 0; // +0x1E8
        virtual void LockSpawning(bool lock) = 0; // +0x1F0
        virtual bool OnLoadLevel(const char *levelPath) = 0; // +0x1F8
        virtual IEntityLayer *AddLayer(
            const char *name, const char *parent, std::uint16_t id,
            bool hasPhysics, int specs, bool defaultLoaded,
            bool state0007, bool state0008) = 0; // +0x200
        virtual void LoadLayers(const char *dataFile) = 0; // +0x208
        virtual void LinkLayerChildren() = 0; // +0x210
        virtual void AddEntityToLayer(const char *layer, EntityId entityId) = 0; // +0x218
        virtual void Opaque68() = 0;
        virtual void ClearLayers() = 0; // +0x228
        virtual void Opaque70() = 0;
        virtual IEntityLayer *FindLayer(const char *layer) const = 0; // +0x238
        virtual void Opaque72() = 0;
        virtual void EnableDefaultLayers(
            bool isSerialized, bool disableNonDefaultLayers) = 0; // +0x248
        virtual void EnableLayer(const char *layer, bool enable,
                                 bool isSerialized = true) = 0; // +0x250
        virtual bool IsLayerEnabled(const char *layer,
                                    bool mustBeLoaded) const = 0; // +0x258
        virtual bool ShouldSerializedEntity(IEntity *entity) = 0; // +0x260
        virtual void RegisterPhysicCallbacks() = 0; // +0x268
        virtual void UnregisterPhysicCallbacks() = 0; // +0x270
        virtual void PurgeDeferredCollisionEvents(bool force = false) = 0; // +0x278
        virtual bool EntitiesUseGUIDs() const = 0; // +0x280
        virtual void SetEntitiesUseGUIDs(bool enable) = 0; // +0x288
        virtual void DebugDraw() = 0; // +0x290
        virtual void ResumePhysicsForSuppressedEntities(bool wakeUp) = 0; // +0x298
        virtual void SaveInternalState(IDataWriteStream &writer) const = 0; // +0x2A0
        virtual void LoadInternalState(IDataReadStream &reader) = 0; // +0x2A8
        virtual int GetLayerId(const char *layerName) const = 0; // +0x2B0
        virtual const char *GetLayerName(int layerId) const = 0; // +0x2B8
        virtual int GetLayerChildCount(const char *layerName) const = 0; // +0x2C0
        virtual const char *GetLayerChild(const char *layerName,
                                          int childIndex) const = 0; // +0x2C8
        virtual int GetVisibleLayerIDs(std::uint8_t *layerMask,
                                       std::uint32_t maxCount) const = 0; // +0x2D0
        virtual void ToggleLayerVisibility(const char *layer, bool enable,
                                           bool includeParent = true) = 0; // +0x2D8
        virtual void ToggleLayersBySubstring(
            const char *searchSubstring, const char *exceptionSubstring,
            bool enable) = 0; // +0x2E0
        virtual IBSPTree3D *CreateBSPTree3D(
            const IBSPTree3D::FaceList &faceList) = 0; // +0x2E8
        virtual void ReleaseBSPTree3D(IBSPTree3D *&tree) = 0; // +0x2F0
        virtual void Opaque95() = 0;
        virtual void Opaque96() = 0;
        virtual void Opaque97() = 0;

        IEntity *GetEntityByGuid(const EntityGUID &guid) const {
            const EntityId entityId = FindEntityByGuid(guid);
            return entityId != 0 ? GetEntity(entityId) : nullptr;
        }

        bool IsEntityLoaded(const EntityGUID &guid) const {
            return GetEntityByGuid(guid) != nullptr;
        }

        static IEntitySystem *GetInstance();
    };

    static_assert(offsetof(SEntitySpawnParams, entityClass) == 0x18, "SEntitySpawnParams::entityClass offset mismatch");
    static_assert(offsetof(SEntitySpawnParams, name) == 0x38, "SEntitySpawnParams::name offset mismatch");
    static_assert(offsetof(SEntitySpawnParams, ignoreLock) == 0x4A, "SEntitySpawnParams::ignoreLock offset mismatch");
    static_assert(offsetof(SEntitySpawnParams, position) == 0x50, "SEntitySpawnParams::position offset mismatch");
    static_assert(offsetof(SEntitySpawnParams, rotation) == 0x5C, "SEntitySpawnParams::rotation offset mismatch");
    static_assert(offsetof(SEntitySpawnParams, scale) == 0x6C, "SEntitySpawnParams::scale offset mismatch");
    static_assert(sizeof(SEntitySpawnParams) == 0x90, "SEntitySpawnParams size mismatch");
} // namespace SDK
