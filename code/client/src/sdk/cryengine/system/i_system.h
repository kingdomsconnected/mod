#pragma once

#include "c_camera_native.h"
#include "i_localization_manager.h"
#include "i_system_event_dispatcher.h"
#include "xml_node_ref.h"

#include <cstddef>
#include <string_view>

namespace SDK {
    /** KCD2 primary ISystem vtable; unknown slots preserve the native order. */
    class ISystem {
      public:
        virtual ~ISystem()       = default;
        virtual void Opaque001() = 0;
        virtual void Opaque002() = 0;
        virtual void Opaque003() = 0;
        virtual void Opaque004() = 0;
        virtual void Opaque005() = 0;
        virtual void Opaque006() = 0;
        virtual void Opaque007() = 0;
        virtual void Opaque008() = 0;
        virtual void Opaque009() = 0;
        virtual void Opaque010() = 0;
        virtual void Opaque011() = 0;
        virtual void Opaque012() = 0;
        virtual void Opaque013() = 0;
        virtual void Opaque014() = 0;
        virtual void Opaque015() = 0;
        virtual void Opaque016() = 0;
        virtual void Opaque017() = 0;
        virtual void Opaque018() = 0;
        virtual void Opaque019() = 0;
        virtual void Opaque020() = 0;
        virtual void Opaque021() = 0;
        virtual void Opaque022() = 0;
        virtual void Opaque023() = 0;
        virtual void Opaque024() = 0;
        virtual void Opaque025() = 0;
        virtual void Opaque026() = 0;
        virtual void Opaque027() = 0;
        virtual void Opaque028() = 0;
        virtual void Opaque029() = 0;
        virtual void Opaque030() = 0;
        virtual void Opaque031() = 0;
        virtual void Opaque032() = 0;
        virtual void Opaque033() = 0;
        virtual void Opaque034() = 0;
        virtual void Opaque035() = 0;
        virtual void Opaque036() = 0;
        virtual void Opaque037() = 0;
        virtual void Opaque038() = 0;
        virtual void Opaque039() = 0;
        virtual void Opaque040() = 0;
        virtual void Opaque041() = 0;
        virtual void Opaque042() = 0;
        virtual void Opaque043() = 0;
        virtual void Opaque044() = 0;
        virtual void Opaque045() = 0;
        virtual void Opaque046() = 0;
        virtual void Opaque047() = 0;
        virtual void Opaque048() = 0;
        virtual void Opaque049() = 0;
        virtual void Opaque050() = 0;
        virtual void Opaque051() = 0;
        virtual void Opaque052() = 0;
        virtual void Opaque053() = 0;
        virtual void Opaque054() = 0;
        virtual void Opaque055() = 0;
        virtual void Opaque056() = 0;
        virtual void Opaque057() = 0;
        virtual void Opaque058() = 0;
        virtual void Opaque059() = 0;
        virtual void Opaque060() = 0;
        virtual void Opaque061() = 0;
        virtual void Opaque062() = 0;
        virtual void Opaque063() = 0;
        virtual void Opaque064() = 0;
        virtual void Opaque065() = 0;
        virtual void Opaque066() = 0;
        virtual void Opaque067() = 0;
        virtual void Opaque068() = 0;
        virtual void Opaque069() = 0;
        virtual void Opaque070() = 0;
        virtual void Opaque071() = 0;
        virtual void Opaque072() = 0;
        virtual void Opaque073() = 0;
        virtual void Opaque074() = 0;
        virtual void Opaque075() = 0;
        virtual void Opaque076() = 0;
        virtual void Opaque077() = 0;
        virtual void Opaque078() = 0;
        virtual void Opaque079() = 0;
        virtual void Opaque080() = 0;
        virtual void Opaque081() = 0;
        virtual void Opaque082() = 0;
        virtual void Opaque083() = 0;
        virtual void Opaque084() = 0;

        /** CSystem's dispatcher, stored at CSystem+0x9E8 by its constructor. */
        virtual ISystemEventDispatcher *GetISystemEventDispatcher() = 0; // 85

