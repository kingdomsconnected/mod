#pragma once

#include "../system/xml_node_ref.h"
#include "i_action_map_action_iterator.h"

namespace SDK {
    /** Manager-owned action map; its enabled and active states are distinct. */
    class IActionMap {
      public:
        virtual ~IActionMap()                            = default;
        virtual void Opaque01()                          = 0;
        virtual void Opaque02()                          = 0;
        virtual void Opaque03()                          = 0;
        virtual void Opaque04()                          = 0;
        virtual void Opaque05()                          = 0;
        virtual void Opaque06()                          = 0;
        virtual void Opaque07()                          = 0;
        virtual void Opaque08()                          = 0;
        virtual void Opaque09()                          = 0;
        virtual void Opaque10()                          = 0;
        virtual void Opaque11()                          = 0;
        virtual void Opaque12()                          = 0;
        virtual void Opaque13()                          = 0;
        virtual void Opaque14()                          = 0;
        virtual void Opaque15()                          = 0;
        virtual bool LoadFromXML(const XmlNodeRef &node) = 0; // 16
        virtual void Opaque17()                          = 0;
        virtual void Opaque18()                          = 0;
        /** 0x180A13C20. Returns a `_smart_ptr` by value, hence the hidden result pointer; the caller must `Release` it. */
        virtual IActionMapActionIterator **CreateActionIterator(IActionMapActionIterator **result) = 0; // 19
        virtual void Opaque20()                                                                    = 0;
        virtual void Opaque21()                                                                    = 0;
        virtual void Opaque22()                                                                    = 0;
        virtual void Enable(bool enable)                                                           = 0; // 23
        virtual bool Enabled() const                                                               = 0;
        virtual void SetActive(bool active)                                                        = 0;
        virtual bool IsActive() const                                                              = 0;
        virtual unsigned int GetPriority() const                                                   = 0;
        virtual bool IsExclusive() const                                                           = 0;
    };

    static_assert(sizeof(IActionMap) == 0x8, "IActionMap interface size mismatch");
} // namespace SDK
