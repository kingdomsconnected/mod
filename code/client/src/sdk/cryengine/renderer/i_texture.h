#pragma once

namespace SDK {
    /**
     * CryEngine's `ITexture`, as far as the mod reaches into it: `CTexture`'s
     * primary table (0x183A53FA0) puts it at offset 0, and slot 8 answers the
     * id at +0x60 -- the number Scaleform's `img://TEXID:<id>` resolves
     * through `IRenderer::EF_GetTextureByID` (`CryGFxImageLoader::LoadImage`
     * 0x18080B14C, and the game's own loadMovie helper 0x1835B251C).
     */
    struct ITexture {
        virtual void unk_fn0000()        = 0;
        virtual void unk_fn0001()        = 0;
        virtual void unk_fn0002()        = 0;
        virtual void unk_fn0003()        = 0;
        virtual void unk_fn0004()        = 0;
        virtual void unk_fn0005()        = 0;
        virtual void unk_fn0006()        = 0;
        virtual void unk_fn0007()        = 0;
        virtual int GetTextureID() const = 0; // slot 8, 0x181A74B40

      protected:
        ~ITexture() = default;
    };
} // namespace SDK
