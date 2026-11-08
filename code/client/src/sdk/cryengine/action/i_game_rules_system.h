#pragma once

namespace SDK {
    class IGameRules;

    /**
     * IGameRulesSystem, vtable 0x184052338; CGameRulesSystem's complete
     * implementation is at 0x183C72318.
     */
    class IGameRulesSystem {
      public:
        virtual ~IGameRulesSystem() = default;
        virtual bool RegisterGameRules(const char *rulesName, const char *extensionName) = 0;
        virtual bool CreateGameRules(const char *rulesName) = 0;
        virtual bool DestroyGameRules() = 0;
        virtual void AddGameRulesAlias(const char *gameRules, const char *alias) = 0;
        virtual void AddGameRulesLevelLocation(const char *gameRules, const char *mapLocation) = 0;
        virtual const char *GetGameRulesLevelLocation(const char *gameRules, int index) = 0;
        virtual const char *GetGameRulesName(const char *alias) const = 0;
        virtual bool HaveGameRules(const char *rulesName) = 0;
        virtual void SetCurrentGameRules(IGameRules *gameRules) = 0;
        virtual IGameRules *GetCurrentGameRules() const = 0;
    };
} // namespace SDK
