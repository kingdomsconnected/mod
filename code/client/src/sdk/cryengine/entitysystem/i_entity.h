#pragma once

#include "../../types.h"
#include "../3dengine/i_material.h"
#include "../3dengine/i_particle_effect.h"
#include "../3dengine/i_particle_emitter.h"
#include "../3dengine/spawn_params.h"
#include "../renderer/s_render_light.h"
#include "i_entity_class.h"
#include "i_entity_link.h"
#include "s_entity_event.h"

#include <memory>

namespace SDK {
    /** Lua constant registration at 0x181451BB5; CEntity activation uses it. */
    enum EEntityFlagsExtended : std::uint32_t {
        ENTITY_FLAG_EXTENDED_FORCE_UPDATE = 0x400
    };
    /** whyFlags accepted by the IEntity transform setters. */
    enum EEntityXFormFlags : std::uint32_t {
        ENTITY_XFORM_POS            = 1u << 1,
        ENTITY_XFORM_ROT            = 1u << 2,
        ENTITY_XFORM_SCL            = 1u << 3,
        ENTITY_XFORM_NO_PROPOGATE   = 1u << 4,
        ENTITY_XFORM_PHYSICS_STEP   = 1u << 6,
        ENTITY_XFORM_NOT_REREGISTER = 1u << 10,
        ENTITY_XFORM_NO_EVENT       = 1u << 11,
        ENTITY_XFORM_IGNORE_PHYSICS = 1u << 12,
    };
    /** Per-slot flags taken by `IEntity::SetSlotFlags`; see `CScriptBind_Entity::DrawSlot`. */
    enum EEntitySlotFlags : std::uint32_t {
        ENTITY_SLOT_RENDER         = 1u << 0,
        ENTITY_SLOT_RENDER_NEAREST = 1u << 1,
    };

    class IEntityArchetype;
    class IEntityProxy;
    struct IAIObject;
    struct ICharacterInstance;
    struct IGeomCacheRenderNode;
    struct IPhysicalEntity;
    struct IStatObj;

    /** How a child is attached (`IEntity::EAttachmentFlags`, CryEntitySystem). */
    enum EAttachmentFlags : std::int32_t {
        // The child keeps where it stands in the world; its local transform
        // is worked out from the parent's, rather than the one it carries
        // being read as local.
        ATTACHMENT_KEEP_TRANSFORMATION = 1 << 0,
    };

    struct SChildAttachParams {
        std::int32_t flags    = 0;
        std::int32_t reserved = 0;
        const char *target    = nullptr;
    };

