#pragma once

#include <cstdint>

namespace SDK {
    struct IAttachment;

    /**
     * Abstract CryAnimation character-attachment interface.
     *
     * RTTI identifies this global-namespace type as a struct. Its table at
     * 0x183A71768 contains exactly the virtual destructor and 44 pure
     * operations. Their stripped source contracts remain opaque.
     */
    struct IAttachmentManager {
        virtual ~IAttachmentManager() = default; // +0x000
        virtual void unk_fn0001() = 0;
        /**
         * Adds a named socket (0x1820ED500). The last argument skips the
         * duplicate-name and CRC checks rather than projecting the socket.
         * A bone socket's joint stays unresolved until
         * `IAttachment::AlignJointAttachment` (or `SetJointName`) runs. Only
         * the joint name is stored, so a joint the skeleton lacks is not an
         * error until then. `CScriptBind_Entity::CreateBoneAttachment`
         * (0x1837D1FBC) calls it with the last argument false.
         */
        virtual IAttachment* CreateAttachment(const char* name, std::uint32_t type,
                                              const char* jointName = nullptr,
                                              bool skipDuplicateCheck = false) = 0;
        virtual std::int32_t RemoveAttachmentByInterface(
            const IAttachment* attachment, std::uint32_t loadingFlags = 0) = 0;
        /**
         * Removes a socket by name, case-insensitively (0x180975ADC). The
         * removal tells the payload it is going and then clears the binding,
         * which releases it, so a socket carrying a prop needs no
         * `ClearBinding` of its own first. `CScriptBind_Entity::DestroyAttachment`
         * (0x1837D2310) calls it.
         */
        virtual std::int32_t RemoveAttachmentByName(
            const char* name, std::uint32_t loadingFlags = 0) = 0;
        virtual std::int32_t RemoveAttachmentByNameCRC(
            std::uint32_t nameCrc, std::uint32_t loadingFlags = 0) = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        [[nodiscard]] virtual IAttachment* GetInterfaceByName(const char* name) const = 0;
        [[nodiscard]] virtual IAttachment* GetInterfaceByIndex(std::uint32_t index) const = 0;
        [[nodiscard]] virtual IAttachment* GetInterfaceByNameCRC(std::uint32_t nameCrc) const = 0;
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        [[nodiscard]] virtual std::int32_t GetAttachmentCount() const = 0;
        [[nodiscard]] virtual std::int32_t GetIndexByName(const char* name) const = 0;
        [[nodiscard]] virtual std::int32_t GetIndexByNameCRC(std::uint32_t nameCrc) const = 0;
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
        virtual void unk_fn0028() = 0;
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
        virtual void unk_fn0040() = 0;
        virtual void unk_fn0041() = 0;
        virtual void unk_fn0042() = 0;
        virtual void unk_fn0043() = 0;
        virtual void unk_fn0044() = 0; // +0x160
    };

    static_assert(sizeof(IAttachmentManager) == 0x8,
                  "IAttachmentManager size mismatch");
} // namespace SDK
