#pragma once

#include "../../types.h"

namespace SDK {
    class ILevelInfo;

    class ILevelSystemListener {
      public:
        virtual ~ILevelSystemListener() = default;
        virtual void OnLevelNotFound(const char *levelName) = 0;
        virtual void OnLoadingStart(ILevelInfo *level) = 0;
        virtual void OnLoadingLevelEntitiesStart(ILevelInfo *level) = 0;
        virtual void Opaque04() = 0;
        virtual void OnLoadingComplete(ILevelInfo *level) = 0;
        virtual void OnLoadingError(ILevelInfo *level, const char *error) = 0;
        virtual void OnLoadingProgress(ILevelInfo *level, int progressAmount) = 0;
        virtual void OnUnloadComplete(ILevelInfo *level) = 0;
        virtual void Opaque09() = 0;
    };

    /**
     * ILevelSystem as implemented by CLevelSystem's 25-slot main vtable at
     * 0x184051EE0. Its separate two-slot system-event base is at 0x184051EC8.
     */
    class ILevelSystem : public ILevelSystemListener {
      public:
        virtual void Rescan(const char *levelsFolder, std::uint32_t tag) = 0; // +0x050
        virtual int GetLevelCount() = 0;
        virtual void Opaque12() = 0; // GetLevelTypeList; container ABI intentionally not exposed.
        virtual ILevelInfo *GetLevelInfo(const char *levelName) = 0;
        virtual ILevelInfo *GetLevelInfo(int level) = 0;
        virtual void AddListener(ILevelSystemListener *listener) = 0;
        virtual void RemoveListener(ILevelSystemListener *listener) = 0;
        virtual ILevelInfo *GetCurrentLevel() const = 0;
        virtual ILevelInfo *LoadLevel(const char *levelName) = 0;
        virtual void UnLoadLevel() = 0;
        virtual ILevelInfo *SetEditorLoadedLevel(const char *levelName, bool readLevelInfoMetadata = false) = 0;
        virtual bool IsLevelLoaded() = 0;
        virtual void PrepareNextLevel(const char *levelName) = 0;
        virtual void Opaque23() = 0;
        virtual float GetLastLevelLoadTime() = 0;
    };
} // namespace SDK