    /**
     * IEntity, abstract vtable 0x1840727A8; CEntity's implementation is at
     * 0x184072C78. KCD2 inserts several entries relative to the public legacy
     * CryEngine interface, so only slots confirmed from CEntity are named.
     */
    class IEntity {
      public:
        virtual ~IEntity()                                                              = default; // +0x000
        virtual EntityId GetId() const                                                  = 0;       // +0x008
        virtual EntityGUID GetGuid() const                                              = 0;       // +0x010
        virtual IEntityClass *GetClass() const                                          = 0;       // +0x018
        virtual IEntityArchetype *GetArchetype()                                        = 0;       // +0x020
        virtual void SetFlags(std::uint32_t flags)                                      = 0;       // +0x028
        virtual std::uint32_t GetFlags() const                                          = 0;       // +0x030
        virtual void AddFlags(std::uint32_t flags)                                      = 0;       // +0x038
        virtual void ClearFlags(std::uint32_t flags)                                    = 0;       // +0x040
        virtual bool CheckFlags(std::uint32_t flags) const                              = 0;       // +0x048
        virtual void SetFlagsExtended(std::uint32_t flags)                              = 0;       // +0x050
        virtual std::uint32_t GetFlagsExtended() const                                  = 0;       // +0x058
        virtual bool CheckFlagsExtended(std::uint32_t flags) const                      = 0;       // +0x060
        virtual void Opaque13()                                                         = 0;       // +0x068, returns the byte at +0xF6; set on level-placed entities
        virtual void Opaque14()                                                         = 0;       // +0x070
        virtual void Opaque15()                                                         = 0;       // +0x078
        virtual bool IsGarbage() const                                                  = 0;       // +0x080, bit 5 of the flags word at +0x08 (m_bGarbage)
        virtual void SetName(const char *name)                                          = 0; // +0x088
        virtual const char *GetName() const                                             = 0; // +0x090
        virtual const char *GetEntityTextDescription() const                            = 0; // +0x098
        virtual void SerializeXML(XmlNodeRef &entityNode, bool loading)                 = 0; // +0x0A0
        virtual bool IsLoadedFromLevelFile() const                                      = 0; // +0x0A8
        virtual void AttachChild(IEntity *child, const SChildAttachParams &params = {}) = 0; // +0x0B0
        virtual void DetachAll(int detachFlags = 0)                                     = 0; // +0x0B8
        virtual void DetachThis(int detachFlags = 0, int whyFlags = 0)                  = 0; // +0x0C0
        virtual int GetChildCount() const                                               = 0; // +0x0C8
        virtual IEntity *GetChild(int index) const                                      = 0; // +0x0D0
        virtual IEntity *GetParent() const                                              = 0; // +0x0D8
        virtual void Opaque28()                                                         = 0;
        virtual bool IsParentAttachmentValid() const                                    = 0; // +0x0E8
        /** Sets the world transform (+0xF0); `DriveHorseToHarness` places a lagging horse through it. */
        virtual void SetWorldTM(const Matrix34 &tm, int whyFlags = 0)                   = 0; // +0x0F0
        /** Sets the transform relative to the parent (+0xF8); `RollCartWheel` turns a wheel through it. */
        virtual void SetLocalTM(const Matrix34 &tm, int whyFlags = 0)                   = 0; // +0x0F8
        /**
         * The entity's world transform, held at CEntity+0x58 and returned by
         * reference. CView::Update reads exactly this slot for a view linked
         * to a camera entity: the translation column becomes the view
         * position and the 3x3 its rotation, which is what lets an entity
         * drive the camera.
         */
        virtual const Matrix34 &GetWorldTM() const = 0; // +0x100
        virtual void Opaque33()                    = 0;
        /**
         * The axis-aligned world box around the entity's geometry.
         *
         * An entity with no geometry comes back as the engine's reset box --
         * min at +1e15 and max at -1e15 -- rather than as a box at the
         * origin, so `min.x > max.x` is the test for "nothing to bound".
         */
        virtual void GetWorldBounds(AABB &bounds) const = 0; // +0x110
        /** The same box in entity space, before the world transform. */
        virtual void GetLocalBounds(AABB &bounds) const                                                                   = 0; // +0x118
        virtual void Opaque36()                                                                                           = 0;
        virtual void Opaque37()                                                                                           = 0;
        virtual void Opaque38()                                                                                           = 0;
        virtual void SetPos(const Vec3 &position, int whyFlags = 0, bool recalcPhysicsBounds = false, bool force = false) = 0; // +0x138
        virtual const Vec3 &GetPos() const                                                                                = 0; // +0x140
        virtual void SetRotation(const Quat &rotation, int whyFlags = 0)                                                  = 0; // +0x148
        virtual const Quat &GetRotation() const                                                                           = 0; // +0x150
        virtual void SetScale(const Vec3 &scale, int whyFlags = 0)                                                        = 0; // +0x158
        virtual const Vec3 &GetScale() const                                                                              = 0; // +0x160
        virtual void SetPosRotScale(const Vec3 &position, const Quat &rotation, const Vec3 &scale, int whyFlags = 0)      = 0; // +0x168
        virtual Vec3 GetWorldPos() const                                                                                  = 0; // +0x170
        virtual Ang3 GetWorldAngles() const                                                                               = 0; // +0x178
        virtual Quat GetWorldRotation() const                                                                             = 0; // +0x180
        virtual const Vec3 &GetForwardDir() const                                                                         = 0; // +0x188
        virtual void Opaque50()                                                                                           = 0;
        virtual void Activate(bool active)                                                                                = 0;
        virtual void Opaque52()                                                                                           = 0;
        virtual bool IsActive() const                                                                                     = 0;
        virtual void Opaque54()                                                                                           = 0;
        virtual void Opaque55()                                                                                           = 0;
        virtual void Opaque56()                                                                                           = 0;
        virtual void Opaque57()                                                                                           = 0;
        virtual void Opaque58()                                                                                           = 0;
        virtual int LoadObject(int slot, const char *fileName, int loadFlags)                                             = 0; // +0x1D8, 0x1820F8F10: LoadCharacter for .chr/.cdf/.cga, else LoadGeometry; the level loader's for a placed `Geometry` (bit 0 physicalizes, bit 1 keeps loaded)
        virtual bool SendEvent(SEntityEvent &event)                                                                       = 0; // +0x1E0, 0x1804B6048
        virtual void Opaque61()                                                                                           = 0;
        virtual void KillTimer(int timerId)                                                                               = 0; // +0x1F0; CProjectile::CancelTimers (0x182AAB840)
        /** Sets CEntity +0x08 bit 0x10, raises ENTITY_EVENT_HIDE/UNHIDE and recurses into children. */
        virtual void Hide(bool hide) = 0; // +0x1F8, 0x18087963C
        /** CEntity +0x08 bit 0x10; also read by CScriptBind_Entity::IsHidden. */
        virtual bool IsHidden() const = 0; // +0x200, 0x1804B5260
        virtual void Opaque65()       = 0;
        /** CEntity +0x08 bit 0x40000; set by Invisible and events 41/42. */
        virtual bool IsInvisible() const = 0; // +0x210, 0x1809DFFF0
        virtual IAIObject *GetAI() const = 0; // +0x218
        virtual void Opaque68()          = 0;
        virtual void Opaque69()          = 0;
        virtual void Opaque70()          = 0;
        /**
         * Creates or removes the entity's AI object. Params whose type field
         * is zero remove the existing object and create nothing, which is how
         * CScriptBind_Action::RegisterWithAI clears an entity before
         * registering it for real.
         */
        virtual bool RegisterInAISystem(const void *params) = 0; // +0x238
        virtual void Opaque72()                             = 0;
        virtual void Opaque73()                             = 0;
        /** Proxy by kind; 2 is the script proxy. */
        virtual IEntityProxy *GetProxy(int proxyType) const = 0; // +0x250
        virtual void Opaque75()                             = 0;
        /** +0x260. Builds the proxy of that kind and attaches it; the entity keeps ownership, so the returned share is only a way to read the pointer out. */
        virtual std::shared_ptr<IEntityProxy> CreateProxy(int proxyType) = 0;
        virtual void Opaque77()                                          = 0;
        virtual void Opaque78()                                          = 0;
        virtual IPhysicalEntity *GetPhysics() const                      = 0; // +0x278
        virtual void Opaque80()                                          = 0;
        virtual void Opaque81()                                          = 0;
        virtual void Opaque82()                                          = 0;
        virtual void Opaque83()                                          = 0;
        /**
         * The entity's own material, over every slot that has no material of
         * its own; null takes it away. Also maps the physics parts onto the
         * material's surface type when it has one, and sends
         * ENTITY_EVENT_MATERIAL. What Lua's `entity:SetMaterial` reaches.
         */
        virtual void SetMaterial(IMaterial *material) = 0; // +0x2A0, 0x18041C004
        virtual void Opaque85()                                          = 0;
        virtual void Opaque86()                                          = 0;
        /**
         * Empties one render slot, releasing the mesh, character, light or
         * particle emitter in it. Proven from CScriptBind_Entity::FreeSlot
         * 0x180AC0A2C, which calls this slot straight after resolving the
         * entity.
         */
        virtual void FreeSlot(int slot) = 0; // +0x2B8, 0x180AC08D0
        /** One past the highest slot index in use, so an empty entity answers 0. */
        virtual int GetSlotCount() const = 0; // +0x2C0, 0x180421810
        virtual void Opaque89()          = 0;
        virtual void Opaque90()          = 0;
        // KCD2 returns a reference (0x180B7258C), unlike newer CryEngine.
        virtual const Matrix34 &GetSlotWorldTM(int slot) const = 0; // +0x2D8
        virtual void Opaque92()                                = 0;
        /**
         * Places the contents of one slot relative to the entity. KCD2's
         * override takes no whyFlags argument. Boxed in by GetSlotWorldTM
         * (+0x2D8) and SetSlotMaterial (+0x308), whose symbols both survive,
         * with the CryEngine slot order filling that gap exactly.
         */
        virtual void SetSlotLocalTM(int slot, const Matrix34 &localTM) = 0; // +0x2E8, 0x180AB2EA8
        virtual void Opaque94()                                        = 0;
        virtual void Opaque95()                                        = 0;
        virtual void Opaque96()                                        = 0;
        /**
         * Overrides the material on one render slot; null restores whatever the
         * loaded geometry declared. +0x308, symbol surviving:
         * `?SetSlotMaterial@CEntity@@UEAAXHPEAVIMaterial@@@Z` at 0x1837FD93C.
         */
        virtual void SetSlotMaterial(int slot, IMaterial *material) = 0; // +0x308
        virtual void Opaque98()                                     = 0;
        /**
         * One slot's `EEntitySlotFlags`, stored per slot on the render proxy.
         * Proven by `CScriptBind_Entity::DrawSlot` (0x1837D2D30), which reads
         * the flags, clears or sets `ENTITY_SLOT_RENDER`, and writes them back.
         */
        virtual void SetSlotFlags(int slot, std::uint32_t flags) = 0; // +0x310, 0x1809CEEB8
        virtual std::uint32_t GetSlotFlags(int slot) const      = 0; // +0x318, 0x1807F1CEC