        virtual void Opaque086() = 0;
        virtual void Opaque087() = 0;
        virtual void Opaque088() = 0;
        virtual void Opaque089() = 0;
        virtual void Opaque090() = 0;
        virtual void Opaque091() = 0;
        virtual void Opaque092() = 0;
        virtual void Opaque093() = 0;
        virtual void Opaque094() = 0;
        virtual void Opaque095() = 0;
        virtual void Opaque096() = 0;
        virtual void Opaque097() = 0;
        virtual void Opaque098() = 0;
        virtual void Opaque099() = 0;
        virtual void Opaque100() = 0;
        virtual void Opaque101() = 0;
        virtual void Opaque102() = 0;
        virtual void Opaque103() = 0;
        virtual void Opaque104() = 0;
        virtual void Opaque105() = 0;
        virtual void Opaque106() = 0;
        virtual void Opaque107() = 0;
        virtual void Opaque108() = 0;
        virtual void Opaque109() = 0;
        virtual void Opaque110() = 0;
        virtual void Opaque111() = 0;
        virtual void Opaque112() = 0;
        virtual void Opaque113() = 0;
        virtual void Opaque114() = 0;
        virtual void Opaque115() = 0;
        virtual void Opaque116() = 0;
        virtual void Opaque117() = 0;
        virtual void Opaque118() = 0;
        virtual void Opaque119() = 0;
        virtual void Opaque120() = 0;
        virtual void Opaque121() = 0;
        virtual void Opaque122() = 0;
        virtual void Opaque123() = 0;
        virtual void Opaque124() = 0;
        virtual void Opaque125() = 0;
        virtual void Opaque126() = 0;
        virtual void Opaque127() = 0;
        virtual void Opaque128() = 0;
        virtual void Opaque129() = 0;

        // MSVC supplies the XmlNodeRef return buffer after this.
        virtual XmlNodeRef LoadXmlFromBuffer(const char *buffer, std::size_t size, bool reuseStrings, bool useReadOnlyParser) = 0; // 130

        /**
         * `CSystem` slot 131 (0x180D1B0EC): the file read through CryPak and parsed by
         * `CXmlUtils::LoadXmlFromFile`, as the table and localization loaders read theirs
         * (`reuseStrings` false, both flags true). Invalid for a file CryPak cannot open.
         */
        virtual XmlNodeRef LoadXmlFromFile(const char *path, bool reuseStrings, bool useReadOnlyParser, bool suppressWarnings) = 0; // 131
        virtual void Opaque132() = 0;
        virtual void Opaque133() = 0;
        virtual void Opaque134() = 0;

        /**
         * CSystem::m_ViewCamera at CSystem+0x288 (0x1805D77F0): the camera
         * the frame is being drawn through, cached by SetViewCamera. It is
         * the one CD3D9Renderer::ProjectToScreen projects with.
         */
        virtual const CCamera &GetViewCamera() const = 0; // 135

        virtual void Opaque136() = 0;
        virtual void Opaque137() = 0;
        virtual void Opaque138() = 0;
        virtual void Opaque139() = 0;
        virtual void Opaque140() = 0;
        virtual void Opaque141() = 0;
        virtual void Opaque142() = 0;
        virtual void Opaque143() = 0;
        virtual void Opaque144() = 0;
        virtual void Opaque145() = 0;
        virtual void Opaque146() = 0;
        virtual void Opaque147() = 0;
        virtual void Opaque148() = 0;
        virtual void Opaque149() = 0;
        virtual void Opaque150() = 0;
        virtual void Opaque151() = 0;
        virtual void Opaque152() = 0;
        virtual void Opaque153() = 0;
        virtual void Opaque154() = 0;
        virtual void Opaque155() = 0;
        virtual void Opaque156() = 0;
        virtual void Opaque157() = 0;
        virtual void Opaque158() = 0;
        virtual void Opaque159() = 0;
        virtual void Opaque160() = 0;
        virtual void Opaque161() = 0;
        virtual void Opaque162() = 0;
        virtual void Opaque163() = 0;
        virtual void Opaque164() = 0;
        virtual void Opaque165() = 0;
        virtual void Opaque166() = 0;
        virtual void Opaque167() = 0;
        virtual void Opaque168() = 0;
        virtual void Opaque169() = 0;
        virtual void Opaque170() = 0;
        virtual void Opaque171() = 0;
        virtual void Opaque172() = 0;

        /** CSystem+0xD50, the string table `CSystem::Init` creates (0x1803D3300). */
        virtual ILocalizationManager *GetLocalizationManager() const = 0; // 173

        /**
         * The engine's own way out, and the one every exit in the game takes.
         *
         * It logs the reason, shuts the subsystems down and exits the
         * process without returning. `core/boot/quit.cpp` detours it to tear
         * the mod down first. Calls here and from the game's menu both pass
         * through that hook.
         *
         * Not declared as a virtual, although the native is one: its address
         * is already resolved for that detour, and a slot counted off the
         * table beside it would be a second place to be wrong.
         */
        void Quit(std::string_view reason, unsigned int exitCode = 0);

        static ISystem *GetInstance();
    };

    static_assert(sizeof(ISystem) == 0x8, "ISystem interface size mismatch");
} // namespace SDK
