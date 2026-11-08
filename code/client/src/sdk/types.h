#pragma once

#include "cryengine/system/xml_node_ref.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace SDK {
    /**
     * CryEngine's DynArray as this build lays it out: one pointer to the
     * elements, with the element count in the four bytes in front of them
     * (`SmallDynStorage`; `SkelLoader_Helpers::InitializeBones` reads
     * `*(u32 *)(data - 4)`). An empty array may hold null, so the count is
     * read only through a non-null pointer. A borrowed view, like StdVector.
     */
    template <typename Element> struct DynArray {
        Element *m_pData;

        std::uint32_t Size() const {
            return m_pData != nullptr ? reinterpret_cast<const std::uint32_t *>(m_pData)[-1] : 0;
        }

        Element &operator[](std::size_t index) const {
            return m_pData[index];
        }
    };

    /**
     * Layout of a `std::vector` the game owns: the three pointers MSVC emits
     * with iterator debugging off.
     *
     * The mod's own `std::vector` is not that -- its debug build carries a
     * fourth pointer to an iterator-debugging proxy -- so a game vector is
     * read through this and never through `std::vector`. A borrowed view: the
     * buffer is the game allocator's, and there is no way here to grow one.
     */
    template <typename Element> struct StdVector {
        Element *first;
        Element *last;
        Element *capacityEnd;

        std::size_t Size() const {
            return static_cast<std::size_t>(last - first);
        }

        bool IsEmpty() const {
            return first == last;
        }

        const Element *begin() const {
            return first;
        }

        const Element *end() const {
            return last;
        }

        const Element &operator[](std::size_t index) const {
            return first[index];
        }
    };

    /**
     * Warhorse's small-buffer vector, `StackVector<T, N>`, as a borrowed read.
     *
     * A global-namespace template of the game's own, not CryEngine's. The
     * layout is read out of `C_Pickpocketing::InterruptPickpocketing`
     * (0x182EA9C68), which builds a `StackVector<WUID, 11>` on its stack: the
     * inline storage first, a flag byte behind it, a pointer to that storage
     * serving as the arena, then a `std::vector` whose stateful allocator
     * points at the arena -- allocator, begin, end, capacity. Only the begin/end
     * pair is ever read here; `ResolvePickpocketingOutcome` reads the same two
     * at +0x70/+0x78 for N = 11.
     */
    template <typename Element, std::size_t Capacity> struct StackVector {
        alignas(Element) std::byte inlineStorage[sizeof(Element) * Capacity];
        bool inlineInUse;
        void *arena;
        void *allocator;
        Element *first;
        Element *last;
        Element *capacityEnd;

        std::size_t Size() const {
            return first != nullptr ? static_cast<std::size_t>(last - first) : 0;
        }

        const Element *begin() const {
            return first;
        }

        const Element *end() const {
            return first != nullptr ? last : first;
        }

        // For layout assertions on one instantiation: `offsetof` is a macro,
        // and a two-argument template name does not survive being passed to it.
        static constexpr std::size_t InlineInUseOffset() {
            return offsetof(StackVector, inlineInUse);
        }
        static constexpr std::size_t FirstOffset() {
            return offsetof(StackVector, first);
        }
        static constexpr std::size_t LastOffset() {
            return offsetof(StackVector, last);
        }
    };

    /**
     * Exact object layout of the engine's reference-counted CryStringT: one
     * pointer to a copy-on-write payload whose refcount, length and capacity
     * sit in the twelve bytes in front of it, on the game's heap. Copying the
     * struct shares that buffer without touching its refcount, so it is only
     * ever safe for a borrowed read.
     */
    template <typename Character> struct CryStringT {
        Character *m_pData;

        /**
         * The engine's own operator=(const char *), which is what every write
         * to a string the game owns has to go through. Storing a pointer the
         * mod allocated would leave the game freeing it through a foreign heap.
         */
        CryStringT &Assign(const Character *value);
    };

    template <> CryStringT<char> &CryStringT<char>::Assign(const char *value);

    /**
     * A CryStringT<char> built and destroyed through the engine's own
     * constructor and destructor, for passing text into a native that takes one
     * by reference. Constructing the struct by hand is not an option: the
     * destructor walks back twelve bytes to a refcount header only the engine's
     * allocator lays out.
     */
    class ScopedCryString final {
      public:
        explicit ScopedCryString(const char *value);
        ~ScopedCryString();

        ScopedCryString(const ScopedCryString &)            = delete;
        ScopedCryString &operator=(const ScopedCryString &) = delete;

        const CryStringT<char> &Get() const {
            return m_value;
        }

        operator const CryStringT<char> &() const {
            return m_value;
        }

        /**
         * For a native that assigns into a string the caller owns, through
         * the engine's own operator=, and leaves releasing it to the caller.
         */
        CryStringT<char> *Out() {
            return &m_value;
        }

        const char *c_str() const {
            return m_value.m_pData != nullptr ? m_value.m_pData : "";
        }

      private:
        CryStringT<char> m_value {nullptr};
    };

    /**
     * The storage for a CryStringT<char> a native member returns by value:
     * handed to it unconstructed as the hidden result slot, constructed there
     * by the native, and released through the engine's own destructor.
     */
    class ReturnedCryString final {
      public:
        ReturnedCryString() = default;
        ~ReturnedCryString();

        ReturnedCryString(const ReturnedCryString &)            = delete;
        ReturnedCryString &operator=(const ReturnedCryString &) = delete;

        CryStringT<char> *Slot() {
            return &m_value;
        }

        const char *c_str() const {
            return m_value.m_pData != nullptr ? m_value.m_pData : "";
        }

      private:
        CryStringT<char> m_value {nullptr};
    };

    /**
     * A CryStringT<char> built through the engine's constructor and handed to
     * whoever destroys it -- a native taking one by value, which MSVC passes
     * as a pointer to the caller's temporary and destroys in the callee. The
     * caller must not release it afterwards, which is exactly what
     * ScopedCryString would do.
     */
    CryStringT<char> ConstructCryString(const char *value);

    /**
     * Layout of a `std::string` the game owns: MSVC's release
     * `basic_string<char>` -- the sixteen-byte small-buffer union, then the
     * length, then the capacity.
     *
     * The mod's own `std::string` is not that. Its build has iterator
     * debugging on, so `_String_val` inherits `_Container_base12` and carries
     * a proxy pointer in front of the buffer, which makes the object eight
     * bytes longer. Handing one of those to a native that takes a
     * `std::string &` writes the length where the native reads the buffer.
     *
     * Only the small-buffer form can be built here. A longer string would
     * have to allocate, and the natives that take one of these move it into
     * game-owned storage and later free it through the game's allocator.
     */
    struct StdString {
        static constexpr std::size_t kMaxInlineLength = 15;

        union {
            char inlineBuffer[16];
            char *pointer;
        } storage {};
        std::size_t length = 0;
        std::size_t capacity = kMaxInlineLength;

        explicit StdString(const char *value) {
            for (; length < kMaxInlineLength && value[length] != '\0'; ++length) {
                storage.inlineBuffer[length] = value[length];
            }
            storage.inlineBuffer[length] = '\0';
        }

        /**
         * The whole text, for a native that takes a `std::string` by value and destroys it itself.
         * A longer text lives on the CRT heap: the game frees a `std::string` buffer through
         * `GameHeapFree1804FD8D0` (0x1804FD8D0), which hands anything outside its bucket pages to
         * the CRT's own `free` -- the same heap a /MD module's `malloc` uses. Never destroy one here
         * after passing it.
         */
        static StdString HandedOver(const char *value) {
            StdString text("");
            const std::size_t size = std::strlen(value);
            if (size <= kMaxInlineLength) {
                std::memcpy(text.storage.inlineBuffer, value, size + 1);
            }
            else {
                text.storage.pointer = static_cast<char *>(std::malloc(size + 1));
                std::memcpy(text.storage.pointer, value, size + 1);
                text.capacity = size;
            }
            text.length = size;
            return text;
        }

        /** The text, for reading one the game owns in place; past 15 characters it lives on the heap. */
        const char *CStr() const {
            return capacity > kMaxInlineLength ? storage.pointer : storage.inlineBuffer;
        }
    };

    using EntityId = std::uint32_t;
    using EntityGUID = std::uint64_t;
    /** Lua's ScriptHandle payload is a full qword even when it carries an EntityId. */
    using ScriptHandle = std::uint64_t;
    using ChannelId = std::uint16_t;

    /**
     * What `IActor::GetActorClass` answers: which concrete actor class a body
     * is, as a number rather than as the class-name string beside it. The
     * game declares the return as a plain `int` (`?GetActorClass@...UEBAHXZ`),
     * so the enum only gives the values a name; it does not change the ABI.
     *
     * Each enumerator is the constant that class's own override returns:
     * `C_Animal` 0x181A72480, `C_NPCActor` 0x181A74270, `C_Horse`
     * 0x181A74D20, `C_Dog` 0x181A83AC0, and `C_Player` through its vtable
     * slot 84, whose `return 17` the linker folded onto 0x181A7DFD0. The
     * numbering is sparse because the classes in between are not modeled
     * here, not because those values are unused.
     */
    enum ActorClass : int {
        ActorClass_Animal = 2,
        ActorClass_NPC    = 9,
        ActorClass_Horse  = 10,
        ActorClass_Player = 17,
        ActorClass_Dog    = 18,
    };

    /**
     * Render-node kinds, as `IRenderNode::GetRenderNodeType` answers and
     * `I3DEngine::CreateRenderNode` accepts.
     *
     * Spelled and valued by the engine's own enumerator name table at
     * 0x18492FEB0 -- 27 `{value, name}` pairs, the ones CryEngine's type info
     * prints and parses. This is Warhorse's own numbering and **not** the
     * later CryEngine one, which renamed the removed kinds to `Dummy_N` and
     * pushed everything from `eERType_TerrainSector` on up by two or three.
     * The table agrees with every constant-returning override in the binary:
     * `CBrush` 1, `CVegetation` 2, `CLightEntity` 3, `CFogVolumeRenderNode`
     * 6, `CDecalRenderNode` 7, `CParticleEmitter` 8, `CWaterVolumeRenderNode`
     * 9, `CWaterWaveRenderNode` 10, `CRoadRenderNode` 11,
     * `CDistanceCloudRenderNode` 12, `CRopeRenderNode` 15, `CMovableBrush`
     * 17, `CRenderProxy` 19, `CBreakableGlassRenderNode` 21,
     * `CCloudBlockerRenderNode` 22, `CMergedMeshRenderNode` 23 and
     * `CGeomCacheRenderNode` 24.
     */
    enum EERType : std::int32_t {
        eERType_NotRenderNode = 0,
        eERType_Brush = 1,
        eERType_Vegetation = 2,
        eERType_Light = 3,
        eERType_Cloud = 4,
        eERType_MergedMeshInstance = 5,
        eERType_FogVolume = 6,
        eERType_Decal = 7,
        eERType_ParticleEmitter = 8,
        eERType_WaterVolume = 9,
        eERType_WaterWave = 10,
        eERType_Road = 11,
        eERType_DistanceCloud = 12,
        eERType_VolumeObject = 13,
        eERType_TerrainSector = 14,
        eERType_Rope = 15,
        eERType_PrismObject = 16,
        eERType_MovableBrush = 17,
        eERType_LightPropagationVolume = 18,
        eERType_RenderProxy = 19,
        eERType_GameEffect = 20,
        eERType_BreakableGlass = 21,
        eERType_CloudBlocker = 22,
        eERType_MergedMesh = 23,
        eERType_GeomCache = 24,
        eERType_CharacterDoNotUse = 25,
        // 26 is a live kind -- the count below leaves room for it -- that the
        // engine's name table does not register, so it has no proven spelling.
        eERType_TypesNum = 27,
    };

    struct CryGUID {
        std::uint64_t hipart;
        std::uint64_t lopart;
    };

    struct Vec2 {
        float x;
        float y;
    };

    struct Vec3 {
        float x;
        float y;
        float z;
    };

    /** Three diagonal scaling components used by skeleton-pose queries. */
    struct Diag33 {
        float x;
        float y;
        float z;
    };

    static_assert(sizeof(Diag33) == 0xC, "Diag33 size mismatch");

    struct Ang3 {
        float x;
        float y;
        float z;
    };

    struct Quat {
        float x;
        float y;
        float z;
        float w;
    };

    /**
     * Quat_tpl<float>::SetRotationXYZ and its inverse. Radians, ordered
     * (about X, about Y, about Z) and composed Rz * Rx * Ry, so for a camera
     * they read as (pitch, roll, yaw).
     */
    Quat QuatFromAngles(const Ang3 &anglesRadians);
    Ang3 AnglesFromQuat(const Quat &rotation);

    /** Rigid transform stored as a quaternion followed by a translation. */
    struct QuatT {
        Quat q;
        Vec3 t;
    };

    static_assert(sizeof(QuatT) == 0x1C, "QuatT size mismatch");
    static_assert(offsetof(QuatT, q) == 0x00, "QuatT::q offset mismatch");
    static_assert(offsetof(QuatT, t) == 0x10, "QuatT::t offset mismatch");

    /** Rigid transform with uniform scale, returned by attachment world poses. */
    struct QuatTS {
        Quat q;
        Vec3 t;
        float s;
    };

    static_assert(sizeof(QuatTS) == 0x20, "QuatTS size mismatch");

    struct Matrix33 {
        float m[3][3];

      private:
        static void AssertLayout() { static_assert(offsetof(Matrix33, m) == 0, "Matrix33::m offset mismatch"); }
    };
    static_assert(sizeof(Matrix33) == 0x24, "Matrix33 size mismatch");

    struct Matrix44 {
        float m[4][4];
    };

    struct Matrix34 {
        float m[3][4];

        // Row-major affine transform.
        constexpr Vec3 TransformPoint(const Vec3 &p) const {
            return Vec3 {m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3], m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3], m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3]};
        }

      private:
        static void AssertLayout() {
            static_assert(offsetof(Matrix34, m) == 0x00, "Matrix34::m offset mismatch");
        }
    };

    /**
     * The transform a Matrix34 carries, taken apart. Ordinary maths, not a
     * call into the engine.
     */
    Matrix34 Matrix34FromTransform(const Vec3 &position, const Quat &rotation, const Vec3 &scale);

    /**
     * Splits `matrix` into the three. A column with no length leaves that
     * axis of `scale` at zero and its basis at the identity's, so a
     * degenerate matrix yields a finite rotation rather than NaNs.
     */
    void Matrix34ToTransform(const Matrix34 &matrix, Vec3 &position, Quat &rotation, Vec3 &scale);

    struct AABB {
        Vec3 min;
        Vec3 max;
    };

    /**
     * Where a ray enters `box`, in metres along a unit `direction`, or a
     * negative number when it misses. A ray starting inside the box reports
     * zero.
     */
    float RayBoxEntryDistance(const Vec3 &origin, const Vec3 &direction, const AABB &box);

    struct Plane {
        Vec3 normal;
        float distance;
    };

    // KCD2 adds rain layers and previous drops speed to the CryEngine layout.
    // Shared fields: SetRainParams 0x180552FE8 / GetRainParams 0x180E81E38.
    struct SRainParams {
        Matrix44 matOccTrans;          // 0x000
        Matrix44 matOccTransRender;    // 0x040
        Quat qRainRotation;            // 0x080
        AABB areaAABB;                 // 0x090
        Vec3 vWorldPos;                // 0x0A8
        Vec3 vColor;                   // 0x0B4
        float fAmount;                 // 0x0C0
        float fCurrentAmount;          // 0x0C4
        float fRadius;                 // 0x0C8
        std::int32_t nRainLayers;      // 0x0CC
        float fFakeGlossiness;         // 0x0D0
        float fFakeReflectionAmount;   // 0x0D4
        float fDiffuseDarkening;       // 0x0D8
        float fRainDropsAmount;        // 0x0DC
        float fRainDropsSpeed;         // 0x0E0
        float fPreviousRainDropsSpeed; // 0x0E4, maintained by SetRainParams
        float fRainDropsLighting;      // 0x0E8
        float fMistAmount;             // 0x0EC
        float fMistHeight;             // 0x0F0
        float fPuddlesAmount;          // 0x0F4
        float fPuddlesMaskAmount;      // 0x0F8
        float fPuddlesRippleAmount;    // 0x0FC
        float fSplashesAmount;         // 0x100
        std::int32_t nUpdateFrameID;   // 0x104
        bool bApplyOcclusion;          // 0x108, not returned by GetRainParams
        bool bIgnoreVisareas;          // 0x109
        bool bDisableOcclusion;        // 0x10A
        std::byte m_opaque010B[0x1];
        float m_unknown010C;
        std::int32_t m_unknown0110;
        float m_unknown0114;
        std::byte m_opaque0118[0x8];

      private:
        static void AssertLayout() {
            static_assert(offsetof(SRainParams, matOccTrans) == 0x00);
            static_assert(offsetof(SRainParams, matOccTransRender) == 0x40);
            static_assert(offsetof(SRainParams, qRainRotation) == 0x80);
            static_assert(offsetof(SRainParams, areaAABB) == 0x90);
            static_assert(offsetof(SRainParams, vWorldPos) == 0xA8);
            static_assert(offsetof(SRainParams, vColor) == 0xB4);
            static_assert(offsetof(SRainParams, fAmount) == 0xC0);
            static_assert(offsetof(SRainParams, fCurrentAmount) == 0xC4);
            static_assert(offsetof(SRainParams, fRadius) == 0xC8);
            static_assert(offsetof(SRainParams, nRainLayers) == 0xCC);
            static_assert(offsetof(SRainParams, fFakeGlossiness) == 0xD0);
            static_assert(offsetof(SRainParams, fFakeReflectionAmount) == 0xD4);
            static_assert(offsetof(SRainParams, fDiffuseDarkening) == 0xD8);
            static_assert(offsetof(SRainParams, fRainDropsAmount) == 0xDC);
            static_assert(offsetof(SRainParams, fRainDropsSpeed) == 0xE0);
            static_assert(offsetof(SRainParams, fPreviousRainDropsSpeed) == 0xE4);
            static_assert(offsetof(SRainParams, fRainDropsLighting) == 0xE8);
            static_assert(offsetof(SRainParams, fMistAmount) == 0xEC);
            static_assert(offsetof(SRainParams, fMistHeight) == 0xF0);
            static_assert(offsetof(SRainParams, fPuddlesAmount) == 0xF4);
            static_assert(offsetof(SRainParams, fPuddlesMaskAmount) == 0xF8);
            static_assert(offsetof(SRainParams, fPuddlesRippleAmount) == 0xFC);
            static_assert(offsetof(SRainParams, fSplashesAmount) == 0x100);
            static_assert(offsetof(SRainParams, nUpdateFrameID) == 0x104);
            static_assert(offsetof(SRainParams, bApplyOcclusion) == 0x108);
            static_assert(offsetof(SRainParams, bIgnoreVisareas) == 0x109);
            static_assert(offsetof(SRainParams, bDisableOcclusion) == 0x10A);
            static_assert(offsetof(SRainParams, m_unknown010C) == 0x10C);
            static_assert(offsetof(SRainParams, m_unknown0110) == 0x110);
            static_assert(offsetof(SRainParams, m_unknown0114) == 0x114);
        }
    };

    // CryAction's small serialization handle is passed by value in the mapped
    // interfaces. Only its ABI-sized payload is needed by this SDK layer.
    struct TSerialize {
        void *pSerialize;
    };

    static_assert(sizeof(Vec2) == 0x8, "Vec2 size mismatch");
    static_assert(sizeof(StdVector<void *>) == 0x18, "StdVector size mismatch");
    static_assert(sizeof(StdString) == 0x20, "StdString size mismatch");
    static_assert(sizeof(CryStringT<char>) == 0x8, "CryStringT<char> size mismatch");
    static_assert(sizeof(Vec3) == 0xC, "Vec3 size mismatch");
    static_assert(sizeof(Ang3) == 0xC, "Ang3 size mismatch");
    static_assert(sizeof(Quat) == 0x10, "Quat size mismatch");
    static_assert(sizeof(QuatT) == 0x1C, "QuatT size mismatch");
    static_assert(sizeof(Matrix44) == 0x40, "Matrix44 size mismatch");
    static_assert(sizeof(Matrix34) == 0x30, "Matrix34 size mismatch");
    static_assert(sizeof(AABB) == 0x18, "AABB size mismatch");
    static_assert(sizeof(SRainParams) == 0x120, "SRainParams size mismatch");
    static_assert(sizeof(TSerialize) == 0x8, "TSerialize size mismatch");
    static_assert(sizeof(XmlNodeRef) == 0x8, "XmlNodeRef size mismatch");
    static_assert(sizeof(CryGUID) == 0x10, "CryGUID size mismatch");
} // namespace SDK