        virtual ICharacterInstance *GetCharacter(int slot) = 0; // +0x328

        virtual void Opaque102() = 0;
        // Bit 31 selects the entity slot instead of a compound subobject.
        virtual IStatObj *GetStatObj(int slot)                         = 0; // +0x338
        virtual IParticleEmitter *GetParticleEmitter(int slot)         = 0; // +0x340
        virtual IGeomCacheRenderNode *GetGeomCacheRenderNode(int slot) = 0; // +0x348
        virtual void MoveSlot(IEntity *targetEntity, int slot)         = 0; // +0x350
        virtual int SetStatObj(IStatObj *statObj, int slot, bool updatePhysics,
            float mass = -1.0F)                                        = 0; // +0x358
        virtual int LoadGeometry(int slot, const char *fileName, const char *geometryName = nullptr,
            int loadFlags = 0)                                         = 0; // +0x360
        virtual int LoadCharacter(int slot, const char *fileName,
            int loadFlags = 0)                                         = 0; // +0x368
        virtual int LoadGeomCache(int slot, const char *fileName)      = 0; // +0x370
        virtual int LoadParticleEmitter(int slot, IParticleEffect *effect, const SpawnParams *params = nullptr, bool prime = false,
            bool serialize = false)                                    = 0; // +0x378
        virtual int SetParticleEmitter(int slot, IParticleEmitter *emitter,
            bool serialize = false)                                    = 0; // +0x380
        /** Copies the descriptor into a render command; no Lua script is involved. */
        virtual int LoadLight(int slot, SRenderLight *light)           = 0; // +0x388, 0x1837FB37C
        virtual void InvalidateTM(int whyFlags = 0,
            bool recalcPhysicsBounds           = false)                = 0; // +0x390
        virtual void EnablePhysics(bool enable)                        = 0; // +0x398
        virtual bool IsPhysicsEnabled() const                          = 0; // +0x3A0
        /** The head of the entity's link list, null with none; `C_Dice::InitializeDiceTableScene` walks it at +0x3A8. */
        virtual IEntityLink *GetEntityLinks()                          = 0; // +0x3A8
        /**
         * Appends a named link to `entityId` and sends the entity
         * ENTITY_EVENT_LINK (12) with it; a link of the same name to the same
         * entity is refused and answers null. KCD2 takes the name as a
         * `CryStringT` it copies (0x1803D0D38), where CryEngine takes a
         * `const char *`.
         */
        virtual IEntityLink *AddEntityLink(const CryStringT<char> &name, EntityId entityId, EntityGUID entityGuid) = 0; // +0x3B0
        virtual void Opaque119()                                       = 0;
        virtual void Opaque120()                                       = 0;
        virtual void Opaque121()                                       = 0;
        virtual void Opaque122()                                       = 0;
        virtual void Opaque123()                                       = 0;
        virtual void Opaque124()                                       = 0;
        virtual void Opaque125()                                       = 0;
        virtual void Opaque126()                                       = 0;
        virtual void Opaque127()                                       = 0;
        virtual void Opaque128()                                       = 0;
        virtual void Opaque129()                                       = 0;
        virtual void Opaque130()                                       = 0;
        virtual void Opaque131()                                       = 0;
        virtual void Opaque132()                                       = 0;
        virtual void Opaque133()                                       = 0;
        virtual void Opaque134()                                       = 0;
        virtual void Opaque135()                                       = 0;
        virtual void Opaque136()                                       = 0;
        virtual void Opaque137()                                       = 0;
        virtual void Opaque138()                                       = 0;
        virtual void Opaque139()                                       = 0;
        virtual void Opaque140()                                       = 0;
        virtual void Opaque141()                                       = 0;
        virtual void Opaque142()                                       = 0;
        virtual void Opaque143()                                       = 0;
        virtual void Opaque144()                                       = 0;
        virtual void Opaque145()                                       = 0;
        virtual void Opaque146()                                       = 0;
        virtual void Opaque147()                                       = 0;
        virtual void Opaque148()                                       = 0;
        virtual void Opaque149()                                       = 0;
        virtual void Opaque150()                                       = 0;
        virtual void Opaque151()                                       = 0;
        virtual void Opaque152()                                       = 0;
    };

    static_assert(sizeof(SChildAttachParams) == 0x10, "SChildAttachParams size mismatch");
} // namespace SDK
