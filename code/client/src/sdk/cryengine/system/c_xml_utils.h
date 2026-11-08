#pragma once

namespace SDK {
    /**
     * CrySystem's XML front end, the `IXmlUtils` behind `CSystem+0xA20`.
     *
     * `LoadXmlFromFile` (slot 1, 0x1809792B4) is where every XML the engine
     * reads by path is parsed: `ISystem::LoadXmlFromFile` (slot 131)
     * forwards to it, and the Mannequin database loader, its sub-database
     * pass and the `.chrparams` and tag-definition loaders all end in it. It
     * answers a fresh `XmlNodeRef` through the hidden return pointer. Only
     * hooked, so no member is mapped.
     */
    class CXmlUtils;
} // namespace SDK
