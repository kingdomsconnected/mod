#pragma once

#include "../system/xml_node_ref.h"

#include "i_material.h"
#include "i_surface_type.h"
#include "i_surface_type_manager.h"

#include <cstdint>

namespace SDK {
    /**
     * The engine's one material registry, implemented by `CMatMan`.
     *
     * A process global rather than a `C3DEngine` member: init allocates a
     * single 0x118-byte instance and `GetMaterialManager` only reads the
     * pointer back. One registry, keyed by name, shared by everything that
     * draws. Its table is exactly 32 entries.
     */
    struct IMaterialManager {
        virtual ~IMaterialManager() = default;

        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;

        /**
         * The already-loaded material of that name, or null. Touches no disk.
         *
         * Answers null for an entry that is registered but no longer live, so
         * a hit is always usable.
         */
        virtual IMaterial *FindMaterial(const char *name) = 0; // +0x30

        /**
         * The material of that name, loading `<name>.mtl` if it is not
         * registered yet. Calls `FindMaterial` itself, so this is the one call
         * a caller needs.
         *
         * `makeIfNotFound` falls back to the default material instead of null
         * when the load fails; pass false to let a bad name report itself.
         * `nonRemovable` also pins the result in the manager's keep-alive list.
         * `loadingFlags` bit 0 skips the alternate loader, bit 1 re-loads even
         * when `FindMaterial` hit. `parseFlag` reaches the XML parse and its
         * meaning is not recovered; the engine passes true.
         */
        virtual IMaterial *LoadMaterial(const char *name, bool makeIfNotFound, bool nonRemovable, std::uint32_t loadingFlags, bool parseFlag) = 0; // +0x38

        /**
         * +0x40 (`CMatMan::LoadMaterialFromXml`, 0x1834C38C8). Builds the material named `name` from
         * a parsed `.mtl` document, rebuilding an existing one of that name in place -- the one way
         * to make a loaded material re-read its file.
         */
        virtual IMaterial *LoadMaterialFromXml(const char *name, XmlNodeRef node) = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        /**
         * A private copy of `material`, or of one named sub-material of it.
         *
         * +0x58, from `CScriptBind_Entity::CloneMaterial` (0x1837D1D84),
         * which clones a slot's material and hands the copy straight back to
         * the slot. The copy is what makes a per-instance tint possible: the
         * shipped material is shared by everything that names it, so writing a
         * param on one would recolour every one of them.
         */
        virtual IMaterial *CloneMaterial(IMaterial *material, const char *subMaterialName = nullptr) = 0; // +0x58
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual void unk_fn0016() = 0;
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;
        virtual void unk_fn0019() = 0;

        /**
         * Turns an `SRayHit::surfaceIndex` into a named type; never null.
         * Two instructions, loading the `CSurfaceTypeManager` at
         * `CMatMan+0xB8` and tail-calling its own `GetSurfaceType`, so both
         * arguments carry through. `why` only decorates a warning.
         */
        virtual ISurfaceType *GetSurfaceType(int surfaceId, const char *why = nullptr) = 0; // +0xA0

        virtual ISurfaceTypeManager *GetSurfaceTypeManager() = 0; // +0xA8

        virtual void unk_fn0022() = 0;
        virtual void unk_fn0023() = 0;
        virtual void unk_fn0024() = 0;
        virtual void unk_fn0025() = 0;
        virtual void unk_fn0026() = 0;
        virtual void unk_fn0027() = 0;
        virtual void unk_fn0028() = 0;
        virtual void unk_fn0029() = 0;
        virtual void unk_fn0030() = 0;
        virtual void unk_fn0031() = 0;
    };

    static_assert(sizeof(IMaterialManager) == 0x8, "IMaterialManager size mismatch");
} // namespace SDK
