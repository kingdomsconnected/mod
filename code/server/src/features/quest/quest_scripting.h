#pragma once

#include <unordered_map>

#include "shared/features/quest/quest_entity.h"

#include <scripting/builtins/entity.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace KCDC::Scripting {
    /**
     * Server-side handle for a replicated quest. Wraps the NetworkID and
     * resolves through replication, so a handle to a removed quest reads as
     * gone rather than dangling.
     *
     * Bindings only: every verb is one call into
     * `KCDC::Features::Quest::Quests`, so a script and a command write the
     * same journal entry.
     */
    class Quest final: public Framework::Scripting::Builtins::Entity {
      public:
        Quest(std::uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {}

        /** The live quest, or nullptr once it has been removed. */
        Shared::Entities::QuestEntity *ResolveQuest() const;

        /** The key the quest is filed under, which is also the node name the game gives it. */
        std::string GetKey() const;

        std::string GetTitle() const;
        void SetTitle(const std::string &title);

        std::string GetDescription() const;
        void SetDescription(const std::string &description);

        /** Read-only: the journal section a quest files under is decided when it is given. */
        std::string GetType() const;

        /** `active`, `done` or `failed`. Assignment announces; `setProgress` can do it quietly. */
        std::string GetProgress() const;
        void SetProgress(const std::string &progress);

        /** Network id of the player this quest belongs to, or 0 when it went to the whole world. */
        double GetPlayerId() const;

        double GetObjectiveCount() const;

        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);
        static v8pp::class_<Quest> &GetClass(v8::Isolate *isolate);

      private:
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Quest>>> _classes;
    };

    // What this feature tells loaded resources happened to a quest. Event arguments are network IDs; the handles a resource sees are created inside the scripting isolate.
    namespace Events {
        /**
         * A player started or stopped following a quest in their journal.
         *
         * Reported by that player's client, which is the only place the
         * decision is made, so a handler reacts rather than vetoes.
         */
        void QuestTrackingChanged(std::uint64_t questNetworkId, std::uint64_t playerNetworkId, bool tracked);
    } // namespace Events
} // namespace KCDC::Scripting
