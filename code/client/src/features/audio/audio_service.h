#pragma once

#include "sdk/cryengine/audio/i_audio_interfaces_common_data.h"
#include "sdk/types.h"

#include <glm/glm.hpp>

#include <string>

namespace KCDC::Features::Audio {
    /**
     * The game's own sound triggers, played from this machine.
     *
     * Every gameplay sound in KCD2 is an ATL trigger: a name from
     * `Libs/GameAudio/*.xml` standing for an FMOD event. The audio system
     * resolves a name to a control id and plays it on an audio object, and the
     * three kinds of object are the three verbs here -- the global one for 2D,
     * a pooled one placed at a point, and an entity's own, which follows it.
     *
     * Nothing here is replicated: a sound everyone should hear is one the
     * server tells every client to play. Every verb returns whether the game
     * took it, and logs which of the audio system, the name or the entity was
     * missing when it did not.
     *
     * Game thread only.
     */
    class AudioService final {
      public:
        /** The control id for a trigger name, or 0 when no control file declares it. */
        static SDK::AudioControlId ResolveTrigger(const std::string &name);

        /** Plays on the ATL's global audio object: no position, no attenuation. Fire and forget -- every 2D sound in the game shares that object, so stopping one trigger on it is not offered. */
        static bool Play(const std::string &trigger);

        /**
         * Plays once at a point in the world, in metres. The audio object is
         * handed back straight away and the ATL keeps it until the trigger
         * ends, so the sound cannot be stopped or moved; anything that needs
         * either belongs on an entity. Occlusion is ignored rather than
         * ray-traced, as the game's own material effects do it.
         */
        static bool PlayAt(const std::string &trigger, const glm::vec3 &position);

        /** Plays on an entity's audio component, creating it if absent. `entityId` is an engine id -- what `Player.entityId` reports -- and is reused once the entity dies. */
        static bool PlayOnEntity(const std::string &trigger, SDK::EntityId entityId);

        /**
         * Plays a sound file on an entity's audio component: `<file>.ogg`, a game-root path read
         * through CryPak, so a file in a mounted pak plays like one of the game's own. It goes out
         * as a programmer sound on one of the game's voice host events, chosen by the
         * `standalone_file_purpose` switch -- `inner`, `10`, `20`, `35`, `50`, `100`, `200` for
         * barks heard up to that many metres, `sfp_dialog`, `sfp_cutscene`, `sfp_music` -- so the
         * game's own volume and distance rules apply. One file per entity per 0.1 s
         * (`wh_snd_playfile_cooldown`).
         */
        static bool PlayFileOnEntity(const std::string &file, SDK::EntityId entityId, const std::string &purpose);

        /** Stops every instance of one trigger on the entity. */
        static bool StopOnEntity(const std::string &trigger, SDK::EntityId entityId);

        /** Stops everything the entity is playing, the game's own sounds included. */
        static bool StopAllOnEntity(SDK::EntityId entityId);

        /** Sets a continuous parameter on the entity's audio object. Names are in `Libs/Tables/GameAudio/SkaldAtlRtpc.xml`. */
        static bool SetEntityRtpc(SDK::EntityId entityId, const std::string &rtpc, float value);

        /** Sets a discrete switch on the entity's audio object. Both names are in `Libs/GameAudio/g_switches.xml`. */
        static bool SetEntitySwitch(SDK::EntityId entityId, const std::string &switchName, const std::string &stateName);
    };
} // namespace KCDC::Features::Audio
