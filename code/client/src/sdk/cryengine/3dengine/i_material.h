#pragma once

#include "../../types.h"

#include <cstdint>

namespace SDK {
    class IMaterialManager;

    /**
     * CryEngine's MTL_FLAG_*, the field at `CMatInfo+0x40`. KCD2 keeps the
     * stock numbering, which `CBrush::SetMaterial` (0x1806BC2F0) proves by
     * testing 0x2000 and 0x100000 to decide a material must not be drawn.
     *
     * Only the ones this mod sets are named.
     */
    enum EMaterialFlags : std::uint32_t {
        MTL_FLAG_2SIDED = 0x0002,
        MTL_FLAG_ADDITIVE = 0x0004,
        MTL_FLAG_NOSHADOW = 0x0020,
        MTL_FLAG_MULTI_SUBMTL = 0x0100,
        MTL_FLAG_NODRAW = 0x0400,

        /**
         * The material draws in a forward pass, the only pass that blends.
         *
         * `CMatInfo::UpdateMaterialFlags` sets this from the parsed opacity
         * when the material loads, and never re-runs -- so a material made
         * translucent at runtime keeps its opaque classification unless this
         * is set by hand.
         */
        MTL_FLAG_REQUIRE_FORWARD_RENDERING = 0x8000,
    };

    /**
     * A loaded material, as `IMaterialManager` hands it out and
     * `IRenderNode::SetMaterial` takes it.
     *
     * Every named slot below is read off one of the game's own script binds
     * rather than counted; the table is longer than what is declared, because
     * KCD2 ships no `CMatInfo` vftable symbol to bound it with.
     *
     * The engine owns the reference count. `CBrush::SetMaterial` assigns
     * through a smart pointer into `CBrush+0x88`, so a material handed
     * straight from the manager to a render node needs no release here, and
     * must not be given one.
     */
    struct IMaterial {
      public:
        virtual ~IMaterial() = default;

        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        /** The manager that owns this material, and the only way to reach `CloneMaterial` from one. +0x30, from `CScriptBind_Entity::CloneMaterial` (0x1837D1D84). */
        virtual IMaterialManager *GetMaterialManager() = 0; // +0x030
        virtual void unk_fn0007() = 0;
        /** The name the manager keys it by: the `.mtl` path, no extension. */
        virtual const char *GetName() const = 0; // +0x040
        /**
         * Replaces the whole flag field. Its body is the one instruction
         * `mov [rcx+40h], edx` -- the word `GetFlags` reads back, which is
         * what pins the pair.
         */
        virtual void SetFlags(std::uint32_t flags) = 0; // +0x048

        /** CryEngine's MTL_FLAG_*, read before a node decides to draw. */
        virtual std::uint32_t GetFlags() const = 0; // +0x050
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual void unk_fn0016() = 0;
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;
        virtual void unk_fn0019() = 0;
        virtual void unk_fn0020() = 0;
        virtual void unk_fn0021() = 0;
        virtual void unk_fn0022() = 0;
        virtual void unk_fn0023() = 0;
        virtual void unk_fn0024() = 0;
        virtual void unk_fn0025() = 0;
        virtual void unk_fn0026() = 0;
        virtual void unk_fn0027() = 0;
        /** How many children a multi-material has; 0 for a plain one. +0xE0, `CMatInfo` 0x1804846B0. */
        virtual int GetSubMtlCount() = 0; // +0x0E0
        virtual void unk_fn0029() = 0;
        virtual void unk_fn0030() = 0;
        virtual void unk_fn0031() = 0;
        virtual void unk_fn0032() = 0;
        virtual void unk_fn0033() = 0;
        virtual void unk_fn0034() = 0;
        virtual void unk_fn0035() = 0;
        virtual void unk_fn0036() = 0;
        virtual void unk_fn0037() = 0;
        virtual void unk_fn0038() = 0;
        virtual void unk_fn0039() = 0;
        /** One of a multi-material's children, or this material for index 0 on a plain one. +0x140. */
        virtual IMaterial *GetSubMtl(int index) = 0; // +0x140
        virtual void unk_fn0041() = 0;
        virtual void unk_fn0042() = 0;
        virtual void unk_fn0043() = 0;
        /**
         * Writes one named scalar of the shader's public params -- `opacity`,
         * `alpha`, `shininess`, `glow`. False when the shader declares no
         * such name.
         *
         * **By value, and a setter only**, unlike stock CryEngine's
         * `SetGetMaterialParamFloat(const char *, float &, bool bGet)`.
         * `CScriptBind_Entity::SetMaterialFloat` (0x1837DDFF0) loads the
         * value into XMM2 and never touches R8, so declaring it with a
         * `float &` passes an address the callee never reads.
         */
        virtual bool SetMaterialParamFloat(const char *paramName, float value) = 0; // +0x160

        /**
         * The same for a vector param -- `diffuse`, `specular`, `emissive`.
         * This one does take its value by address:
         * `CScriptBind_Entity::SetMaterialVec3` (0x1837DE0A8) loads R8 with a
         * pointer. Const because nothing is written back.
         */
        virtual bool SetMaterialParamVec3(const char *paramName, const Vec3 &value) = 0; // +0x168
        virtual void unk_fn0046() = 0;
        virtual void unk_fn0047() = 0;
        virtual void unk_fn0048() = 0;
        virtual void unk_fn0049() = 0;
        virtual void unk_fn0050() = 0;

        /**
         * Points one EEfResTextures slot (0 diffuse ... 11 opacity, 13 last)
         * at a texture file. `CMatInfo` 0x18073EFB0: the name is copied and
         * queued with the slot on the material's deferred list, so the swap
         * lands on a later frame rather than inside the call, and a slot past
         * 13 is ignored. The book screen renders its page into a page
         * material's slot 11 through exactly this call.
         *
         * Writes the material itself, so it is only for a material the caller
         * owns: one shared by name with every other user would change them all.
         */
        virtual void SetTexture(const CryStringT<char> &textureName, int textureSlot) = 0; // +0x198
    };

    static_assert(sizeof(IMaterial) == 0x8, "IMaterial size mismatch");
} // namespace SDK
