#pragma once

#include "../../types.h"

namespace SDK {
    /**
     * CrySystem's string table, `CLocalizedStringsManager` (vtable
     * 0x183DBDFA0), held at CSystem+0xD50 and handed out by
     * `ISystem::GetLocalizationManager`.
     */
    class ILocalizationManager {
      public:
        virtual ~ILocalizationManager() = default; // 0
        virtual void Opaque01()         = 0;
        virtual void Opaque02()         = 0;
        virtual void Opaque03()         = 0;
        virtual void Opaque04()         = 0;
        virtual void Opaque05()         = 0;
        virtual void Opaque06()         = 0;
        virtual void Opaque07()         = 0;
        virtual void Opaque08()         = 0;
        virtual void Opaque09()         = 0;
        virtual void Opaque10()         = 0;
        /** Slot 11 (0x1809EAF50): the text language in use, lowercase (`english`); empty before one is set. */
        virtual const char *GetLanguage() const = 0;
        virtual void Opaque12()         = 0;
        virtual void Opaque13()         = 0;
        virtual void Opaque14()         = 0;
        virtual void Opaque15()         = 0;
        virtual void Opaque16()         = 0;
        virtual void Opaque17()         = 0;
        virtual void Opaque18()         = 0;
        virtual void Opaque19()         = 0;
        virtual void Opaque20()         = 0;
        virtual void Opaque21()         = 0;
        virtual void Opaque22()         = 0;

        /**
         * Slot 23 (0x18051D468), the one lookup every `@key` goes through,
         * Scaleform's included. `label` must start with `@`. On a miss it
         * answers false and copies `label` through, so an unknown key reads
         * back as itself. `out` is written with the engine's own operator=.
         */
        virtual bool LocalizeLabel(const char *label, CryStringT<char> &out, bool english = false) = 0;

        static ILocalizationManager *GetInstance();

        /**
         * `CLocalizedStringsManager::LoadExcelXmlSpreadsheet` (0x18071AEC0) as the game's mod
         * loader calls it for a `<file>__<id>.xml` patch: merged into the keys already loaded,
         * logged as `[Mod] Loading localization patch`. `path` is CryPak-relative.
         */
        bool LoadPatch(const char *path);

        /**
         * The same loader re-reading a stock file over the keys it holds, which is what undoes
         * every patch merged into it. Also in patch mode: only that mode finds a key already
         * loaded and replaces its text; a plain load adds a second entry the lookup never reaches.
         */
        bool ReloadFile(const char *path);
    };

    static_assert(sizeof(ILocalizationManager) == 0x8, "ILocalizationManager interface size mismatch");
} // namespace SDK
